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
#include "msg_header.h"
#include <string.h>

namespace tcpshm {

// Simple single thread persist Queue that can be mmap-ed to a file
template<uint32_t Bytes, bool ToLittleEndian>
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
        return &header;
    }

    void Push() {
        MsgHeader& header = blk_[write_idx_];
        uint32_t blk_sz = (header.size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        header.ack_seq = ack_seq_num_;
        header.ConvertByteOrder<ToLittleEndian>();
        write_idx_ += blk_sz;
    }

    MsgHeader* GetSendable(int& blk_sz) {
        blk_sz = write_idx_ - send_idx_;
        return blk_ + send_idx_;
    }

    void Sendout(int blk_sz) {
        send_idx_ += blk_sz;
    }

    void LoginAck(uint32_t ack_seq) {
        Ack(ack_seq);
        send_idx_ = read_idx_;
    }

    // the next seq_num peer side expect
    void Ack(uint32_t ack_seq) {
        if((int)(ack_seq - read_seq_num_) <= 0) return; // if ack_seq is not newer than read_seq_num_
        // we assume that a successfuly logined client will not attack us
        // so_seq will never go beyond the msg write_idx_ points to during a connection lifecycle
        do {
            read_idx_ +=
                (Endian<ToLittleEndian>::Convert(blk_[read_idx_].size) + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
            read_seq_num_++;
        } while(read_seq_num_ != ack_seq);
        if(read_idx_ == write_idx_) {
            read_idx_ = write_idx_ = send_idx_ = 0;
        }
    }

    uint32_t& MyAck() {
        return ack_seq_num_;
    }

    bool SanityCheckAndGetSeq(uint32_t* seq_start, uint32_t* seq_end) {
        uint32_t end = read_seq_num_;
        uint32_t idx = read_idx_;
        while(idx < write_idx_) {
            MsgHeader header = blk_[idx];
            header.ConvertByteOrder<ToLittleEndian>();
            if((int)(ack_seq_num_ - header.ack_seq) < 0) return false; // ack_seq in this msg is too new
            idx += (header.size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
            end++;
        }
        if(idx != write_idx_) return false;
        *seq_start = read_seq_num_;
        *seq_end = end;
        return true;
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
} // namespace tcpshm
