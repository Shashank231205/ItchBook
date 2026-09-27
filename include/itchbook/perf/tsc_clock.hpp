#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#endif

namespace itchbook {

// Raw cycle counter. rdtsc is not serialising; that is acceptable for latency distributions
// over millions of samples and far cheaper than a clock_gettime() call.
[[nodiscard]] inline std::uint64_t read_tsc() noexcept {
#if defined(__x86_64__) || defined(__i386__)
  return __rdtsc();
#else
  return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Converts TSC ticks to nanoseconds using a rate measured against steady_clock.
class TscClock {
 public:
  [[nodiscard]] static TscClock calibrate(
      std::chrono::milliseconds window = std::chrono::milliseconds{100});

  [[nodiscard]] double ns_per_tick() const noexcept { return ns_per_tick_; }

  [[nodiscard]] std::uint64_t to_ns(std::uint64_t ticks) const noexcept {
    return static_cast<std::uint64_t>(static_cast<double>(ticks) * ns_per_tick_);
  }

 private:
  explicit TscClock(double ns_per_tick) noexcept : ns_per_tick_(ns_per_tick) {}

  double ns_per_tick_;
};

}  // namespace itchbook
