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

namespace tcpshm {

template<uint32_t Bytes>
class SPSCVarQueue
{
public:
    static constexpr uint32_t BLK_CNT = Bytes / sizeof(MsgHeader);
    // static_assert(BLK_CNT && !(BLK_CNT & (BLK_CNT - 1)), "BLK_CNT must be a power of 2");

    MsgHeader* Alloc(uint16_t size_) {
        size = size_ + sizeof(MsgHeader);
        uint32_t blk_sz = (size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        if(blk_sz >= free_write_cnt) {
            asm volatile("" : "=m"(read_idx) : :); // force read memory
            uint32_t read_idx_cache = read_idx;
            if(read_idx_cache <= write_idx) {
                free_write_cnt = BLK_CNT - write_idx;
                if(blk_sz >= free_write_cnt && read_idx_cache != 0) { // wrap around
                    blk[0].size = 0;
                    asm volatile("" : : "m"(blk) :); // memory fence
                    blk[write_idx].size = 1;
                    write_idx = 0;
                    free_write_cnt = read_idx_cache;
                }
            }
            else {
                free_write_cnt = read_idx_cache - write_idx;
            }
            if(free_write_cnt <= blk_sz) {
                return nullptr;
            }
        }
        return &blk[write_idx];
    }


    void Push() {
        uint32_t blk_sz = (size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        blk[write_idx + blk_sz].size = 0;

        asm volatile("" : : "m"(blk) :); // memory fence
        blk[write_idx].size = size;
        write_idx += blk_sz;
        free_write_cnt -= blk_sz;
    }

    MsgHeader* Front() {
        asm volatile("" : "=m"(blk) : :); // force read memory
        uint16_t size = blk[read_idx].size;
        if(size == 1) { // wrap around
            read_idx = 0;
            size = blk[0].size;
        }
        if(size == 0) return nullptr;
        return &blk[read_idx];
    }

    void Pop() {
        asm volatile("" : "=m"(blk) : "m"(read_idx) :); // memory fence
        uint32_t blk_sz = (blk[read_idx].size + sizeof(MsgHeader) - 1) / sizeof(MsgHeader);
        read_idx += blk_sz;
        asm volatile("" : : "m"(read_idx) : ); // force write memory
    }

private:
    MsgHeader blk[BLK_CNT];

    alignas(64) uint32_t write_idx = 0;
    uint32_t free_write_cnt;
    uint16_t size;
    alignas(64) uint32_t read_idx = 0;
};
} // namespace tcpshm
