#pragma once

#include "RdmaSocket.hpp"

class RdmaClientSocket : protected RdmaSocket
{
public:
    RdmaClientSocket(const char *ip, const char *port, int thread_num, int buffer_size, int timeout) : RdmaSocket(thread_num, buffer_size)
    {
        // init
        timeout_ = timeout;
        record_valid_ = false;
        record_ = nullptr;
        record_len_ = 0;
        // resolve address
        addrinfo *address = NULL;
        TEST_NZ(getaddrinfo(ip, port, NULL, &address));
        TEST_NZ(rdma_resolve_addr(id_, NULL, address->ai_addr, timeout));
        freeaddrinfo(address);
        // set connection context
        id_->context = (ConnectionContext *)malloc(sizeof(ConnectionContext));
        // run Loop function to listen event
        this->Loop();
    }

    ~RdmaClientSocket() override {}

    void Write(char *buffer, int size)
    {
        std::unique_lock<std::mutex> lg(record_lock_);
        record_cv_.wait(lg, [&]()
                        { return !record_valid_; });
        record_ = buffer, record_len_ = size, record_valid_ = true;
        record_cv_.notify_one();
        record_cv_.wait(lg, [&]()
                        { return !record_valid_; });
    }

    void Loop() override
    {
        auto event_loop = [&]()
        {
            rdma_conn_param params;
            memset(&params, 0, sizeof(rdma_conn_param));
            params.initiator_depth = params.responder_resources = 1;
            params.rnr_retry_count = 7; /* infinite retry */

            rdma_cm_event *event = NULL;
            while (rdma_get_cm_event(channel_, &event) == 0)
            {
                struct rdma_cm_event event_copy;
                memcpy(&event_copy, event, sizeof(rdma_cm_event));
                rdma_ack_cm_event(event);
                switch (event_copy.event)
                {
                case RDMA_CM_EVENT_ADDR_RESOLVED:
                    SAY("RDMA_CM_EVENT_ADDR_RESOLVED");
                    InitConnection(event_copy.id);
                    PreConnectionCB(event_copy.id, buffer_size_);
                    TEST_NZ(rdma_resolve_route(event_copy.id, timeout_));
                    break;
                case RDMA_CM_EVENT_ROUTE_RESOLVED:
                    SAY("RDMA_CM_EVENT_ROUTE_RESOLVED");
                    TEST_NZ(rdma_connect(event_copy.id, &params));
                    break;
                case RDMA_CM_EVENT_ESTABLISHED:
                    SAY("RDMA_CM_EVENT_ESTABLISHED");
                    // none
                    break;
                case RDMA_CM_EVENT_DISCONNECTED:
                    SAY("RDMA_CM_EVENT_DISCONNECTED");
                    rdma_destroy_qp(event_copy.id);
                    DisconnectCB(event_copy.id);
                    rdma_destroy_id(event_copy.id);
                    return;
                default:
                    DIE("unknown event");
                }
            }
        };
        this->thread_pool_->AddJob(event_loop);
    }

protected:
    void PreConnectionCB(rdma_cm_id *id, int buffer_size)
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), buffer_size);
        TEST_Z(ctx->buffer_mr = ibv_reg_mr(context_->pd, ctx->buffer, buffer_size, 0));
        posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
        TEST_Z(ctx->msg_mr = ibv_reg_mr(context_->pd, ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE));
        PostReceive(id);
    }

    void CompletionCB(ibv_wc *wc)
    {
        rdma_cm_id *id = (rdma_cm_id *)(uintptr_t)(wc->wr_id);
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        if (wc->opcode & IBV_WC_RECV)
        {
            switch (ctx->msg->id)
            {
            case MSG_MR:
                ctx->peer_addr = ctx->msg->data.mr.addr;
                ctx->peer_rkey = ctx->msg->data.mr.rkey;
                // there is no need break
            case MSG_READY:
            {
                std::unique_lock<std::mutex> lg(record_lock_);
                record_cv_.wait(lg, [&]()
                                { return record_valid_; });
                char *dest = ((ConnectionContext *)id->context)->buffer;
                memcpy(dest, record_, record_len_);
                Send(id, record_len_);
                PostReceive(id);
                record_valid_ = false;
                record_cv_.notify_one();
            }
            break;
            case MSG_DONE:
                rdma_disconnect(id);
                return;
            default:
                return;
            }
        }
    }

    void Send(rdma_cm_id *id, uint32_t len) override
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        ibv_send_wr wr, *bad_wr = NULL;
        ibv_sge sge;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)id;
        wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.imm_data = htonl(len);
        wr.wr.rdma.remote_addr = ctx->peer_addr;
        wr.wr.rdma.rkey = ctx->peer_rkey;
        if (len)
        {
            wr.sg_list = &sge;
            wr.num_sge = 1;
            sge.addr = (uintptr_t)ctx->buffer;
            sge.length = len;
            sge.lkey = ctx->buffer_mr->lkey;
        }
        TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
    }

    void PostReceive(struct rdma_cm_id *id) override
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        ibv_recv_wr wr, *bad_wr = NULL;
        ibv_sge sge;
        memset(&wr, 0, sizeof(ibv_recv_wr));
        wr.wr_id = (uintptr_t)id;
        wr.sg_list = &sge;
        wr.num_sge = 1;
        sge.addr = (uintptr_t)ctx->msg;
        sge.length = sizeof(*ctx->msg);
        sge.lkey = ctx->msg_mr->lkey;
        TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
    }

    void InitContext(ibv_context *verbs)
    {
        if (context_)
        {
            if (context_->ctx != verbs)
                DIE("cannot handle events in more than one context.");
            return;
        }

        context_ = (Context *)malloc(sizeof(Context));
        context_->ctx = verbs;
        TEST_Z(context_->pd = ibv_alloc_pd(context_->ctx));
        TEST_Z(context_->comp_channel = ibv_create_comp_channel(context_->ctx));
        TEST_Z(context_->cq = ibv_create_cq(context_->ctx, 10, NULL, context_->comp_channel, 0)); /* cqe=10 is arbitrary */
        TEST_NZ(ibv_req_notify_cq(context_->cq, 0));

        // create poll thread
        auto poller = [&]()
        {
            ibv_cq *cq;
            ibv_wc wc;
            void *ctx = NULL;
            while (1)
            {
                TEST_NZ(ibv_get_cq_event(context_->comp_channel, &cq, &ctx));
                ibv_ack_cq_events(cq, 1);
                TEST_NZ(ibv_req_notify_cq(cq, 0));

                while (ibv_poll_cq(cq, 1, &wc))
                {
                    if (wc.status == IBV_WC_SUCCESS)
                    {
                        CompletionCB(&wc);
                    }
                    else
                    {
                        DIE("poll_cq: status is not IBV_WC_SUCCESS");
                    }
                }
            }
        };
        thread_pool_->AddJob(poller);
    }

    void InitConnection(rdma_cm_id *id)
    {
        InitContext(id->verbs);

        ibv_qp_init_attr qp_attr;
        memset(&qp_attr, 0, sizeof(ibv_qp_init_attr));
        qp_attr.send_cq = context_->cq;
        qp_attr.recv_cq = context_->cq;
        qp_attr.qp_type = IBV_QPT_RC;
        qp_attr.cap.max_send_wr = 10;
        qp_attr.cap.max_recv_wr = 10;
        qp_attr.cap.max_send_sge = 1;
        qp_attr.cap.max_recv_sge = 1;

        TEST_NZ(rdma_create_qp(id, context_->pd, &qp_attr));
    }

private:
    int timeout_;
    // record
    char *record_;
    int record_len_;
    bool record_valid_;
    std::mutex record_lock_;
    std::condition_variable record_cv_;
};