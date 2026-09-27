#pragma once

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace itchbook {

// Spin-wait hint: reduces power and frees pipeline resources for a sibling hyper-thread.
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  _mm_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield" ::: "memory");
#endif
}

// Pins the calling thread to a single CPU. Returns false if unsupported or the call fails.
bool pin_current_thread(unsigned cpu) noexcept;

}  // namespace itchbook
