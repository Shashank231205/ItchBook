#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <type_traits>

#include "itchbook/core/types.hpp"

namespace itchbook {

// Bounded wait-free single-producer/single-consumer ring buffer.
//
// Producer and consumer indices live on separate cache lines to avoid false sharing. Each side
// also keeps a private copy of the other side's index and only re-reads the shared atomic when
// that copy says the queue looks full (producer) or empty (consumer), which removes most
// cross-core cache-line transfers in steady state.
template <typename T, std::size_t Capacity>
  requires(std::has_single_bit(Capacity) && std::is_nothrow_default_constructible_v<T> &&
           std::is_nothrow_copy_assignable_v<T>)
class SpscQueue {
 public:
  SpscQueue() = default;
  SpscQueue(const SpscQueue&) = delete;
  SpscQueue& operator=(const SpscQueue&) = delete;

  [[nodiscard]] bool try_push(const T& value) noexcept {
    const std::size_t tail = producer_.tail.load(std::memory_order_relaxed);
    if (tail - producer_.cached_head == Capacity) {
      producer_.cached_head = consumer_.head.load(std::memory_order_acquire);
      if (tail - producer_.cached_head == Capacity) {
        return false;
      }
    }
    slots_[tail & kMask] = value;
    // Release publishes the slot write before the consumer can observe the new tail.
    producer_.tail.store(tail + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool try_pop(T& out) noexcept {
    const std::size_t head = consumer_.head.load(std::memory_order_relaxed);
    if (head == consumer_.cached_tail) {
      consumer_.cached_tail = producer_.tail.load(std::memory_order_acquire);
      if (head == consumer_.cached_tail) {
        return false;
      }
    }
    out = slots_[head & kMask];
    // Release hands the slot back to the producer only after the read above has completed.
    consumer_.head.store(head + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t size_approx() const noexcept {
    const std::size_t head = consumer_.head.load(std::memory_order_acquire);
    const std::size_t tail = producer_.tail.load(std::memory_order_acquire);
    return tail - head;
  }

  [[nodiscard]] bool empty_approx() const noexcept { return size_approx() == 0; }
  [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

 private:
  static constexpr std::size_t kMask = Capacity - 1;

  struct alignas(kCacheLineSize) ProducerState {
    std::atomic<std::size_t> tail{0};
    std::size_t cached_head{0};
  };

  struct alignas(kCacheLineSize) ConsumerState {
    std::atomic<std::size_t> head{0};
    std::size_t cached_tail{0};
  };

  ProducerState producer_;
  ConsumerState consumer_;
  alignas(kCacheLineSize) std::array<T, Capacity> slots_{};
};

}  // namespace itchbook
