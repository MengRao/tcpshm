#pragma once
#include <bits/stdc++.h>
#include "ptcp_conn.h"
#include "spsc_queue.h"
#include "mmap.h"

// single thread class
class TCPSHMConnection
{
public:
    TCPSHMConnection(const std::string& ptcp_dir, const std::string& local_name, const std::string& remote_name)
        : local_name_(local_name)
        , remote_name_(remote_name)
        , ptcp_conn_(ptcp_dir, local_name, remote_name) {
    }

    bool reset(int sock_fd, bool use_shm, int remote_ack_seq, int64_t now, int* local_ack_seq) {
        if(use_shm) {
            if(!shm_sendq_) {
                std::string shm_send_file = "/" + local_name_ + "_" + remote_name_ + ".shm";
                shm_sendq_ = my_mmap<SHMQ>(shm_send_file, true);
                if(!shm_sendq_) return false
            }
            if(!shm_recvq_) {
                std::string shm_recv_file = "/" + remote_name_ + "_" + local_name_ + ".shm";
                shm_recvq_ = my_mmap<SHMQ>(shm_recv_file, true);
                if(!shm_recvq_) return false
            }
        }
        else {
            if(shm_sendq_) {
                my_munmap<SHMQ>(shm_sendq_);
                shm_sendq_ = nullptr;
            }
            if(shm_recvq_) {
                my_munmap<SHMQ>(shm_recvq_);
                shm_recvq_ = nullptr;
            }
        }
        return ptcp_conn_.reset(sock_fd, use_shm, remote_ack_seq, now, local_ack_seq);
    }

    MsgHeader* Alloc(uint16_t size) {
        if(shm_sendq_) {
            return shm_sendq_->Alloc(size);
        }
        return ptcp_conn_.Alloc(size);
    }

    void Push() {
        if(shm_sendq_) {
            return shm_sendq_->Push<T>();
        }
        return ptcp_conn_.Push<T>();
    }

    // poll
    MsgHeader* Front(int64_t now) {
        ptcp_conn_.SendHB(now); // for shm, we need to send HB
        MsgHeader* ret = ptcp_conn_.Front(now); // for shm, we need to recv HB and Front() always return nullptr
        if(shm_recvq_) {
            return shm_recvq_->Front();
        }
        else {
            return ret;
        }
    }

    void Pop() {
        if(shm_recvq_) {
            return shm_recvq_->Pop();
        }
        return ptcp_conn_.Pop();
    }

private:
    std::string local_name_;
    std::string remote_name_;
    PTCPConnection ptcp_conn_;
    typedef SPSCVarQueue<128> SHMQ;
    SHMQ* shm_sendq_ = nullptr;
    SHMQ* shm_recvq_ = nullptr;
};

