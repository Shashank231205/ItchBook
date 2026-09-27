#include "itchbook/sim/feed_generator.hpp"

#include <gtest/gtest.h>

#include "itchbook/book/book_builder.hpp"
#include "itchbook/itch/parser.hpp"

namespace itchbook::sim {
namespace {

TEST(FeedGenerator, IsDeterministicForAGivenSeed) {
  const FeedConfig config{.instruments = 3, .events = 5'000, .seed = 11};
  EXPECT_EQ(generate_feed(config), generate_feed(config));

  FeedConfig other = config;
  other.seed = 12;
  EXPECT_NE(generate_feed(config), generate_feed(other));
}

TEST(FeedGenerator, ProducesAConsistentFeedThatReplaysCleanly) {
  const FeedConfig config{.instruments = 5, .events = 100'000, .seed = 3};
  const auto feed = generate_feed(config);

  BookBuilder<> builder;
  itch::Parser parser(builder);
  EXPECT_EQ(parser.parse(feed), feed.size());

  EXPECT_EQ(parser.stats().malformed, 0U);
  EXPECT_EQ(builder.stats().unknown_orders, 0U);
  EXPECT_EQ(builder.stats().rejected_adds, 0U);
  EXPECT_GT(builder.stats().trades, 0U);
  for (StockLocate locate = 1; locate <= config.instruments; ++locate) {
    ASSERT_NE(builder.book(locate), nullptr);
    const TopOfBook top = builder.book(locate)->top();
    if (top.has_bid() && top.has_ask()) {
      EXPECT_LT(top.bid_price, top.ask_price);
    }
    EXPECT_EQ(builder.symbol(locate).view(), "SYM" + std::to_string(locate - 1));
  }
}

}  // namespace
}  // namespace itchbook::sim
