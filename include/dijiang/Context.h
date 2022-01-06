#pragma once

#include "Message.h"

#include <rdma/rdma_cma.h>

#include <functional>

struct Context
{
    ibv_context *ctx;
    ibv_pd *pd;
    ibv_cq *cq;
    ibv_comp_channel *comp_channel;
};

struct ConnectionContext {
    char *buffer;
    ibv_mr *buffer_mr;

    Message *msg;
    ibv_mr *msg_mr;

    uint64_t peer_addr;
    uint32_t peer_rkey;
};

typedef std::function<void(rdma_cm_id *)> ConnectionCB;
typedef std::function<void(ibv_wc *)> CompletionCB;