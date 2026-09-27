#include "itchbook/platform/cpu.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace itchbook {

bool pin_current_thread(unsigned cpu) noexcept {
#if defined(__linux__)
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
  static_cast<void>(cpu);
  return false;
#endif
}

}  // namespace itchbook
