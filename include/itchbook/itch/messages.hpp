#pragma once

#include <cstddef>
#include <cstdint>

#include "itchbook/core/symbol.hpp"
#include "itchbook/core/types.hpp"

namespace itchbook::itch {

// Nasdaq TotalView-ITCH 5.0. Only messages that affect the order book or instrument
// metadata are decoded; everything else is skipped using the frame length.
enum class MessageType : char {
  SystemEvent = 'S',
  StockDirectory = 'R',
  AddOrder = 'A',
  AddOrderMpid = 'F',
  OrderExecuted = 'E',
  OrderExecutedWithPrice = 'C',
  OrderCancel = 'X',
  OrderDelete = 'D',
  OrderReplace = 'U',
  Trade = 'P',
};

// Every message on disk is preceded by a 2-byte big-endian length.
inline constexpr std::size_t kLengthPrefix = 2;

struct Header {
  StockLocate stock_locate{};
  std::uint16_t tracking_number{};
  Timestamp timestamp{};

  friend bool operator==(const Header&, const Header&) = default;
};

struct SystemEvent {
  Header header;
  char event_code{};

  friend bool operator==(const SystemEvent&, const SystemEvent&) = default;
};

struct StockDirectory {
  Header header;
  Symbol stock;
  char market_category{};
  char financial_status{};
  std::uint32_t round_lot_size{};

  friend bool operator==(const StockDirectory&, const StockDirectory&) = default;
};

struct AddOrder {
  Header header;
  OrderId order_id{};
  Side side{};
  Quantity shares{};
  Symbol stock;
  Price price{};

  friend bool operator==(const AddOrder&, const AddOrder&) = default;
};

struct OrderExecuted {
  Header header;
  OrderId order_id{};
  Quantity executed_shares{};
  std::uint64_t match_number{};

  friend bool operator==(const OrderExecuted&, const OrderExecuted&) = default;
};

struct OrderExecutedWithPrice {
  Header header;
  OrderId order_id{};
  Quantity executed_shares{};
  std::uint64_t match_number{};
  bool printable{};
  Price execution_price{};

  friend bool operator==(const OrderExecutedWithPrice&, const OrderExecutedWithPrice&) = default;
};

struct OrderCancel {
  Header header;
  OrderId order_id{};
  Quantity cancelled_shares{};

  friend bool operator==(const OrderCancel&, const OrderCancel&) = default;
};

struct OrderDelete {
  Header header;
  OrderId order_id{};

  friend bool operator==(const OrderDelete&, const OrderDelete&) = default;
};

struct OrderReplace {
  Header header;
  OrderId original_order_id{};
  OrderId new_order_id{};
  Quantity shares{};
  Price price{};

  friend bool operator==(const OrderReplace&, const OrderReplace&) = default;
};

// Execution against a non-displayed order; does not change the visible book.
struct Trade {
  Header header;
  OrderId order_id{};
  Side side{};
  Quantity shares{};
  Symbol stock;
  Price price{};
  std::uint64_t match_number{};

  friend bool operator==(const Trade&, const Trade&) = default;
};

}  // namespace itchbook::itch
