#pragma once
#include "msg_header.h"

// HeartbeatMsg is special that it only has MsgHeader
struct HeartbeatMsg
{
    static const int msg_type = 0;
};

struct LoginMsg
{
    static const int msg_type = 1;
    char client_name[16];
    char last_server_name[16];
    char use_shm;
    // add more fields... e.g. username and password
} __attribute__((aligned(8)));

struct LoginRspMsg
{
    static const int msg_type = 2;
    char server_name[16];
    int error_code;
} __attribute__((aligned(8)));
