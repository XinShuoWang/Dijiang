#pragma once

#include "Context.h"
#include "Message.h"
#include "Context.h"
#include "Debug.hpp"
#include "ThreadPool.hpp"
#include "RdmaServerSocketHelper.h"

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

#include <thread>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class RdmaServerSocket
{
public:
    RdmaServerSocket(const char *port, const int thread_num)
    {
        // init
        thread_pool_ = new ThreadPool(thread_num);
        listener_ = NULL;
        channel_ = NULL;
        context_ = NULL;
        // init address
        memset(&address_, 0, sizeof(sockaddr_in6));
        address_.sin6_family = AF_INET6;
        address_.sin6_port = htons(atoi(port));
        // create channel
        TEST_Z(channel_ = rdma_create_event_channel());
        TEST_NZ(rdma_create_id(channel_, &listener_, NULL, RDMA_PS_TCP));
        TEST_NZ(rdma_bind_addr(listener_, (sockaddr *)&address_));
        TEST_NZ(rdma_listen(listener_, 10)); /* backlog=10 is arbitrary */
    }

    ~RdmaServerSocket()
    {
        rdma_destroy_id(listener_);
        rdma_destroy_event_channel(channel_);
        free(context_);
        delete thread_pool_;
    }

    void Loop()
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
                pre_conn_cb_(event_copy.id);
                TEST_NZ(rdma_accept(event_copy.id, &cm_params));
                break;
            case RDMA_CM_EVENT_ESTABLISHED:
                SAY("RDMA_CM_EVENT_ESTABLISHED");
                connect_cb_(event_copy.id);
                break;
            case RDMA_CM_EVENT_DISCONNECTED:
                SAY("RDMA_CM_EVENT_DISCONNECTED");
                rdma_destroy_qp(event_copy.id);
                disconnect_cb_(event_copy.id);
                rdma_destroy_id(event_copy.id);
                break;
            default:
                DIE("No matching event!");
            }
        }
    }

    void RegisterMessageCallback(std::function<void(char *, int)> func, const int bufferSize)
    {
        completion_cb_ = [func](ibv_wc *wc)
        {
            rdma_cm_id *id = (rdma_cm_id *)(uintptr_t)wc->wr_id;
            ConnectionContext *ctx = (ConnectionContext *)id->context;
            if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM)
            {
                uint32_t size = ntohl(wc->imm_data);
                if (size == 0)
                {
                    ctx->msg->id = MSG_DONE;
                    ServerSendMessage(id);
                }
                else
                {
                    func(ctx->buffer, size);
                    ServerPostReceive(id);
                    ctx->msg->id = MSG_READY;
                    ServerSendMessage(id);
                }
            }
        };
        RegisterCommonCallback(bufferSize);
    }

private:
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

    void RegisterCommonCallback(const int bufferSize)
    {
        pre_conn_cb_ = [&, bufferSize](rdma_cm_id *id)
        {
            ConnectionContext *ctx = (ConnectionContext *)malloc(sizeof(ConnectionContext));
            id->context = ctx;
            posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), bufferSize);
            TEST_Z(ctx->buffer_mr = ibv_reg_mr(context_->pd, ctx->buffer, bufferSize, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));
            posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
            TEST_Z(ctx->msg_mr = ibv_reg_mr(context_->pd, ctx->msg, sizeof(*ctx->msg), 0));
            ServerPostReceive(id);
        };

        connect_cb_ = [&](rdma_cm_id *id)
        {
            ConnectionContext *ctx = (ConnectionContext *)id->context;
            ctx->msg->id = MSG_MR;
            ctx->msg->data.mr.addr = (uintptr_t)ctx->buffer_mr->addr;
            ctx->msg->data.mr.rkey = ctx->buffer_mr->rkey;
            ServerSendMessage(id);
        };

        disconnect_cb_ = [](rdma_cm_id *id)
        {
            ConnectionContext *ctx = (ConnectionContext *)id->context;
            ibv_dereg_mr(ctx->buffer_mr);
            ibv_dereg_mr(ctx->msg_mr);
            free(ctx->buffer);
            free(ctx->msg);
            free(ctx);
        };
    }

    // task
    ConnectionCB pre_conn_cb_, connect_cb_, disconnect_cb_;
    CompletionCB completion_cb_;
    ThreadPool *thread_pool_;
    // rdma socket
    sockaddr_in6 address_;
    rdma_cm_id *listener_;
    rdma_event_channel *channel_;
    // context
    Context *context_;
};