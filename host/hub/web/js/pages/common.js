// Building blocks shared by several pages.
import { html } from '/vendor/preact.js';
import { useStore, useLive, lastValue, state } from '../store.js';
import { command } from '../api.js';
import { toast } from '../components/toasts.js';
import { Icon } from '../components/icons.js';
import { StatusBadges, Badge } from '../components/ui.js';
import { TimeChart, Sparkline } from '../components/chart.js';
import { SEVERITY, ago, signalsOf, formatSignal, colorFor, deviceName } from '../schema.js';

// Execute a DEX command with user feedback. Returns the hub's result object.
export async function run(key, cmd, peripheral = null, data = undefined, success = null) {
  try {
    const result = await command(key, cmd, peripheral, data);
    if (!result.ok) {
      const why = result.timeout ? result.error : (result.result || []).join(', ') || result.decodeError || 'failed';
      toast({ severity: 'error', title: `${cmd} failed`, detail: `${deviceName(key)}: ${why}`, ttl: 7000 });
    } else if (success) toast({ severity: 'success', title: success, detail: deviceName(key) });
    return result;
  } catch (error) {
    toast({ severity: 'error', title: `${cmd} rejected`, detail: error.message, ttl: 8000 });
    return { ok: false, error: error.message };
  }
}

export function EventRow({ event, onAck }) {
  const sev = SEVERITY[event.severity] || SEVERITY.info;
  return html`<div class=${`event ${event.severity} ${event.acked ? '' : 'unacked'}`} style=${`--tone: var(--${sev.tone})`}>
    <div class="ico"><${Icon} name=${sev.icon} size=${17} /></div>
    <div>
      <div class="t">${event.title}</div>
      <div class="small muted"><a href=${`#/device/${encodeURIComponent(event.device)}`}>${event.detail || deviceName(event.device)}</a></div>
    </div>
    <div class="row small faint">
      <span title=${new Date(event.ts).toLocaleString()}>${ago(event.ts)}</span>
      ${!event.acked && onAck ? html`<button class="btn ghost icon" title="Acknowledge" onClick=${() => onAck(event)}><${Icon} name="check" size=${15} /></button>` : null}
    </div>
  </div>`;
}

export function LiveTile({ signal, selected, onClick }) {
  useLive();
  const last = lastValue(signal.id);
  return html`<div class=${`live-value ${selected ? 'on' : ''}`} onClick=${onClick}
      style=${selected ? `border-color:${colorFor(signal.id)}` : ''} title=${last ? new Date(last.t).toLocaleString() : ''}>
    <div class="l">${signal.label}</div>
    <div class="v num">${last ? formatSignal(signal, last.v) : '—'}</div>
  </div>`;
}

// Charts for a set of signals: one chart per unit (or per enum) so scales make sense.
export function groupSignals(signals) {
  const groups = new Map();
  for (const s of signals) {
    const key = s.kind === 'number' ? `u:${s.unit || ''}` : `e:${s.enum || s.kind}:${s.id}`;
    if (!groups.has(key)) groups.set(key, []);
    groups.get(key).push(s);
  }
  return [...groups.values()];
}

export function SignalCharts({ signals, range, height = 240, compactLabels = false }) {
  const { theme } = useStore();
  if (!signals.length) return null;
  return html`<div class="grid two">${groupSignals(signals).map((group) => {
    const unit = group[0].kind === 'number' ? group[0].unit || 'No unit' : group[0].label;
    const prepared = compactLabels ? group.map((s) => ({ ...s, chartLabel: s.label })) : group;
    return html`<div class="card" key=${group.map((s) => s.id).join('-')}>
      <div class="card-head"><h3>${unit}</h3><div class="spacer"></div><span class="faint small">${group.length} signal${group.length > 1 ? 's' : ''}</span></div>
      <${TimeChart} signals=${prepared} range=${range} height=${height} theme=${theme} />
    </div>`;
  })}</div>`;
}

export function DeviceCard({ device }) {
  useStore();
  const alert = device.status?.notifications?.length || device.peripherals.some((p) => p.status?.notifications?.length);
  return html`<a class=${`card link ${device.online ? '' : 'offline'} ${alert ? 'alert' : ''}`} href=${`#/device/${encodeURIComponent(device.key)}`}>
    <div class="card-head">
      <span class=${`dot ${device.online ? 'on' : 'off'}`}></span>
      <div class="grow" style="min-width:0">
        <h2 style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis">${device.name || device.key}</h2>
        <div class="small faint mono">${device.host}:${device.port}${device.firmware ? ` · fw ${device.firmware}` : ''}</div>
      </div>
      <${StatusBadges} status=${device.status} />
    </div>
    <div class="stack">
      ${device.peripherals.map((p) => html`<${PeripheralSummary} device=${device} p=${p} />`)}
      ${!device.peripherals.length ? html`<span class="faint small">No peripherals reported yet</span>` : null}
    </div>
    <div class="row between small faint" style="margin-top:12px">
      <span>Seen ${ago(device.lastSeen)}</span><span>${device.samples.toLocaleString()} samples</span>
    </div>
  </a>`;
}

function PeripheralSummary({ device, p }) {
  const signals = signalsOf(device.key, p.id).filter((s) => s.kind !== 'number' || s.unit).slice(0, 3);
  return html`<div class="subcard">
    <div class="row between">
      <span class="peripheral-chip"><${Icon} name="layers" size=${13} />${p.typeName || 'Unknown'} #${p.id}</span>
      <${StatusBadges} status=${p.status} />
    </div>
    ${signals.length ? html`<div class="grid" style="grid-template-columns:repeat(${signals.length},1fr);gap:8px;margin-top:8px">
      ${signals.map((s) => html`<${MiniSignal} signal=${s} />`)}</div>` : null}
  </div>`;
}

function MiniSignal({ signal }) {
  useLive();
  const last = lastValue(signal.id);
  return html`<div style="min-width:0">
    <div class="small faint" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis">${signal.label}</div>
    <div class="num">${last ? formatSignal(signal, last.v) : '—'}</div>
    ${signal.kind === 'number' ? html`<${Sparkline} id=${signal.id} />` : null}
  </div>`;
}

export const peripheralOptions = (device) => device?.peripherals.map((p) => [p.id, `${p.typeName || 'Unknown'} #${p.id}`]) ?? [];
export const devicesSorted = () => Object.values(state.devices).sort((a, b) => (a.name || a.key).localeCompare(b.name || b.key));
export { Badge };
