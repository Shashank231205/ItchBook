#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "itchbook/book/order.hpp"
#include "itchbook/core/types.hpp"
#include "itchbook/memory/index_pool.hpp"
#include "itchbook/memory/order_id_map.hpp"

namespace itchbook {

static_assert(IndexPool<Order>::kNull == kNullOrder);

// Price-time priority (L3) limit order book for a single instrument.
//
// Levels on each side live in a contiguous vector sorted worst -> best, so the touch is always
// at back(). Market activity clusters near the touch, which makes level insert/erase a short
// memmove at the end of the vector and keeps the hot levels in a couple of cache lines.
class OrderBook {
 public:
  static constexpr std::size_t kDefaultExpectedOrders = 64;

  explicit OrderBook(std::size_t expected_orders = kDefaultExpectedOrders);

  // All mutators return false when the order id is unknown (or, for add, already present or
  // zero-sized) and leave the book unchanged in that case.
  bool add(OrderId id, Side side, Price price, Quantity quantity);
  bool reduce(OrderId id, Quantity quantity);
  bool remove(OrderId id);
  // Cancel/replace: the new order loses time priority and keeps the original side.
  bool replace(OrderId original_id, OrderId new_id, Quantity quantity, Price price);
  void clear() noexcept;

  [[nodiscard]] const Order* find(OrderId id) const noexcept;
  [[nodiscard]] std::optional<LevelView> best(Side side) const noexcept;
  [[nodiscard]] TopOfBook top() const noexcept;
  // Writes up to out.size() levels, best first; returns the number written.
  std::size_t depth(Side side, std::span<LevelView> out) const noexcept;

  // Visits resting orders at one price in time priority.
  template <typename Fn>
  void for_each_order(Side side, Price price, Fn&& fn) const;

  [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }
  [[nodiscard]] std::size_t level_count(Side side) const noexcept { return levels(side).size(); }
  [[nodiscard]] bool empty() const noexcept { return orders_.empty(); }

 private:
  using Levels = std::vector<PriceLevel>;

  [[nodiscard]] static constexpr bool is_worse(Side side, Price lhs, Price rhs) noexcept {
    return side == Side::Buy ? lhs < rhs : lhs > rhs;
  }

  template <typename LevelsT>
  [[nodiscard]] static auto search(LevelsT& levels, Side side, Price price) noexcept {
    return std::lower_bound(levels.begin(), levels.end(), price,
                            [side](const PriceLevel& level, Price target) {
                              return is_worse(side, level.price, target);
                            });
  }

  [[nodiscard]] Levels& levels(Side side) noexcept { return side == Side::Buy ? bids_ : asks_; }
  [[nodiscard]] const Levels& levels(Side side) const noexcept {
    return side == Side::Buy ? bids_ : asks_;
  }

  [[nodiscard]] const PriceLevel* find_level(Side side, Price price) const noexcept;
  PriceLevel& find_or_insert_level(Side side, Price price);
  void link(PriceLevel& level, OrderIndex index) noexcept;
  void unlink(PriceLevel& level, OrderIndex index) noexcept;
  void erase_order(OrderIndex index);

  IndexPool<Order> orders_;
  OrderIdMap<OrderIndex> index_;
  Levels bids_;
  Levels asks_;
};

template <typename Fn>
void OrderBook::for_each_order(Side side, Price price, Fn&& fn) const {
  const PriceLevel* level = find_level(side, price);
  if (level == nullptr) {
    return;
  }
  for (OrderIndex i = level->head; i != kNullOrder; i = orders_[i].next) {
    fn(orders_[i]);
  }
}

}  // namespace itchbook
