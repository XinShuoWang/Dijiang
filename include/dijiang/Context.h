#pragma once

#include <rdma/rdma_cma.h>

struct Context
{
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_comp_channel *comp_channel;
};

struct ConnectionContext
{
    char *buffer;
    struct ibv_mr *buffer_mr;
    struct Message *msg;
    struct ibv_mr *msg_mr;
};