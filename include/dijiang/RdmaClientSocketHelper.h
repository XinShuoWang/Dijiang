#pragma once

#include "Debug.hpp"
#include "Context.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rdma/rdma_cma.h>

static void ClientWriteRemote(struct rdma_cm_id *id, uint32_t len)
{
    ClientConnectionContext *ctx = (ClientConnectionContext *)id->context;
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

static void ClientPostReceive(struct rdma_cm_id *id)
{
    ClientConnectionContext *ctx = (ClientConnectionContext *)id->context;
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
