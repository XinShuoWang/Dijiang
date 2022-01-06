#pragma once

#include "Debug.hpp"
#include "RdmaClientSocketHelper.h"

class RdmaClientSocket
{
public:
    RdmaClientSocket(const char *ip, const char *port, int thread_num, int buffer_size, int timeout)
    {
        // init
        speaker_ = NULL;
        channel_ = NULL;
        context_ = NULL;
        timeout_ = timeout;
        buffer_size_ = buffer_size;
        thread_pool_ = new ThreadPool(thread_num);
        // set callback
        RegisterCallback(buffer_size);
        // resolve address
        addrinfo *address = NULL;
        TEST_NZ(getaddrinfo(ip, port, NULL, &address));
        TEST_Z(channel_ = rdma_create_event_channel());
        TEST_NZ(rdma_create_id(channel_, &speaker_, NULL, RDMA_PS_TCP));
        TEST_NZ(rdma_resolve_addr(speaker_, NULL, address->ai_addr, timeout));
        freeaddrinfo(address);
        // set connection context
        speaker_->context = (ConnectionContext *)malloc(sizeof(ConnectionContext));
    }

    ~RdmaClientSocket()
    {
        free(context_);
        rdma_destroy_event_channel(channel_);
        delete thread_pool_;
    }

    void Loop()
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
                printf("RDMA_CM_EVENT_ADDR_RESOLVED \n");
                InitConnection(event_copy.id);
                pre_conn_cb_(event_copy.id);
                TEST_NZ(rdma_resolve_route(event_copy.id, timeout_));
                break;
            case RDMA_CM_EVENT_ROUTE_RESOLVED:
                printf("RDMA_CM_EVENT_ROUTE_RESOLVED \n");
                TEST_NZ(rdma_connect(event_copy.id, &params));
                break;
            case RDMA_CM_EVENT_ESTABLISHED:
                printf("RDMA_CM_EVENT_ESTABLISHED \n");
                // none
                break;
            case RDMA_CM_EVENT_DISCONNECTED:
                printf("RDMA_CM_EVENT_DISCONNECTED \n");
                rdma_destroy_qp(event_copy.id);
                rdma_destroy_id(event_copy.id);
                return;
            default:
                DIE("unknown event");
            }
        }
    }

    void Write(char *data, int size)
    {
    }

private:
    void RegisterCallback(const int bufferSize)
    {
        pre_conn_cb_ = [&](rdma_cm_id *id)
        {
            ConnectionContext *ctx = (ConnectionContext *)id->context;
            posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), bufferSize);
            TEST_Z(ctx->buffer_mr = ibv_reg_mr(context_->pd, ctx->buffer, bufferSize, 0));
            posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
            TEST_Z(ctx->msg_mr = ibv_reg_mr(context_->pd, ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE));
            ClientPostReceive(id);
        };

        completion_cb_ = [](ibv_wc *wc)
        {
            rdma_cm_id *id = (rdma_cm_id *)(uintptr_t)(wc->wr_id);
            ConnectionContext *ctx = (ConnectionContext *)id->context;
            if (wc->opcode & IBV_WC_RECV)
            {
                if (ctx->msg->id == MSG_MR)
                {
                    printf("received MR, sending file name\n");
                    ctx->peer_addr = ctx->msg->data.mr.addr;
                    ctx->peer_rkey = ctx->msg->data.mr.rkey;
                    memset(((ConnectionContext *)id->context)->buffer, 'a', 20);
                    ((ConnectionContext *)id->context)->buffer[21] = '\0';
                    ClientWriteRemote(id, 21);
                    printf("received MR, sending file name\n");
                }
                else if (ctx->msg->id == MSG_READY)
                {
                    printf("received READY, sending chunk\n");
                    memset(((ConnectionContext *)id->context)->buffer, 'a', 20);
                    ((ConnectionContext *)id->context)->buffer[21] = '\0';
                    ClientWriteRemote(id, 21);
                    printf("received READY, sending chunk\n");
                }
                else if (ctx->msg->id == MSG_DONE)
                {
                    printf("received DONE, disconnecting\n");
                    rdma_disconnect(id);
                    return;
                }
                ClientPostReceive(id);
            }
        };
    }

    void BuildQPAttribute(ibv_qp_init_attr *qp_attr)
    {
        memset(qp_attr, 0, sizeof(ibv_qp_init_attr));
        qp_attr->send_cq = context_->cq;
        qp_attr->recv_cq = context_->cq;
        qp_attr->qp_type = IBV_QPT_RC;
        qp_attr->cap.max_send_wr = 10;
        qp_attr->cap.max_recv_wr = 10;
        qp_attr->cap.max_send_sge = 1;
        qp_attr->cap.max_recv_sge = 1;
    }

    void BuildContext(ibv_context *verbs)
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
                        completion_cb_(&wc);
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
        ibv_qp_init_attr qp_attr;
        BuildContext(id->verbs);
        BuildQPAttribute(&qp_attr);
        TEST_NZ(rdma_create_qp(id, context_->pd, &qp_attr));
    }

    ConnectionCB pre_conn_cb_;
    CompletionCB completion_cb_;
    ThreadPool *thread_pool_;
    // context
    rdma_cm_id *speaker_;
    rdma_event_channel *channel_;
    Context *context_;
    int timeout_;
    int buffer_size_;
};