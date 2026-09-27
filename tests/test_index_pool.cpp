#include "itchbook/memory/index_pool.hpp"

#include <gtest/gtest.h>

namespace itchbook {
namespace {

struct Node {
  int value{};
};

TEST(IndexPool, AcquireAssignsSequentialIndices) {
  IndexPool<Node> pool(4);
  EXPECT_EQ(pool.acquire(Node{1}), 0U);
  EXPECT_EQ(pool.acquire(Node{2}), 1U);
  EXPECT_EQ(pool.size(), 2U);
  EXPECT_EQ(pool[1].value, 2);
}

TEST(IndexPool, ReleasedSlotsAreReusedLastInFirstOut) {
  IndexPool<Node> pool;
  const auto a = pool.acquire(Node{1});
  const auto b = pool.acquire(Node{2});
  pool.release(a);
  pool.release(b);

  EXPECT_EQ(pool.acquire(Node{3}), b);
  EXPECT_EQ(pool.acquire(Node{4}), a);
  EXPECT_EQ(pool[a].value, 4);
  EXPECT_EQ(pool.size(), 2U);
}

TEST(IndexPool, IndicesSurviveGrowth) {
  IndexPool<Node> pool(1);
  const auto first = pool.acquire(Node{42});
  for (int i = 0; i < 1'000; ++i) {
    static_cast<void>(pool.acquire(Node{i}));
  }
  EXPECT_EQ(pool[first].value, 42);
}

TEST(IndexPool, ClearResetsState) {
  IndexPool<Node> pool;
  static_cast<void>(pool.acquire(Node{1}));
  pool.clear();
  EXPECT_TRUE(pool.empty());
  EXPECT_EQ(pool.acquire(Node{2}), 0U);
}

}  // namespace
}  // namespace itchbook
