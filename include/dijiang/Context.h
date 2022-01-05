#pragma once

#include "Message.h"

#include <rdma/rdma_cma.h>

struct Context
{
    ibv_context *ctx;
    ibv_pd *pd;
    ibv_cq *cq;
    ibv_comp_channel *comp_channel;
};

struct ServerConnectionContext
{
    char *buffer;
    ibv_mr *buffer_mr;
    Message *msg;
    ibv_mr *msg_mr;
};

struct ClientConnectionContext
{
    char *buffer;
    ibv_mr *buffer_mr;

    Message *msg;
    ibv_mr *msg_mr;

    uint64_t peer_addr;
    uint32_t peer_rkey;
};