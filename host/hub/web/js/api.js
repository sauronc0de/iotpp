// REST + SSE client.
import { state, update, pushSamples } from './store.js';
import { notifyEvent } from './components/toasts.js';
import { refreshSignals, rebuildSignals } from './schema.js';

export async function api(path, { method = 'GET', body } = {}) {
  const response = await fetch('/api' + path, {
    method,
    headers: body !== undefined ? { 'content-type': 'application/json' } : {},
    body: body !== undefined ? JSON.stringify(body) : undefined,
  });
  const json = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(json.error || response.statusText);
  return json;
}

const enc = encodeURIComponent;
export const command = (key, cmd, peripheral = null, data = undefined) =>
  api(`/devices/${enc(key)}/command`, { method: 'POST', body: { command: cmd, peripheral, data } });
export const rawCommand = (key, cmd, payload) =>
  api(`/devices/${enc(key)}/raw`, { method: 'POST', body: { command: cmd, payload } });
export const history = (ids, from, to, points) =>
  api(`/history?ids=${ids.join(',')}&from=${Math.round(from)}&to=${Math.round(to)}&points=${points}`);

export async function refreshDevices() {
  const list = await api('/devices');
  update({ devices: Object.fromEntries(list.map((d) => [d.key, d])) });
  rebuildSignals();
}

export async function refreshCounts() {
  update({ counts: await api('/events/counts') });
}

export async function ackEvents(ids = []) {
  await api('/events/ack', { method: 'POST', body: ids.length ? { ids } : {} });
}

export function connect() {
  const source = new EventSource('/api/stream');
  source.onopen = () => {
    update({ connected: true });
    // Resynchronize after reconnects.
    refreshDevices().catch(() => {});
    refreshCounts().catch(() => {});
  };
  source.onerror = () => update({ connected: false });
  source.addEventListener('device', (e) => {
    const device = JSON.parse(e.data);
    update((s) => ({ devices: { ...s.devices, [device.key]: device } }));
  });
  source.addEventListener('removed', (e) => {
    const { key } = JSON.parse(e.data);
    update((s) => { const devices = { ...s.devices }; delete devices[key]; return { devices }; });
  });
  source.addEventListener('samples', (e) => {
    const sample = JSON.parse(e.data);
    if (Object.keys(sample.values).some((id) => !state.signalsById.has(Number(id)))) refreshSignals();
    pushSamples(sample.ts, sample.values);
  });
  source.addEventListener('event', (e) => {
    const event = JSON.parse(e.data);
    update((s) => ({
      recent: [event, ...s.recent].slice(0, 50),
      counts: { ...s.counts, [event.severity]: (s.counts[event.severity] || 0) + 1 },
    }));
    notifyEvent(event);
  });
  source.addEventListener('ack', (e) => {
    const { ids, all } = JSON.parse(e.data);
    const acked = new Set(ids);
    refreshCounts().catch(() => {});
    update((s) => ({ recent: s.recent.map((ev) => (all || acked.has(ev.id) ? { ...ev, acked: true } : ev)) }));
  });
  return source;
}
