#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "itchbook/itch/codec.hpp"
#include "itchbook/itch/endian.hpp"
#include "itchbook/itch/messages.hpp"

namespace itchbook::itch {

template <typename Handler, typename Message>
concept Handles = requires(Handler& handler, const Message& message) { handler.on(message); };

struct ParseStats {
  std::uint64_t messages{};
  std::uint64_t dispatched{};
  std::uint64_t ignored{};
  std::uint64_t malformed{};
};

// Invokes fn(message_body) for every complete length-prefixed frame and returns the number of
// bytes consumed, so a streaming reader can carry an incomplete tail into its next read.
template <typename Fn>
std::size_t for_each_frame(std::span<const std::byte> buffer, Fn&& fn) {
  std::size_t offset = 0;
  while (buffer.size() - offset >= kLengthPrefix) {
    const std::size_t length = load_be<std::uint16_t>(buffer.data() + offset);
    if (buffer.size() - offset - kLengthPrefix < length) {
      break;
    }
    fn(buffer.subspan(offset + kLengthPrefix, length));
    offset += kLengthPrefix + length;
  }
  return offset;
}

// Zero-copy, allocation-free decoder. Dispatch is resolved at compile time: a handler only
// implements on() for the messages it cares about and the rest are counted as ignored.
template <typename Handler>
class Parser {
 public:
  explicit Parser(Handler& handler) noexcept : handler_(handler) {}

  std::size_t parse(std::span<const std::byte> buffer) {
    return for_each_frame(buffer,
                          [this](std::span<const std::byte> message) { dispatch(message); });
  }

  void dispatch(std::span<const std::byte> message) {
    ++stats_.messages;
    if (message.empty()) [[unlikely]] {
      ++stats_.malformed;
      return;
    }

    const std::byte* data = message.data();
    const std::size_t size = message.size();
    switch (static_cast<MessageType>(static_cast<char>(data[0]))) {
      case MessageType::SystemEvent:
        deliver<SystemEvent>(data, size, Codec<SystemEvent>::kSize);
        break;
      case MessageType::StockDirectory:
        deliver<StockDirectory>(data, size, Codec<StockDirectory>::kSize);
        break;
      case MessageType::AddOrder:
        deliver<AddOrder>(data, size, Codec<AddOrder>::kSize);
        break;
      case MessageType::AddOrderMpid:
        deliver<AddOrder>(data, size, Codec<AddOrder>::kMpidSize);
        break;
      case MessageType::OrderExecuted:
        deliver<OrderExecuted>(data, size, Codec<OrderExecuted>::kSize);
        break;
      case MessageType::OrderExecutedWithPrice:
        deliver<OrderExecutedWithPrice>(data, size, Codec<OrderExecutedWithPrice>::kSize);
        break;
      case MessageType::OrderCancel:
        deliver<OrderCancel>(data, size, Codec<OrderCancel>::kSize);
        break;
      case MessageType::OrderDelete:
        deliver<OrderDelete>(data, size, Codec<OrderDelete>::kSize);
        break;
      case MessageType::OrderReplace:
        deliver<OrderReplace>(data, size, Codec<OrderReplace>::kSize);
        break;
      case MessageType::Trade:
        deliver<Trade>(data, size, Codec<Trade>::kSize);
        break;
      default:
        ++stats_.ignored;
        break;
    }
  }

  [[nodiscard]] const ParseStats& stats() const noexcept { return stats_; }

 private:
  template <typename Message>
  void deliver(const std::byte* data, std::size_t size, std::size_t expected_size) {
    if constexpr (Handles<Handler, Message>) {
      if (size < expected_size) [[unlikely]] {
        ++stats_.malformed;
        return;
      }
      handler_.on(Codec<Message>::decode(data));
      ++stats_.dispatched;
    } else {
      ++stats_.ignored;
    }
  }

  Handler& handler_;
  ParseStats stats_;
};

}  // namespace itchbook::itch
