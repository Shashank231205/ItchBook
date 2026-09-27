#include "itchbook/itch/parser.hpp"

#include <gtest/gtest.h>

#include <variant>
#include <vector>

#include "itchbook/itch/encoder.hpp"

namespace itchbook::itch {
namespace {

using AnyMessage =
    std::variant<SystemEvent, StockDirectory, AddOrder, OrderExecuted, OrderExecutedWithPrice,
                 OrderCancel, OrderDelete, OrderReplace, Trade>;

struct RecordingHandler {
  std::vector<AnyMessage> messages;

  template <typename Message>
  void on(const Message& message) {
    messages.emplace_back(message);
  }
};

struct AddOrderOnlyHandler {
  int adds = 0;
  void on(const AddOrder&) { ++adds; }
};

constexpr Header kHeader{42, 7, 0x0000'1234'5678'9ABCULL};

std::vector<AnyMessage> sample_messages() {
  const Symbol aapl = Symbol::from("AAPL");
  return {
      SystemEvent{kHeader, 'O'},
      StockDirectory{kHeader, aapl, 'Q', 'N', 100},
      AddOrder{kHeader, 0xDEAD'BEEF'0000'0001ULL, Side::Buy, 300, aapl, 1'905'100},
      AddOrder{kHeader, 2, Side::Sell, 1, aapl, 4'294'967'295U},
      OrderExecuted{kHeader, 3, 100, 987'654'321},
      OrderExecutedWithPrice{kHeader, 4, 50, 11, true, 1'905'200},
      OrderExecutedWithPrice{kHeader, 5, 60, 12, false, 1'905'300},
      OrderCancel{kHeader, 6, 25},
      OrderDelete{kHeader, 7},
      OrderReplace{kHeader, 8, 9, 400, 1'905'000},
      Trade{kHeader, 10, Side::Sell, 700, aapl, 1'904'900, 13},
  };
}

std::vector<std::byte> encode_all(const std::vector<AnyMessage>& messages) {
  Encoder encoder;
  for (const AnyMessage& message : messages) {
    std::visit([&encoder](const auto& m) { encoder.write(m); }, message);
  }
  return encoder.release();
}

std::vector<std::byte> frame(std::initializer_list<std::byte> body) {
  std::vector<std::byte> out(kLengthPrefix);
  store_be(out.data(), static_cast<std::uint16_t>(body.size()));
  out.insert(out.end(), body);
  return out;
}

TEST(Endian, LoadsAndStoresBigEndian) {
  std::byte buffer[8]{};
  store_be<std::uint32_t>(buffer, 0x0102'0304U);
  EXPECT_EQ(buffer[0], std::byte{0x01});
  EXPECT_EQ(buffer[3], std::byte{0x04});
  EXPECT_EQ(load_be<std::uint32_t>(buffer), 0x0102'0304U);

  store_be48(buffer, 0x0000'A1B2'C3D4'E5F6ULL);
  EXPECT_EQ(buffer[0], std::byte{0xA1});
  EXPECT_EQ(buffer[5], std::byte{0xF6});
  EXPECT_EQ(load_be48(buffer), 0x0000'A1B2'C3D4'E5F6ULL);
}

TEST(Symbol, TrimsTrailingPadding) {
  EXPECT_EQ(Symbol::from("MSFT").view(), "MSFT");
  EXPECT_EQ(Symbol::from("ABCDEFGHIJ").view(), "ABCDEFGH");
  EXPECT_EQ(Symbol{}.view(), "");
}

TEST(Parser, RoundTripsEverySupportedMessageType) {
  const auto expected = sample_messages();
  const auto bytes = encode_all(expected);

  RecordingHandler handler;
  Parser parser(handler);
  EXPECT_EQ(parser.parse(bytes), bytes.size());

  EXPECT_EQ(handler.messages, expected);
  EXPECT_EQ(parser.stats().messages, expected.size());
  EXPECT_EQ(parser.stats().dispatched, expected.size());
  EXPECT_EQ(parser.stats().malformed, 0U);
}

TEST(Parser, DecodesAddOrderWithMpidAsAddOrder) {
  Encoder encoder;
  const AddOrder add{kHeader, 99, Side::Sell, 200, Symbol::from("IBM"), 1'500'000};
  encoder.write(add);
  std::vector<std::byte> bytes(encoder.bytes().begin(), encoder.bytes().end());
  bytes[kLengthPrefix] = std::byte{'F'};
  for (char c : {'N', 'S', 'D', 'Q'}) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  store_be(bytes.data(), static_cast<std::uint16_t>(Codec<AddOrder>::kMpidSize));

  RecordingHandler handler;
  Parser parser(handler);
  parser.parse(bytes);
  ASSERT_EQ(handler.messages.size(), 1U);
  EXPECT_EQ(std::get<AddOrder>(handler.messages[0]), add);
}

TEST(Parser, StopsAtIncompleteTrailingFrame) {
  const auto bytes = encode_all(sample_messages());
  const auto first_frame = kLengthPrefix + Codec<SystemEvent>::kSize;
  const std::span<const std::byte> truncated(bytes.data(), first_frame + 5);

  RecordingHandler handler;
  Parser parser(handler);
  EXPECT_EQ(parser.parse(truncated), first_frame);
  EXPECT_EQ(handler.messages.size(), 1U);
}

TEST(Parser, CountsUnknownAndMalformedMessages) {
  std::vector<std::byte> bytes = frame({std::byte{'Z'}, std::byte{0}, std::byte{1}});
  const auto short_add = frame({std::byte{'A'}, std::byte{0}, std::byte{1}});
  const auto empty = frame({});
  bytes.insert(bytes.end(), short_add.begin(), short_add.end());
  bytes.insert(bytes.end(), empty.begin(), empty.end());

  RecordingHandler handler;
  Parser parser(handler);
  EXPECT_EQ(parser.parse(bytes), bytes.size());
  EXPECT_TRUE(handler.messages.empty());
  EXPECT_EQ(parser.stats().messages, 3U);
  EXPECT_EQ(parser.stats().ignored, 1U);
  EXPECT_EQ(parser.stats().malformed, 2U);
}

TEST(Parser, IgnoresMessagesTheHandlerDoesNotHandle) {
  const auto bytes = encode_all(sample_messages());
  AddOrderOnlyHandler handler;
  Parser parser(handler);
  parser.parse(bytes);
  EXPECT_EQ(handler.adds, 2);
  EXPECT_EQ(parser.stats().dispatched, 2U);
  EXPECT_EQ(parser.stats().ignored, sample_messages().size() - 2);
}

}  // namespace
}  // namespace itchbook::itch
