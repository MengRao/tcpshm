#include "../tcpshm_server.h"
#include <bits/stdc++.h>
#include "rdtsc.h"
#include "common.h"

using namespace std;

struct UserData
{
    int v = 0;
};

struct ServerConf : public CommonConf
{
    static const int64_t Second = 2000000000LL;

    static const int MaxNewConnections = 5;
    static const int MaxShmConnsPerGrp = 4;
    static const int MaxShmGrps = 2;
    static const int MaxTcpConnsPerGrp = 4;
    static const int MaxTcpGrps = 1;
    static const int TcpQueueSize = 2048; // must be multiple of 8
    static const int TcpRecvBufSize = 4096; // mulst be multiple of 8

    static const int64_t NewConnectionTimeout = 2 * Second;
    static const int64_t ConnectionTimeout = 10 * Second;
    static const int64_t HeartBeatInverval = 3 * Second;

    using ConnectionUserData = UserData;
};

class EchoServer;
using TSServer = TcpShmServer<EchoServer, ServerConf>;

class EchoServer
{
public:
    EchoServer(const std::string& ptcp_dir, const std::string& name)
        : srv(ptcp_dir, name, this) {
        // capture SIGTERM to gracefully stop the server
        // we can also send other signals to crash the server and see how it recovers on restart
        signal(SIGTERM, EchoServer::SignalHandler);
    }

    static void SignalHandler(int s) {
        stopped = true;
    }

    void Run(const char* listen_ipv4, uint16_t listen_port) {
        if(!srv.Start(listen_ipv4, listen_port)) return;
        vector<thread> threads;
        for(int i = 0; i < ServerConf::MaxTcpGrps; i++) {
            threads.emplace_back([this, i]() {
                while(!stopped) {
                    srv.PollTcp(rdtsc(), i);
                }
            });
        }

        for(int i = 0; i < ServerConf::MaxShmGrps; i++) {
            threads.emplace_back([this, i]() {
                while(!stopped) {
                    srv.PollShm(i);
                }
            });
        }

        while(!stopped) {
            srv.PollCtl(rdtsc());
        }

        for(auto& thr : threads) {
            thr.join();
        }
        srv.Stop();
    }

private:
    friend TSServer;
    void OnSystemError(const char* errno_msg, int sys_errno) {
        cout << "System Error: " << errno_msg << " syserrno: " << strerror(sys_errno) << endl;
    }

    // if accept, set user_data in login_rsp, and return grpid with respect to tcp or shm
    // else set error_msg in login_rsp if possible, and return -1
    int
    OnNewConnection(const struct sockaddr_in* addr, const TSServer::LoginMsg* login, TSServer::LoginRspMsg* login_rsp) {
        cout << "New Connection from: " << inet_ntoa(addr->sin_addr) << ":" << ntohs(addr->sin_port)
             << ", name: " << login->client_name << ", use_shm: " << (bool)login->use_shm << endl;
        auto hh = hash<string>{}(string(login->client_name));
        if(login->use_shm) {
            if(ServerConf::MaxShmGrps > 0) {
                return hh % ServerConf::MaxShmGrps;
            }
            else {
                strcpy(login_rsp->error_msg, "Shm disabled");
                return -1;
            }
        }
        else {
            if(ServerConf::MaxTcpGrps > 0) {
                return hh % ServerConf::MaxTcpGrps;
            }
            else {
                strcpy(login_rsp->error_msg, "Tcp disabled");
                return -1;
            }
        }
    }


    void OnClientLogon(struct sockaddr_in* addr, TSServer::Connection* conn) {
        cout << "Client Logon from: " << inet_ntoa(addr->sin_addr) << ":" << ntohs(addr->sin_port)
             << ", name: " << conn->GetRemoteName() << endl;
    }

    void OnClientDisconnected(TSServer::Connection* conn, const char* reason, int sys_errno) {
        cout << "Client disconnected, name: " << conn->GetRemoteName() << " reason: " << reason
             << " syserrno: " << strerror(sys_errno) << endl;
    }

    bool OnClientMsg(TSServer::Connection* conn, MsgHeader* recv_header) {
        auto size = recv_header->size - sizeof(MsgHeader);
        MsgHeader* send_header = conn->Alloc(size);
        if(!send_header) return false;
        send_header->msg_type = recv_header->msg_type;
        memcpy(send_header + 1, recv_header + 1, size);
        conn->Push();
        static int exp_v = 0;
        int v = *(int*)(recv_header + 1);
        cout << "v: " << v << endl;
        /*
        if(v != conn->user_data.v) {
            cout << "bad, exp_v: " << conn->user_data.v << " v: " << v << endl;
        }
        else {
            conn->user_data.v++;
        }
        */
        return true;
    }

    TSServer srv;
    static volatile bool stopped;
};

volatile bool EchoServer::stopped = false;

int main() {

    EchoServer server("server", "server");
    server.Run("0.0.0.0", 12345);
    cout << "server quit" << endl;

    return 0;
}
