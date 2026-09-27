#include "itchbook/perf/latency_histogram.hpp"

#include <cmath>

namespace itchbook {

std::uint64_t LatencyHistogram::percentile(double p) const noexcept {
  if (count_ == 0) {
    return 0;
  }
  const double fraction = std::clamp(p, 0.0, 100.0) / 100.0;
  const auto rank = std::max<std::uint64_t>(
      1, static_cast<std::uint64_t>(std::ceil(fraction * static_cast<double>(count_))));
  if (rank >= count_) {
    return max_;
  }

  std::uint64_t seen = 0;
  for (std::size_t i = 0; i < kBucketCount; ++i) {
    seen += buckets_[i];
    if (seen >= rank) {
      return std::clamp(bucket_lower_bound(i), min_, max_);
    }
  }
  return max_;
}

void LatencyHistogram::merge(const LatencyHistogram& other) noexcept {
  for (std::size_t i = 0; i < kBucketCount; ++i) {
    buckets_[i] += other.buckets_[i];
  }
  count_ += other.count_;
  sum_ += other.sum_;
  min_ = std::min(min_, other.min_);
  max_ = std::max(max_, other.max_);
}

void LatencyHistogram::reset() noexcept { *this = LatencyHistogram{}; }

}  // namespace itchbook
