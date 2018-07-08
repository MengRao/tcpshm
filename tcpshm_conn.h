#pragma once
#include "ptcp_conn.h"
#include "spsc_varq.h"
#include "mmap.h"


template<class Conf>
class TcpShmConnection
{
public:
    TcpShmConnection() {
        remote_name_[0] = 0;
    }

    void init(const char* ptcp_dir, const char* local_name) {
        ptcp_dir_ = ptcp_dir;
        local_name_ = local_name;
    }

    bool Reset(bool use_shm, uint32_t* local_ack_seq, const char** error_msg) {
        std::string ptcp_send_file = std::string(ptcp_dir_) + "/" + local_name_ + "_" + remote_name_ + ".ptcp";
        std::string ptcp_ack_seq_file = std::string(ptcp_dir_) + "/" + local_name_ + "_" + remote_name_ + ".seq";
        std::string shm_send_file = std::string("/") + local_name_ + "_" + remote_name_ + ".shm";
        std::string shm_recv_file = std::string("/") + remote_name_ + "_" + local_name_ + ".shm";

        if(use_shm) {
            if(!shm_sendq_) {
                shm_sendq_ = my_mmap<SHMQ>(shm_send_file.c_str(), true, error_msg);
                if(!shm_sendq_) return false;
            }
            if(!shm_recvq_) {
                shm_recvq_ = my_mmap<SHMQ>(shm_recv_file.c_str(), true, error_msg);
                if(!shm_recvq_) return false;
            }
        }
        return ptcp_conn_.Reset(ptcp_send_file.c_str(), ptcp_ack_seq_file.c_str(), use_shm, local_ack_seq, error_msg);
    }

    void Release() {
        remote_name_[0] = 0;
        if(shm_sendq_) {
            my_munmap<SHMQ>(shm_sendq_);
            shm_sendq_ = nullptr;
        }
        if(shm_recvq_) {
            my_munmap<SHMQ>(shm_recvq_);
            shm_recvq_ = nullptr;
        }
        ptcp_conn_.Release();
    }

    void Open(int sock_fd, uint32_t remote_ack_seq, int64_t now) {
        ptcp_conn_.Open(sock_fd, remote_ack_seq, now);
    }

    bool IsClosed() {
        return ptcp_conn_.IsClosed();
    }

    const char* getCloseReason(int& sys_errno) {
        return ptcp_conn_.getCloseReason(sys_errno);
    }

    char* GetRemoteName() {
        return remote_name_;
    }

    MsgHeader* Alloc(uint16_t size) {
        if(shm_sendq_) {
            return shm_sendq_->Alloc(size);
        }
        return ptcp_conn_.Alloc(size);
    }

    void Push() {
        if(shm_sendq_) {
            return shm_sendq_->Push();
        }
        return ptcp_conn_.Push();
    }

    MsgHeader* TcpFront(int64_t now) {
        ptcp_conn_.SendHB(now); 
        return ptcp_conn_.Front(now); // for shm, we need to recv HB and Front() always return nullptr
    }

    void TcpPop() {
        ptcp_conn_.Pop();
    }

    MsgHeader* ShmFront() {
        return shm_recvq_->Front();
    }

    void ShmPop() {
        shm_recvq_->Pop();
    }


private:
    const char* local_name_;
    char remote_name_[Conf::NameSize];
    const char* ptcp_dir_ = nullptr;
    PTCPConnection<Conf> ptcp_conn_;
    typedef SPSCVarQueue<Conf::ShmQueueSize> SHMQ;
    alignas(64) SHMQ* shm_sendq_ = nullptr;
    SHMQ* shm_recvq_ = nullptr;
};

