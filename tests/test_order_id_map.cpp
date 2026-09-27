#include "itchbook/memory/order_id_map.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <unordered_map>

namespace itchbook {
namespace {

TEST(OrderIdMap, InsertFindErase) {
  OrderIdMap<std::uint32_t> map;
  EXPECT_TRUE(map.insert(10, 1));
  EXPECT_TRUE(map.insert(20, 2));

  ASSERT_NE(map.find(10), nullptr);
  EXPECT_EQ(*map.find(10), 1U);
  EXPECT_EQ(*map.find(20), 2U);
  EXPECT_EQ(map.find(30), nullptr);

  EXPECT_TRUE(map.erase(10));
  EXPECT_FALSE(map.erase(10));
  EXPECT_EQ(map.find(10), nullptr);
  EXPECT_EQ(map.size(), 1U);
}

TEST(OrderIdMap, RejectsDuplicateKeys) {
  OrderIdMap<std::uint32_t> map;
  EXPECT_TRUE(map.insert(5, 1));
  EXPECT_FALSE(map.insert(5, 2));
  EXPECT_EQ(*map.find(5), 1U);
}

TEST(OrderIdMap, GrowsAndKeepsLoadFactorAtMostHalf) {
  OrderIdMap<std::uint32_t> map(4);
  for (std::uint32_t i = 0; i < 10'000; ++i) {
    ASSERT_TRUE(map.insert(OrderId{i} * 7919, i));
  }
  EXPECT_EQ(map.size(), 10'000U);
  EXPECT_LE(map.size() * 2, map.capacity());
  for (std::uint32_t i = 0; i < 10'000; ++i) {
    ASSERT_EQ(*map.find(OrderId{i} * 7919), i);
  }
}

TEST(OrderIdMap, ClearEmptiesButKeepsCapacity) {
  OrderIdMap<std::uint32_t> map;
  map.insert(1, 1);
  const std::size_t capacity = map.capacity();
  map.clear();
  EXPECT_TRUE(map.empty());
  EXPECT_EQ(map.find(1), nullptr);
  EXPECT_EQ(map.capacity(), capacity);
}

// Dense sequential keys in a small table produce long probe chains and wrap-around, which is
// exactly what backward-shift deletion must handle.
TEST(OrderIdMap, MatchesStdUnorderedMapUnderChurn) {
  OrderIdMap<std::uint32_t> map(8);
  std::unordered_map<OrderId, std::uint32_t> reference;
  std::mt19937_64 rng(7);
  std::uniform_int_distribution<OrderId> key_dist(1, 2'000);
  std::uniform_int_distribution<int> op_dist(0, 2);

  for (std::uint32_t step = 0; step < 200'000; ++step) {
    const OrderId key = key_dist(rng);
    switch (op_dist(rng)) {
      case 0:
        ASSERT_EQ(map.insert(key, step), reference.emplace(key, step).second);
        break;
      case 1:
        ASSERT_EQ(map.erase(key), reference.erase(key) == 1);
        break;
      default: {
        const auto it = reference.find(key);
        const std::uint32_t* found = map.find(key);
        ASSERT_EQ(found != nullptr, it != reference.end());
        if (found != nullptr) {
          ASSERT_EQ(*found, it->second);
        }
        break;
      }
    }
    ASSERT_EQ(map.size(), reference.size());
  }

  for (const auto& [key, value] : reference) {
    ASSERT_NE(map.find(key), nullptr);
    EXPECT_EQ(*map.find(key), value);
  }
}

}  // namespace
}  // namespace itchbook
