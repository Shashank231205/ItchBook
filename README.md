# ItchBook

[![CI](https://github.com/Shashank231205/ItchBook/actions/workflows/ci.yml/badge.svg)](https://github.com/Shashank231205/ItchBook/actions/workflows/ci.yml)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

A low-latency **Nasdaq TotalView-ITCH 5.0** feed handler and **L3 limit order book** in modern C++.

ItchBook memory-maps a raw exchange capture, decodes it with zero copies, and rebuilds a
price-time-priority order book for every instrument. Top-of-book changes go to a consumer
thread over a lock-free SPSC queue. The hot path never allocates, locks or makes a virtual
call, and every message is timed with the CPU cycle counter.

## Highlights

- **Zero-copy parsing**: `mmap` plus compile-time message dispatch through C++20 concepts. A
  handler implements only the messages it needs.
- **Cache-conscious order book**: orders in an index-addressed slab with intrusive FIFO queues
  per level, levels in a contiguous vector with the touch at `back()`, and 32-byte orders.
- **Custom open-addressing hash map** with Fibonacci hashing and tombstone-free backward-shift
  deletion. It is up to 3× faster than `std::unordered_map` on lookups and up to 8× faster on
  insert/erase churn (see below).
- **Wait-free SPSC ring buffer** with cache-line-separated indices and cached remote indices.
- **Tail-latency measurement**: `rdtsc` timing and an HdrHistogram-style log-linear histogram,
  reporting p50 to p99.99.
- **Verified correctness**: randomized differential tests against reference implementations,
  ASan, UBSan and TSan in CI, `-Werror` with a strict warning set on GCC and Clang.

## Architecture

```
 ITCH file ──mmap──► Parser<Handler> ──► BookBuilder ──► OrderBook × N   (book thread)
                      Codec<M>::decode       │
                                             └─ on_top_of_book ─► SpscQueue ─► consumer thread
```

```
include/itchbook/
├── core/          Price, OrderId, Side, Symbol
├── itch/          endian helpers, message structs, Codec<M>, Parser, Encoder
├── memory/        IndexPool<T>, OrderIdMap<V>
├── book/          Order/PriceLevel, OrderBook, BookBuilder<Listener>
├── concurrency/   SpscQueue<T, N>
├── perf/          read_tsc, TscClock, LatencyHistogram
├── platform/      CPU pinning, spin-wait hint
├── io/            MappedFile (RAII mmap)
└── sim/           deterministic synthetic ITCH feed generator
src/               out-of-line implementations
apps/              itchbook-replay, itchbook-gen
tests/             GoogleTest suite (47 tests)
benchmarks/        Google Benchmark suite
docs/DESIGN.md     design decisions, complexity, trade-offs
```

See **[docs/DESIGN.md](docs/DESIGN.md)** for the reasoning behind each component.

## Build

Requires Linux, CMake ≥ 3.25, Ninja, and GCC ≥ 13 or Clang ≥ 17. GoogleTest and Google
Benchmark are fetched automatically.

```bash
cmake --preset release          # Release, -march=native, warnings as errors
cmake --build --preset release
ctest --preset release
```

Other presets: `debug`, `asan` (Address + UndefinedBehavior sanitizers) and `tsan`.

## Usage

**Replay a real Nasdaq capture** (a full trading day is about 8 GB uncompressed):

```bash
./scripts/download_sample.sh                     # fetches a sample day into ./data
./build/release/apps/itchbook-replay data/01302019.NASDAQ_ITCH50 \
    --producer-cpu 2 --consumer-cpu 3
```

**Or generate a synthetic feed**, which is deterministic and internally consistent:

```bash
./build/release/apps/itchbook-gen /tmp/feed.itch --events 5000000 --instruments 100
./build/release/apps/itchbook-replay /tmp/feed.itch --top 5
```

Sample output (this run was on a shared 4-vCPU GitHub Actions runner, so numbers are noisy):

```
messages           5000102 (book 5000100 | ignored 2 | malformed 0)
elapsed            0.827 s
throughput         6.04 M msg/s | 180 MiB/s
latency (ns/msg)   p50 117 | p90 209 | p99 366 | p99.9 706 | p99.99 13399 | max 291132
top-of-book        1805813 changes | 1805813 consumed | 0 producer stalls
unknown order refs 0
```

The p99.99 and max figures come from the virtualized runner being preempted. On an isolated,
pinned core (`isolcpus`, performance governor), the tail tightens significantly.

## Benchmarks

GCC 13, `-O3`, on the same GitHub runner (AMD EPYC, 2.45 GHz). Run
`./scripts/run_benchmarks.sh` on your own hardware for pinned, repeated measurements.

| Benchmark | Result |
|---|---|
| Order book add + remove, 10k resting orders | **63 ns** per pair (~32 M ops/s) |
| Top of book query | **2.2 ns** |
| `OrderIdMap` random lookup, 1M keys | **10.6 ns** (vs 34.3 ns `std::unordered_map`, **3.2×**) |
| `OrderIdMap` insert + erase, 4k keys | **272 M ops/s** (vs 35 M/s `std::unordered_map`, **7.8×**) |
| ITCH decode only | **99 M msg/s**, 2.9 GiB/s |
| Decode + full book rebuild | **11.6 M msg/s** |
| SPSC queue, cross-thread | **193 M items/s** |

## Testing

| Area | Approach |
|---|---|
| Parser / codec | Round trip for every message type; truncated, malformed and unknown frames |
| Order book | Rule-by-rule unit tests plus 50k random operations diffed against a reference book after every step |
| Hash map | 200k random operations diffed against `std::unordered_map` |
| SPSC queue | Edge cases plus a 2M-item two-thread ordering test under ThreadSanitizer |
| End to end | Generated feed → replay with zero unknown refs, zero rejects and no crossed books |

CI (GitHub Actions) runs GCC 13 and Clang 18 in Debug and Release, ASan with UBSan, TSan,
clang-format, an end-to-end replay and a benchmark smoke run on every push.

## License

[MIT](LICENSE)
