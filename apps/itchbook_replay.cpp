#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli.hpp"
#include "itchbook/book/book_builder.hpp"
#include "itchbook/concurrency/spsc_queue.hpp"
#include "itchbook/io/mapped_file.hpp"
#include "itchbook/itch/parser.hpp"
#include "itchbook/perf/latency_histogram.hpp"
#include "itchbook/perf/tsc_clock.hpp"
#include "itchbook/platform/cpu.hpp"

namespace {

using namespace itchbook;

constexpr std::string_view kUsage = R"(usage: itchbook-replay <capture.itch> [options]

Replays a Nasdaq TotalView-ITCH 5.0 capture through the book builder on one thread and streams
top-of-book updates over a lock-free SPSC queue to a consumer thread.

options:
  --producer-cpu N   pin the parser/book thread to CPU N
  --consumer-cpu N   pin the consumer thread to CPU N
  --top N            number of most active symbols to print   (default 10)
  --no-latency       skip per-message latency sampling (pure throughput)
)";

struct Options {
  std::string path;
  std::optional<unsigned> producer_cpu;
  std::optional<unsigned> consumer_cpu;
  std::size_t top_symbols = 10;
  bool measure_latency = true;
};

struct TopUpdate {
  StockLocate stock_locate{};
  Timestamp timestamp{};
  TopOfBook top{};
};

using UpdateQueue = SpscQueue<TopUpdate, std::size_t{1} << 16>;

// Runs on the book thread. Never drops an update: if the consumer falls behind, the producer
// spins and the stall is counted so back-pressure is visible in the report.
class QueueingListener {
 public:
  explicit QueueingListener(UpdateQueue& queue) noexcept : queue_(&queue) {}

  void on_top_of_book(StockLocate locate, Timestamp timestamp, const TopOfBook& top) noexcept {
    const TopUpdate update{locate, timestamp, top};
    while (!queue_->try_push(update)) {
      ++stalls_;
      cpu_relax();
    }
  }

  [[nodiscard]] std::uint64_t stalls() const noexcept { return stalls_; }

 private:
  UpdateQueue* queue_;
  std::uint64_t stalls_{};
};

struct ConsumerReport {
  std::uint64_t updates{};
  std::uint64_t two_sided{};
  std::uint64_t locked_or_crossed{};
  double spread_bps_sum{};
  std::vector<std::uint64_t> updates_per_instrument = std::vector<std::uint64_t>(kMaxInstruments);

  void record(const TopUpdate& update) {
    ++updates;
    ++updates_per_instrument[update.stock_locate];
    const TopOfBook& top = update.top;
    if (!top.has_bid() || !top.has_ask()) {
      return;
    }
    ++two_sided;
    if (top.bid_price >= top.ask_price) {
      ++locked_or_crossed;
      return;
    }
    const double bid = to_double(top.bid_price);
    const double ask = to_double(top.ask_price);
    spread_bps_sum += (ask - bid) / ((ask + bid) / 2.0) * 10'000.0;
  }
};

void consume(UpdateQueue& queue, const std::atomic<bool>& done, ConsumerReport& report) {
  TopUpdate update;
  for (;;) {
    if (queue.try_pop(update)) {
      report.record(update);
    } else if (done.load(std::memory_order_acquire)) {
      break;
    } else {
      cpu_relax();
    }
  }
  // The producer's last push happens-before done is set, so this drain sees everything.
  while (queue.try_pop(update)) {
    report.record(update);
  }
}

std::optional<Options> parse_options(const cli::Args& args) {
  const auto positional = args.positional();
  if (positional.size() != 1 || args.flag("--help")) {
    return std::nullopt;
  }
  Options options;
  options.path = std::string(positional[0]);
  options.producer_cpu = args.option<unsigned>("--producer-cpu");
  options.consumer_cpu = args.option<unsigned>("--consumer-cpu");
  options.top_symbols = args.option<std::size_t>("--top").value_or(options.top_symbols);
  options.measure_latency = !args.flag("--no-latency");
  return options;
}

void print_latency(const LatencyHistogram& ticks, const TscClock& clock) {
  const auto ns = [&clock](std::uint64_t t) { return clock.to_ns(t); };
  cli::print("latency (ns/msg)   p50 {} | p90 {} | p99 {} | p99.9 {} | p99.99 {} | max {}\n",
             ns(ticks.percentile(50)), ns(ticks.percentile(90)), ns(ticks.percentile(99)),
             ns(ticks.percentile(99.9)), ns(ticks.percentile(99.99)), ns(ticks.max()));
}

template <typename Builder>
void print_most_active(const Builder& builder, const ConsumerReport& report, std::size_t limit) {
  std::vector<StockLocate> locates;
  for (std::size_t i = 0; i < kMaxInstruments; ++i) {
    if (report.updates_per_instrument[i] != 0) {
      locates.push_back(static_cast<StockLocate>(i));
    }
  }
  const std::size_t count = std::min(limit, locates.size());
  std::partial_sort(locates.begin(), locates.begin() + static_cast<std::ptrdiff_t>(count),
                    locates.end(), [&report](StockLocate a, StockLocate b) {
                      return report.updates_per_instrument[a] > report.updates_per_instrument[b];
                    });

  cli::print("\n{:<8} {:>12} {:>8} {:>12} {:>12} {:>8}\n", "symbol", "bbo updates", "orders", "bid",
             "ask", "levels");
  for (std::size_t i = 0; i < count; ++i) {
    const StockLocate locate = locates[i];
    const OrderBook* book = builder.book(locate);
    const TopOfBook top = book->top();
    cli::print("{:<8} {:>12} {:>8} {:>12} {:>12} {:>8}\n", builder.symbol(locate).view(),
               report.updates_per_instrument[locate], book->order_count(),
               top.has_bid() ? std::format("{:.4f}", to_double(top.bid_price)) : "-",
               top.has_ask() ? std::format("{:.4f}", to_double(top.ask_price)) : "-",
               book->level_count(Side::Buy) + book->level_count(Side::Sell));
  }
}

int run(const Options& options) {
  const MappedFile file(options.path);
  const std::span<const std::byte> bytes = file.bytes();

  auto queue = std::make_unique<UpdateQueue>();
  std::atomic<bool> done{false};
  ConsumerReport report;
  std::thread consumer([&] {
    if (options.consumer_cpu && !pin_current_thread(*options.consumer_cpu)) {
      std::fprintf(stderr, "warning: could not pin consumer thread\n");
    }
    consume(*queue, done, report);
  });

  if (options.producer_cpu && !pin_current_thread(*options.producer_cpu)) {
    std::fprintf(stderr, "warning: could not pin producer thread\n");
  }

  const TscClock clock = TscClock::calibrate();
  BookBuilder<QueueingListener> builder{QueueingListener{*queue}};
  itch::Parser parser(builder);
  LatencyHistogram ticks;

  const auto start = std::chrono::steady_clock::now();
  std::size_t consumed = 0;
  if (options.measure_latency) {
    consumed = itch::for_each_frame(bytes, [&](std::span<const std::byte> message) {
      const std::uint64_t begin = read_tsc();
      parser.dispatch(message);
      ticks.record(read_tsc() - begin);
    });
  } else {
    consumed = parser.parse(bytes);
  }
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;

  done.store(true, std::memory_order_release);
  consumer.join();

  const itch::ParseStats& parsed = parser.stats();
  const BuilderStats& built = builder.stats();
  const double seconds = elapsed.count();

  cli::print("file               {} ({:.1f} MiB)\n", options.path,
             static_cast<double>(bytes.size()) / (1024.0 * 1024.0));
  cli::print("messages           {} (book {} | ignored {} | malformed {})\n", parsed.messages,
             parsed.dispatched, parsed.ignored, parsed.malformed);
  cli::print("elapsed            {:.3f} s\n", seconds);
  cli::print("throughput         {:.2f} M msg/s | {:.0f} MiB/s\n",
             static_cast<double>(parsed.messages) / seconds / 1e6,
             static_cast<double>(consumed) / seconds / (1024.0 * 1024.0));
  if (options.measure_latency) {
    print_latency(ticks, clock);
  }
  cli::print("top-of-book        {} changes | {} consumed | {} producer stalls\n",
             built.top_changes, report.updates, builder.listener().stalls());
  cli::print("trades             {}\n", built.trades);
  cli::print("unknown order refs {}\n", built.unknown_orders);
  const std::uint64_t quoted = report.two_sided - report.locked_or_crossed;
  if (quoted > 0) {
    cli::print("avg quoted spread  {:.2f} bps over {} two-sided updates ({} locked/crossed)\n",
               report.spread_bps_sum / static_cast<double>(quoted), report.two_sided,
               report.locked_or_crossed);
  }
  if (consumed != bytes.size()) {
    cli::print("warning: {} trailing bytes did not form a complete message\n",
               bytes.size() - consumed);
  }
  print_most_active(builder, report, options.top_symbols);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::optional<Options> options = parse_options(cli::Args(argc, argv));
    if (!options) {
      std::fputs(kUsage.data(), stderr);
      return 2;
    }
    return run(*options);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "itchbook-replay: %s\n", e.what());
    return 1;
  }
}
