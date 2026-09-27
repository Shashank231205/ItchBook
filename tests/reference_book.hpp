#pragma once

#include <map>
#include <unordered_map>
#include <vector>

#include "itchbook/book/order.hpp"
#include "itchbook/core/types.hpp"

namespace itchbook::testing {

// Deliberately naive book used as an oracle for randomized differential tests.
class ReferenceBook {
 public:
  bool add(OrderId id, Side side, Price price, Quantity quantity) {
    if (quantity == 0 || orders_.contains(id)) {
      return false;
    }
    orders_.emplace(id, Entry{side, price, quantity});
    return true;
  }

  bool reduce(OrderId id, Quantity quantity) {
    const auto it = orders_.find(id);
    if (it == orders_.end()) {
      return false;
    }
    if (quantity >= it->second.quantity) {
      orders_.erase(it);
    } else {
      it->second.quantity -= quantity;
    }
    return true;
  }

  bool remove(OrderId id) { return orders_.erase(id) == 1; }

  bool replace(OrderId original_id, OrderId new_id, Quantity quantity, Price price) {
    const auto it = orders_.find(original_id);
    if (it == orders_.end() || quantity == 0) {
      return false;
    }
    if (new_id != original_id && orders_.contains(new_id)) {
      return false;
    }
    const Side side = it->second.side;
    orders_.erase(it);
    return add(new_id, side, price, quantity);
  }

  [[nodiscard]] std::vector<LevelView> depth(Side side) const {
    std::map<Price, LevelView> levels;
    for (const auto& [id, entry] : orders_) {
      if (entry.side != side) {
        continue;
      }
      LevelView& level = levels[entry.price];
      level.price = entry.price;
      level.quantity += entry.quantity;
      ++level.order_count;
    }
    std::vector<LevelView> out;
    if (side == Side::Buy) {
      for (auto it = levels.rbegin(); it != levels.rend(); ++it) {
        out.push_back(it->second);
      }
    } else {
      for (const auto& [price, level] : levels) {
        out.push_back(level);
      }
    }
    return out;
  }

  [[nodiscard]] std::size_t order_count() const noexcept { return orders_.size(); }

 private:
  struct Entry {
    Side side;
    Price price;
    Quantity quantity;
  };

  std::unordered_map<OrderId, Entry> orders_;
};

}  // namespace itchbook::testing
