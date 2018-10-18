/*
MIT License

Copyright (c) 2018 Meng Rao <raomeng1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include "ptcp_conn.h"
#include "spsc_varq.h"
#include "mmap.h"

namespace tcpshm {

template<class Conf>
class TcpShmConnection
{
public:
    std::string GetPtcpFile() {
        return std::string(ptcp_dir_) + "/" + local_name_ + "_" + remote_name_ + ".ptcp";
    }

    bool IsClosed() {
        return ptcp_conn_.IsClosed();
    }

    // Close this connection
    void Close() {
        ptcp_conn_.RequestClose();
    }

    const char* GetCloseReason(int* sys_errno) {
        return ptcp_conn_.GetCloseReason(sys_errno);
    }

    char* GetRemoteName() {
        return remote_name_;
    }

    const char* GetLocalName() {
        return local_name_;
    }

    const char* GetPtcpDir() {
        return ptcp_dir_;
    }

    // allocate a msg of specified size in send queue
    // the returned address is guaranteed to be 8 byte aligned
    // return nullptr if no enough space
    MsgHeader* Alloc(uint16_t size) {
        if(shm_sendq_) return shm_sendq_->Alloc(size);
        return ptcp_conn_.Alloc(size);
    }

    // submit the last msg from Alloc() and send out
    void Push() {
        if(shm_sendq_)
            shm_sendq_->Push();
        else
            ptcp_conn_.Push();
    }

    // for shm, same as Push
    // for tcp, don't send out immediately as we have more to push
    void PushMore() {
        if(shm_sendq_)
            shm_sendq_->Push();
        else
            ptcp_conn_.PushMore();
    }

    // get the next msg from recv queue, return nullptr if queue is empty
    // the returned address is guaranteed to be 8 byte aligned
    // if caller dont call Pop() later, it will get the same msg again
    // user dont need to call Front() directly as polling functions will do it
    MsgHeader* Front() {
        if(shm_recvq_) return shm_recvq_->Front();
        return ptcp_conn_.Front();
    }

    // consume the msg we got from Front() or polling function
    void Pop() {
        if(shm_recvq_)
            shm_recvq_->Pop();
        else
            ptcp_conn_.Pop();
    }

    typename Conf::ConnectionUserData user_data;

private:
    template<class T1, class T2>
    friend class TcpShmClient;
    template<class T1, class T2>
    friend class TcpShmServer;

    TcpShmConnection() {
        remote_name_[0] = 0;
    }

    void init(const char* ptcp_dir, const char* local_name) {
        ptcp_dir_ = ptcp_dir;
        local_name_ = local_name;
    }

    bool OpenFile(bool use_shm, const char** error_msg) {
        if(use_shm) {
            std::string shm_send_file = std::string("/") + local_name_ + "_" + remote_name_ + ".shm";
            std::string shm_recv_file = std::string("/") + remote_name_ + "_" + local_name_ + ".shm";
            if(!shm_sendq_) {
                shm_sendq_ = my_mmap<SHMQ>(shm_send_file.c_str(), true, error_msg);
                if(!shm_sendq_) return false;
            }
            if(!shm_recvq_) {
                shm_recvq_ = my_mmap<SHMQ>(shm_recv_file.c_str(), true, error_msg);
                if(!shm_recvq_) return false;
            }
            return true;
        }
        std::string ptcp_send_file = GetPtcpFile();
        return ptcp_conn_.OpenFile(ptcp_send_file.c_str(), error_msg);
    }

    bool GetSeq(uint32_t* local_ack_seq, uint32_t* local_seq_start, uint32_t* local_seq_end, const char** error_msg) {
        if(shm_sendq_) return true;
        if(!ptcp_conn_.GetSeq(local_ack_seq, local_seq_start, local_seq_end)) {
            *error_msg = "Ptcp file corrupt";
            errno = 0;
            return false;
        }
        return true;
    }

    void Reset() {
        if(shm_sendq_) {
            memset(shm_sendq_, 0, sizeof(SHMQ));
            memset(shm_recvq_, 0, sizeof(SHMQ));
        }
        else {
            ptcp_conn_.Reset();
        }
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

    bool TryCloseFd() {
        return ptcp_conn_.TryCloseFd();
    }

    MsgHeader* TcpFront(int64_t now) {
        ptcp_conn_.SendHB(now);
        return ptcp_conn_.Front(); // for shm, we need to recv HB and Front() always return nullptr
    }

    MsgHeader* ShmFront() {
        return shm_recvq_->Front();
    }

private:
    const char* local_name_;
    char remote_name_[Conf::NameSize];
    const char* ptcp_dir_ = nullptr;
    PTCPConnection<Conf> ptcp_conn_;
    using SHMQ = SPSCVarQueue<Conf::ShmQueueSize>;
    alignas(64) SHMQ* shm_sendq_ = nullptr;
    SHMQ* shm_recvq_ = nullptr;
};
} // namespace tcpshm
