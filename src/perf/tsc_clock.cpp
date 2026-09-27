#include "itchbook/perf/tsc_clock.hpp"

#include <thread>

namespace itchbook {

TscClock TscClock::calibrate(std::chrono::milliseconds window) {
  using Clock = std::chrono::steady_clock;

  const auto wall_start = Clock::now();
  const std::uint64_t tsc_start = read_tsc();
  std::this_thread::sleep_for(window);
  const std::uint64_t tsc_end = read_tsc();
  const auto wall_end = Clock::now();

  const double elapsed_ns = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();
  const auto ticks = static_cast<double>(tsc_end - tsc_start);
  return TscClock{ticks > 0.0 ? elapsed_ns / ticks : 1.0};
}

}  // namespace itchbook
