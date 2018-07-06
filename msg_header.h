#pragma once

struct MsgHeader
{
    uint16_t size; // size of this msg, including MsgHeader
    uint16_t msg_type;
    uint32_t seq_num; // used in ptcp, not used in shm
};

