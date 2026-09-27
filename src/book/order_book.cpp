#include "itchbook/book/order_book.hpp"

#include <cassert>

namespace itchbook {

namespace {
constexpr std::size_t kExpectedLevelsPerSide = 32;
}  // namespace

OrderBook::OrderBook(std::size_t expected_orders)
    : orders_(expected_orders), index_(expected_orders) {
  bids_.reserve(kExpectedLevelsPerSide);
  asks_.reserve(kExpectedLevelsPerSide);
}

bool OrderBook::add(OrderId id, Side side, Price price, Quantity quantity) {
  if (quantity == 0) {
    return false;
  }
  const OrderIndex index = orders_.acquire(Order{id, price, quantity, side});
  if (!index_.insert(id, index)) {
    orders_.release(index);
    return false;
  }
  link(find_or_insert_level(side, price), index);
  return true;
}

bool OrderBook::reduce(OrderId id, Quantity quantity) {
  const OrderIndex* slot = index_.find(id);
  if (slot == nullptr) {
    return false;
  }
  const OrderIndex index = *slot;
  Order& order = orders_[index];
  if (quantity >= order.quantity) {
    erase_order(index);
    return true;
  }
  order.quantity -= quantity;
  auto it = search(levels(order.side), order.side, order.price);
  assert(it != levels(order.side).end() && it->price == order.price);
  it->quantity -= quantity;
  return true;
}

bool OrderBook::remove(OrderId id) {
  const OrderIndex* slot = index_.find(id);
  if (slot == nullptr) {
    return false;
  }
  erase_order(*slot);
  return true;
}

bool OrderBook::replace(OrderId original_id, OrderId new_id, Quantity quantity, Price price) {
  const OrderIndex* slot = index_.find(original_id);
  if (slot == nullptr || quantity == 0) {
    return false;
  }
  if (new_id != original_id && index_.contains(new_id)) {
    return false;
  }
  const OrderIndex index = *slot;
  const Side side = orders_[index].side;
  erase_order(index);
  return add(new_id, side, price, quantity);
}

void OrderBook::clear() noexcept {
  orders_.clear();
  index_.clear();
  bids_.clear();
  asks_.clear();
}

const Order* OrderBook::find(OrderId id) const noexcept {
  const OrderIndex* slot = index_.find(id);
  return slot == nullptr ? nullptr : &orders_[*slot];
}

std::optional<LevelView> OrderBook::best(Side side) const noexcept {
  const Levels& side_levels = levels(side);
  if (side_levels.empty()) {
    return std::nullopt;
  }
  const PriceLevel& level = side_levels.back();
  return LevelView{level.price, level.quantity, level.order_count};
}

TopOfBook OrderBook::top() const noexcept {
  TopOfBook top;
  if (!bids_.empty()) {
    top.bid_price = bids_.back().price;
    top.bid_quantity = bids_.back().quantity;
  }
  if (!asks_.empty()) {
    top.ask_price = asks_.back().price;
    top.ask_quantity = asks_.back().quantity;
  }
  return top;
}

std::size_t OrderBook::depth(Side side, std::span<LevelView> out) const noexcept {
  const Levels& side_levels = levels(side);
  const std::size_t count = std::min(out.size(), side_levels.size());
  auto level = side_levels.rbegin();
  for (std::size_t i = 0; i < count; ++i, ++level) {
    out[i] = LevelView{level->price, level->quantity, level->order_count};
  }
  return count;
}

const PriceLevel* OrderBook::find_level(Side side, Price price) const noexcept {
  const Levels& side_levels = levels(side);
  const auto it = search(side_levels, side, price);
  return it != side_levels.end() && it->price == price ? &*it : nullptr;
}

PriceLevel& OrderBook::find_or_insert_level(Side side, Price price) {
  Levels& side_levels = levels(side);
  const auto it = search(side_levels, side, price);
  if (it != side_levels.end() && it->price == price) {
    return *it;
  }
  return *side_levels.insert(it, PriceLevel{.price = price});
}

void OrderBook::link(PriceLevel& level, OrderIndex index) noexcept {
  Order& order = orders_[index];
  order.prev = level.tail;
  order.next = kNullOrder;
  if (level.tail != kNullOrder) {
    orders_[level.tail].next = index;
  } else {
    level.head = index;
  }
  level.tail = index;
  level.quantity += order.quantity;
  ++level.order_count;
}

void OrderBook::unlink(PriceLevel& level, OrderIndex index) noexcept {
  const Order& order = orders_[index];
  if (order.prev != kNullOrder) {
    orders_[order.prev].next = order.next;
  } else {
    level.head = order.next;
  }
  if (order.next != kNullOrder) {
    orders_[order.next].prev = order.prev;
  } else {
    level.tail = order.prev;
  }
  level.quantity -= order.quantity;
  --level.order_count;
}

void OrderBook::erase_order(OrderIndex index) {
  const Order& order = orders_[index];
  Levels& side_levels = levels(order.side);
  const auto level = search(side_levels, order.side, order.price);
  assert(level != side_levels.end() && level->price == order.price);

  unlink(*level, index);
  if (level->order_count == 0) {
    side_levels.erase(level);
  }
  index_.erase(order.id);
  orders_.release(index);
}

}  // namespace itchbook
