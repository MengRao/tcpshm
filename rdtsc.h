inline unsigned long long rdtsc() {
    return __builtin_ia32_rdtsc();
}

inline unsigned long long rdtscp() {
    unsigned int dummy;
    return __builtin_ia32_rdtscp(&dummy);
}

