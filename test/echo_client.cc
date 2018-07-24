#include "../tcpshm_client.h"
#include <bits/stdc++.h>
#include "rdtsc.h"
#include "common.h"
#include "cpupin.h"

using namespace std;
using namespace tcpshm;

struct ClientConf : public CommonConf
{
    // as the program is using rdtsc to measure time difference, Second is CPU frequency
    static const int64_t Second = 2000000000LL;

    static const int TcpQueueSize = 10240;   // must be multiple of 8
    static const int TcpRecvBufSize = 10240; // must be multiple of 8

    static const int64_t ConnectionTimeout = 10 * Second;
    static const int64_t HeartBeatInverval = 3 * Second;

    using ConnectionUserData = char;
};

class EchoClient;
using TSClient = TcpShmClient<EchoClient, ClientConf>;

class EchoClient : public TSClient
{
public:
    EchoClient(const std::string& ptcp_dir, const std::string& name)
        : TSClient(ptcp_dir, name)
        , conn(GetConnection()) {
        srand(time(NULL));
    }

    void Run(bool use_shm, const char* server_ipv4, uint16_t server_port) {
        if(!Connect(use_shm, server_ipv4, server_port, 0)) return;
        // we mmap the send and recv number to file in case of program crash
        string send_num_file =
            string(conn.GetPtcpDir()) + "/" + conn.GetLocalName() + "_" + conn.GetRemoteName() + ".send_num";
        string recv_num_file =
            string(conn.GetPtcpDir()) + "/" + conn.GetLocalName() + "_" + conn.GetRemoteName() + ".recv_num";
        const char* error_msg;
        send_num = my_mmap<int>(send_num_file.c_str(), false, &error_msg);
        recv_num = my_mmap<int>(recv_num_file.c_str(), false, &error_msg);
        if(!send_num || !recv_num) {
            cout << "System Error: " << error_msg << " syserrno: " << strerror(errno) << endl;
            return;
        }
        cout << "client started, send_num: " << *send_num << " recv_num: " << *recv_num << endl;
        long before = rdtsc();
        if(use_shm) {
            thread shm_thr([this]() {
                // uncommment below cpupins to get more stable latency
                cpupin(6);
                while(!conn.IsClosed()) {
                    if(PollNum()) {
                        conn.Close();
                        break;
                    }
                    PollShm();
                }
            });

            // we still need to poll tcp for heartbeats even if using shm
            cpupin(7);
            while(!conn.IsClosed()) {
                PollTcp(rdtsc());
            }
            shm_thr.join();
        }
        else {
            cpupin(7);
            while(!conn.IsClosed()) {
                if(PollNum()) {
                    conn.Close();
                    break;
                }
                PollTcp(rdtsc());
            }
        }
        long latency = rdtsc() - before;
        Stop();
        cout << "client stopped, send_num: " << *send_num << " recv_num: " << *recv_num << " latency: " << latency
             << endl;
    }

private:
    bool PollNum() {
        if(*send_num < MaxNum) {
            // for slow mode, we wait to recv an echo msg before sending the next one
            if(slow && *send_num != *recv_num) return false;
            // we randomly send one of the 4 msgs
            int tp = 1; // rand() % 4 + 1;
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
            // if all echo msgs are got, we are done
            if(*send_num == *recv_num) return true;
        }
        return false;
    }

    template<class T>
    bool TrySendMsg() {
        MsgHeader* header = conn.Alloc(sizeof(T));
        if(!header) return false;
        header->msg_type = T::msg_type;
        T* msg = (T*)(header + 1);
        for(auto& v : msg->val) v = (*send_num)++;
        conn.Push();
        return true;
    }

    template<class T>
    void handleMsg(T* msg) {
        for(auto v : msg->val) {
            if(v != *recv_num) {
                cout << "bad: v: " << v << " recv_num: " << (*recv_num) << endl;
                exit(1);
            }
            (*recv_num)++;
        }
    }

private:
    friend TSClient;
    // called within Connect()
    // reporting errors on connecting to the server
    void OnSystemError(const char* error_msg, int sys_errno) {
        cout << "System Error: " << error_msg << " syserrno: " << strerror(sys_errno) << endl;
    }

    // called within Connect()
    // Login rejected by server
    void OnLoginReject(const LoginRspMsg* login_rsp) {
        cout << "Login Rejected: " << login_rsp->error_msg << endl;
    }

    // called within Connect()
    // confirmation for login success
    int64_t OnLoginSuccess(const LoginRspMsg* login_rsp) {
        cout << "Login Success" << endl;
        return rdtsc();
    }

    // called within Connect()
    // server and client ptcp sequence number don't match, we need to fix it manually
    void OnSeqNumberMismatch(uint32_t local_ack_seq,
                             uint32_t local_seq_start,
                             uint32_t local_seq_end,
                             uint32_t remote_ack_seq,
                             uint32_t remote_seq_start,
                             uint32_t remote_seq_end) {
        cout << "Seq number mismatch, name: " << conn.GetRemoteName() << " ptcp file: " << conn.GetPtcpFile()
             << " local_ack_seq: " << local_ack_seq << " local_seq_start: " << local_seq_start
             << " local_seq_end: " << local_seq_end << " remote_ack_seq: " << remote_ack_seq
             << " remote_seq_start: " << remote_seq_start << " remote_seq_end: " << remote_seq_end << endl;
    }

    // called by APP thread
    void OnServerMsg(MsgHeader* header) {
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
        conn.Pop();
    }

    // called by tcp thread
    void OnDisconnected(const char* reason, int sys_errno) {
        cout << "Client disconnected reason: " << reason << " syserrno: " << strerror(sys_errno) << endl;
    }

private:
    static const int MaxNum = 4000000;
    Connection& conn;
    // set slow to false to send msgs as fast as it can
    bool slow = true;
    int* send_num;
    int* recv_num;
};

int main(int argc, const char** argv) {
    if(argc != 4) {
        cout << "usage: echo_client NAME SERVER_IP USE_SHM[0|1]" << endl;
        exit(1);
    }
    const char* name = argv[1];
    const char* server_ip = argv[2];
    bool use_shm = argv[3][0] != '0';

    EchoClient client(name, name);
    client.Run(use_shm, server_ip, 12345);

    return 0;
}

