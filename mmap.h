#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

template<class T>
T* my_mmap(const char* filename, bool use_shm) {
    int fd = -1;
    if(use_shm) {
        fd = shm_open(filename, O_CREAT | O_RDWR, 0666);
    }
    else {
        fd = open(filename, O_CREAT | O_RDWR, 0644);
    }
    if(fd == -1) {
        std::cerr << "open failed: " << strerror(errno) << std::endl;
        return nullptr;
    }
    if(ftruncate(fd, sizeof(T))) {
        std::cerr << "ftruncate failed: " << strerror(errno) << std::endl;
        close(fd);
        return nullptr;
    }
    T* ret = (T*)mmap(0, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if(ret == MAP_FAILED) {
        std::cerr << "mmap failed: " << strerror(errno) << std::endl;
        return nullptr;
    }
    return ret;
}

template<class T>
void my_munmap(void* addr) {
    mnumap(addr, sizeof(T));
}
