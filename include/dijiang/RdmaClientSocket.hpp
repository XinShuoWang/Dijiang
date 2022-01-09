#pragma once

#include "RdmaSocket.hpp"

class RdmaClientSocket : protected RdmaSocket
{
public:
    RdmaClientSocket(const char *ip, const char *port, int thread_num, int buffer_size, int timeout) : RdmaSocket(thread_num, buffer_size)
    {
        // init
        timeout_ = timeout;
        // resolve address
        addrinfo *address = NULL;
        TEST_NZ(getaddrinfo(ip, port, NULL, &address));
        TEST_NZ(rdma_resolve_addr(id_, NULL, address->ai_addr, timeout));
        freeaddrinfo(address);
        // set connection context
        id_->context = (ConnectionContext *)malloc(sizeof(ConnectionContext));
    }

    ~RdmaClientSocket() override {}

    void Loop() override
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
                PreConnectionCB(event_copy.id, buffer_size_);
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
                DisconnectCB(event_copy.id);
                rdma_destroy_id(event_copy.id);
                return;
            default:
                DIE("unknown event");
            }
        }
    }

protected:
    void PreConnectionCB(rdma_cm_id *id, int buffer_size) override
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), buffer_size);
        TEST_Z(ctx->buffer_mr = ibv_reg_mr(context_->pd, ctx->buffer, buffer_size, 0));
        posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
        TEST_Z(ctx->msg_mr = ibv_reg_mr(context_->pd, ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE));
        PostReceive(id);
    }

    void CompletionCB(ibv_wc *wc) override
    {
        rdma_cm_id *id = (rdma_cm_id *)(uintptr_t)(wc->wr_id);
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        if (wc->opcode & IBV_WC_RECV)
        {
            if (ctx->msg->id == MSG_MR)
            {
                SAY("received MR, sending file name\n");
                ctx->peer_addr = ctx->msg->data.mr.addr;
                ctx->peer_rkey = ctx->msg->data.mr.rkey;
                memset(((ConnectionContext *)id->context)->buffer, 'a', 20);
                ((ConnectionContext *)id->context)->buffer[21] = '\0';
                Send(id, 21);
                SAY("received MR, sending file name\n");
            }
            else if (ctx->msg->id == MSG_READY)
            {
                SAY("received READY, sending chunk\n");
                memset(((ConnectionContext *)id->context)->buffer, 'a', 20);
                ((ConnectionContext *)id->context)->buffer[21] = '\0';
                Send(id, 21);
                printf("received READY, sending chunk\n");
            }
            else if (ctx->msg->id == MSG_DONE)
            {
                SAY("received DONE, disconnecting\n");
                rdma_disconnect(id);
                return;
            }
            PostReceive(id);
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

private:
    int timeout_;
};