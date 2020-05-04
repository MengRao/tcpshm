#pragma once

// configurations that must be the same between server and client
struct CommonConf
{
    static const uint32_t NameSize = 16;
    static const uint32_t ShmQueueSize = 1024 * 1024; // must be power of 2
    static const bool ToLittleEndian = true; // set to the endian of majority of the hosts

    using LoginUserData = char;
    using LoginRspUserData = char;
};

template<uint32_t N, uint16_t MsgType>
struct MsgTpl
{
    static const uint16_t msg_type = MsgType;
    int val[N];
};

typedef MsgTpl<1, 1> Msg1;
typedef MsgTpl<2, 2> Msg2;
typedef MsgTpl<3, 3> Msg3;
typedef MsgTpl<4, 4> Msg4;

