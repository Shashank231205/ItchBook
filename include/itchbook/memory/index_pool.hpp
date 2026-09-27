#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace itchbook {

// Slab of T addressed by 32-bit indices. Released slots are recycled LIFO, so a warmed-up pool
// never allocates and recently freed (cache-hot) slots are reused first. Indices, unlike
// pointers, stay valid when the slab grows.
template <typename T>
class IndexPool {
 public:
  using Index = std::uint32_t;
  static constexpr Index kNull = std::numeric_limits<Index>::max();

  explicit IndexPool(std::size_t capacity = 0) {
    slots_.reserve(capacity);
    free_.reserve(capacity);
  }

  template <typename... Args>
  [[nodiscard]] Index acquire(Args&&... args) {
    ++live_;
    if (!free_.empty()) {
      const Index index = free_.back();
      free_.pop_back();
      slots_[index] = T{std::forward<Args>(args)...};
      return index;
    }
    const auto index = static_cast<Index>(slots_.size());
    slots_.push_back(T{std::forward<Args>(args)...});
    return index;
  }

  void release(Index index) {
    free_.push_back(index);
    --live_;
  }

  [[nodiscard]] T& operator[](Index index) noexcept { return slots_[index]; }
  [[nodiscard]] const T& operator[](Index index) const noexcept { return slots_[index]; }

  [[nodiscard]] std::size_t size() const noexcept { return live_; }
  [[nodiscard]] bool empty() const noexcept { return live_ == 0; }
  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.capacity(); }

  void clear() noexcept {
    slots_.clear();
    free_.clear();
    live_ = 0;
  }

 private:
  std::vector<T> slots_;
  std::vector<Index> free_;
  std::size_t live_{};
};

}  // namespace itchbook
