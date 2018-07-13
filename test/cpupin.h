#include <sched.h>

bool cpupin(int cpuid, int prio = 99) {
    if(prio > 0) {
        sched_param param;
        param.sched_priority = prio;
        if(sched_setscheduler(0, SCHED_FIFO, &param)) {
            std::cout << "sched_setscheduler error: " << strerror(errno) << std::endl;
            return false;
        }
    }

    cpu_set_t my_set;
    CPU_ZERO(&my_set);
    CPU_SET(cpuid, &my_set);
    if(sched_setaffinity(0, sizeof(cpu_set_t), &my_set)) {
        std::cout << "sched_setaffinity error: " << strerror(errno) << std::endl;
        return false;
    }

    return true;
}
