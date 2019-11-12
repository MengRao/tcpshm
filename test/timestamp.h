#include <time.h>

inline unsigned long long now() {
  timespec ts;
  ::clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

