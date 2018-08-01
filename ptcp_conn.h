#pragma once
#include "ptcp_queue.h"
#include "mmap.h"
#include "string.h"

namespace tcpshm {

// HeartbeatMsg is special that it only has MsgHeader
struct HeartbeatMsg
{
    static const uint16_t msg_type = 0;
};

// Note that we allow user to reuse msg_type 1 and 2
// Because LoginMsg and LoginRspMsg are expected only at the beginning of a connection
template<class Conf>
struct LoginMsgTpl
{
    static const uint16_t msg_type = 1;
    uint32_t client_seq_start;
    uint32_t client_seq_end;
    // user can put more information in user_data for auth, such as username, password...
    typename Conf::LoginUserData user_data;

    // below are all char types, no alignment requirement
    char use_shm;
    char client_name[Conf::NameSize];
    char last_server_name[Conf::NameSize];

    void ConvertByteOrder() {
        Endian<Conf::ToLittleEndian> ed;
        ed.ConvertInPlace(client_seq_start);
        ed.ConvertInPlace(client_seq_end);
    }
};

template<class Conf>
struct LoginRspMsgTpl
{
    static const uint16_t msg_type = 2;
    uint32_t server_seq_start;
    uint32_t server_seq_end;
    typename Conf::LoginRspUserData user_data;

    // below are all char types, no alignment requirement
    char status; // 0: OK, 1: seqnum mismatch, 2: other error
    char server_name[Conf::NameSize];
    char error_msg[32]; // empty error_msg means success

    void ConvertByteOrder() {
        Endian<Conf::ToLittleEndian> ed;
        ed.ConvertInPlace(server_seq_start);
        ed.ConvertInPlace(server_seq_end);
    }
};

// Single thread class except RequestClose()
template<class Conf>
class PTCPConnection
{
public:
    PTCPConnection() {
        hbmsg_.size = sizeof(MsgHeader);
        hbmsg_.msg_type = HeartbeatMsg::msg_type;
        hbmsg_.ack_seq = 0;
        hbmsg_.ConvertByteOrder<Conf::ToLittleEndian>();
    }

    bool OpenFile(const char* ptcp_queue_file,
                  const char** error_msg) {
        if(!q_) {
            q_ = my_mmap<PTCPQ>(ptcp_queue_file, false, error_msg);
            if(!q_) return false;
        }
        return true;
    }

    bool GetSeq(uint32_t* local_ack_seq, uint32_t* local_seq_start, uint32_t* local_seq_end) {
        *local_ack_seq = q_->MyAck();
        return q_->SanityCheckAndGetSeq(local_seq_start, local_seq_end);
    }

    void Reset() {
        memset(q_, 0, sizeof(PTCPQ));
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
        sockfd_ = sock_fd;
        writeidx_ = readidx_ = nextmsg_idx_ = 0;
        recv_time_ = send_time_ = now_ = now;
        if(q_) {
            q_->LoginAck(remote_ack_seq);
            SendPending();
        }
    }

    MsgHeader* Alloc(uint16_t size) {
        return q_->Alloc(size);
    }

    void Push() {
        q_->Push();
        SendPending();
    }

    void PushMore() {
        q_->Push();
    }

    // safe if IsClosed
    MsgHeader* Front() {
        if(UseShm()) { // for shm, we only expect HB in tcp channel so just read something and ignore
            DoRecv(sizeof(MsgHeader));
            return nullptr;
        }
        while(nextmsg_idx_ != readidx_) {
            MsgHeader* header = (MsgHeader*)(recvbuf_ + readidx_);
            if(header->msg_type == HeartbeatMsg::msg_type) {
                readidx_ += sizeof(MsgHeader);
                continue;
            }
            if(last_my_ack_ == q_->MyAck()) break;
            last_my_ack_ = q_->MyAck();
            return header;
        }
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

        if(int len = DoRecv(Conf::TcpRecvBufSize - writeidx_)) {
            int old_writeidx = writeidx_;
            writeidx_ += len;
            while(writeidx_ - nextmsg_idx_ >= 8) {
                MsgHeader* header = (MsgHeader*)(recvbuf_ + nextmsg_idx_);
                if(old_writeidx - nextmsg_idx_ < 8) { // we haven't converted this header
                    header->ConvertByteOrder<Conf::ToLittleEndian>();
                }
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
        }
        if(readidx_ != nextmsg_idx_) {
            return (MsgHeader*)(recvbuf_ + readidx_);
        }
        return nullptr;
    }

    // we have consumed the msg we got from Front()
    void Pop() {
        MsgHeader* header = (MsgHeader*)(recvbuf_ + readidx_);
        readidx_ += (header->size + 7) & -8;
        q_->MyAck()++;
    }

    // safe if IsClosed
    void SendHB(int64_t now) {
        now_ = now;
        if(now_ - send_time_ < Conf::HeartBeatInverval) return;
        if(q_) {
            if(SendPending()) return;
            hbmsg_.ack_seq = Endian<Conf::ToLittleEndian>::Convert(q_->MyAck());
        }
        int sent = ::send(sockfd_, &hbmsg_, sizeof(hbmsg_), MSG_NOSIGNAL);
        if(sent < 0 && errno == EAGAIN) return;
        if(sent != sizeof(MsgHeader)) { // for simplicity, we see partial sendout as error
            Close("Send error", sent < 0 ? errno : 0);
            return;
        }
        send_time_ = now_; // successfully sent
    }

    // return false only if no pending data to send
    bool SendPending() {
        if(IsClosed()) return false;
        int blk_sz;
        const char* p = (char*)q_->GetSendable(blk_sz);
        if(blk_sz == 0) return false;
        uint32_t size = blk_sz << 3;
        do {
            int sent = ::send(sockfd_, p, size, MSG_NOSIGNAL);
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
        } while(size > 0);
        int sent_blk = blk_sz - (size >> 3);
        if(sent_blk > 0) {
            send_time_ = now_;
            q_->Sendout(sent_blk);
        }
        return true;
    }

    bool IsClosed() {
        return sockfd_ < 0;
    }

    const char* GetCloseReason(int* sys_errno) {
        *sys_errno = close_errno_;
        return close_reason_;
    }

    void RequestClose() {
        Close("Request close", 0);
    }

    bool UseShm() {
        return q_ == nullptr;
    }

private:
    void Close(const char* reason, int sys_errno) {
        if(sockfd_ < 0) return;
        ::close(sockfd_);
        sockfd_ = -1;
        close_reason_ = reason;
        close_errno_ = sys_errno;
    }

    int DoRecv(int len) {
        if(len == 0) return 0;
        int ret = ::recv(sockfd_, recvbuf_ + writeidx_, len, 0);
        if(ret <= 0) {
            if(ret < 0) {
                if(errno == EAGAIN) {
                    if(now_ - recv_time_ > Conf::ConnectionTimeout) {
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
        recv_time_ = now_;
        return ret;
    }

private:
    typedef PTCPQueue<Conf::TcpQueueSize, Conf::ToLittleEndian> PTCPQ;
    PTCPQ* q_ = nullptr; // may be mmaped to file
    int sockfd_ = -1;
    const char* close_reason_ = "nil";
    int close_errno_ = 0;
    static_assert((Conf::TcpRecvBufSize % 8) == 0, "Conf::TcpRecvBufSize must be multiple of 8");
    alignas(8) char recvbuf_[Conf::TcpRecvBufSize];
    int writeidx_ = 0;
    int nextmsg_idx_ = 0;
    int readidx_ = 0;
    int64_t recv_time_ = 0;
    int64_t send_time_ = 0;
    int64_t now_ = 0;
    MsgHeader hbmsg_;

    uint32_t last_my_ack_ = 0;
};
} // namespace tcpshm
