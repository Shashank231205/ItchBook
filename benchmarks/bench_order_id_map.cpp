#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "itchbook/memory/order_id_map.hpp"

namespace {

using itchbook::OrderId;

// ITCH order ids are increasing with gaps (ids are shared across all instruments).
std::vector<OrderId> make_keys(std::size_t count) {
  std::mt19937_64 rng(1);
  std::uniform_int_distribution<OrderId> gap(1, 16);
  std::vector<OrderId> keys(count);
  OrderId next = 1'000'000;
  for (OrderId& key : keys) {
    next += gap(rng);
    key = next;
  }
  return keys;
}

struct FlatMap {
  itchbook::OrderIdMap<std::uint32_t> map;
  explicit FlatMap(std::size_t n) : map(n) {}
  bool insert(OrderId k, std::uint32_t v) { return map.insert(k, v); }
  const std::uint32_t* find(OrderId k) const { return map.find(k); }
  bool erase(OrderId k) { return map.erase(k); }
};

struct StdMap {
  std::unordered_map<OrderId, std::uint32_t> map;
  explicit StdMap(std::size_t n) { map.reserve(n); }
  bool insert(OrderId k, std::uint32_t v) { return map.emplace(k, v).second; }
  const std::uint32_t* find(OrderId k) const {
    const auto it = map.find(k);
    return it == map.end() ? nullptr : &it->second;
  }
  bool erase(OrderId k) { return map.erase(k) == 1; }
};

template <typename Map>
void BM_InsertErase(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  const auto keys = make_keys(n);
  for ([[maybe_unused]] auto _ : state) {
    Map map(n);
    for (std::size_t i = 0; i < n; ++i) {
      map.insert(keys[i], static_cast<std::uint32_t>(i));
    }
    for (const OrderId key : keys) {
      bool erased = map.erase(key);
      benchmark::DoNotOptimize(erased);
    }
  }
  state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(2 * n));
}

template <typename Map>
void BM_RandomLookup(benchmark::State& state) {
  const auto n = static_cast<std::size_t>(state.range(0));
  const auto keys = make_keys(n);
  Map map(n);
  for (std::size_t i = 0; i < n; ++i) {
    map.insert(keys[i], static_cast<std::uint32_t>(i));
  }
  std::vector<OrderId> probes = keys;
  std::shuffle(probes.begin(), probes.end(), std::mt19937_64(2));

  std::size_t i = 0;
  for ([[maybe_unused]] auto _ : state) {
    const std::uint32_t* value = map.find(probes[i]);
    benchmark::DoNotOptimize(value);
    i = i + 1 == n ? 0 : i + 1;
  }
  state.SetItemsProcessed(state.iterations());
}

}  // namespace

BENCHMARK(BM_InsertErase<FlatMap>)->Name("OrderIdMap/InsertErase")->Range(1 << 10, 1 << 20);
BENCHMARK(BM_InsertErase<StdMap>)->Name("unordered_map/InsertErase")->Range(1 << 10, 1 << 20);
BENCHMARK(BM_RandomLookup<FlatMap>)->Name("OrderIdMap/RandomLookup")->Range(1 << 10, 1 << 20);
BENCHMARK(BM_RandomLookup<StdMap>)->Name("unordered_map/RandomLookup")->Range(1 << 10, 1 << 20);
