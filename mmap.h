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
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

namespace tcpshm {

template<class T>
T* my_mmap(const char* filename, bool use_shm, const char** error_msg) {
    int fd = -1;
    if(use_shm) {
        fd = shm_open(filename, O_CREAT | O_RDWR, 0666);
    }
    else {
        fd = open(filename, O_CREAT | O_RDWR, 0644);
    }
    if(fd == -1) {
        *error_msg = "open";
        return nullptr;
    }
    if(ftruncate(fd, sizeof(T))) {
        *error_msg = "ftruncate";
        close(fd);
        return nullptr;
    }
    T* ret = (T*)mmap(0, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if(ret == MAP_FAILED) {
        *error_msg = "mmap";
        return nullptr;
    }
    return ret;
}

template<class T>
void my_munmap(void* addr) {
    munmap(addr, sizeof(T));
}
} // namespace tcpshm
