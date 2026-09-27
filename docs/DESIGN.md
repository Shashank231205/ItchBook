# ItchBook design

This document explains how ItchBook is put together and why each decision was made. The goal
throughout is the one a production feed handler has: rebuild every order book from the exchange
feed exactly, with the lowest and most predictable per-message latency, and never allocate or
block on the hot path.

## 1. Data flow

```
             mmap (zero copy)
 ITCH file ─────────────────► for_each_frame ──► Parser<Handler> ──► BookBuilder<Listener>
                              (length-prefixed    (switch on type,     │  one OrderBook per
                               frames)             Codec<M>::decode)   │  stock locate
                                                                        │
                                                   on_top_of_book()     ▼
                               consumer thread ◄── SpscQueue<TopUpdate> ◄── QueueingListener
                               (stats, strategy)   (lock-free, bounded)
```

The book thread does parse → decode → book update → publish, one message at a time. Anything
that is not needed to keep the book correct (statistics, strategy logic, logging) runs on another
thread, fed by a single-producer/single-consumer queue.

## 2. Module layout

| Module | Responsibility | Depends on |
|---|---|---|
| `core/` | Domain types (`Price`, `OrderId`, `Side`, `Symbol`) | — |
| `itch/` | Wire format: endian helpers, message structs, `Codec<M>`, `Parser`, `Encoder` | core |
| `memory/` | `IndexPool<T>`, `OrderIdMap<V>` | core |
| `book/` | `OrderBook` (one instrument), `BookBuilder` (ITCH → books) | core, memory, itch messages |
| `concurrency/` | `SpscQueue<T, N>` | core |
| `perf/` | `read_tsc`, `TscClock`, `LatencyHistogram` | — |
| `platform/` | CPU pinning, spin-wait hint | — |
| `io/` | `MappedFile` (RAII `mmap`) | — |
| `sim/` | Deterministic synthetic ITCH generator | itch |

Dependencies only point downwards: `book/` does not know about files or threads, and `itch/` does
not know about books. Every layer can be tested and benchmarked on its own.

## 3. Wire decoding

* **`Codec<M>` is the single source of truth** for each message's type byte, size and field
  offsets. The decoder and the encoder used by tests and the generator are side by side, so they
  cannot drift apart. Round-trip tests cover every supported message type.
* **Fields are read with `memcpy` + `bswap`**, not packed structs and `reinterpret_cast`. That
  avoids unaligned-access and strict-aliasing undefined behaviour, and compilers turn it into a
  single `movbe` / `bswap` instruction.
* **Dispatch is resolved at compile time.** `Parser<Handler>` checks with a C++20 concept whether
  the handler has `on(const M&)` for each message type. A handler implements only what it needs;
  everything else is skipped with no virtual call and no branch on a function pointer.
* **Framing is robust.** Each frame's length prefix is checked, messages shorter than their
  type's size are counted as `malformed` instead of being read out of bounds, and unknown types
  are skipped. `parse()` returns the number of bytes consumed, so a streaming reader (for
  example a UDP/MoldUDP64 receiver) can carry a partial frame into its next read.

## 4. Order book

### Data structures

```
OrderIdMap<OrderIndex>          IndexPool<Order>                 bids_ (worst → best)
 order id ─► 32-bit index ───►  [ id | price | qty | side |      [ 99.97 | 99.98 | 99.99 ] ◄ back() = best bid
                                  prev | next ] ...                         │
                                                                   head ─► o1 ⇄ o2 ⇄ o3 ◄─ tail (FIFO)
```

* **Orders live in an `IndexPool`**: a contiguous slab addressed by `uint32_t` indices, with a
  LIFO free list. After warm-up the pool never allocates, and recently freed (cache-hot) slots are
  reused first. Indices stay valid when the slab grows, unlike pointers, and are half the size of
  a pointer, so `Order` fits in 32 bytes.
* **Each price level is an intrusive doubly linked FIFO** threaded through `Order::prev/next`.
  Cancelling from the middle of the queue is O(1), and walking a level visits orders in time
  priority, which is what queue-position models need.
* **Levels are a sorted `std::vector` per side, stored worst → best**, so the touch is always
  `back()`. Almost all activity happens within a few ticks of the touch, so inserting or erasing
  a level is a short `memmove` at the end of the vector. It also keeps the hot levels in one or two
  cache lines. A tree (`std::map`) would do a pointer-chasing heap allocation for every new level.
* **`OrderIdMap`** is an open-addressing table with linear probing, Fibonacci hashing (ITCH ids
  are nearly sequential, so a multiplicative hash spreads them well) and a maximum load factor of
  0.5. Deletion uses **backward shift** instead of tombstones. The ITCH stream erases about as
  many orders as it adds, and tombstones would keep lengthening probe chains until the next
  rehash.

### Complexity

| Operation | Cost |
|---|---|
| add (existing level) | O(1) hash insert + O(log L) level search + O(1) link |
| add (new level near touch) | + O(k) `memmove`, where k is the distance from the touch |
| execute / cancel (partial) | O(1) hash lookup + O(log L) level search |
| delete / full fill | O(1) unlink + level erase as above |
| best bid / ask | O(1) |

`L` is the number of price levels on one side, typically tens.

### Semantics

* Executions and cancels larger than the resting quantity count as a full fill, matching exchange
  behaviour.
* `replace` (ITCH `U`) keeps the original side, gives the order a new id, and **loses time
  priority**, exactly as Nasdaq specifies.
* Every mutator returns `false` and leaves the book unchanged for unknown or duplicate ids. The
  builder counts these instead of crashing, which matters when you start replaying mid-session.

## 5. Publishing top of book

`BookBuilder` caches the last `TopOfBook` for each instrument and notifies its `Listener` only when
the touch actually changes. Most ITCH messages change depth away from the touch, so this filters
out most of the downstream traffic. Listener hooks (`on_top_of_book`, `on_trade`) are optional and
detected at compile time. With `NullListener` all publishing code is compiled out.

## 6. SPSC queue

`SpscQueue<T, N>` is a bounded, wait-free ring buffer:

* The producer index (`tail`) and consumer index (`head`) sit on **separate 64-byte cache lines**
  to avoid false sharing.
* Each side keeps a **cached copy of the other side's index** and reloads the shared atomic only
  when the cache says the queue is full (producer) or empty (consumer). In steady state this
  removes most cross-core cache-line transfers.
* Memory ordering is minimal: `relaxed` loads of its own index, `acquire` loads of the other
  side's index, and a `release` store to publish. ThreadSanitizer runs in CI to check this.
* The capacity must be a power of two, so wrap-around is a mask, not a modulo.

The replay tool's producer spins instead of dropping updates when the queue is full, and counts
each spin as a *stall*, so back-pressure shows up in the report.

## 7. Measurement

* `read_tsc()` reads the cycle counter in a few tens of cycles, several times cheaper than
  `clock_gettime`. `TscClock` calibrates ticks to nanoseconds against `steady_clock`.
* `LatencyHistogram` is a fixed-size log-linear histogram in the style of HdrHistogram: 16 linear
  sub-buckets per power of two, so relative error is at most 6.25% over the full `uint64` range.
  `record()` does no allocation and no division.
* The replay tool timestamps every message around `dispatch()` and reports p50 to p99.99 and max,
  not an average. Tail latency is what matters.

## 8. Testing strategy

| Layer | Technique |
|---|---|
| Codec / parser | Encode → parse round trip for every message type; truncated, malformed and unknown frames |
| `OrderIdMap` | 200k random operations checked against `std::unordered_map` in a deliberately small table (long probe chains, wrap-around) |
| `OrderBook` | Unit tests for every rule, plus 50k random add/reduce/remove/replace operations checked against a naive `std::map` reference book, comparing full depth after every step |
| `BookBuilder` | ITCH message sequences → book state, published top-of-book events and trades |
| `SpscQueue` | Wrap-around and full/empty edge cases; a 2M-item two-thread ordering test under TSan |
| Generator | Determinism; a generated feed replays with zero unknown refs and zero rejects |

CI runs on every push: GCC 13 and Clang 18 in Debug and Release with `-Werror` and a strict
warning set (`-Wconversion -Wsign-conversion -Wold-style-cast -Wshadow ...`), AddressSanitizer
with UndefinedBehaviorSanitizer, ThreadSanitizer, a clang-format check, an end-to-end replay of a
5M-event feed, and a smoke run of the benchmarks.

## 9. Trade-offs and next steps

* **One order-id map per instrument** keeps each `OrderBook` self-contained and testable. A single
  global map would save the per-message locate lookup, because ITCH ids are globally unique. That
  is worth measuring on a full-day capture.
* **Price-indexed level arrays** (a flat array indexed by `(price - base) / tick`) would make every
  level lookup O(1), at the cost of memory and handling of re-centring. That fits liquid names
  better than the whole universe.
* **Live feed.** The parser already consumes partial buffers. The natural next step is a
  MoldUDP64 receiver with sequence-gap detection and a retransmission request path, reading from
  a kernel-bypass (AF_XDP) socket.
* **Hugepages** (`MAP_HUGETLB` / `madvise(MADV_HUGEPAGE)`) for the order pool and the mapped
  capture, to reduce TLB misses.
