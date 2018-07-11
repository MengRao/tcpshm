#pragma once
#include "msg_header.h"
#include <bits/stdc++.h>

// Simple single thread persist Queue that can be mmap-ed to a file
template<uint32_t Bytes>
class PTCPQueue
{
public:
    static_assert(Bytes % sizeof(MsgHeader) == 0, "Bytes must be multiple of 8");
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
        /*
        std::cout << "Alloc read_idx_: " << read_idx_ << " send_idx_: " << send_idx_ << " write_idx_: " << write_idx_
                  << std::endl;
                  */
        return &header;
    }

    void Push() {
        MsgHeader& header = blk_[write_idx_];
        uint32_t blk_sz = (header.size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        header.ack_seq = ack_seq_num_;
        write_idx_ += blk_sz;
        /*
        std::cout << "Push read_idx_: " << read_idx_ << " send_idx_: " << send_idx_ << " write_idx_: " << write_idx_
                  << std::endl;
                  */
    }

    MsgHeader* GetSendable(int& blk_sz) {
        blk_sz = write_idx_ - send_idx_;
        return blk_ + send_idx_;
    }

    void Sendout(int blk_sz) {
        send_idx_ += blk_sz;
        /*
        std::cout << "Sendout read_idx_: " << read_idx_ << " send_idx_: " << send_idx_ << " write_idx_: " << write_idx_
                  << std::endl;
                  */
    }

    void Disconnect() {
        send_idx_ = read_idx_;
    }

    // the next seq_num peer side expect
    void Ack(uint32_t ack_seq) {
        /*
        std::cout << "Ack ack_seq: " << ack_seq << " read_seq_num_: " << read_seq_num_ << " read_idx_: " << read_idx_
                  << " send_idx_: " << send_idx_ << " write_idx_: " << write_idx_ << std::endl;
                  */
        while(read_idx_ < write_idx_) {
            if(read_seq_num_ >= ack_seq) break;
            read_idx_ += (blk_[read_idx_].size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
            read_seq_num_++;
        }
        if(read_idx_ == write_idx_) {
            read_idx_ = write_idx_ = send_idx_ = 0;
        }
        else if(send_idx_ < read_idx_) { // could happen on reconnect recovery
            send_idx_ = read_idx_;
        }
    }

    uint32_t& MyAck() {
        return ack_seq_num_;
    }

    void Print() {
        std::cout << "Print read_idx_: " << read_idx_ << " send_idx_: " << send_idx_ << " write_idx_: " << write_idx_
                  << " read_seq_num_: " << read_seq_num_ << " ack_seq_num_: " << ack_seq_num_ << std::endl;
    }

private:
    MsgHeader blk_[BLK_CNT];
    // invariant: read_idx_ <= send_idx_ <= write_idx_
    // where send_idx_ may point to the middle of a msg
    uint32_t write_idx_;
    uint32_t read_idx_;
    uint32_t send_idx_;
    uint32_t read_seq_num_; // the seq_num_ of msg read_idx_ points to
    uint32_t ack_seq_num_;
};

