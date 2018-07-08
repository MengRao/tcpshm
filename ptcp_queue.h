#pragma once
#include "msg_header.h"

// Simple single thread persist Queue that can be mmap-ed to a file
template<uint32_t Bytes>
class PTCPQueue
{
public:
    static const uint32_t BLK_CNT = Bytes / sizeof(MsgHeader);

    MsgHeader* Alloc(uint16_t size) {
        size += sizeof(MsgHeader);
        uint32_t blk_sz = (size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        uint32_t avail_sz = BLK_CNT - write_idx_;
        if(blk_sz > avail_sz) {
            if(blk_sz > avail_sz + read_idx_) return nullptr;
            memmove(blk_, blk_ + read_idx_, (write_idx_ - read_idx_) * sizeof(MsgHeader));
            write_idx_ -= read_idx_;
            send_idx_ -= read_idx_;
            read_idx_ = 0;
        }
        MsgHeader& header = blk_[write_idx_];
        header.size = size;
        // header.msg_type = T::msg_type;
        return &header;
    }

    void Push() {
        MsgHeader& header = blk_[write_idx_];
        uint32_t blk_sz = (header.size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        header.seq_num = seq_num_++; // seq_num starts from 0
        write_idx_ += blk_sz;
    }

    MsgHeader* GetSendable(int* blk_sz) {
        *blk_sz = write_idx_ - send_idx_;
        return blk_ + send_idx_;
    }

    void Sendout(int blk_sz) {
        send_idx_ += blk_sz;
    }

    void Disconnect() {
        send_idx_ = read_idx_;
    }

    // the next seq_num peer side expect
    void Ack(int ack_seq) {
        while(read_idx_ < write_idx_) {
            if(blk_[read_idx_].seq_num >= ack_seq) break;
            read_idx_ += (blk_[read_idx_].size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        }
        if(read_idx_ == write_idx_) {
            read_idx_ = write_idx_ = send_idx_ = 0;
        }
        else if(send_idx_ < read_idx_) { // could happen on reconnect recovery
            send_idx_ = read_idx_;
        }
    }

private:
    MsgHeader blk_[BLK_CNT];
    // invariant: read_idx_ <= send_idx_ <= write_idx_
    // where send_idx_ may point to the middle of a msg
    uint32_t write_idx_;
    uint32_t read_idx_;
    uint32_t send_idx_;
    uint32_t seq_num_;
};

