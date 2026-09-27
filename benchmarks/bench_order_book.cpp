#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "itchbook/book/order_book.hpp"

namespace {

using namespace itchbook;

constexpr Price kMid = 100 * kPriceScale;
constexpr Price kTick = kPriceScale / 100;

struct Op {
  OrderId id;
  Side side;
  Price price;
};

// Pre-generated so the timed loop measures only the book, not the RNG.
std::vector<Op> make_ops(std::size_t count, std::uint32_t max_levels) {
  std::mt19937_64 rng(5);
  std::geometric_distribution<std::uint32_t> distance(0.3);
  std::vector<Op> ops;
  ops.reserve(count);
  for (OrderId id = 1; id <= count; ++id) {
    const Side side = (id & 1U) != 0 ? Side::Buy : Side::Sell;
    const Price offset = (1U + std::min(distance(rng), max_levels)) * kTick;
    ops.push_back({id, side, side == Side::Buy ? kMid - offset : kMid + offset});
  }
  return ops;
}

// Steady state: a book with `resting` live orders where every iteration adds one order near the
// touch and removes the oldest, the dominant pattern in real ITCH traffic.
void BM_AddRemoveSteadyState(benchmark::State& state) {
  const auto resting = static_cast<std::size_t>(state.range(0));
  const auto ops = make_ops(1 << 20, 50);
  OrderBook book(resting * 2);
  for (std::size_t i = 0; i < resting; ++i) {
    book.add(ops[i].id, ops[i].side, ops[i].price, 100);
  }

  std::size_t head = 0;
  std::size_t tail = resting;
  for ([[maybe_unused]] auto _ : state) {
    const Op& add = ops[tail];
    book.add(add.id, add.side, add.price, 100);
    book.remove(ops[head].id);
    if (++tail == ops.size()) {
      state.PauseTiming();
      book.clear();
      for (std::size_t i = 0; i < resting; ++i) {
        book.add(ops[i].id, ops[i].side, ops[i].price, 100);
      }
      head = 0;
      tail = resting;
      state.ResumeTiming();
    } else {
      ++head;
    }
  }
  state.SetItemsProcessed(state.iterations() * 2);
}

void BM_TopOfBook(benchmark::State& state) {
  OrderBook book;
  for (const Op& op : make_ops(10'000, 50)) {
    book.add(op.id, op.side, op.price, 100);
  }
  for ([[maybe_unused]] auto _ : state) {
    TopOfBook top = book.top();
    benchmark::DoNotOptimize(top);
  }
}

}  // namespace

BENCHMARK(BM_AddRemoveSteadyState)->Arg(1'000)->Arg(10'000)->Arg(100'000);
BENCHMARK(BM_TopOfBook);
