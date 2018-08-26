#include <sys/time.h>

static const int kMicroSecondsPerSecond = 1000 * 1000;

inline int64_t now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int64_t seconds = tv.tv_sec;
    return seconds * kMicroSecondsPerSecond + tv.tv_usec;
}

inline unsigned long long rdtsc() {
    return __builtin_ia32_rdtsc();
}

inline unsigned long long rdtscp() {
    unsigned int dummy;
    return __builtin_ia32_rdtscp(&dummy);
}

