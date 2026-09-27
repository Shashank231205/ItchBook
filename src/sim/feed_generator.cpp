#include "itchbook/sim/feed_generator.hpp"

#include <algorithm>
#include <random>
#include <string>

#include "itchbook/core/symbol.hpp"
#include "itchbook/itch/encoder.hpp"
#include "itchbook/itch/messages.hpp"

namespace itchbook::sim {

namespace {

constexpr Timestamp kMarketOpen = 34'200'000'000'000;  // 09:30:00 in ns since midnight
constexpr std::size_t kAverageFrameBytes = 32;
constexpr Quantity kRoundLot = 100;

struct LiveOrder {
  OrderId id;
  StockLocate locate;
  Side side;
  Quantity shares;
};

class FeedBuilder {
 public:
  explicit FeedBuilder(const FeedConfig& config) : config_(config), rng_(config.seed) {
    const Price floor = config.tick_size * (config.max_levels_from_mid + 1);
    std::uniform_int_distribution<Price> ticks_from_start(0, 400);
    for (std::size_t i = 0; i < config.instruments; ++i) {
      const Price offset = ticks_from_start(rng_) * config.tick_size;
      mids_.push_back(std::max(floor, config.start_price + offset));
      symbols_.push_back(Symbol::from("SYM" + std::to_string(i)));
    }
    encoder_.reserve(config.events * kAverageFrameBytes);
  }

  std::vector<std::byte> build() {
    write_preamble();
    for (std::size_t i = 0; i < config_.events; ++i) {
      now_ += 1 + static_cast<Timestamp>(gap_(rng_));
      step();
    }
    encoder_.write(itch::SystemEvent{{0, 0, now_}, 'C'});
    return encoder_.release();
  }

 private:
  void write_preamble() {
    for (std::size_t i = 0; i < config_.instruments; ++i) {
      const auto locate = static_cast<StockLocate>(i + 1);
      encoder_.write(itch::StockDirectory{header(locate), symbols_[i], 'Q', 'N', kRoundLot});
    }
    encoder_.write(itch::SystemEvent{{0, 0, now_}, 'Q'});
  }

  void step() {
    const double roll = unit_(rng_);
    if (live_.empty() || roll < 0.45) {
      add_order();
      return;
    }
    const std::size_t slot = std::uniform_int_distribution<std::size_t>{0, live_.size() - 1}(rng_);
    if (roll < 0.57) {
      execute(slot);
    } else if (roll < 0.67) {
      cancel(slot);
    } else if (roll < 0.95) {
      remove(slot);
    } else {
      replace(slot);
    }
  }

  [[nodiscard]] itch::Header header(StockLocate locate) const noexcept { return {locate, 0, now_}; }

  // The generator does not match orders, so the mid is fixed per instrument; a drifting mid
  // would leave stale resting orders on the wrong side and produce crossed books.
  [[nodiscard]] Price quote_price(StockLocate locate, Side side) {
    const Price mid = mids_[locate - 1U];
    const std::uint32_t levels = std::min(config_.max_levels_from_mid, 1U + distance_(rng_));
    const Price offset = levels * config_.tick_size;
    return side == Side::Buy ? mid - offset : mid + offset;
  }

  [[nodiscard]] Quantity lot_size() { return kRoundLot * lots_(rng_); }

  void add_order() {
    const auto locate = static_cast<StockLocate>(
        std::uniform_int_distribution<std::size_t>{1, config_.instruments}(rng_));
    const Side side = unit_(rng_) < 0.5 ? Side::Buy : Side::Sell;
    const LiveOrder order{next_id_++, locate, side, lot_size()};
    encoder_.write(itch::AddOrder{header(locate), order.id, side, order.shares,
                                  symbols_[locate - 1U], quote_price(locate, side)});
    live_.push_back(order);
  }

  void execute(std::size_t slot) {
    const LiveOrder& order = live_[slot];
    const Quantity shares = std::min(order.shares, lot_size());
    encoder_.write(itch::OrderExecuted{header(order.locate), order.id, shares, next_match_++});
    consume(slot, shares);
  }

  void cancel(std::size_t slot) {
    const LiveOrder& order = live_[slot];
    if (order.shares <= kRoundLot) {
      remove(slot);
      return;
    }
    const Quantity lots = order.shares / kRoundLot;
    const Quantity shares = kRoundLot * std::uniform_int_distribution<Quantity>{1, lots - 1}(rng_);
    encoder_.write(itch::OrderCancel{header(order.locate), order.id, shares});
    consume(slot, shares);
  }

  void remove(std::size_t slot) {
    const LiveOrder& order = live_[slot];
    encoder_.write(itch::OrderDelete{header(order.locate), order.id});
    erase(slot);
  }

  void replace(std::size_t slot) {
    LiveOrder& order = live_[slot];
    const OrderId new_id = next_id_++;
    const Quantity shares = lot_size();
    encoder_.write(itch::OrderReplace{header(order.locate), order.id, new_id, shares,
                                      quote_price(order.locate, order.side)});
    order.id = new_id;
    order.shares = shares;
  }

  void consume(std::size_t slot, Quantity shares) {
    LiveOrder& order = live_[slot];
    order.shares -= shares;
    if (order.shares == 0) {
      erase(slot);
    }
  }

  void erase(std::size_t slot) {
    live_[slot] = live_.back();
    live_.pop_back();
  }

  const FeedConfig& config_;
  std::mt19937_64 rng_;
  std::uniform_real_distribution<double> unit_{0.0, 1.0};
  std::geometric_distribution<std::uint32_t> distance_{0.35};
  std::uniform_int_distribution<Quantity> lots_{1, 10};
  std::exponential_distribution<double> gap_{1.0 / 2'000.0};

  itch::Encoder encoder_;
  std::vector<Symbol> symbols_;
  std::vector<Price> mids_;
  std::vector<LiveOrder> live_;
  Timestamp now_{kMarketOpen};
  OrderId next_id_{1};
  std::uint64_t next_match_{1};
};

}  // namespace

std::vector<std::byte> generate_feed(const FeedConfig& config) {
  return FeedBuilder{config}.build();
}

}  // namespace itchbook::sim
