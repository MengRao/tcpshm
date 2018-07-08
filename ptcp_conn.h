#pragma once
#include "ptcp_queue.h"
#include "mmap.h"
#include "string.h"

// HeartbeatMsg is special that it only has MsgHeader
struct HeartbeatMsg
{
    static const int msg_type = 0;
};

template<class Conf>
struct LoginMsgTpl
{
    static const int msg_type = 1;
    char client_name[Conf::NameSize];
    char last_server_name[Conf::NameSize];
    char use_shm;
    typename Conf::LoginUserData user_data;
};

template<class Conf>
struct LoginRspMsgTpl
{
    static const int msg_type = 2;
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
    }

    bool Reset(const char* ptcp_send_file,
               const char* ptcp_ack_seq_file,
               bool use_shm,
               uint32_t* local_ack_seq,
               const char** error_msg) {
        if(!use_shm && !pq_) {
            pq_ = my_mmap<PTCPQ>(ptcp_send_file, false, error_msg);
            if(!pq_) return false;
        }
        if(!hbmsg_) {
            hbmsg_ = my_mmap<MsgHeader>(ptcp_ack_seq_file, false, error_msg);
            if(!hbmsg_) return false;
            hbmsg_->size = sizeof(MsgHeader);
            hbmsg_->msg_type = HeartbeatMsg::msg_type;
        }
        *local_ack_seq = hbmsg_->seq_num;
        return true;
    }

    void Release() {
        Close("Release", 0);
        if(pq_) {
            my_munmap<PTCPQ>(pq_);
            pq_ = nullptr;
        }
        if(hbmsg_) {
            my_munmap<MsgHeader>(hbmsg_);
            hbmsg_ = nullptr;
        }
    }

    void Open(int sock_fd, uint32_t remote_ack_seq, int64_t now) {
        Close("Reconnect", 0);
        if(pq_) {
            pq_->Ack(remote_ack_seq);
        }
        sockfd_ = sock_fd;
        req_close_ = false;
        active_time_ = last_hb_time_ = now;
    }

    MsgHeader* Alloc(uint16_t size) {
        return pq_->Alloc(size);
    }

    void Push() {
        pq_->Push();
        SendPending();
    }

    // safe if IsClosed
    MsgHeader* Front(int64_t now) {
        if(req_close_) {
            req_close_ = false;
            Close("Request close", 0);
            return nullptr;
        }
        while(true) {
            int remain_size = writeidx_ - readidx_;
            if(remain_size >= 8) { // we've read the next header
                MsgHeader* header = (MsgHeader*)(recvbuf_ + readidx_);
                int msg_size = (header->size + 7) & -8; // round up to the nearest multiple of 8
                if(remain_size >= msg_size) {
                    // we have got a full msg
                    active_time_ = now;
                    if(header->msg_type == HeartbeatMsg::msg_type) { // heartbeat msg
                        if(pq_) {
                            pq_->Ack(header->seq_num);
                        }
                        readidx_ += msg_size;
                        continue;
                    }
                    // handle app msgs...
                    if(UseShm()) { // we dont expect app msgs in tcp channel in shm mode
                        Close("Got Tcp app msg in shm mode", 0);
                        return nullptr;
                    }
                    if(header->seq_num < hbmsg_->seq_num) { // duplicate msg, ignore...
                        readidx_ += msg_size;
                        continue;
                    }
                    // we have to Pop() this msg before calling the next Front()
                    return header;
                }
                if(msg_size > Conf::TcpRecvBufSize) {
                    Close("Msg size larger than recv buf", 0);
                    return nullptr;
                }
                if(writeidx_ + msg_size > Conf::TcpRecvBufSize) {
                    memmove(recvbuf_, recvbuf_ + readidx_, remain_size);
                    writeidx_ = remain_size;
                    readidx_ = 0;
                }
            }
            else if(remain_size == 0) {
                writeidx_ = readidx_ = 0;
            }
            // for 0 < remain_size < 8, there must be enough buf space to read the entire header
            // because all msgs and recvbuf_ itself are 8 types aligned

            int ret = ::recv(sockfd_, recvbuf_ + writeidx_, Conf::TcpRecvBufSize - writeidx_, 0);
            if(ret <= 0) {
                if(ret < 0) {
                    if(errno == EAGAIN) {
                        if(now - active_time_ > kTimeOutInterval) {
                            Close("Timeout", 0);
                        }
                        return nullptr;
                    }
                }
                // ret == 0 or ret < 0 for other errno
                if(ret == 0)
                    Close("Remote close", 0);
                else
                    Close("Recv error", errno);
                return nullptr;
            }
            writeidx_ += ret;
        }
    }

    // we have consumed the msg we got from Front()
    void Pop() {
        MsgHeader* header = (MsgHeader*)(recvbuf_ + readidx_);
        hbmsg_->seq_num = header->seq_num + 1;
        readidx_ += (header->size + 7) & -8;
    }

    // safe if IsClosed
    void SendHB(int64_t now) {
        if((UseShm() || SendPending()) && now - last_hb_time_ >= kHeartBeatInterval) {
            int sent = ::send(sockfd_, hbmsg_, sizeof(MsgHeader), 0);
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
        const char* p = (char*)pq_->GetSendable(&blk_sz);
        if(blk_sz == 0) return true;
        uint32_t size = blk_sz << 3;
        while(size > 0) {
            int sent = ::send(sockfd_, p, size, 0);
            if(sent < 0) {
                if(errno != EAGAIN || (size & 7)) {
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
        pq_->Sendout(sent_blk);
        return size == 0;
    }

    bool IsClosed() {
        return sockfd_ < 0;
    }

    const char* getCloseReason(int& sys_errno) {
        sys_errno = close_errno_;
        return close_reason_;
    }

    void RequestClose() {
        req_close_ = true;
    }

    void Close(const char* reason, int sys_errno) {
        if(sockfd_ < 0) return;
        ::close(sockfd_);
        sockfd_ = -1;
        if(pq_) {
            pq_->Disconnect();
        }
        writeidx_ = readidx_ = 0;
        close_reason_ = reason;
        close_errno_ = sys_errno;
    }

    bool UseShm() {
        return pq_ == nullptr;
    }

private:
    static const int kTimeOutInterval = 12345678;
    static const int kHeartBeatInterval = 1234567;
    typedef PTCPQueue<Conf::TcpQueueSize> PTCPQ;
    PTCPQ* pq_ = nullptr; // may be mmaped to file, or nullptr
    int sockfd_ = -1;
    bool req_close_ = false;
    const char* close_reason_ = "nil";
    int close_errno_ = 0;
    static_assert((Conf::TcpRecvBufSize & 7) == 0, "Conf::TcpRecvBufSize must be multiple of 8");
    alignas(8) char recvbuf_[Conf::TcpRecvBufSize];
    int writeidx_ = 0;
    int readidx_ = 0;;
    int64_t active_time_ = 0;
    int64_t last_hb_time_ = 0;
    MsgHeader* hbmsg_ = nullptr; // must be mmaped to file, even for shm
};

