#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "itchbook/core/types.hpp"

namespace itchbook {

// Open-addressing hash map specialised for OrderId keys: flat storage, linear probing,
// Fibonacci hashing, and tombstone-free backward-shift deletion. Order-heavy workloads
// erase as often as they insert, which is where tombstones would degrade probe lengths.
template <typename Value>
class OrderIdMap {
 public:
  static constexpr OrderId kEmptyKey = std::numeric_limits<OrderId>::max();

  explicit OrderIdMap(std::size_t expected_size = 64) { rehash(capacity_for(expected_size)); }

  [[nodiscard]] Value* find(OrderId key) noexcept {
    return const_cast<Value*>(std::as_const(*this).find(key));
  }

  [[nodiscard]] const Value* find(OrderId key) const noexcept {
    for (std::size_t i = home(key);; i = next(i)) {
      const Slot& slot = slots_[i];
      if (slot.key == key) {
        return &slot.value;
      }
      if (slot.key == kEmptyKey) {
        return nullptr;
      }
    }
  }

  [[nodiscard]] bool contains(OrderId key) const noexcept { return find(key) != nullptr; }

  // Returns false (and leaves the map unchanged) if the key is already present.
  bool insert(OrderId key, Value value) {
    assert(key != kEmptyKey);
    if ((size_ + 1) * 2 > slots_.size()) {
      rehash(slots_.size() * 2);
    }
    for (std::size_t i = home(key);; i = next(i)) {
      Slot& slot = slots_[i];
      if (slot.key == key) {
        return false;
      }
      if (slot.key == kEmptyKey) {
        slot = Slot{key, std::move(value)};
        ++size_;
        return true;
      }
    }
  }

  bool erase(OrderId key) noexcept {
    std::size_t hole = home(key);
    while (slots_[hole].key != key) {
      if (slots_[hole].key == kEmptyKey) {
        return false;
      }
      hole = next(hole);
    }

    // Pull later members of the probe chain back into the hole whenever the hole lies between
    // their home slot and their current slot, so every lookup still terminates correctly.
    for (std::size_t i = next(hole); slots_[i].key != kEmptyKey; i = next(i)) {
      if (distance(home(slots_[i].key), i) >= distance(hole, i)) {
        slots_[hole] = std::move(slots_[i]);
        hole = i;
      }
    }
    slots_[hole].key = kEmptyKey;
    --size_;
    return true;
  }

  void clear() noexcept {
    for (Slot& slot : slots_) {
      slot.key = kEmptyKey;
    }
    size_ = 0;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }

 private:
  struct Slot {
    OrderId key{kEmptyKey};
    Value value{};
  };

  static constexpr std::size_t kMinCapacity = 16;
  static constexpr std::uint64_t kGoldenRatio = 0x9E3779B97F4A7C15ULL;

  [[nodiscard]] static std::size_t capacity_for(std::size_t expected_size) noexcept {
    return std::bit_ceil(std::max(kMinCapacity, expected_size * 2));
  }

  [[nodiscard]] std::size_t home(OrderId key) const noexcept {
    return static_cast<std::size_t>((key * kGoldenRatio) >> shift_);
  }

  [[nodiscard]] std::size_t next(std::size_t index) const noexcept { return (index + 1) & mask_; }

  [[nodiscard]] std::size_t distance(std::size_t from, std::size_t to) const noexcept {
    return (to - from) & mask_;
  }

  void rehash(std::size_t capacity) {
    std::vector<Slot> old = std::exchange(slots_, std::vector<Slot>(capacity));
    mask_ = capacity - 1;
    shift_ = 64U - static_cast<unsigned>(std::countr_zero(capacity));
    size_ = 0;
    for (Slot& slot : old) {
      if (slot.key != kEmptyKey) {
        insert(slot.key, std::move(slot.value));
      }
    }
  }

  std::vector<Slot> slots_;
  std::size_t mask_{};
  unsigned shift_{};
  std::size_t size_{};
};

}  // namespace itchbook
