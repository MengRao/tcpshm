#pragma once
#include "ptcp_queue.h"

// single thread class
class PTCPConnection
{
public:
    PTCPConnection(const std::string& ptcp_dir, const std::string& local_name, const std::string& remote_name)
        : ptcp_dir_(ptcp_dir)
        , local_name_(local_name)
        , remote_name_(remote_name) {
    }

    bool reset(int sock_fd, bool use_shm, int remote_ack_seq, int64_t now, int* local_ack_seq) {
        Close();
        if(use_shm) {
            if(pq_) {
                my_munmap<PTCPQ>(pq_);
                pq_ = nullptr;
            }
        }
        else {
            if(!pq_) {
                std::string ptcp_send_file = ptcp_dir + "/" + local_name_ + "_" + remote_name_ + ".ptcp";
                pq_ = my_mmap<PTCPQ>(ptcp_send_file, false);
                if(!pq_) return false;
            }
        }
        // we could just use "hbmsg_ = new MsgHeader" for shm, but for dealloc consistency we do the same
        if(!hbmsg_) {
            std::string hb_msg_file = ptcp_dir + "/" + local_name + "_" + remote_name_ + ".seq";
            hbmsg_ = my_mmap<MsgHeader>(hb_msg_file, false);
            if(!hbmsg_) return false;
        }
        if(pq_) {
            pq_->Ack(remote_ack_seq);
        }
        sockfd_ = sock_fd;
        active_time_ = last_hb_time_ = now;
        *local_ack_seq = hbmsg_->seq_num;
        return true;
    }

    MsgHeader* Alloc(uint16_t size) {
        return pq_->Alloc(size);
    }

    void Push() {
        pq_->Push();
        if(!closed_) {
            SendPending();
        }
    }

    // safe if IsClosed
    MsgHeader* Front(int64_t now) {
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
                        Close();
                        return nullptr;
                    }
                    if(header->seq_num < hb_msg_->seq_num) { // duplicate msg, ignore...
                        readidx_ += msg_size;
                        continue;
                    }
                    // we have to Pop() this msg before calling the next Front()
                    return header;
                }
                if(msg_size > kBufSize) { // msg too large that we can't handle
                    Close();
                    return nullptr;
                }
                if(write_size + msg_size > kBufSize) {
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

            int ret = ::recv(sockfd_, recvbuf_ + writeidx_, kBufSize - writeidx_, 0);
            if(ret <= 0) {
                if(ret < 0) {
                    if(errno == EAGAIN) {
                        if(now - active_time_ > kTimeOutInterval) {
                            Close();
                        }
                        return nullptr;
                    }
                }
                // ret == 0 or ret < 0 for other errno
                Close();
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
            if(send < 0 && errno == EAGAIN) {
                return;
            }
            if(send != sizeof(MsgHeader)) { // for simplicity, we see partial sendout as error
                Close();
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
            if(send < 0) {
                if(errno != EAGAIN || (size & 7)) {
                    Close();
                    return false;
                }
                else
                    break;
            }
            p += sent;
            size -= sent;
        }
        int sent_blk = blk_sz - (size >> 3);
        pq_->Sentout(sent_blk);
        return size == 0;
    }

    bool IsClosed() {
        return sockfd_ < 0;
    }

    void Close() {
        if(sockfd_ >= 0) {
            ::close(sockfd_);
            sockfd_ = -1;
            if(pq_) {
                pq_->Disconnect();
            }
            // don't reset readidx_ and writeidx_ for further Front() and Push()
            // writeidx_ = readidx_ = 0;
        }
    }

    bool UseShm() {
        return pq_ == nullptr;
    }

private:
    static const int kBufSize = 8 * 1024;
    static const int kTimeOutInterval = 12345678;
    static const int kHeartBeatInterval = 1234567;
    std::string ptcp_dir_;
    std::string local_name_;
    std::string remote_name_;
    typedef PTCPQueue<1024> PTCPQ;
    PTCPQ* pq_ = nullptr; // may be mmaped to file, or nullptr
    int sockfd_ = -1;
    char recvbuf_[kBufSize] __attribute__((aligned(8));
    int writeidx_ = 0;
    int readidx_ = 0;;
    int64_t active_time_ = 0;
    int64_t last_hb_time_ = 0;
    MsgHeader* hbmsg_ = nullptr; // must be mmaped to file, even for shm
};

