#pragma once

#include <cstdint>
#include <limits>

#include "itchbook/core/types.hpp"

namespace itchbook {

using OrderIndex = std::uint32_t;
inline constexpr OrderIndex kNullOrder = std::numeric_limits<OrderIndex>::max();

// Resting order. prev/next form an intrusive FIFO list inside its price level, which gives
// O(1) cancel from anywhere in the queue without a separate container per level.
struct Order {
  OrderId id{};
  Price price{};
  Quantity quantity{};
  Side side{};
  OrderIndex prev{kNullOrder};
  OrderIndex next{kNullOrder};
};
static_assert(sizeof(Order) == 32, "two orders per cache line");

struct PriceLevel {
  Price price{};
  std::uint32_t order_count{};
  AggregateQuantity quantity{};
  OrderIndex head{kNullOrder};
  OrderIndex tail{kNullOrder};
};

struct LevelView {
  Price price{};
  AggregateQuantity quantity{};
  std::uint32_t order_count{};

  friend bool operator==(const LevelView&, const LevelView&) = default;
};

struct TopOfBook {
  Price bid_price{};
  AggregateQuantity bid_quantity{};
  Price ask_price{};
  AggregateQuantity ask_quantity{};

  [[nodiscard]] bool has_bid() const noexcept { return bid_quantity != 0; }
  [[nodiscard]] bool has_ask() const noexcept { return ask_quantity != 0; }

  friend bool operator==(const TopOfBook&, const TopOfBook&) = default;
};

}  // namespace itchbook
