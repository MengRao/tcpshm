#include "tcpshm_server.h"
#include <bits/stdc++.h>
#include "rdtsc.h"

using namespace std;

struct ServerConf
{
    static const int64_t Second = 2000000000LL;

    static const int NameSize = 16;
    static const int MaxNewConnections = 5;
    static const int MaxShmConnsPerGrp = 4;
    static const int MaxShmGrps = 2;
    static const int MaxTcpConnsPerGrp = 4;
    static const int MaxTcpGrps = 1;
    static const int ShmQueueSize = 2048; // must be power of 2
    static const int TcpQueueSize = 2048; // must be multiple of 8
    static const int TcpRecvBufSize = 4096; // mulst be multiple of 8

    static const int64_t NewConnectionTimeout = 2 * Second;
    static const int64_t ConnectionTimeout = 10 * Second;
    static const int64_t HeartBeatInverval = 3 * Second;

    using LoginUserData = char;
    using LoginRspUserData = char;
};

class MyServer;
using TSServer = TcpShmServer<MyServer, ServerConf>;

class MyServer
{
public:
    MyServer(const std::string& ptcp_dir, const std::string& name)
        : srv(ptcp_dir, name, this) {
    }

    void Run(const char* listen_ipv4, uint16_t listen_port) {
        if(!srv.Start(listen_ipv4, listen_port)) return;
        vector<thread> threads;
        for(int i = 0; i < ServerConf::MaxTcpGrps; i++) {
            threads.emplace_back([this, i]() {
                while(true) {
                    srv.PollTcp(rdtsc(), i);
                }
            });
        }

        for(int i = 0; i < ServerConf::MaxShmGrps; i++) {
            threads.emplace_back([this, i]() {
                while(true) {
                    srv.PollShm(i);
                }
            });
        }

        while(true) {
            srv.PollCtl(rdtsc());
        }
    }

private:
    friend TSServer;
    void OnSystemError(const char* errno_msg, int sys_errno) {
        cout << "Server Err: " << errno_msg << " syserrno: " << strerror(sys_errno) << endl;
    }

    // if accept, set user_data in login_rsp, and return grpid with respect to tcp or shm
    // else set error_msg in login_rsp if possible, and return -1
    int
    OnNewConnection(const struct sockaddr_in* addr, const TSServer::LoginMsg* login, TSServer::LoginRspMsg* login_rsp) {
        return 0;
    }


    void OnClientLogon(struct sockaddr_in* addr, TSServer::Connection* conn) {
    }

    void OnClientDisconnected(TSServer::Connection* conn) {
        cout << "client disconnected" << conn->GetRemoteName() << endl;
    }

    bool OnClientMsg(TSServer::Connection* conn, MsgHeader* head) {
        return true;
    }

    TSServer srv;
};

int main() {

    MyServer server("server", "server");
    server.Run("127.0.0.1", 12345);

    return 0;
}
