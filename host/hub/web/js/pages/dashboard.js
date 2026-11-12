import { html, useState } from '/vendor/preact.js';
import { useStore, save, update } from '../store.js';
import { ackEvents } from '../api.js';
import { Icon } from '../components/icons.js';
import { Card, Empty, Segmented, useTicker } from '../components/ui.js';
import { rangeOf } from '../components/chart.js';
import { DeviceCard, EventRow, SignalCharts, devicesSorted } from './common.js';

function Kpi({ icon, value, label, tone = '' }) {
  return html`<div class=${`card kpi ${tone}`}><div class="icon"><${Icon} name=${icon} size=${20} /></div>
    <div><div class="value">${value}</div><div class="label">${label}</div></div></div>`;
}

export function Dashboard() {
  const s = useStore();
  useTicker(5000);
  const [range, setRange] = useState('5m');
  const devices = devicesSorted();
  const online = devices.filter((d) => d.online).length;
  const active = s.recent.filter((e) => e.severity === 'notification' && !e.acked);
  const problems = (s.counts.error || 0) + (s.counts.warning || 0);

  // Pinned signals, or a sensible default: the first monitored peripheral's signals with a unit.
  let charted = s.pinned.map((id) => s.signalsById.get(id)).filter(Boolean);
  const pinned = charted.length > 0;
  if (!pinned) {
    const first = s.signals.find((x) => x.kind === 'number' && x.unit);
    charted = first ? s.signals.filter((x) => x.device === first.device && x.peripheral === first.peripheral && x.unit &&
      ['V', 'W', 'Hz'].includes(x.unit)) : [];
  }

  return html`
    ${active.length ? html`<div class="banner">
      <div class="ico"><${Icon} name="bell" size=${20} /></div>
      <div class="grow"><strong>${active.length} active notification${active.length > 1 ? 's' : ''}</strong>
        <div class="small muted">${active.slice(0, 3).map((e) => `${e.title} — ${e.detail}`).join(' · ')}</div></div>
      <button class="btn" onClick=${() => ackEvents(active.map((e) => e.id))}><${Icon} name="check" size=${15} />Acknowledge</button>
      <a class="btn ghost" href="#/events">View all</a>
    </div>` : null}

    <div class="grid kpis">
      <${Kpi} icon="cpu" value=${`${online}/${devices.length}`} label="Devices online" tone=${online === devices.length ? 'ok' : 'warn'} />
      <${Kpi} icon="bell" value=${s.counts.notification || 0} label="Unread notifications" tone="notif" />
      <${Kpi} icon="alert" value=${problems} label="Unread errors & warnings" tone=${problems ? 'warn' : 'ok'} />
      <${Kpi} icon="activity" value=${s.signals.length} label="Signals tracked" />
    </div>

    <${Card} title=${pinned ? 'Pinned signals' : 'Live overview'} icon="activity"
      actions=${html`${!pinned ? html`<span class="small faint">Pin signals from the Signals page (★)</span>` : html`
        <button class="btn ghost small" onClick=${() => { save('pinned', []); update({ pinned: [] }); }}>Clear pins</button>`}
        <${Segmented} value=${range} onChange=${setRange} options=${[['5m', 'Live'], ['1h', '1h'], ['24h', '24h'], ['7d', '7d']]} />`}>
      ${charted.length ? html`<${SignalCharts} signals=${charted} range=${rangeOf(range)} height=${200} />`
        : html`<${Empty} icon="activity" title="No monitoring data yet">Signals appear automatically when devices publish.<//>`}
    <//>

    <div class="row between"><h2>Devices</h2><a href="#/devices" class="small">Manage →</a></div>
    ${devices.length ? html`<div class="grid cards">${devices.map((d) => html`<${DeviceCard} key=${d.key} device=${d} />`)}</div>`
      : html`<${Card}><${Empty} icon="radar" title="No devices yet">Devices are discovered when they publish monitoring data,
        answer a discovery broadcast, or when you add them on the Devices page.<//><//>`}

    <${Card} title="Recent activity" icon="bell" class="flush-body" actions=${html`<a href="#/events" class="small">All notifications →</a>`}>
      ${s.recent.length ? html`<div class="events" style="margin:0 -18px -18px">${s.recent.slice(0, 8).map((e) =>
        html`<${EventRow} key=${e.id} event=${e} onAck=${(ev) => ackEvents([ev.id])} />`)}</div>`
        : html`<${Empty} icon="bell" title="Nothing yet" />`}
    <//>`;
}
