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
