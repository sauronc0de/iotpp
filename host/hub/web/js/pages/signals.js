// Signal explorer: every signal of every device, filterable, plotted by unit.
import { html, useMemo, useState } from '/vendor/preact.js';
import { useStore, update, save, useLive, lastValue } from '../store.js';
import { parse, setQuery } from '../router.js';
import { Icon } from '../components/icons.js';
import { Button, Card, Empty, Segmented } from '../components/ui.js';
import { RANGES, rangeOf, downloadCsv } from '../components/chart.js';
import { colorFor, formatSignal } from '../schema.js';
import { SignalCharts } from './common.js';

function SignalItem({ signal, selected, pinned, onToggle, onPin }) {
  useLive();
  const last = lastValue(signal.id);
  return html`<div class=${`signal-item ${selected ? 'on' : ''}`} onClick=${onToggle}>
    <input type="checkbox" checked=${selected} />
    <span class="swatch" style=${`background:${colorFor(signal.id)}`}></span>
    <span class="label" title=${signal.fullLabel}>${signal.label}</span>
    <span class="num small">${last ? formatSignal(signal, last.v) : ''}</span>
    <button class="btn ghost icon" title=${pinned ? 'Unpin from dashboard' : 'Pin to dashboard'}
      style=${pinned ? 'color:var(--warn)' : ''} onClick=${(e) => { e.stopPropagation(); onPin(); }}>
      <${Icon} name="star" size=${14} /></button>
  </div>`;
}

const toLocalInput = (ms) => new Date(ms - new Date().getTimezoneOffset() * 60e3).toISOString().slice(0, 16);

export function Signals() {
  const s = useStore();
  const query = parse().query;
  const [search, setSearch] = useState(query.get('q') || '');
  const [device, setDevice] = useState(query.get('device') || '');
  const [type, setType] = useState(query.get('type') || '');
  const [unit, setUnit] = useState(query.get('unit') || '');
  const [range, setRange] = useState(query.get('range') || '1h');
  const [custom, setCustom] = useState({ from: Date.now() - 86400e3, to: Date.now() });
  const [selected, setSelected] = useState(new Set((query.get('ids') || '').split(',').filter(Boolean).map(Number)));

  const units = useMemo(() => [...new Set(s.signals.map((x) => (x.kind === 'number' ? x.unit || '—' : x.kind)))].sort(), [s.signals]);
  const types = useMemo(() => [...new Set(s.signals.map((x) => x.typeName))].sort(), [s.signals]);
  const needle = search.toLowerCase();
  const filtered = s.signals.filter((x) => (!device || x.device === device) && (!type || x.typeName === type) &&
    (!unit || (x.kind === 'number' ? x.unit || '—' : x.kind) === unit) && (!needle || x.fullLabel.toLowerCase().includes(needle)));
  const groups = new Map();
  for (const x of filtered) groups.set(x.source, [...(groups.get(x.source) || []), x]);

  const commit = (next) => { setSelected(next); setQuery({ ids: [...next].join(',') }); };
  const toggle = (id) => { const n = new Set(selected); n.has(id) ? n.delete(id) : n.add(id); commit(n); };
  const pin = (id) => {
    const pinned = s.pinned.includes(id) ? s.pinned.filter((x) => x !== id) : [...s.pinned, id];
    save('pinned', pinned);
    update({ pinned });
  };
  const filter = (key, setter) => (e) => { setter(e.target.value); setQuery({ [key]: e.target.value }); };
  const chosen = [...selected].map((id) => s.signalsById.get(id)).filter(Boolean);
  const view = range === 'custom' ? { from: custom.from, to: custom.to } : rangeOf(range);
  const bounds = () => (view.span ? [Date.now() - view.span, Date.now()] : [view.from, view.to]);

  return html`<div class="explorer">
    <div class="card" style="padding:12px">
      <div class="stack" style="gap:8px">
        <div class="search"><${Icon} name="search" size=${15} /><input placeholder="Search signals…" value=${search} onInput=${filter('q', setSearch)} /></div>
        <div class="row" style="gap:6px">
          <select class="grow" value=${device} onChange=${filter('device', setDevice)}><option value="">All devices</option>
            ${Object.values(s.devices).map((d) => html`<option value=${d.key}>${d.name || d.key}</option>`)}</select>
          <select class="grow" value=${type} onChange=${filter('type', setType)}><option value="">All types</option>
            ${types.map((t) => html`<option>${t}</option>`)}</select>
        </div>
        <div class="chips">${units.map((u) => html`<button class=${`badge tone-${unit === u ? 'accent' : 'neutral'}`} style="cursor:pointer"
          onClick=${() => { const v = unit === u ? '' : u; setUnit(v); setQuery({ unit: v }); }}>${u}</button>`)}</div>
        <div class="row between small faint"><span>${filtered.length} signals · ${selected.size} selected</span>
          <span class="row" style="gap:4px">
            <button class="btn ghost small" onClick=${() => commit(new Set([...selected, ...filtered.map((x) => x.id)]))}>All</button>
            <button class="btn ghost small" onClick=${() => commit(new Set())}>None</button></span></div>
      </div>
      <div class="signal-list">${[...groups].map(([source, list]) => html`
        <div class="group-title">${source}</div>
        ${list.map((x) => html`<${SignalItem} key=${x.id} signal=${x} selected=${selected.has(x.id)} pinned=${s.pinned.includes(x.id)}
          onToggle=${() => toggle(x.id)} onPin=${() => pin(x.id)} />`)}`)}
        ${!filtered.length ? html`<${Empty} icon="search" title="No signals match" />` : null}
      </div>
    </div>
    <div class="stack" style="gap:16px">
      <div class="card row between" style="padding:12px 16px">
        <${Segmented} value=${range} onChange=${(r) => { setRange(r); setQuery({ range: r }); }}
          options=${[...RANGES.map(([k, l]) => [k, l]), ['custom', 'Custom']]} />
        ${range === 'custom' ? html`<div class="row">
          <input type="datetime-local" value=${toLocalInput(custom.from)} onChange=${(e) => setCustom({ ...custom, from: new Date(e.target.value).getTime() })} />
          <span class="faint">→</span>
          <input type="datetime-local" value=${toLocalInput(custom.to)} onChange=${(e) => setCustom({ ...custom, to: new Date(e.target.value).getTime() })} />
        </div>` : null}
        <${Button} icon="download" disabled=${!chosen.length} onClick=${() => downloadCsv(chosen, ...bounds())}>CSV<//>
      </div>
      ${chosen.length ? html`<${SignalCharts} signals=${chosen} range=${view} height=${280} />`
        : html`<${Card}><${Empty} icon="activity" title="Select signals to plot">Signals are grouped into one chart per unit.
          New signals defined in DEX appear here automatically.<//><//>`}
    </div>
  </div>`;
}
