#pragma once

namespace tcpshm {

struct MsgHeader
{
    // size of this msg, including header itself
    // auto set by lib, can be read by user
    uint16_t size;
    // msg type of app msg is set by user and starts from 3
    uint16_t msg_type;
    // internally used for ptcp, must not be modified by user
    uint32_t ack_seq;
};
} // namespace tcpshm

