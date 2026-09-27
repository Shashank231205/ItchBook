// Thin typed wrapper over the WebAssembly C ABI exported by web/wasm_api.cpp.
import createItchBook from './itchbook.js';

const PRICE_SCALE = 10000;

export async function loadEngine() {
  const wasm = await createItchBook();
  const doubles = (ptr, count) => wasm.HEAPF64.slice(ptr >> 3, (ptr >> 3) + count);

  return {
    init: (instruments, events, seed) => wasm._ib_init(instruments, events, seed),
    step: (maxMessages) => wasm._ib_step(maxMessages),
    runToEnd: () => wasm._ib_run_to_end(),
    instrumentCount: () => wasm._ib_instrument_count(),
    symbol: (locate) => wasm.UTF8ToString(wasm._ib_symbol(locate)),

    depth(locate, levels) {
      const ptr = wasm._ib_depth(locate, levels);
      const [bidCount, askCount] = doubles(ptr, 2);
      const read = (offset, count) => {
        const raw = doubles(ptr + 16 + offset * 8, count * 3);
        const out = [];
        for (let i = 0; i < count; i++) {
          out.push({ price: raw[i * 3] / PRICE_SCALE, size: raw[i * 3 + 1], orders: raw[i * 3 + 2] });
        }
        return out;
      };
      return { bids: read(0, bidCount), asks: read(levels * 3, askCount) };
    },

    top(locate) {
      const [bid, bidSize, ask, askSize, orders] = doubles(wasm._ib_top(locate), 5);
      return {
        bid: bidSize > 0 ? bid / PRICE_SCALE : null,
        ask: askSize > 0 ? ask / PRICE_SCALE : null,
        bidSize,
        askSize,
        orders,
      };
    },

    trades(limit) {
      const ptr = wasm._ib_trades(limit);
      const [count] = doubles(ptr, 1);
      const raw = doubles(ptr + 8, count * 5);
      const out = [];
      for (let i = 0; i < count; i++) {
        out.push({
          timestamp: raw[i * 5],
          locate: raw[i * 5 + 1],
          aggressorBuy: raw[i * 5 + 2] === 1,
          price: raw[i * 5 + 3] / PRICE_SCALE,
          size: raw[i * 5 + 4],
        });
      }
      return out;
    },

    stats() {
      const [processed, total, trades, topChanges, timestamp, unknownOrders] = doubles(wasm._ib_stats(), 6);
      return { processed, total, trades, topChanges, timestamp, unknownOrders };
    },
  };
}
