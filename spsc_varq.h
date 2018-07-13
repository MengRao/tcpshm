#pragma once
#include "msg_header.h"

namespace tcpshm {

template<uint32_t Bytes>
class SPSCVarQueue
{
public:
    static constexpr uint32_t BLK_CNT = Bytes / 64;
    static_assert(BLK_CNT && !(BLK_CNT & (BLK_CNT - 1)), "BLK_CNT must be a power of 2");

    MsgHeader* Alloc(uint16_t size) {
        size += sizeof(MsgHeader);
        uint32_t blk_sz = (size + sizeof(Block) - 1) / sizeof(Block);
        uint32_t padding_sz = BLK_CNT - (write_idx % BLK_CNT);
        bool rewind = blk_sz > padding_sz;
        int32_t min_read_idx = write_idx + blk_sz + (rewind ? padding_sz : 0) - BLK_CNT;
        if((int32_t)read_idx_cach < min_read_idx) {
            asm volatile("" : "=m"(read_idx) : :); // force read memory
            read_idx_cach = read_idx;
            if((int32_t)read_idx_cach < min_read_idx) { // no enough space
                return nullptr;
            }
        }
        if(rewind) {
            blk[write_idx % BLK_CNT].header.size = 0;
            write_idx += padding_sz;
        }
        MsgHeader& header = blk[write_idx % BLK_CNT].header;
        header.size = size;
        return &header;
    }

    void Push() {
        uint32_t blk_sz = (blk[write_idx % BLK_CNT].header.size + sizeof(Block) - 1) / sizeof(Block);
        write_idx += blk_sz;
        asm volatile("" : : "m"(write_idx) : ); // force write memory
    }

    MsgHeader* Front() {
        asm volatile("" : "=m"(write_idx) : : ); // force read memory
        if(read_idx == write_idx) {
            return nullptr;
        }
        uint16_t size = blk[read_idx % BLK_CNT].header.size;
        if(size == 0) { // rewind
            read_idx += BLK_CNT - (read_idx % BLK_CNT);
            if(read_idx == write_idx) {
                return nullptr;
            }
        }
        return &blk[read_idx % BLK_CNT].header;
    }

    void Pop() {
        uint32_t blk_sz = (blk[read_idx % BLK_CNT].header.size + sizeof(Block) - 1) / sizeof(Block);
        read_idx += blk_sz;
        asm volatile("" : : "m"(read_idx) : ); // force write memory
    }

private:
    struct Block // size of 64, same as cache line
    {
        alignas(64) MsgHeader header;
    } blk[BLK_CNT];

    alignas(64) uint32_t write_idx = 0;
    uint32_t read_idx_cach = 0; // used only by writing thread
    alignas(64) uint32_t read_idx = 0;
};
} // namespace tcpshm
