#pragma once

#include "Debug.hpp"
#include "Message.h"
#include "Context.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rdma/rdma_cma.h>

static void PostReceive(rdma_cm_id *id)
{
    ibv_recv_wr wr, *bad_wr = NULL;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)id;
    wr.sg_list = NULL;
    wr.num_sge = 0;
    TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}

static void SendMessage(rdma_cm_id *id)
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