// C ABI over the order book engine for the browser demo. Results are written into static
// buffers and returned as pointers so JavaScript can read them straight out of wasm memory
// without per-call allocation.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "itchbook/book/book_builder.hpp"
#include "itchbook/itch/endian.hpp"
#include "itchbook/itch/parser.hpp"
#include "itchbook/sim/feed_generator.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define ITCHBOOK_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define ITCHBOOK_EXPORT extern "C"
#endif

namespace {

using namespace itchbook;

constexpr std::size_t kTapeSize = 64;
constexpr std::size_t kMaxDepth = 32;
constexpr std::size_t kLevelFields = 3;
constexpr std::size_t kTradeFields = 5;
constexpr std::size_t kHeaderSize = 11;

class TradeTape {
 public:
  void on_trade(const TradeEvent& trade) noexcept {
    trades_[next_ % kTapeSize] = trade;
    ++next_;
  }

  // Visits up to `limit` trades, newest first.
  template <typename Fn>
  void for_each_recent(std::size_t limit, Fn&& fn) const {
    const std::size_t count = std::min({limit, next_, kTapeSize});
    for (std::size_t i = 1; i <= count; ++i) {
      fn(trades_[(next_ - i) % kTapeSize]);
    }
  }

 private:
  std::array<TradeEvent, kTapeSize> trades_{};
  std::size_t next_{};
};

class Session {
 public:
  explicit Session(const sim::FeedConfig& config)
      : instruments_(config.instruments), feed_(sim::generate_feed(config)), parser_(builder_) {
    itch::for_each_frame(feed_, [this](std::span<const std::byte>) { ++total_messages_; });
  }

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  std::size_t step(std::size_t max_messages) {
    const std::byte* data = feed_.data();
    std::size_t done = 0;
    while (done < max_messages && feed_.size() - offset_ >= itch::kLengthPrefix) {
      const std::size_t length = itch::load_be<std::uint16_t>(data + offset_);
      const std::size_t body = offset_ + itch::kLengthPrefix;
      if (feed_.size() - body < length) {
        break;
      }
      if (length >= kHeaderSize) {
        last_timestamp_ = itch::load_be48(data + body + 5);
      }
      parser_.dispatch({data + body, length});
      offset_ = body + length;
      ++done;
    }
    processed_ += done;
    return done;
  }

  [[nodiscard]] const BookBuilder<TradeTape>& builder() const noexcept { return builder_; }
  [[nodiscard]] std::size_t instruments() const noexcept { return instruments_; }
  [[nodiscard]] std::size_t processed() const noexcept { return processed_; }
  [[nodiscard]] std::size_t total() const noexcept { return total_messages_; }
  [[nodiscard]] Timestamp last_timestamp() const noexcept { return last_timestamp_; }

 private:
  std::size_t instruments_;
  std::vector<std::byte> feed_;
  BookBuilder<TradeTape> builder_;
  itch::Parser<BookBuilder<TradeTape>> parser_;
  std::size_t offset_{};
  std::size_t processed_{};
  std::size_t total_messages_{};
  Timestamp last_timestamp_{};
};

std::unique_ptr<Session> g_session;
std::array<double, 2 + 2 * kMaxDepth * kLevelFields> g_depth{};
std::array<double, 5> g_top{};
std::array<double, 1 + kTapeSize * kTradeFields> g_trades{};
std::array<double, 6> g_stats{};
std::array<char, Symbol::kWidth + 1> g_symbol{};

const OrderBook* book_at(std::uint32_t locate) {
  if (!g_session || locate == 0 || locate > g_session->instruments()) {
    return nullptr;
  }
  return g_session->builder().book(static_cast<StockLocate>(locate));
}

std::size_t write_levels(const OrderBook& book, Side side, std::size_t levels, double* out) {
  std::array<LevelView, kMaxDepth> views{};
  const std::size_t count = book.depth(side, std::span(views.data(), levels));
  for (std::size_t i = 0; i < count; ++i) {
    out[i * kLevelFields] = static_cast<double>(views[i].price);
    out[i * kLevelFields + 1] = static_cast<double>(views[i].quantity);
    out[i * kLevelFields + 2] = static_cast<double>(views[i].order_count);
  }
  return count;
}

}  // namespace

// Generates a synthetic feed and resets the engine. Returns the number of messages in the feed.
ITCHBOOK_EXPORT double ib_init(std::uint32_t instruments, std::uint32_t events,
                               std::uint32_t seed) {
  sim::FeedConfig config;
  config.instruments = std::clamp<std::size_t>(instruments, 1, 200);
  config.events = events;
  config.seed = seed;
  g_session = std::make_unique<Session>(config);
  return static_cast<double>(g_session->total());
}

ITCHBOOK_EXPORT std::uint32_t ib_step(std::uint32_t max_messages) {
  return g_session ? static_cast<std::uint32_t>(g_session->step(max_messages)) : 0;
}

// Processes the rest of the feed as fast as possible; returns elapsed wall time in ms.
ITCHBOOK_EXPORT double ib_run_to_end() {
  if (!g_session) {
    return 0.0;
  }
  const auto start = std::chrono::steady_clock::now();
  while (g_session->step(1U << 20U) != 0) {
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
      .count();
}

ITCHBOOK_EXPORT std::uint32_t ib_instrument_count() {
  return g_session ? static_cast<std::uint32_t>(g_session->instruments()) : 0;
}

ITCHBOOK_EXPORT const char* ib_symbol(std::uint32_t locate) {
  g_symbol.fill('\0');
  if (g_session && locate != 0 && locate <= g_session->instruments()) {
    const std::string_view name =
        g_session->builder().symbol(static_cast<StockLocate>(locate)).view();
    std::copy(name.begin(), name.end(), g_symbol.begin());
  }
  return g_symbol.data();
}

// Layout: [bid_count, ask_count, bids (price, qty, orders)..., asks (price, qty, orders)...]
ITCHBOOK_EXPORT const double* ib_depth(std::uint32_t locate, std::uint32_t levels) {
  g_depth.fill(0.0);
  const OrderBook* book = book_at(locate);
  if (book == nullptr) {
    return g_depth.data();
  }
  const std::size_t n = std::min<std::size_t>(levels, kMaxDepth);
  double* bids = g_depth.data() + 2;
  double* asks = bids + n * kLevelFields;
  g_depth[0] = static_cast<double>(write_levels(*book, Side::Buy, n, bids));
  g_depth[1] = static_cast<double>(write_levels(*book, Side::Sell, n, asks));
  return g_depth.data();
}

// Layout: [bid_price, bid_qty, ask_price, ask_qty, resting_orders]
ITCHBOOK_EXPORT const double* ib_top(std::uint32_t locate) {
  g_top.fill(0.0);
  if (const OrderBook* book = book_at(locate)) {
    const TopOfBook top = book->top();
    g_top = {static_cast<double>(top.bid_price), static_cast<double>(top.bid_quantity),
             static_cast<double>(top.ask_price), static_cast<double>(top.ask_quantity),
             static_cast<double>(book->order_count())};
  }
  return g_top.data();
}

// Layout: [count, (timestamp_ns, locate, resting_side, price, qty)...], newest first.
ITCHBOOK_EXPORT const double* ib_trades(std::uint32_t limit) {
  g_trades.fill(0.0);
  if (!g_session) {
    return g_trades.data();
  }
  std::size_t count = 0;
  g_session->builder().listener().for_each_recent(limit, [&count](const TradeEvent& trade) {
    double* out = g_trades.data() + 1 + count * kTradeFields;
    out[0] = static_cast<double>(trade.timestamp);
    out[1] = static_cast<double>(trade.stock_locate);
    out[2] = trade.resting_side == Side::Buy ? 0.0 : 1.0;
    out[3] = static_cast<double>(trade.price);
    out[4] = static_cast<double>(trade.quantity);
    ++count;
  });
  g_trades[0] = static_cast<double>(count);
  return g_trades.data();
}

// Layout: [processed, total, trades, top_changes, last_timestamp_ns, unknown_orders]
ITCHBOOK_EXPORT const double* ib_stats() {
  g_stats.fill(0.0);
  if (g_session) {
    const BuilderStats& stats = g_session->builder().stats();
    g_stats = {static_cast<double>(g_session->processed()),
               static_cast<double>(g_session->total()),
               static_cast<double>(stats.trades),
               static_cast<double>(stats.top_changes),
               static_cast<double>(g_session->last_timestamp()),
               static_cast<double>(stats.unknown_orders)};
  }
  return g_stats.data();
}
