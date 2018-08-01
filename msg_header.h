#pragma once
#include "endian.h"

namespace tcpshm {

struct MsgHeader
{
    // size of this msg, including header itself
    // auto set by lib, can be read by user
    uint16_t size;
    // msg type of app msg is set by user and must not be 0
    uint16_t msg_type;
    // internally used for ptcp, must not be modified by user
    uint32_t ack_seq;

    template<bool ToLittle>
    void ConvertByteOrder() {
        Endian<ToLittle> ed;
        ed.ConvertInPlace(size);
        ed.ConvertInPlace(msg_type);
        ed.ConvertInPlace(ack_seq);
    }
};
} // namespace tcpshm

