#pragma once
#include "ptcp_queue.h"
#include "mmap.h"
#include "string.h"

#include <bits/stdc++.h>

// HeartbeatMsg is special that it only has MsgHeader
struct HeartbeatMsg
{
    static const uint16_t msg_type = 0;
};

template<class Conf>
struct LoginMsgTpl
{
    static const uint16_t msg_type = 1;
    char client_name[Conf::NameSize];
    char last_server_name[Conf::NameSize];
    char use_shm;
    typename Conf::LoginUserData user_data;
};

template<class Conf>
struct LoginRspMsgTpl
{
    static const uint16_t msg_type = 2;
    char server_name[Conf::NameSize];
    char error_msg[32]; // empty error_msg means success
    typename Conf::LoginRspUserData user_data;
};

// Single thread class except RequestClose()
template<class Conf>
class PTCPConnection
{
public:
    PTCPConnection() {
        hbmsg_.size = sizeof(MsgHeader);
        hbmsg_.msg_type = HeartbeatMsg::msg_type;
    }

    bool Reset(const char* ptcp_queue_file, bool use_shm, uint32_t* local_ack_seq, const char** error_msg) {
        if(!use_shm && !q_) {
            q_ = my_mmap<PTCPQ>(ptcp_queue_file, false, error_msg);
            if(!q_) return false;
        }
        if(q_)
            *local_ack_seq = q_->MyAck();
        else
            *local_ack_seq = 0;
        return true;
    }

    void Release() {
        Close("Release", 0);
        if(q_) {
            my_munmap<PTCPQ>(q_);
            q_ = nullptr;
        }
    }

    void Open(int sock_fd, uint32_t remote_ack_seq, int64_t now) {
        Close("Reconnect", 0);
        if(q_) {
            q_->Ack(remote_ack_seq);
        }
        sockfd_ = sock_fd;
        writeidx_ = readidx_ = nextmsg_idx_ = 0;
        active_time_ = last_hb_time_ = now;
    }

    MsgHeader* Alloc(uint16_t size) {
        return q_->Alloc(size);
    }

    void Push() {
        q_->Push();
        // std::cout << "Push" << std::endl;
        SendPending();
    }

    // safe if IsClosed
    MsgHeader* Front(int64_t now) {
        if(UseShm()) { // for shm, we only expect HB in tcp channel so just read something and ignore
            DoRecv(now, sizeof(MsgHeader));
            return nullptr;
        }
        while(nextmsg_idx_ != readidx_) {
            MsgHeader* header = (MsgHeader*)(recvbuf_ + readidx_);
            if(header->msg_type == HeartbeatMsg::msg_type) {
                readidx_ += sizeof(MsgHeader);
                continue;
            }
            /*
            std::cout << "Front 1, readidx_: " << readidx_ << " nextmsg_idx_: " << nextmsg_idx_
                      << " writeidx_: " << writeidx_ << std::endl;
                      */
            if(last_my_ack_ == q_->MyAck()) break;
            last_my_ack_ = q_->MyAck();
            return header;
        }
        /*
        if(readidx_ == writeidx_) {
            readidx_ = writeidx_ = nextmsg_idx_ = 0;
        }
        */
        if(readidx_ > 0) {
            int remain_size = writeidx_ - readidx_;
            if(remain_size > 0) {
                if(readidx_ >= remain_size) {
                    memcpy(recvbuf_, recvbuf_ + readidx_, remain_size);
                }
                else {
                    memmove(recvbuf_, recvbuf_ + readidx_, remain_size);
                }
            }
            writeidx_ = remain_size;
            nextmsg_idx_ -= readidx_;
            readidx_ = 0;
        }

        writeidx_ += DoRecv(now, Conf::TcpRecvBufSize - writeidx_);
        while(writeidx_ - nextmsg_idx_ >= 8) {
            MsgHeader* header = (MsgHeader*)(recvbuf_ + nextmsg_idx_);
            q_->Ack(header->ack_seq);
            int msg_size = (header->size + 7) & -8;
            if(msg_size > Conf::TcpRecvBufSize) {
                Close("Msg size larger than recv buf", 0);
                return nullptr;
            }
            if(writeidx_ - nextmsg_idx_ < msg_size) break;
            // we have got a full msg
            if(header->msg_type == HeartbeatMsg::msg_type && readidx_ == nextmsg_idx_) {
                readidx_ += msg_size;
            }
            nextmsg_idx_ += msg_size;
        }
        if(readidx_ != nextmsg_idx_) {
            /*
            std::cout << "Front 2, readidx_: " << readidx_ << " nextmsg_idx_: " << nextmsg_idx_
                      << " writeidx_: " << writeidx_ << std::endl;
                      */
            return (MsgHeader*)(recvbuf_ + readidx_);
        }
        return nullptr;
    }

    // we have consumed the msg we got from Front()
    void Pop() {
        MsgHeader* header = (MsgHeader*)(recvbuf_ + readidx_);
        readidx_ += (header->size + 7) & -8;
        q_->MyAck()++;
        // std::cout << "Pop" << std::endl;
        /*
        std::cout << "Pop, readidx_: " << readidx_ << " nextmsg_idx_: " << nextmsg_idx_ << " writeidx_: " << writeidx_
                  << std::endl;
                  */
    }

    // safe if IsClosed
    void SendHB(int64_t now) {
        if((UseShm() || SendPending()) && now - last_hb_time_ >= Conf::HeartBeatInverval) {
            hbmsg_.ack_seq = q_->MyAck();
            int sent = ::send(sockfd_, &hbmsg_, sizeof(hbmsg_), MSG_NOSIGNAL);
            if(sent < 0 && errno == EAGAIN) {
                return;
            }
            if(sent != sizeof(MsgHeader)) { // for simplicity, we see partial sendout as error
                Close("Send error", sent < 0 ? errno : 0);
                return;
            }
            last_hb_time_ = now; // successfully sent
        }
    }

    // return true if all pending data is sent out
    // safe if IsClosed
    bool SendPending() {
        int blk_sz;
        const char* p = (char*)q_->GetSendable(&blk_sz);
        if(blk_sz == 0) return true;
        uint32_t size = blk_sz << 3;
        while(size > 0) {
            int sent = ::send(sockfd_, p, size, MSG_NOSIGNAL);
            if(sent < 0) {
                if(errno != EAGAIN || (size & 7)) {
                    // std::cout << "SendPending error" << std::endl;
                    Close("Send error", errno);
                    return false;
                }
                else
                    break;
            }
            p += sent;
            size -= sent;
        }
        int sent_blk = blk_sz - (size >> 3);
        q_->Sendout(sent_blk);
        return size == 0;
    }

    bool IsClosed() {
        return sockfd_ < 0;
    }

    const char* GetCloseReason(int& sys_errno) {
        sys_errno = close_errno_;
        return close_reason_;
    }

    void RequestClose() {
        Close("Request close", 0);
    }

    void Close(const char* reason, int sys_errno) {
        if(sockfd_ < 0) return;
        std::cout << "Close: " << reason << std::endl;
        ::close(sockfd_);
        sockfd_ = -1;
        if(q_) {
            q_->Disconnect();
        }
        // writeidx_ = readidx_ = nextmsg_idx_ = 0;
        close_reason_ = reason;
        close_errno_ = sys_errno;
    }

    bool UseShm() {
        return q_ == nullptr;
    }

private:
    int DoRecv(int64_t now, int len) {
        int ret = ::recv(sockfd_, recvbuf_ + writeidx_, len, 0);
        if(ret <= 0) {
            if(ret < 0) {
                if(errno == EAGAIN) {
                    if(now - active_time_ > Conf::ConnectionTimeout) {
                        Close("Timeout", 0);
                    }
                    return 0;
                }
            }
            // ret == 0 or ret < 0 for other errno
            if(ret == 0)
                Close("Remote close", 0);
            else
                Close("Recv error", errno);
            return 0;
        }
        active_time_ = now;
        return ret;
    }

private:
    typedef PTCPQueue<Conf::TcpQueueSize> PTCPQ;
    PTCPQ* q_ = nullptr; // may be mmaped to file, or nullptr
    int sockfd_ = -1;
    const char* close_reason_ = "nil";
    int close_errno_ = 0;
    static_assert((Conf::TcpRecvBufSize % 8) == 0, "Conf::TcpRecvBufSize must be multiple of 8");
    alignas(8) char recvbuf_[Conf::TcpRecvBufSize];
    int writeidx_ = 0;
    int nextmsg_idx_ = 0;
    int readidx_ = 0;
    int64_t active_time_ = 0;
    int64_t last_hb_time_ = 0;
    MsgHeader hbmsg_;

    uint32_t last_my_ack_ = 0;
};

