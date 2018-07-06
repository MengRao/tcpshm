#pragma once
#include "msg_header.h"

template<uint32_t BLK_CNT>
class SPSCVarQueue
{
public:
    static_assert(BLK_CNT && !(BLK_CNT & (BLK_CNT - 1)), "BLK_CNT must be a power of 2");

    MsgHeader* Alloc(uint16_t size) {
        size += sizeof(MsgHeader);
        uint32_t blk_sz = (size + sizeof(Block) - 1) / sizeof(Block);
        // static_assert(blk_sz <= BLK_CNT, "msg size is larger than queue size!");
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
            blk[write_idx % BLK_CNT].header.size = 0; //-(int32_t)padding_sz;
            write_idx += padding_sz;
        }
        MsgHeader& header = blk[write_idx % BLK_CNT].header;
        header.size = size;
        // header.msg_type = T::msg_type; // we can also let caller set msg_type since it's not needed by Queue
        return &header;
    }

    void Push() {
        uint32_t blk_sz = (blk[write_idx % BLK_CNT].size + sizeof(Block) - 1) / sizeof(Block);
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
        uint32_t blk_sz = (blk[read_idx % BLK_CNT].size + sizeof(Block) - 1) / sizeof(Block);
        read_idx += blk_sz;
        asm volatile("" : : "m"(read_idx) : ); // force write memory
    }

    void Print() {
        std::cout << "write_idx: " << write_idx << " read_idx: " << read_idx << std::endl;
    }

private:
    struct Block // size of 64, same as cache line
    {
        MsgHeader header __attribute__((aligned(64)));
    } blk[BLK_CNT];

    uint32_t write_idx __attribute__((aligned(64))) = 0;
    uint32_t read_idx_cach = 0; // used only by writing thread
    uint32_t read_idx __attribute__((aligned(64))) = 0;
};

