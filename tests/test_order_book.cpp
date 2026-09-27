#include "itchbook/book/order_book.hpp"

#include <gtest/gtest.h>

#include <array>
#include <random>
#include <vector>

#include "reference_book.hpp"

namespace itchbook {
namespace {

std::vector<LevelView> full_depth(const OrderBook& book, Side side) {
  std::vector<LevelView> levels(book.level_count(side));
  levels.resize(book.depth(side, levels));
  return levels;
}

std::vector<OrderId> queue_at(const OrderBook& book, Side side, Price price) {
  std::vector<OrderId> ids;
  book.for_each_order(side, price, [&ids](const Order& order) { ids.push_back(order.id); });
  return ids;
}

TEST(OrderBook, StartsEmpty) {
  const OrderBook book;
  EXPECT_TRUE(book.empty());
  EXPECT_FALSE(book.best(Side::Buy).has_value());
  EXPECT_FALSE(book.best(Side::Sell).has_value());
  EXPECT_EQ(book.top(), TopOfBook{});
}

TEST(OrderBook, KeepsLevelsSortedBestFirstOnBothSides) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.add(2, Side::Buy, 102, 20);
  book.add(3, Side::Buy, 101, 30);
  book.add(4, Side::Sell, 105, 40);
  book.add(5, Side::Sell, 103, 50);
  book.add(6, Side::Sell, 104, 60);

  EXPECT_EQ(full_depth(book, Side::Buy),
            (std::vector<LevelView>{{102, 20, 1}, {101, 30, 1}, {100, 10, 1}}));
  EXPECT_EQ(full_depth(book, Side::Sell),
            (std::vector<LevelView>{{103, 50, 1}, {104, 60, 1}, {105, 40, 1}}));
  EXPECT_EQ(book.top(), (TopOfBook{102, 20, 103, 50}));
}

TEST(OrderBook, AggregatesOrdersAtSamePrice) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.add(2, Side::Buy, 100, 15);

  ASSERT_EQ(book.level_count(Side::Buy), 1U);
  EXPECT_EQ(book.best(Side::Buy), (LevelView{100, 25, 2}));
}

TEST(OrderBook, PreservesTimePriorityWithinLevel) {
  OrderBook book;
  book.add(7, Side::Sell, 200, 1);
  book.add(3, Side::Sell, 200, 1);
  book.add(9, Side::Sell, 200, 1);

  EXPECT_EQ(queue_at(book, Side::Sell, 200), (std::vector<OrderId>{7, 3, 9}));
}

TEST(OrderBook, PartialReduceKeepsQueuePosition) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.add(2, Side::Buy, 100, 10);

  EXPECT_TRUE(book.reduce(1, 4));
  EXPECT_EQ(book.find(1)->quantity, 6U);
  EXPECT_EQ(book.best(Side::Buy), (LevelView{100, 16, 2}));
  EXPECT_EQ(queue_at(book, Side::Buy, 100), (std::vector<OrderId>{1, 2}));
}

TEST(OrderBook, FullReduceRemovesOrderAndEmptyLevel) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.add(2, Side::Buy, 99, 10);

  EXPECT_TRUE(book.reduce(1, 10));
  EXPECT_EQ(book.find(1), nullptr);
  EXPECT_EQ(book.level_count(Side::Buy), 1U);
  EXPECT_EQ(book.best(Side::Buy)->price, 99U);
}

TEST(OrderBook, OverfillIsTreatedAsFullFill) {
  OrderBook book;
  book.add(1, Side::Sell, 100, 10);
  EXPECT_TRUE(book.reduce(1, 25));
  EXPECT_TRUE(book.empty());
}

TEST(OrderBook, RemovesFromMiddleOfQueue) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 1);
  book.add(2, Side::Buy, 100, 2);
  book.add(3, Side::Buy, 100, 3);

  EXPECT_TRUE(book.remove(2));
  EXPECT_EQ(queue_at(book, Side::Buy, 100), (std::vector<OrderId>{1, 3}));
  EXPECT_EQ(book.best(Side::Buy), (LevelView{100, 4, 2}));

  EXPECT_TRUE(book.remove(1));
  EXPECT_TRUE(book.remove(3));
  EXPECT_EQ(book.level_count(Side::Buy), 0U);
}

TEST(OrderBook, ReplaceLosesPriorityAndKeepsSide) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.add(2, Side::Buy, 100, 10);

  EXPECT_TRUE(book.replace(1, 11, 5, 100));
  EXPECT_EQ(queue_at(book, Side::Buy, 100), (std::vector<OrderId>{2, 11}));

  EXPECT_TRUE(book.replace(11, 12, 7, 101));
  EXPECT_EQ(book.find(12)->side, Side::Buy);
  EXPECT_EQ(book.best(Side::Buy), (LevelView{101, 7, 1}));
  EXPECT_EQ(book.find(11), nullptr);
}

TEST(OrderBook, RejectsInvalidOperationsWithoutSideEffects) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.add(2, Side::Sell, 101, 10);
  const TopOfBook before = book.top();

  EXPECT_FALSE(book.add(1, Side::Sell, 105, 1));
  EXPECT_FALSE(book.add(3, Side::Buy, 100, 0));
  EXPECT_FALSE(book.reduce(42, 1));
  EXPECT_FALSE(book.remove(42));
  EXPECT_FALSE(book.replace(42, 43, 1, 100));
  EXPECT_FALSE(book.replace(1, 2, 1, 100));

  EXPECT_EQ(book.top(), before);
  EXPECT_EQ(book.order_count(), 2U);
}

TEST(OrderBook, DepthIsBoundedByOutputSize) {
  OrderBook book;
  for (OrderId id = 1; id <= 10; ++id) {
    book.add(id, Side::Sell, static_cast<Price>(100 + id), 1);
  }
  std::array<LevelView, 3> top3{};
  ASSERT_EQ(book.depth(Side::Sell, top3), 3U);
  EXPECT_EQ(top3[0].price, 101U);
  EXPECT_EQ(top3[2].price, 103U);
}

TEST(OrderBook, ClearResetsEverything) {
  OrderBook book;
  book.add(1, Side::Buy, 100, 10);
  book.clear();
  EXPECT_TRUE(book.empty());
  EXPECT_EQ(book.top(), TopOfBook{});
  EXPECT_TRUE(book.add(1, Side::Buy, 100, 10));
}

TEST(OrderBook, MatchesReferenceModelUnderRandomOrderFlow) {
  OrderBook book(8);
  testing::ReferenceBook reference;
  std::mt19937_64 rng(2024);
  std::uniform_int_distribution<int> op_dist(0, 99);
  std::uniform_int_distribution<Price> price_dist(990, 1010);
  std::uniform_int_distribution<Quantity> qty_dist(0, 50);
  std::uniform_int_distribution<OrderId> id_dist(1, 400);

  for (int step = 0; step < 50'000; ++step) {
    const int op = op_dist(rng);
    const OrderId id = id_dist(rng);
    const Quantity qty = qty_dist(rng);
    const Price price = price_dist(rng);
    const Side side = (op & 1) != 0 ? Side::Buy : Side::Sell;

    if (op < 45) {
      ASSERT_EQ(book.add(id, side, price, qty), reference.add(id, side, price, qty));
    } else if (op < 70) {
      ASSERT_EQ(book.reduce(id, qty), reference.reduce(id, qty));
    } else if (op < 90) {
      ASSERT_EQ(book.remove(id), reference.remove(id));
    } else {
      const OrderId new_id = id_dist(rng);
      ASSERT_EQ(book.replace(id, new_id, qty, price), reference.replace(id, new_id, qty, price));
    }

    ASSERT_EQ(book.order_count(), reference.order_count()) << "step " << step;
    ASSERT_EQ(full_depth(book, Side::Buy), reference.depth(Side::Buy)) << "step " << step;
    ASSERT_EQ(full_depth(book, Side::Sell), reference.depth(Side::Sell)) << "step " << step;
  }
}

}  // namespace
}  // namespace itchbook
