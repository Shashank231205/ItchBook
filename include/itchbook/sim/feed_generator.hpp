#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "itchbook/core/types.hpp"

namespace itchbook::sim {

struct FeedConfig {
  std::size_t instruments = 8;
  std::size_t events = 1'000'000;
  std::uint64_t seed = 42;
  Price start_price = 100 * kPriceScale;
  Price tick_size = kPriceScale / 100;
  std::uint32_t max_levels_from_mid = 25;
};

// Generates a deterministic, internally consistent ITCH 5.0 stream: every execute, cancel,
// delete and replace references a live order with enough shares. The event mix and the
// geometric distance-from-mid distribution roughly resemble real equity order flow.
[[nodiscard]] std::vector<std::byte> generate_feed(const FeedConfig& config);

}  // namespace itchbook::sim
