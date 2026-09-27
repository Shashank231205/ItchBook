#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "itchbook/book/book_builder.hpp"
#include "itchbook/concurrency/spsc_queue.hpp"
#include "itchbook/itch/parser.hpp"
#include "itchbook/platform/cpu.hpp"
#include "itchbook/sim/feed_generator.hpp"

namespace {

using namespace itchbook;

const std::vector<std::byte>& feed() {
  static const auto bytes = sim::generate_feed({.instruments = 16, .events = 2'000'000, .seed = 9});
  return bytes;
}

struct DecodeOnlyHandler {
  template <typename Message>
  void on(const Message& message) {
    const Message* ptr = &message;
    benchmark::DoNotOptimize(ptr);
  }
};

void BM_ParseOnly(benchmark::State& state) {
  const auto& bytes = feed();
  std::uint64_t messages = 0;
  for ([[maybe_unused]] auto _ : state) {
    DecodeOnlyHandler handler;
    itch::Parser parser(handler);
    parser.parse(bytes);
    messages += parser.stats().messages;
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(messages));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes.size()));
}

void BM_ParseAndBuildBooks(benchmark::State& state) {
  const auto& bytes = feed();
  std::uint64_t messages = 0;
  for ([[maybe_unused]] auto _ : state) {
    state.PauseTiming();
    auto builder = std::make_unique<BookBuilder<>>();
    state.ResumeTiming();
    itch::Parser parser(*builder);
    parser.parse(bytes);
    messages += parser.stats().messages;
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(messages));
  state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(bytes.size()));
}

void BM_SpscQueueThroughput(benchmark::State& state) {
  constexpr std::uint64_t kItems = 1 << 22;
  auto queue = std::make_unique<SpscQueue<std::uint64_t, 1 << 12>>();
  for ([[maybe_unused]] auto _ : state) {
    std::thread consumer([&queue] {
      std::uint64_t received = 0;
      std::uint64_t value = 0;
      while (received < kItems) {
        if (queue->try_pop(value)) {
          ++received;
        } else {
          cpu_relax();
        }
      }
    });
    for (std::uint64_t i = 0; i < kItems; ++i) {
      while (!queue->try_push(i)) {
        cpu_relax();
      }
    }
    consumer.join();
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kItems));
}

}  // namespace

BENCHMARK(BM_ParseOnly)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_ParseAndBuildBooks)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SpscQueueThroughput)->Unit(benchmark::kMillisecond)->UseRealTime();
