#pragma once

#include <cstdlib>

enum MessageId
{
    MSG_INVALID = 0,
    MSG_MR,
    MSG_READY,
    MSG_DONE
};

struct Message
{
    int id;
    union
    {
        struct
        {
            uint64_t addr;
            uint32_t rkey;
        } mr;
    } data;
};