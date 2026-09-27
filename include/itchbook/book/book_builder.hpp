#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "itchbook/book/order_book.hpp"
#include "itchbook/core/symbol.hpp"
#include "itchbook/core/types.hpp"
#include "itchbook/itch/messages.hpp"

namespace itchbook {

struct TradeEvent {
  StockLocate stock_locate{};
  Timestamp timestamp{};
  OrderId order_id{};
  Side resting_side{};
  Price price{};
  Quantity quantity{};
  std::uint64_t match_number{};
};

struct BuilderStats {
  std::uint64_t unknown_orders{};
  std::uint64_t rejected_adds{};
  std::uint64_t top_changes{};
  std::uint64_t trades{};
};

// Default listener: every callback is optional, so this compiles all publishing away.
struct NullListener {};

// ITCH handler that maintains one OrderBook per stock locate and publishes top-of-book
// changes and trades to Listener. Listener hooks are detected at compile time:
//   void on_top_of_book(StockLocate, Timestamp, const TopOfBook&);
//   void on_trade(const TradeEvent&);
template <typename Listener = NullListener>
class BookBuilder {
 public:
  explicit BookBuilder(Listener listener = {})
      : listener_(std::move(listener)),
        books_(kMaxInstruments),
        tops_(kMaxInstruments),
        symbols_(kMaxInstruments) {}

  void on(const itch::StockDirectory& m) { symbols_[m.header.stock_locate] = m.stock; }

  void on(const itch::AddOrder& m) {
    if (!book_for(m.header.stock_locate).add(m.order_id, m.side, m.price, m.shares)) {
      ++stats_.rejected_adds;
      return;
    }
    publish_top(m.header);
  }

  void on(const itch::OrderExecuted& m) {
    OrderBook& book = book_for(m.header.stock_locate);
    const Order* order = book.find(m.order_id);
    if (order == nullptr) {
      ++stats_.unknown_orders;
      return;
    }
    publish_trade(m.header, m.order_id, order->side, order->price, m.executed_shares,
                  m.match_number);
    book.reduce(m.order_id, m.executed_shares);
    publish_top(m.header);
  }

  void on(const itch::OrderExecutedWithPrice& m) {
    OrderBook& book = book_for(m.header.stock_locate);
    const Order* order = book.find(m.order_id);
    if (order == nullptr) {
      ++stats_.unknown_orders;
      return;
    }
    if (m.printable) {
      publish_trade(m.header, m.order_id, order->side, m.execution_price, m.executed_shares,
                    m.match_number);
    }
    book.reduce(m.order_id, m.executed_shares);
    publish_top(m.header);
  }

  void on(const itch::OrderCancel& m) {
    if (!book_for(m.header.stock_locate).reduce(m.order_id, m.cancelled_shares)) {
      ++stats_.unknown_orders;
      return;
    }
    publish_top(m.header);
  }

  void on(const itch::OrderDelete& m) {
    if (!book_for(m.header.stock_locate).remove(m.order_id)) {
      ++stats_.unknown_orders;
      return;
    }
    publish_top(m.header);
  }

  void on(const itch::OrderReplace& m) {
    if (!book_for(m.header.stock_locate)
             .replace(m.original_order_id, m.new_order_id, m.shares, m.price)) {
      ++stats_.unknown_orders;
      return;
    }
    publish_top(m.header);
  }

  void on(const itch::Trade& m) {
    publish_trade(m.header, m.order_id, m.side, m.price, m.shares, m.match_number);
  }

  [[nodiscard]] const OrderBook* book(StockLocate locate) const noexcept {
    return books_[locate].get();
  }
  [[nodiscard]] const Symbol& symbol(StockLocate locate) const noexcept { return symbols_[locate]; }
  [[nodiscard]] const BuilderStats& stats() const noexcept { return stats_; }
  [[nodiscard]] Listener& listener() noexcept { return listener_; }
  [[nodiscard]] const Listener& listener() const noexcept { return listener_; }

 private:
  OrderBook& book_for(StockLocate locate) {
    std::unique_ptr<OrderBook>& book = books_[locate];
    if (!book) [[unlikely]] {
      book = std::make_unique<OrderBook>();
    }
    return *book;
  }

  void publish_top(const itch::Header& header) {
    const TopOfBook current = books_[header.stock_locate]->top();
    TopOfBook& previous = tops_[header.stock_locate];
    if (current == previous) {
      return;
    }
    previous = current;
    ++stats_.top_changes;
    if constexpr (requires {
                    listener_.on_top_of_book(header.stock_locate, header.timestamp, current);
                  }) {
      listener_.on_top_of_book(header.stock_locate, header.timestamp, current);
    }
  }

  void publish_trade(const itch::Header& header, OrderId order_id, Side side, Price price,
                     Quantity quantity, std::uint64_t match_number) {
    ++stats_.trades;
    if constexpr (requires(const TradeEvent& trade) { listener_.on_trade(trade); }) {
      listener_.on_trade(TradeEvent{header.stock_locate, header.timestamp, order_id, side, price,
                                    quantity, match_number});
    }
  }

  Listener listener_;
  std::vector<std::unique_ptr<OrderBook>> books_;
  std::vector<TopOfBook> tops_;
  std::vector<Symbol> symbols_;
  BuilderStats stats_;
};

}  // namespace itchbook
