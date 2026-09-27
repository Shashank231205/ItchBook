#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace itchbook {

using OrderId = std::uint64_t;
using Price = std::uint32_t;
using Quantity = std::uint32_t;
using AggregateQuantity = std::uint64_t;
using Timestamp = std::uint64_t;
using StockLocate = std::uint16_t;

// ITCH prices are fixed point with four implied decimal places.
inline constexpr Price kPriceScale = 10'000;

inline constexpr std::size_t kMaxInstruments =
    std::size_t{std::numeric_limits<StockLocate>::max()} + 1;

inline constexpr std::size_t kCacheLineSize = 64;

enum class Side : std::uint8_t { Buy, Sell };

[[nodiscard]] constexpr Side opposite(Side side) noexcept {
  return side == Side::Buy ? Side::Sell : Side::Buy;
}

[[nodiscard]] constexpr double to_double(Price price) noexcept {
  return static_cast<double>(price) / static_cast<double>(kPriceScale);
}

}  // namespace itchbook
