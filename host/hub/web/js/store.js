// Global reactive state. Two channels: `state` (inventory, events, UI) and
// `live` (high-rate monitoring samples) so charts do not re-render pages.
import { useEffect, useState } from '/vendor/preact.js';

function channel() {
  const listeners = new Set();
  let scheduled = false;
  return {
    version: 0,
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    emit() {
      if (scheduled) return;
      scheduled = true;
      requestAnimationFrame(() => {
        scheduled = false;
        this.version++;
        listeners.forEach((fn) => fn(this.version));
      });
    },
  };
}

export const changes = channel();
export const liveChanges = channel();

export const state = {
  schema: null,
  devices: {},          // key -> device
  signals: [],          // catalog (DB ids + schema metadata)
  signalsById: new Map(),
  counts: {},           // unacked events by severity
  recent: [],           // latest events (newest first)
  toasts: [],
  connected: false,
  muted: load('muted', false),
  pinned: load('pinned', []), // signal ids shown on the dashboard
};

// Live buffers: signal id -> { t: [ms], v: [] } (bounded).
const LIVE_MAX = 20000;
export const live = new Map();

export function update(patch) {
  Object.assign(state, typeof patch === 'function' ? patch(state) : patch);
  changes.emit();
}

export function pushSamples(ts, values) {
  for (const [id, value] of Object.entries(values)) {
    const key = Number(id);
    let buffer = live.get(key);
    if (!buffer) live.set(key, (buffer = { t: [], v: [] }));
    const n = buffer.t.length;
    if (n && buffer.t[n - 1] >= ts) {
      // Out of order (replayed history): insert in place.
      let i = n;
      while (i > 0 && buffer.t[i - 1] > ts) i--;
      if (buffer.t[i - 1] === ts) { buffer.v[i - 1] = value; continue; }
      buffer.t.splice(i, 0, ts);
      buffer.v.splice(i, 0, value);
    } else {
      buffer.t.push(ts);
      buffer.v.push(value);
    }
    if (buffer.t.length > LIVE_MAX * 1.2) {
      buffer.t.splice(0, buffer.t.length - LIVE_MAX);
      buffer.v.splice(0, buffer.v.length - LIVE_MAX);
    }
  }
  liveChanges.emit();
}

export function lastValue(id) {
  const b = live.get(id);
  return b && b.t.length ? { t: b.t[b.t.length - 1], v: b.v[b.v.length - 1] } : null;
}

export function useStore() {
  const [, force] = useState(0);
  useEffect(() => changes.subscribe(() => force((v) => v + 1)), []);
  return state;
}

export function useLive() {
  const [version, force] = useState(0);
  useEffect(() => liveChanges.subscribe(force), []);
  return version;
}

export function load(key, fallback) {
  try { const v = localStorage.getItem(key); return v === null ? fallback : JSON.parse(v); } catch { return fallback; }
}
export function save(key, value) {
  try { localStorage.setItem(key, JSON.stringify(value)); } catch {}
}
