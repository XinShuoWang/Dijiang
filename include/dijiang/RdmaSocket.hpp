#pragma once

#include "Debug.hpp"
#include "Context.h"
#include "ThreadPool.hpp"

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

#include <thread>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class RdmaSocket
{
public:
    RdmaSocket(int thread_num)
    {
        // set callback
        pre_conn_cb_ = [](rdma_cm_id *)
        { return; };
        connect_cb_ = [](rdma_cm_id *)
        { return; };
        disconnect_cb_ = [](rdma_cm_id *)
        { return; };
        completion_cb_ = [](ibv_wc *)
        { return; };
        // init resources
        thread_pool_ = new ThreadPool(thread_num);
        context_ = NULL;
        TEST_Z(channel_ = rdma_create_event_channel());
        TEST_NZ(rdma_create_id(channel_, &id_, NULL, RDMA_PS_TCP));
    }

    virtual ~RdmaSocket()
    {
        rdma_destroy_id(id_);
        rdma_destroy_event_channel(channel_);
        free(context_);
        delete thread_pool_;
    }

    virtual void Loop() = 0;

protected:
    virtual void SetPreConnectionCB(int buffer_size) {}

    virtual void SetOnConnectionCB() {}

    virtual void SetDisconnectCB()
    {
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

    virtual void SetCompletionCB() {}

    virtual void Send(rdma_cm_id *id, uint32_t len) = 0;

    virtual void PostReceive(rdma_cm_id *id) = 0;

    virtual void InitContext(ibv_context *verbs)
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
        for(int i = 0; i < thread_pool_->GetThreadNum(); ++i) thread_pool_->AddJob(poller);
    }

    virtual void InitConnection(rdma_cm_id *id)
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

protected:
    // callback
    ConnectionCB pre_conn_cb_, connect_cb_, disconnect_cb_;
    CompletionCB completion_cb_;
    ThreadPool *thread_pool_;
    rdma_cm_id *id_;
    rdma_event_channel *channel_;
    Context *context_;
};