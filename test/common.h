#pragma once

// configurations that must be the same between server and client
struct CommonConf
{
    static const int NameSize = 16;
    static const int ShmQueueSize = 2048; // must be power of 2

    using LoginUserData = char;
    using LoginRspUserData = char;
};

template<uint32_t N, uint16_t MsgType>
struct MsgTpl
{
    static const uint16_t msg_type = MsgType;
    int val[N];
};

typedef MsgTpl<4, 1> Msg1;
typedef MsgTpl<9, 2> Msg2;
typedef MsgTpl<17, 3> Msg3;
typedef MsgTpl<50, 4> Msg4;

