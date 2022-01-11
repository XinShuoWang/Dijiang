#pragma once

#include "Debug.hpp"
#include "Context.h"
#include "ThreadPool.hpp"

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>

#include <thread>
#include <mutex>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class RdmaSocket
{
public:
    RdmaSocket(int thread_num, int buffer_size)
    {
        // init resources
        thread_pool_ = new ThreadPool(thread_num);
        buffer_size_ = buffer_size;
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
    virtual void DisconnectCB(rdma_cm_id *id)
    {
        ConnectionContext *ctx = (ConnectionContext *)id->context;
        ibv_dereg_mr(ctx->buffer_mr);
        ibv_dereg_mr(ctx->msg_mr);
        free(ctx->buffer);
        free(ctx->msg);
        free(ctx);
    }

    virtual void Send(rdma_cm_id *id, uint32_t len) = 0;

    virtual void PostReceive(rdma_cm_id *id) = 0;

protected:
    ThreadPool *thread_pool_;
    rdma_cm_id *id_;
    rdma_event_channel *channel_;
    Context *context_;
    int buffer_size_;
};