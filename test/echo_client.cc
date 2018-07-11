#include "../tcpshm_client.h"
#include <bits/stdc++.h>
#include "rdtsc.h"
#include "common.h"

using namespace std;

struct ClientConf : public CommonConf
{
    static const int64_t Second = 2000000000LL;

    static const int TcpQueueSize = 40960;   // must be multiple of 8
    static const int TcpRecvBufSize = 40960; // must be multiple of 8

    static const int64_t ConnectionTimeout = 10 * Second;
    static const int64_t HeartBeatInverval = 3 * Second;

    using ConnectionUserData = int;
};

class EchoClient;
using TSClient = TcpShmClient<EchoClient, ClientConf>;

class EchoClient
{
public:
    EchoClient(const std::string& ptcp_dir, const std::string& name)
        : cli(ptcp_dir, name, this)
        , conn(cli.getConnection()) {
        srand(time(NULL));
    }

    void Run(bool use_shm, const char* server_ipv4, uint16_t server_port) {
        if(!cli.Connect(use_shm, server_ipv4, server_port)) return;
        string send_num_file =
            string(conn->GetPtcpDir()) + "/" + conn->GetLocalName() + "_" + conn->GetRemoteName() + ".send_num";
        string recv_num_file =
            string(conn->GetPtcpDir()) + "/" + conn->GetLocalName() + "_" + conn->GetRemoteName() + ".recv_num";
        const char* error_msg;
        send_num = my_mmap<int>(send_num_file.c_str(), false, &error_msg);
        recv_num = my_mmap<int>(recv_num_file.c_str(), false, &error_msg);
        if(!send_num || !recv_num) {
            cout << "System Error: " << error_msg << " syserrno: " << strerror(errno) << endl;
            return;
        }
        cout << "client started, send_num: " << *send_num << " recv_num: " << *recv_num << endl;
        long before = rdtsc();
        vector<thread> threads;
        if(use_shm) {
            thread shm_thr([this]() {
                while(!conn->IsClosed()) {
                    if(PollNum()) {
                        conn->RequestClose();
                        break;
                    }
                    cli.PollShm();
                }
            });

            while(!conn->IsClosed()) {
                cli.PollTcp(rdtsc());
            }
            shm_thr.join();
        }
        else {
            while(!conn->IsClosed()) {
                if(PollNum()) {
                    conn->RequestClose();
                    break;
                }
                cli.PollTcp(rdtsc());
            }
        }
        long latency = rdtsc() - before;
        cli.Stop();
        cout << "client stopped, send_num: " << *send_num << " recv_num: " << *recv_num << " latency: " << latency
             << endl;
    }

private:
    bool PollNum() {
        if(*send_num < MaxNum) {
            if(slow && *send_num != *recv_num) return false;
            int tp = rand() % 4 + 1;
            if(tp == 1) {
                TrySendMsg<Msg1>();
            }
            else if(tp == 2) {
                TrySendMsg<Msg2>();
            }
            else if(tp == 3) {
                TrySendMsg<Msg3>();
            }
            else if(tp == 4) {
                TrySendMsg<Msg4>();
            }
        }
        else {
            if(*send_num == *recv_num) return true;
        }
        return false;
    }

    template<class T>
    bool TrySendMsg() {
        MsgHeader* header = conn->Alloc(sizeof(T));
        if(!header) return false;
        header->msg_type = T::msg_type;
        T* msg = (T*)(header + 1);
        for(auto& v : msg->val) v = (*send_num)++;
        // msg->time = rdtsc();
        conn->Push();
        // cout << "sent, send_num: " << (*send_num) << endl;
        return true;
    }

    template<class T>
    void handleMsg(T* msg) {
        for(auto v : msg->val) {
            if(v != *recv_num) {
                cout << "bad: v: " << v << " recv_num: " << (*recv_num) << endl;
                exit(1);
            }
            // cout << "ok v: " << v << endl;
            (*recv_num)++;
        }
    }

private:
    friend TSClient;
    void OnSystemError(const char* error_msg, int sys_errno) {
        cout << "System Error: " << error_msg << " syserrno: " << strerror(sys_errno) << endl;
    }

    void FillLoginUserData(ClientConf::LoginUserData* login_user_data) {
    }

    void OnLoginReject(const TSClient::LoginRspMsg* login_rsp) {
        cout << "Login Rejected: " << login_rsp->error_msg << endl;
    }

    int64_t OnLoginSuccess(const TSClient::LoginRspMsg* login_rsp) {
        cout << "Login Success" << endl;
        return rdtsc();
    }

    bool OnServerMsg(MsgHeader* header) {
        auto msg_type = header->msg_type;
        if(msg_type == 1) {
            handleMsg((Msg1*)(header + 1));
        }
        else if(msg_type == 2) {
            handleMsg((Msg2*)(header + 1));
        }
        else if(msg_type == 3) {
            handleMsg((Msg3*)(header + 1));
        }
        else if(msg_type == 4) {
            handleMsg((Msg4*)(header + 1));
        }
        else {
            assert(false);
        }
        conn->Pop();
    }

    void OnDisconnected(const char* reason, int sys_errno) {
        cout << "Client disconnected reason: " << reason << " syserrno: " << strerror(sys_errno) << endl;
    }

private:
    static const int MaxNum = 10000000;
    TSClient cli;
    TSClient::Connection* conn;
    volatile bool stopped = false;
    bool slow = false;
    int* send_num;
    int* recv_num;
};

int main() {

    EchoClient client("client", "client");
    client.Run(false, "127.0.0.1", 12345);

    return 0;
}

