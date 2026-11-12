import { html, useState } from '/vendor/preact.js';
import { useStore } from '../store.js';
import { api } from '../api.js';
import { toast } from '../components/toasts.js';
import { Button, Card, Empty, Modal, StatusBadges, useTicker } from '../components/ui.js';
import { ago } from '../schema.js';
import { DeviceCard, devicesSorted } from './common.js';
import { navigate } from '../router.js';

function AddDevice({ onClose }) {
  const [host, setHost] = useState('');
  const [port, setPort] = useState(4444);
  const [name, setName] = useState('');
  const add = async () => {
    try {
      const device = await api('/devices', { method: 'POST', body: { host, port: Number(port), name } });
      onClose();
      navigate(`/device/${device.key}`);
    } catch (error) {
      toast({ severity: 'error', title: 'Cannot add device', detail: error.message });
    }
  };
  return html`<${Modal} title="Add device" onClose=${onClose} actions=${[
    html`<${Button} kind="ghost" onClick=${onClose}>Cancel<//>`,
    html`<${Button} kind="primary" icon="plus" onClick=${add} disabled=${!host}>Add<//>`]}>
    <div>
      <div class="field"><label>Host or IPv4</label><input value=${host} onInput=${(e) => setHost(e.target.value)} placeholder="192.168.1.50" autofocus /></div>
      <div class="field"><label>DEX port</label><input type="number" value=${port} onInput=${(e) => setPort(e.target.value)} /></div>
      <div class="field"><label>Name<span class="hint">Optional</span></label><input value=${name} onInput=${(e) => setName(e.target.value)} placeholder="Kitchen sentinel" /></div>
    </div>
  <//>`;
}

export function Devices() {
  useStore();
  useTicker(5000);
  const [adding, setAdding] = useState(false);
  const [view, setView] = useState('cards');
  const devices = devicesSorted();
  const discover = async () => {
    await api('/discover', { method: 'POST' });
    toast({ severity: 'info', title: 'Discovery broadcast sent', detail: 'Devices answering DEX Info will appear automatically' });
  };
  return html`
    <div class="row between">
      <div class="seg">${[['cards', 'Cards'], ['table', 'Table']].map(([v, l]) =>
        html`<button class=${view === v ? 'on' : ''} onClick=${() => setView(v)}>${l}</button>`)}</div>
      <div class="row">
        <${Button} icon="radar" onClick=${discover}>Discover<//>
        <${Button} icon="plus" kind="primary" onClick=${() => setAdding(true)}>Add device<//>
      </div>
    </div>
    ${!devices.length ? html`<${Card}><${Empty} icon="radar" title="No devices">Run discovery, add a device by IP,
      or start the simulator: <code>run hub sim</code><//><//>`
    : view === 'cards' ? html`<div class="grid cards">${devices.map((d) => html`<${DeviceCard} key=${d.key} device=${d} />`)}</div>`
    : html`<div class="card flush"><table>
        <thead><tr><th></th><th>Name</th><th>Address</th><th>Firmware</th><th>State</th><th>Peripherals</th><th>Last seen</th><th>Samples</th></tr></thead>
        <tbody>${devices.map((d) => html`<tr class="clickable" onClick=${() => navigate(`/device/${d.key}`)}>
          <td><span class=${`dot ${d.online ? 'on' : 'off'}`}></span></td>
          <td><strong>${d.name || d.key}</strong></td>
          <td class="mono">${d.host}:${d.port}</td>
          <td>${d.firmware || '—'}</td>
          <td><${StatusBadges} status=${d.status} /></td>
          <td><span class="chips">${d.peripherals.map((p) => html`<span class="peripheral-chip">${p.typeName || '?'} #${p.id}</span>`)}</span></td>
          <td class="faint">${ago(d.lastSeen)}</td>
          <td class="num">${d.samples.toLocaleString()}</td>
        </tr>`)}</tbody></table></div>`}
    ${adding ? html`<${AddDevice} onClose=${() => setAdding(false)} />` : null}`;
}
