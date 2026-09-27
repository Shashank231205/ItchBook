#include "itchbook/perf/latency_histogram.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>

namespace itchbook {
namespace {

TEST(LatencyHistogram, EmptyHistogramReportsZeros) {
  const LatencyHistogram histogram;
  EXPECT_EQ(histogram.count(), 0U);
  EXPECT_EQ(histogram.min(), 0U);
  EXPECT_EQ(histogram.max(), 0U);
  EXPECT_EQ(histogram.percentile(50), 0U);
  EXPECT_DOUBLE_EQ(histogram.mean(), 0.0);
}

TEST(LatencyHistogram, BucketsCoverValuesWithBoundedRelativeError) {
  constexpr std::array<std::uint64_t, 11> kValues{
      0, 1, 15, 16, 17, 31, 32, 33, 1'000, 123'456'789, std::numeric_limits<std::uint64_t>::max()};
  for (const std::uint64_t value : kValues) {
    const std::size_t index = LatencyHistogram::bucket_index(value);
    ASSERT_LT(index, LatencyHistogram::kBucketCount) << value;
    const std::uint64_t lower = LatencyHistogram::bucket_lower_bound(index);
    EXPECT_LE(lower, value);
    EXPECT_LE(value - lower, value / LatencyHistogram::kSubBuckets) << value;
    if (index + 1 < LatencyHistogram::kBucketCount) {
      EXPECT_GT(LatencyHistogram::bucket_lower_bound(index + 1), value);
    }
  }
}

TEST(LatencyHistogram, SmallValuesAreExact) {
  LatencyHistogram histogram;
  for (std::uint64_t v = 1; v <= 10; ++v) {
    histogram.record(v);
  }
  EXPECT_EQ(histogram.percentile(50), 5U);
  EXPECT_EQ(histogram.percentile(100), 10U);
  EXPECT_EQ(histogram.percentile(0), 1U);
  EXPECT_DOUBLE_EQ(histogram.mean(), 5.5);
}

TEST(LatencyHistogram, PercentilesOnUniformDataAreWithinBucketPrecision) {
  LatencyHistogram histogram;
  for (std::uint64_t v = 1; v <= 100'000; ++v) {
    histogram.record(v);
  }
  const auto p50 = static_cast<double>(histogram.percentile(50));
  const auto p99 = static_cast<double>(histogram.percentile(99));
  EXPECT_NEAR(p50, 50'000.0, 50'000.0 / 16);
  EXPECT_NEAR(p99, 99'000.0, 99'000.0 / 16);
  EXPECT_EQ(histogram.percentile(100), 100'000U);
}

TEST(LatencyHistogram, MergeCombinesCountsAndExtremes) {
  LatencyHistogram a;
  LatencyHistogram b;
  a.record(10);
  b.record(1'000);
  b.record(5);
  a.merge(b);
  EXPECT_EQ(a.count(), 3U);
  EXPECT_EQ(a.min(), 5U);
  EXPECT_EQ(a.max(), 1'000U);

  a.reset();
  EXPECT_EQ(a.count(), 0U);
}

}  // namespace
}  // namespace itchbook
