#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace itchbook {

// Fixed-memory log-linear histogram (HdrHistogram-style). Every power of two is split into 16
// linear sub-buckets, bounding relative error to 1/16 (6.25%) across the full uint64 range.
// record() is branch-light, allocation-free, and safe to call on the hot path.
class LatencyHistogram {
 public:
  static constexpr unsigned kSubBucketBits = 4;
  static constexpr std::uint64_t kSubBuckets = std::uint64_t{1} << kSubBucketBits;
  static constexpr std::size_t kBucketCount = kSubBuckets + (64 - kSubBucketBits) * kSubBuckets;

  void record(std::uint64_t value) noexcept {
    ++buckets_[bucket_index(value)];
    ++count_;
    sum_ += value;
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);
  }

  // p in [0, 100]. Returns the lower bound of the bucket holding the p-th percentile sample,
  // clamped to the observed [min, max].
  [[nodiscard]] std::uint64_t percentile(double p) const noexcept;

  void merge(const LatencyHistogram& other) noexcept;
  void reset() noexcept;

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
  [[nodiscard]] std::uint64_t min() const noexcept { return count_ == 0 ? 0 : min_; }
  [[nodiscard]] std::uint64_t max() const noexcept { return max_; }
  [[nodiscard]] double mean() const noexcept {
    return count_ == 0 ? 0.0 : static_cast<double>(sum_) / static_cast<double>(count_);
  }

  [[nodiscard]] static constexpr std::size_t bucket_index(std::uint64_t value) noexcept {
    if (value < kSubBuckets) {
      return static_cast<std::size_t>(value);
    }
    const unsigned exponent = static_cast<unsigned>(std::bit_width(value)) - 1U;
    const unsigned shift = exponent - kSubBucketBits;
    const std::uint64_t sub_bucket = (value >> shift) - kSubBuckets;
    return static_cast<std::size_t>(kSubBuckets + std::uint64_t{shift} * kSubBuckets + sub_bucket);
  }

  [[nodiscard]] static constexpr std::uint64_t bucket_lower_bound(std::size_t index) noexcept {
    if (index < kSubBuckets) {
      return index;
    }
    const std::uint64_t offset = index - kSubBuckets;
    const std::uint64_t shift = offset / kSubBuckets;
    const std::uint64_t sub_bucket = offset % kSubBuckets;
    return (kSubBuckets + sub_bucket) << shift;
  }

 private:
  std::array<std::uint64_t, kBucketCount> buckets_{};
  std::uint64_t count_{};
  std::uint64_t sum_{};
  std::uint64_t min_{std::numeric_limits<std::uint64_t>::max()};
  std::uint64_t max_{};
};

}  // namespace itchbook
