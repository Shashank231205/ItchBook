#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "itchbook/itch/endian.hpp"
#include "itchbook/itch/messages.hpp"

// Single source of truth for ITCH 5.0 field offsets. Each Codec<M> knows its wire type,
// size, and how to decode/encode one message body (the part after the length prefix).
namespace itchbook::itch {

namespace detail {

[[nodiscard]] inline Header read_header(const std::byte* p) noexcept {
  return {load_be<std::uint16_t>(p + 1), load_be<std::uint16_t>(p + 3), load_be48(p + 5)};
}

inline void write_header(std::byte* p, MessageType type, const Header& header) noexcept {
  p[0] = static_cast<std::byte>(type);
  store_be(p + 1, header.stock_locate);
  store_be(p + 3, header.tracking_number);
  store_be48(p + 5, header.timestamp);
}

[[nodiscard]] inline char read_char(const std::byte* p) noexcept { return static_cast<char>(*p); }

inline void write_char(std::byte* p, char value) noexcept { *p = static_cast<std::byte>(value); }

[[nodiscard]] inline Side read_side(const std::byte* p) noexcept {
  return read_char(p) == 'B' ? Side::Buy : Side::Sell;
}

inline void write_side(std::byte* p, Side side) noexcept {
  write_char(p, side == Side::Buy ? 'B' : 'S');
}

inline void write_symbol(std::byte* p, const Symbol& symbol) noexcept {
  std::memcpy(p, symbol.raw().data(), Symbol::kWidth);
}

}  // namespace detail

template <typename Message>
struct Codec;

template <>
struct Codec<SystemEvent> {
  static constexpr MessageType kType = MessageType::SystemEvent;
  static constexpr std::size_t kSize = 12;

  [[nodiscard]] static SystemEvent decode(const std::byte* p) noexcept {
    return {detail::read_header(p), detail::read_char(p + 11)};
  }

  static void encode(const SystemEvent& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    detail::write_char(p + 11, m.event_code);
  }
};

template <>
struct Codec<StockDirectory> {
  static constexpr MessageType kType = MessageType::StockDirectory;
  static constexpr std::size_t kSize = 39;

  [[nodiscard]] static StockDirectory decode(const std::byte* p) noexcept {
    return {detail::read_header(p), Symbol::from_wire(p + 11), detail::read_char(p + 19),
            detail::read_char(p + 20), load_be<std::uint32_t>(p + 21)};
  }

  static void encode(const StockDirectory& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    detail::write_symbol(p + 11, m.stock);
    detail::write_char(p + 19, m.market_category);
    detail::write_char(p + 20, m.financial_status);
    store_be(p + 21, m.round_lot_size);
  }
};

template <>
struct Codec<AddOrder> {
  static constexpr MessageType kType = MessageType::AddOrder;
  static constexpr std::size_t kSize = 36;
  // 'F' shares the 'A' layout and appends a 4-byte MPID attribution.
  static constexpr std::size_t kMpidSize = 40;

  [[nodiscard]] static AddOrder decode(const std::byte* p) noexcept {
    return {detail::read_header(p),    load_be<std::uint64_t>(p + 11),
            detail::read_side(p + 19), load_be<std::uint32_t>(p + 20),
            Symbol::from_wire(p + 24), load_be<std::uint32_t>(p + 32)};
  }

  static void encode(const AddOrder& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.order_id);
    detail::write_side(p + 19, m.side);
    store_be(p + 20, m.shares);
    detail::write_symbol(p + 24, m.stock);
    store_be(p + 32, m.price);
  }
};

template <>
struct Codec<OrderExecuted> {
  static constexpr MessageType kType = MessageType::OrderExecuted;
  static constexpr std::size_t kSize = 31;

  [[nodiscard]] static OrderExecuted decode(const std::byte* p) noexcept {
    return {detail::read_header(p), load_be<std::uint64_t>(p + 11), load_be<std::uint32_t>(p + 19),
            load_be<std::uint64_t>(p + 23)};
  }

  static void encode(const OrderExecuted& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.order_id);
    store_be(p + 19, m.executed_shares);
    store_be(p + 23, m.match_number);
  }
};

template <>
struct Codec<OrderExecutedWithPrice> {
  static constexpr MessageType kType = MessageType::OrderExecutedWithPrice;
  static constexpr std::size_t kSize = 36;

  [[nodiscard]] static OrderExecutedWithPrice decode(const std::byte* p) noexcept {
    return {detail::read_header(p),           load_be<std::uint64_t>(p + 11),
            load_be<std::uint32_t>(p + 19),   load_be<std::uint64_t>(p + 23),
            detail::read_char(p + 31) == 'Y', load_be<std::uint32_t>(p + 32)};
  }

  static void encode(const OrderExecutedWithPrice& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.order_id);
    store_be(p + 19, m.executed_shares);
    store_be(p + 23, m.match_number);
    detail::write_char(p + 31, m.printable ? 'Y' : 'N');
    store_be(p + 32, m.execution_price);
  }
};

template <>
struct Codec<OrderCancel> {
  static constexpr MessageType kType = MessageType::OrderCancel;
  static constexpr std::size_t kSize = 23;

  [[nodiscard]] static OrderCancel decode(const std::byte* p) noexcept {
    return {detail::read_header(p), load_be<std::uint64_t>(p + 11), load_be<std::uint32_t>(p + 19)};
  }

  static void encode(const OrderCancel& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.order_id);
    store_be(p + 19, m.cancelled_shares);
  }
};

template <>
struct Codec<OrderDelete> {
  static constexpr MessageType kType = MessageType::OrderDelete;
  static constexpr std::size_t kSize = 19;

  [[nodiscard]] static OrderDelete decode(const std::byte* p) noexcept {
    return {detail::read_header(p), load_be<std::uint64_t>(p + 11)};
  }

  static void encode(const OrderDelete& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.order_id);
  }
};

template <>
struct Codec<OrderReplace> {
  static constexpr MessageType kType = MessageType::OrderReplace;
  static constexpr std::size_t kSize = 35;

  [[nodiscard]] static OrderReplace decode(const std::byte* p) noexcept {
    return {detail::read_header(p), load_be<std::uint64_t>(p + 11), load_be<std::uint64_t>(p + 19),
            load_be<std::uint32_t>(p + 27), load_be<std::uint32_t>(p + 31)};
  }

  static void encode(const OrderReplace& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.original_order_id);
    store_be(p + 19, m.new_order_id);
    store_be(p + 27, m.shares);
    store_be(p + 31, m.price);
  }
};

template <>
struct Codec<Trade> {
  static constexpr MessageType kType = MessageType::Trade;
  static constexpr std::size_t kSize = 44;

  [[nodiscard]] static Trade decode(const std::byte* p) noexcept {
    return {detail::read_header(p),        load_be<std::uint64_t>(p + 11),
            detail::read_side(p + 19),     load_be<std::uint32_t>(p + 20),
            Symbol::from_wire(p + 24),     load_be<std::uint32_t>(p + 32),
            load_be<std::uint64_t>(p + 36)};
  }

  static void encode(const Trade& m, std::byte* p) noexcept {
    detail::write_header(p, kType, m.header);
    store_be(p + 11, m.order_id);
    detail::write_side(p + 19, m.side);
    store_be(p + 20, m.shares);
    detail::write_symbol(p + 24, m.stock);
    store_be(p + 32, m.price);
    store_be(p + 36, m.match_number);
  }
};

}  // namespace itchbook::itch
