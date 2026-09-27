import { loadEngine } from './engine.js';

const INSTRUMENTS = 24;
const EVENTS = 1_000_000;
const DEPTH = 12;
const TAPE = 40;
const SIDE_PANELS_INTERVAL_MS = 150;

const $ = (id) => document.getElementById(id);
const fmtInt = new Intl.NumberFormat('en-US');
const fmtPrice = (p) => (p == null ? '—' : p.toFixed(2));

const state = {
  engine: null,
  playing: false,
  speed: 100,
  selected: 1,
  seed: 42,
  engineMs: 0,
  engineMessages: 0,
  lastSidePanels: 0,
};

function marketTime(ns) {
  if (!ns) return '—';
  const totalMs = Math.floor(ns / 1e6);
  const h = Math.floor(totalMs / 3_600_000);
  const m = Math.floor((totalMs % 3_600_000) / 60_000);
  const s = Math.floor((totalMs % 60_000) / 1000);
  const ms = totalMs % 1000;
  const pad = (v, n = 2) => String(v).padStart(n, '0');
  return `${pad(h)}:${pad(m)}:${pad(s)}.${pad(ms, 3)}`;
}

function setStatus(text) {
  $('status').textContent = text;
}

function setPlaying(playing) {
  state.playing = playing;
  $('play').textContent = playing ? '❚❚ Pause' : '▶ Play';
}

function setControlsEnabled(enabled) {
  for (const id of ['play', 'step', 'reset', 'bench']) $(id).disabled = !enabled;
}

function newFeed(seed) {
  setPlaying(false);
  setControlsEnabled(false);
  setStatus(`Generating ${fmtInt.format(EVENTS)}-event ITCH feed in WebAssembly…`);
  // Yield so the status text paints before the synchronous generation starts.
  setTimeout(() => {
    const t0 = performance.now();
    const total = state.engine.init(INSTRUMENTS, EVENTS, seed);
    const ms = performance.now() - t0;
    state.seed = seed;
    state.engineMs = 0;
    state.engineMessages = 0;
    $('bench-result').hidden = true;
    setStatus(`Feed ready: ${fmtInt.format(total)} messages generated in ${ms.toFixed(0)} ms (seed ${seed})`);
    setControlsEnabled(true);
    state.engine.step(2000);
    renderAll(true);
  }, 30);
}

function advance(count) {
  const t0 = performance.now();
  const processed = state.engine.step(count);
  state.engineMs += performance.now() - t0;
  state.engineMessages += processed;
  if (processed === 0) {
    setPlaying(false);
    setStatus('End of feed. Press "New feed" to replay a fresh one.');
  }
}

function renderStats() {
  const s = state.engine.stats();
  $('clock').textContent = marketTime(s.timestamp);
  $('messages').textContent = `${fmtInt.format(s.processed)} / ${fmtInt.format(s.total)}`;
  $('progress').style.width = `${s.total ? (100 * s.processed) / s.total : 0}%`;
  $('trades').textContent = fmtInt.format(s.trades);
  $('bbo').textContent = fmtInt.format(s.topChanges);
  $('latency').textContent =
    state.engineMessages > 0 ? `${((state.engineMs * 1e6) / state.engineMessages).toFixed(0)} ns/msg` : '—';
}

function ladderRows(levels, maxSize, reverse) {
  const rows = levels.map(
    (l) => `<div class="row">
      <div class="bar" style="width:${((100 * l.size) / maxSize).toFixed(1)}%"></div>
      <span>${l.orders}</span><span>${fmtInt.format(l.size)}</span><span class="price">${fmtPrice(l.price)}</span>
    </div>`,
  );
  const filler = '<div class="row"></div>'.repeat(Math.max(0, DEPTH - levels.length));
  return reverse ? filler + rows.reverse().join('') : rows.join('') + filler;
}

function renderLadder() {
  const { bids, asks } = state.engine.depth(state.selected, DEPTH);
  const maxSize = Math.max(1, ...bids.map((l) => l.size), ...asks.map((l) => l.size));
  $('asks').innerHTML = ladderRows(asks, maxSize, true);
  $('bids').innerHTML = ladderRows(bids, maxSize, false);
  $('ladder-symbol').textContent = state.engine.symbol(state.selected);

  if (bids.length && asks.length) {
    const spread = asks[0].price - bids[0].price;
    const mid = (asks[0].price + bids[0].price) / 2;
    $('spread').textContent = `spread ${spread.toFixed(2)}  ·  mid ${mid.toFixed(3)}  ·  ${((spread / mid) * 1e4).toFixed(1)} bps`;
  } else {
    $('spread').textContent = 'one-sided book';
  }
}

function renderSymbols() {
  const items = [];
  const count = state.engine.instrumentCount();
  for (let locate = 1; locate <= count; locate++) {
    const t = state.engine.top(locate);
    items.push(`<li data-locate="${locate}" class="${locate === state.selected ? 'active' : ''}">
      <span class="sym">${state.engine.symbol(locate)}</span>
      <span class="bid-text">${fmtPrice(t.bid)}</span>
      <span class="ask-text">${fmtPrice(t.ask)}</span>
      <span>${t.orders}</span>
    </li>`);
  }
  $('symbols').innerHTML = items.join('');
}

function renderTape() {
  $('tape').innerHTML = state.engine
    .trades(TAPE)
    .map(
      (t) => `<li>
        <span>${marketTime(t.timestamp).slice(3)}</span>
        <span>${state.engine.symbol(t.locate)}</span>
        <span class="${t.aggressorBuy ? 'bid-text' : 'ask-text'}">${fmtPrice(t.price)}</span>
        <span>${fmtInt.format(t.size)}</span>
      </li>`,
    )
    .join('');
}

function renderAll(force = false) {
  renderStats();
  renderLadder();
  const now = performance.now();
  if (force || now - state.lastSidePanels > SIDE_PANELS_INTERVAL_MS) {
    state.lastSidePanels = now;
    renderSymbols();
    renderTape();
  }
}

function frame() {
  if (state.playing) {
    advance(state.speed);
    renderAll();
  }
  requestAnimationFrame(frame);
}

function runBenchmark() {
  setPlaying(false);
  setControlsEnabled(false);
  setStatus('Benchmarking: replaying the full feed with rendering disabled…');
  setTimeout(() => {
    const total = state.engine.init(INSTRUMENTS, EVENTS, state.seed);
    const ms = state.engine.runToEnd();
    const s = state.engine.stats();
    const rate = total / (ms / 1000) / 1e6;
    const result = $('bench-result');
    result.hidden = false;
    result.innerHTML = `⚡ Replayed <b>${fmtInt.format(total)}</b> ITCH messages and rebuilt ${INSTRUMENTS} order books in
      <b>${ms.toFixed(1)} ms</b>, i.e. <b>${rate.toFixed(2)} M msg/s</b> (${((ms * 1e6) / total).toFixed(0)} ns/msg) inside your browser.
      ${fmtInt.format(s.trades)} trades, ${fmtInt.format(s.topChanges)} top-of-book changes, ${s.unknownOrders} unknown order refs.`;
    setStatus('Benchmark complete. Press "New feed" to watch a replay.');
    setControlsEnabled(true);
    renderAll(true);
  }, 30);
}

function wireControls() {
  $('play').addEventListener('click', () => setPlaying(!state.playing));
  $('step').addEventListener('click', () => {
    setPlaying(false);
    advance(1);
    renderAll(true);
  });
  $('speed').addEventListener('change', (e) => {
    state.speed = Number(e.target.value);
  });
  $('reset').addEventListener('click', () => newFeed(Math.floor(Math.random() * 1e9)));
  $('bench').addEventListener('click', runBenchmark);
  $('symbols').addEventListener('click', (e) => {
    const item = e.target.closest('li[data-locate]');
    if (!item) return;
    state.selected = Number(item.dataset.locate);
    renderAll(true);
  });
}

async function main() {
  try {
    state.engine = await loadEngine();
  } catch (err) {
    setStatus(`Failed to load WebAssembly engine: ${err}`);
    return;
  }
  wireControls();
  newFeed(state.seed);
  requestAnimationFrame(frame);
}

main();
