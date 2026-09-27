#include "itchbook/book/book_builder.hpp"

#include <gtest/gtest.h>

#include <vector>

#include "itchbook/itch/encoder.hpp"
#include "itchbook/itch/parser.hpp"

namespace itchbook {
namespace {

struct TopEvent {
  StockLocate locate;
  Timestamp timestamp;
  TopOfBook top;

  friend bool operator==(const TopEvent&, const TopEvent&) = default;
};

struct RecordingListener {
  std::vector<TopEvent> tops;
  std::vector<TradeEvent> trades;

  void on_top_of_book(StockLocate locate, Timestamp timestamp, const TopOfBook& top) {
    tops.push_back({locate, timestamp, top});
  }
  void on_trade(const TradeEvent& trade) { trades.push_back(trade); }
};

class BookBuilderTest : public ::testing::Test {
 protected:
  static constexpr StockLocate kAapl = 1;
  static constexpr StockLocate kMsft = 2;

  static itch::Header at(StockLocate locate, Timestamp ts) { return {locate, 0, ts}; }

  void replay() {
    itch::Parser parser(builder_);
    parser.parse(encoder_.bytes());
    encoder_.clear();
  }

  itch::Encoder encoder_;
  BookBuilder<RecordingListener> builder_;
};

TEST_F(BookBuilderTest, RegistersSymbolsFromStockDirectory) {
  encoder_.write(itch::StockDirectory{at(kAapl, 1), Symbol::from("AAPL"), 'Q', 'N', 100});
  replay();
  EXPECT_EQ(builder_.symbol(kAapl).view(), "AAPL");
  EXPECT_EQ(builder_.book(kAapl), nullptr);
}

TEST_F(BookBuilderTest, RoutesOrdersToPerInstrumentBooks) {
  encoder_.write(itch::AddOrder{at(kAapl, 1), 1, Side::Buy, 100, Symbol::from("AAPL"), 1'000'000});
  encoder_.write(itch::AddOrder{at(kMsft, 2), 2, Side::Sell, 200, Symbol::from("MSFT"), 4'000'000});
  replay();

  ASSERT_NE(builder_.book(kAapl), nullptr);
  ASSERT_NE(builder_.book(kMsft), nullptr);
  EXPECT_EQ(builder_.book(kAapl)->top(), (TopOfBook{1'000'000, 100, 0, 0}));
  EXPECT_EQ(builder_.book(kMsft)->top(), (TopOfBook{0, 0, 4'000'000, 200}));
}

TEST_F(BookBuilderTest, PublishesOnlyWhenTopOfBookChanges) {
  const Symbol s = Symbol::from("AAPL");
  encoder_.write(itch::AddOrder{at(kAapl, 1), 1, Side::Buy, 100, s, 1'000'000});
  encoder_.write(itch::AddOrder{at(kAapl, 2), 2, Side::Buy, 100, s, 990'000});
  encoder_.write(itch::AddOrder{at(kAapl, 3), 3, Side::Sell, 300, s, 1'010'000});
  encoder_.write(itch::OrderDelete{at(kAapl, 4), 2});
  replay();

  const auto& tops = builder_.listener().tops;
  ASSERT_EQ(tops.size(), 2U);
  EXPECT_EQ(tops[0], (TopEvent{kAapl, 1, {1'000'000, 100, 0, 0}}));
  EXPECT_EQ(tops[1], (TopEvent{kAapl, 3, {1'000'000, 100, 1'010'000, 300}}));
  EXPECT_EQ(builder_.stats().top_changes, 2U);
}

TEST_F(BookBuilderTest, ExecutionsReduceBookAndEmitTrades) {
  const Symbol s = Symbol::from("AAPL");
  encoder_.write(itch::AddOrder{at(kAapl, 1), 1, Side::Sell, 500, s, 1'010'000});
  encoder_.write(itch::OrderExecuted{at(kAapl, 2), 1, 200, 77});
  encoder_.write(itch::OrderExecutedWithPrice{at(kAapl, 3), 1, 100, 78, true, 1'009'900});
  encoder_.write(itch::OrderExecutedWithPrice{at(kAapl, 4), 1, 50, 79, false, 1'009'900});
  replay();

  const auto& trades = builder_.listener().trades;
  ASSERT_EQ(trades.size(), 2U);
  EXPECT_EQ(trades[0].price, 1'010'000U);
  EXPECT_EQ(trades[0].quantity, 200U);
  EXPECT_EQ(trades[0].resting_side, Side::Sell);
  EXPECT_EQ(trades[1].price, 1'009'900U);
  EXPECT_EQ(trades[1].match_number, 78U);
  EXPECT_EQ(builder_.book(kAapl)->find(1)->quantity, 150U);
}

TEST_F(BookBuilderTest, CancelReplaceAndHiddenTrade) {
  const Symbol s = Symbol::from("AAPL");
  encoder_.write(itch::AddOrder{at(kAapl, 1), 1, Side::Buy, 500, s, 1'000'000});
  encoder_.write(itch::OrderCancel{at(kAapl, 2), 1, 100});
  encoder_.write(itch::OrderReplace{at(kAapl, 3), 1, 2, 250, 1'000'100});
  encoder_.write(itch::Trade{at(kAapl, 4), 0, Side::Buy, 900, s, 1'000'050, 80});
  replay();

  const OrderBook& book = *builder_.book(kAapl);
  EXPECT_EQ(book.find(1), nullptr);
  EXPECT_EQ(book.top(), (TopOfBook{1'000'100, 250, 0, 0}));
  ASSERT_EQ(builder_.listener().trades.size(), 1U);
  EXPECT_EQ(builder_.listener().trades[0].quantity, 900U);
}

TEST_F(BookBuilderTest, CountsReferencesToUnknownOrders) {
  encoder_.write(itch::OrderExecuted{at(kAapl, 1), 404, 1, 1});
  encoder_.write(itch::OrderCancel{at(kAapl, 2), 404, 1});
  encoder_.write(itch::OrderDelete{at(kAapl, 3), 404});
  encoder_.write(itch::OrderReplace{at(kAapl, 4), 404, 405, 1, 1});
  replay();

  EXPECT_EQ(builder_.stats().unknown_orders, 4U);
  EXPECT_TRUE(builder_.listener().tops.empty());
}

TEST(BookBuilder, WorksWithoutListener) {
  itch::Encoder encoder;
  encoder.write(itch::AddOrder{{1, 0, 1}, 1, Side::Buy, 100, Symbol::from("X"), 100});
  BookBuilder<> builder;
  itch::Parser parser(builder);
  parser.parse(encoder.bytes());
  EXPECT_EQ(builder.stats().top_changes, 1U);
}

}  // namespace
}  // namespace itchbook
