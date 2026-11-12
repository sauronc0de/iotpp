// Time-series charts: stored history (server-side downsampled) merged with
// the live SSE stream. uPlot keeps rendering cheap even with many points.
import { html, useEffect, useRef, useState } from '/vendor/preact.js';
import uPlot from '/vendor/uPlot.js';
import { live, liveChanges, useLive } from '../store.js';
import { history } from '../api.js';
import { enumLabel, formatSignal, colorFor } from '../schema.js';

const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
// Axis labels with just enough decimals for the tick spacing.
const axisValues = (u, splits) => {
  const step = splits.length > 1 ? Math.abs(splits[1] - splits[0]) : 1;
  const max = Math.max(...splits.map(Math.abs));
  if (max >= 1e6) return splits.map((v) => (v / 1e6).toFixed(step >= 1e6 ? 0 : 1) + 'M');
  if (max >= 1e4) return splits.map((v) => (v / 1e3).toFixed(step >= 1e3 ? 0 : 1) + 'k');
  const digits = step >= 1 ? 0 : Math.min(6, Math.ceil(-Math.log10(step) - 1e-9));
  return splits.map((v) => v.toFixed(digits));
};

export const RANGES = [
  ['5m', 'Live 5m', 5 * 60e3], ['15m', '15m', 15 * 60e3], ['1h', '1h', 3600e3], ['6h', '6h', 6 * 3600e3],
  ['24h', '24h', 86400e3], ['7d', '7d', 7 * 86400e3], ['30d', '30d', 30 * 86400e3],
];
export const rangeOf = (key) => ({ span: (RANGES.find((r) => r[0] === key) || RANGES[0])[2] });

// Merge stored history with newer live samples, clipped to [from, to].
function series(stored, id, from, to) {
  const t = [], v = [];
  const push = (ts, value) => { if (ts >= from && ts <= to) { t.push(ts / 1000); v.push(value); } };
  const h = stored.get(id);
  let last = -Infinity;
  if (h) for (let i = 0; i < h.t.length; i++) push(h.t[i], h.v[i]), (last = h.t[i]);
  const b = live.get(id);
  if (b) {
    let i = b.t.length;
    while (i > 0 && b.t[i - 1] > last && b.t[i - 1] >= from) i--;
    for (; i < b.t.length; i++) if (b.t[i] > last) push(b.t[i], b.v[i]);
  }
  return [t, v];
}

function align(tables) {
  if (tables.length === 1) return [tables[0][0], tables[0][1]];
  return uPlot.join(tables);
}

export function TimeChart({ signals, range, height = 260, theme }) {
  const el = useRef();
  const [state, setState] = useState('loading');
  const ids = signals.map((s) => s.id).join(',');
  const rangeKey = range.span ? `last-${range.span}` : `${range.from}-${range.to}`;

  useEffect(() => {
    if (!signals.length) return undefined;
    const node = el.current;
    const discrete = signals.every((s) => s.kind !== 'number') ? signals[0] : null;
    const muted = cssVar('--muted'), grid = cssVar('--border');
    const axis = { stroke: muted, grid: { stroke: grid, width: 1 }, ticks: { stroke: grid, width: 1 }, font: '11px system-ui' };
    let ours = false, zoomed = false;
    const plot = new uPlot({
      width: node.clientWidth || 600,
      height,
      cursor: { drag: { x: true, y: false }, points: { size: 7 } },
      legend: { live: true },
      scales: { x: { time: true }, y: discrete ? { range: (u, min, max) => [Math.min(min, 0) - 0.4, Math.max(max, 1) + 0.4] } : {} },
      axes: [
        { ...axis },
        { ...axis, size: discrete ? 90 : 56,
          values: discrete
            ? (u, splits) => splits.map((v) => (Number.isInteger(v) ? enumLabel(discrete.enum, v) : ''))
            : axisValues,
          splits: discrete ? (u, i, min, max) => { const out = []; for (let v = Math.ceil(min); v <= max; v++) out.push(v); return out; } : undefined },
      ],
      series: [
        { value: (u, v) => (v == null ? '—' : new Date(v * 1000).toLocaleString()) },
        ...signals.map((s) => ({
          label: s.chartLabel || s.fullLabel || s.label,
          stroke: colorFor(s.id),
          width: 1.7,
          fill: signals.length === 1 ? colorFor(s.id) + '1f' : undefined,
          points: { show: false },
          paths: s.kind !== 'number' ? uPlot.paths.stepped({ align: 1 }) : undefined,
          value: (u, v) => (v == null ? '—' : formatSignal(s, v)),
        })),
      ],
      hooks: { setScale: [(u, key) => { if (key === 'x' && !ours) zoomed = true; }] },
    }, [[], ...signals.map(() => [])], node);

    const stored = new Map();
    const bounds = () => (range.span ? [Date.now() - range.span, Date.now()] : [range.from, range.to]);
    const draw = () => {
      const [from, to] = bounds();
      const data = align(signals.map((s) => series(stored, s.id, from, to)));
      ours = true;
      if (zoomed) plot.setData(data, false); // Keep the user's zoom window.
      else {
        plot.setData(data, true);
        plot.setScale('x', { min: from / 1000, max: to / 1000 });
      }
      ours = false;
      setState(data[0].length ? 'ready' : 'empty');
    };

    // Live redraws are throttled according to the visible span.
    const every = !range.span ? Infinity : range.span <= 15 * 60e3 ? 400 : 2000;
    let last = 0, timer = 0;
    const schedule = () => {
      if (!range.span || timer) return;
      timer = setTimeout(() => { timer = 0; last = Date.now(); draw(); }, Math.max(0, every - (Date.now() - last)));
    };
    const unsubscribe = liveChanges.subscribe(schedule);
    const tick = range.span ? setInterval(draw, Math.min(10000, range.span / 60)) : 0; // Slide even without data.

    const resize = new ResizeObserver(() => plot.setSize({ width: node.clientWidth, height }));
    resize.observe(node);
    const reset = () => { zoomed = false; draw(); };
    node.addEventListener('dblclick', reset);

    let cancelled = false;
    const [from, to] = bounds();
    history(signals.map((s) => s.id), from, to, Math.round((node.clientWidth || 800) * 1.5))
      .then((result) => {
        if (cancelled) return;
        for (const s of result.series) stored.set(s.id, { t: s.t, v: s.v });
        draw();
      })
      .catch(() => !cancelled && draw());

    return () => {
      cancelled = true;
      unsubscribe();
      clearTimeout(timer);
      clearInterval(tick);
      resize.disconnect();
      node.removeEventListener('dblclick', reset);
      plot.destroy();
    };
  }, [ids, rangeKey, height, theme]);

  return html`<div class="chart" ref=${el}>
    ${state === 'loading' ? html`<div class="empty"><span class="spinner"></span></div>` : null}
    ${state === 'empty' ? html`<div class="empty">No data in this range yet</div>` : null}
  </div>`;
}

// Lightweight canvas sparkline over the live buffer (dashboard cards).
export function Sparkline({ id, span = 10 * 60e3 }) {
  const canvas = useRef();
  const version = useLive();
  useEffect(() => {
    const c = canvas.current;
    if (!c) return;
    const dpr = devicePixelRatio || 1;
    const w = c.clientWidth, h = c.clientHeight;
    if (c.width !== w * dpr) { c.width = w * dpr; c.height = h * dpr; }
    const g = c.getContext('2d');
    g.setTransform(dpr, 0, 0, dpr, 0, 0);
    g.clearRect(0, 0, w, h);
    const b = live.get(id);
    if (!b || b.t.length < 2) return;
    // Zoom to the data available when the buffer is younger than the span.
    const from = Math.max(Date.now() - span, Math.min(b.t[0], Date.now() - 60e3));
    const width = Date.now() - from;
    let i = b.t.length;
    while (i > 0 && b.t[i - 1] >= from) i--;
    const t = b.t.slice(i), v = b.v.slice(i);
    if (t.length < 2) return;
    let min = Math.min(...v), max = Math.max(...v);
    if (min === max) { min -= 1; max += 1; }
    const color = colorFor(id);
    g.beginPath();
    t.forEach((ts, k) => {
      const x = ((ts - from) / width) * w, y = h - 2 - ((v[k] - min) / (max - min)) * (h - 4);
      k ? g.lineTo(x, y) : g.moveTo(x, y);
    });
    g.strokeStyle = color;
    g.lineWidth = 1.6;
    g.stroke();
    g.lineTo(((t[t.length - 1] - from) / width) * w, h);
    g.lineTo(((t[0] - from) / width) * w, h);
    g.fillStyle = color + '22';
    g.fill();
  }, [version, id]);
  return html`<canvas class="spark" ref=${canvas}></canvas>`;
}

export async function downloadCsv(signals, from, to) {
  const result = await history(signals.map((s) => s.id), from, to, 20000);
  const tables = result.series.map((s) => [s.t, s.v]);
  const data = tables.length === 1 ? tables[0] : uPlot.join(tables);
  const header = ['time', ...signals.map((s) => `"${(s.fullLabel || s.label).replaceAll('"', "'")}${s.unit ? ` (${s.unit})` : ''}"`)];
  const lines = [header.join(',')];
  for (let i = 0; i < data[0].length; i++)
    lines.push([new Date(data[0][i]).toISOString(), ...data.slice(1).map((col) => col[i] ?? '')].join(','));
  const link = document.createElement('a');
  link.href = URL.createObjectURL(new Blob([lines.join('\n')], { type: 'text/csv' }));
  link.download = `iotpp-signals-${new Date().toISOString().slice(0, 19)}.csv`;
  link.click();
  URL.revokeObjectURL(link.href);
}
