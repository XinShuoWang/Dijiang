#pragma once

#include "RdmaSocket.hpp"

typedef std::function<void(char *, int)> ServerHandler;

class RdmaServerSocket : protected RdmaSocket
{
public:
    RdmaServerSocket(const char *port, const int thread_num, const int buffer_size) : RdmaSocket(thread_num, buffer_size)
    {
        // init address
        sockaddr_in6 address;
        memset(&address, 0, sizeof(sockaddr_in6));
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(atoi(port));
        // bind and listen
        TEST_NZ(rdma_bind_addr(id_, (sockaddr *)&address));
        TEST_NZ(rdma_listen(id_, 10)); /* backlog=10 is arbitrary */
    }

    ~RdmaServerSocket() override {}

    void RegisterHandler(ServerHandler handler) { handler_ = handler; }

    void Loop() override
    {
        // init params
        rdma_conn_param cm_params;
        memset(&cm_params, 0, sizeof(cm_params));
        cm_params.initiator_depth = cm_params.responder_resources = 1;
        cm_params.rnr_retry_count = 7; /* infinite retry */

        // EventLoop
        rdma_cm_event *event = NULL;
        while (rdma_get_cm_event(channel_, &event) == 0)
        {
            rdma_cm_event event_copy;
            memcpy(&event_copy, event, sizeof(*event));
            rdma_ack_cm_event(event);

            switch (event_copy.event)
            {
            case RDMA_CM_EVENT_CONNECT_REQUEST:
                SAY("RDMA_CM_EVENT_CONNECT_REQUEST");
                InitConnection(event_copy.id);
                PreConnectionCB(event_copy.id, buffer_size_);
                TEST_NZ(rdma_accept(event_copy.id, &cm_params));
                break;
            case RDMA_CM_EVENT_ESTABLISHED:
                SAY("RDMA_CM_EVENT_ESTABLISHED");
                OnConnectionCB(event_copy.id);
                break;
            case RDMA_CM_EVENT_DISCONNECTED:
                SAY("RDMA_CM_EVENT_DISCONNECTED");
                rdma_destroy_qp(event_copy.id);
                DisconnectCB(event_copy.id);
                rdma_destroy_id(event_copy.id);
                break;
            default:
                DIE("No matching event!");
            }
        }
    }

protected:
    void PreConnectionCB(rdma_cm_id *id, int buffer_size) override
    {
        ConnectionContext *ctx = (ConnectionContext *)malloc(sizeof(ConnectionContext));
        id->context = ctx;
        posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), buffer_size);
        TEST_Z(ctx->buffer_mr = ibv_reg_mr(context_->pd, ctx->buffer, buffer_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
        posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
        TEST_Z(ctx->msg_mr = ibv_reg_mr(context_->pd, ctx->msg, sizeof(*ctx->msg), 0));
        PostReceive(id);
    }

    void OnConnectionCB(rdma_cm_id *id) override
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        ctx->msg->id = MSG_MR;
        ctx->msg->data.mr.addr = (uintptr_t)ctx->buffer_mr->addr;
        ctx->msg->data.mr.rkey = ctx->buffer_mr->rkey;
        Send(id, sizeof(*ctx->msg));
    }

    void CompletionCB(ibv_wc *wc) override
    {
        rdma_cm_id *id = (rdma_cm_id *)(uintptr_t)wc->wr_id;
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM)
        {
            uint32_t size = ntohl(wc->imm_data);
            if (size == 0)
            {
                ctx->msg->id = MSG_DONE;
                Send(id, sizeof(*ctx->msg));
            }
            else
            {
                {
                    handler_(ctx->buffer, size);
                }
                PostReceive(id);
                ctx->msg->id = MSG_READY;
                Send(id, sizeof(*ctx->msg));
            }
        }
    }

    void PostReceive(rdma_cm_id *id) override
    {
        ibv_recv_wr wr, *bad_wr = NULL;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)id;
        wr.sg_list = NULL;
        wr.num_sge = 0;
        TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
    }

    void Send(rdma_cm_id *id, uint32_t len) override
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        ibv_send_wr wr, *bad_wr = NULL;
        ibv_sge sge;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)id;
        wr.opcode = IBV_WR_SEND;
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.send_flags = IBV_SEND_SIGNALED;
        sge.addr = (uintptr_t)ctx->msg;
        sge.length = sizeof(*ctx->msg);
        sge.lkey = ctx->msg_mr->lkey;
        TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
    }

private:
    ServerHandler handler_;
};