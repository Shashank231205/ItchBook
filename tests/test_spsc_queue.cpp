#include "itchbook/concurrency/spsc_queue.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <thread>

#include "itchbook/platform/cpu.hpp"

namespace itchbook {
namespace {

TEST(SpscQueue, PopsInFifoOrder) {
  SpscQueue<int, 4> queue;
  EXPECT_TRUE(queue.try_push(1));
  EXPECT_TRUE(queue.try_push(2));

  int value = 0;
  ASSERT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 1);
  ASSERT_TRUE(queue.try_pop(value));
  EXPECT_EQ(value, 2);
  EXPECT_FALSE(queue.try_pop(value));
}

TEST(SpscQueue, RejectsPushWhenFull) {
  SpscQueue<int, 4> queue;
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(queue.try_push(i));
  }
  EXPECT_FALSE(queue.try_push(99));
  EXPECT_EQ(queue.size_approx(), 4U);

  int value = 0;
  ASSERT_TRUE(queue.try_pop(value));
  EXPECT_TRUE(queue.try_push(99));
}

TEST(SpscQueue, WrapsAroundRepeatedly) {
  SpscQueue<std::uint64_t, 8> queue;
  std::uint64_t value = 0;
  for (std::uint64_t i = 0; i < 1'000; ++i) {
    ASSERT_TRUE(queue.try_push(i));
    ASSERT_TRUE(queue.try_pop(value));
    ASSERT_EQ(value, i);
  }
  EXPECT_TRUE(queue.empty_approx());
}

TEST(SpscQueue, ProducerConsumerThreadsPreserveOrder) {
  constexpr std::uint64_t kItems = 2'000'000;
  auto queue = std::make_unique<SpscQueue<std::uint64_t, 1024>>();

  std::thread producer([&queue] {
    for (std::uint64_t i = 0; i < kItems; ++i) {
      while (!queue->try_push(i)) {
        cpu_relax();
      }
    }
  });

  std::uint64_t expected = 0;
  std::uint64_t value = 0;
  bool in_order = true;
  while (expected < kItems) {
    if (queue->try_pop(value)) {
      in_order = in_order && value == expected;
      ++expected;
    } else {
      cpu_relax();
    }
  }
  producer.join();

  EXPECT_TRUE(in_order);
  EXPECT_TRUE(queue->empty_approx());
}

}  // namespace
}  // namespace itchbook
