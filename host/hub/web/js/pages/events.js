import { html, useEffect, useState } from '/vendor/preact.js';
import { useStore } from '../store.js';
import { api, ackEvents } from '../api.js';
import { Button, Card, Empty, Segmented, Toggle, useTicker } from '../components/ui.js';
import { SEVERITY } from '../schema.js';
import { EventRow } from './common.js';

export function Events() {
  const s = useStore();
  useTicker(10000);
  const [severity, setSeverity] = useState('');
  const [device, setDevice] = useState('');
  const [unacked, setUnacked] = useState(false);
  const [list, setList] = useState([]);
  const [more, setMore] = useState(true);
  const params = (before = 0) => `/events?limit=100&before=${before}&severity=${severity}&device=${encodeURIComponent(device)}&unacked=${unacked ? 1 : 0}`;

  useEffect(() => { api(params()).then((l) => { setList(l); setMore(l.length === 100); }); }, [severity, device, unacked]);

  // Merge live events (newest first) and acknowledgements.
  const matches = (e) => (!severity || e.severity === severity) && (!device || e.device === device);
  const newest = list[0]?.id || 0;
  const fresh = s.recent.filter((e) => e.id > newest && matches(e));
  const ackedLive = new Map(s.recent.map((e) => [e.id, e.acked]));
  const shown = [...fresh, ...list].map((e) => (ackedLive.get(e.id) ? { ...e, acked: true } : e)).filter((e) => !unacked || !e.acked);

  const ack = async (ids) => { await ackEvents(ids); setList((l) => l.map((e) => (!ids.length || ids.includes(e.id) ? { ...e, acked: true } : e))); };
  const total = Object.values(s.counts).reduce((a, b) => a + b, 0);

  return html`
    <div class="card row between" style="padding:12px 16px">
      <${Segmented} value=${severity} onChange=${setSeverity} options=${[['', 'All'], ...Object.entries(SEVERITY).map(([k, v]) =>
        [k, html`${v.label}${s.counts[k] ? html` <span class="count" style=${`background:var(--${v.tone})`}>${s.counts[k]}</span>` : ''}`])]} />
      <div class="row">
        <select value=${device} onChange=${(e) => setDevice(e.target.value)}><option value="">All devices</option>
          ${Object.values(s.devices).map((d) => html`<option value=${d.key}>${d.name || d.key}</option>`)}</select>
        <label class="row small"><${Toggle} checked=${unacked} onChange=${setUnacked} /> Unread only</label>
        <${Button} icon="check" disabled=${!total} onClick=${() => ack([])}>Mark all read<//>
        ${'Notification' in window && Notification.permission === 'default' ? html`<${Button} icon="bell"
          onClick=${() => Notification.requestPermission()}>Desktop alerts<//>` : null}
      </div>
    </div>
    <div class="card flush">
      ${shown.length ? html`<div class="events">${shown.map((e) => html`<${EventRow} key=${e.id} event=${e} onAck=${(ev) => ack([ev.id])} />`)}</div>`
        : html`<${Empty} icon="bell" title="No notifications">Status notifications, errors, door events and connectivity changes appear here.<//>`}
    </div>
    ${more && list.length ? html`<div class="row" style="justify-content:center">
      <${Button} onClick=${async () => { const l = await api(params(list[list.length - 1].id)); setList([...list, ...l]); setMore(l.length === 100); }}>Load more<//></div>` : null}`;
}
