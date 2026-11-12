import { html, render, useEffect } from '/vendor/preact.js';
import { useStore, update, save, state } from './store.js';
import { api, connect, refreshDevices, refreshCounts } from './api.js';
import { refreshSignals, rebuildSignals } from './schema.js';
import { useRoute } from './router.js';
import { Icon } from './components/icons.js';
import { Toasts } from './components/toasts.js';
import { Dashboard } from './pages/dashboard.js';
import { Devices } from './pages/devices.js';
import { DevicePage } from './pages/device.js';
import { Signals } from './pages/signals.js';
import { Events } from './pages/events.js';
import { Protocol } from './pages/protocol.js';

const PAGES = [
  ['', 'Dashboard', 'grid', Dashboard, 'Home overview'],
  ['devices', 'Devices', 'cpu', Devices, 'ESP32 sentinels on the network'],
  ['signals', 'Signals', 'activity', Signals, 'Explore and compare monitoring data'],
  ['events', 'Notifications', 'bell', Events, 'Notifications, errors and activity'],
  ['protocol', 'DEX protocol', 'terminal', Protocol, 'Console and generated reference'],
];

function Topbar({ title, subtitle }) {
  const s = useStore();
  const unread = (s.counts.notification || 0) + (s.counts.error || 0) + (s.counts.warning || 0);
  const toggleTheme = () => {
    const theme = s.theme === 'light' ? 'dark' : 'light';
    document.documentElement.dataset.theme = theme;
    try { localStorage.setItem('theme', theme); } catch {}
    update({ theme });
  };
  return html`<header class="topbar">
    <div class="title"><h1>${title}</h1><small>${subtitle}</small></div>
    <div class="spacer"></div>
    <span class="conn"><span class=${`dot ${s.connected ? 'on' : 'off'}`}></span>${s.connected ? 'Live' : 'Reconnecting…'}</span>
    <a class=${`btn ghost icon bell ${s.ringing ? 'ringing' : ''}`} href="#/events" title="Notifications">
      <${Icon} name="bell" />${unread ? html`<span class="count">${unread}</span>` : null}</a>
    <button class="btn ghost icon" title=${s.muted ? 'Unmute alerts' : 'Mute alerts'}
      onClick=${() => { save('muted', !s.muted); update({ muted: !s.muted }); }}><${Icon} name=${s.muted ? 'mute' : 'volume'} /></button>
    <button class="btn ghost icon" title="Toggle theme" onClick=${toggleTheme}><${Icon} name=${s.theme === 'light' ? 'moon' : 'sun'} /></button>
  </header>`;
}

function App() {
  const s = useStore();
  const route = useRoute();
  const unread = (s.counts.notification || 0) + (s.counts.error || 0) + (s.counts.warning || 0);
  useEffect(() => { document.title = unread ? `(${unread}) iotpp hub` : 'iotpp hub'; }, [unread]);
  useEffect(() => rebuildSignals(), [Object.values(s.devices).map((d) => d.name + d.peripherals.length).join()]);

  const [page, ...params] = route.parts;
  const device = page === 'device' ? s.devices[params[0]] : null;
  const entry = PAGES.find((p) => p[0] === (page === 'device' ? 'devices' : page || '')) || PAGES[0];
  const title = device ? device.name || device.key : entry[1];
  const subtitle = device ? `Device · ${device.host}` : entry[4];
  const body = !s.schema ? html`<div class="empty-state"><span class="spinner"></span>Loading…</div>`
    : page === 'device' ? html`<${DevicePage} key=${params[0]} deviceKey=${params[0]} />`
    : html`<${entry[3]} />`;

  return html`<div class="app">
    <aside class="sidebar">
      <div class="brand"><div class="brand-logo"><${Icon} name="activity" size=${18} /></div>
        <div>iotpp hub<small>DEX device manager</small></div></div>
      <nav class="nav">${PAGES.map(([path, label, icon]) => html`<a href=${`#/${path}`}
          class=${entry[0] === path ? 'active' : ''}><${Icon} name=${icon} /><span class="label">${label}</span>
          ${path === 'events' && unread ? html`<span class="count">${unread}</span>` : null}</a>`)}</nav>
      <div class="sidebar-foot">
        <span>${Object.values(s.devices).filter((d) => d.online).length} online · ${s.signals.length} signals</span>
        <span>${s.schema ? `${s.schema.types.length} peripheral types` : ''}</span>
      </div>
    </aside>
    <main class="main">
      <${Topbar} title=${title} subtitle=${subtitle} />
      <div class="content">${body}</div>
    </main>
    <${Toasts} />
  </div>`;
}

async function boot() {
  update({ theme: document.documentElement.dataset.theme || 'dark' });
  render(html`<${App} />`, document.getElementById('app'));
  const schema = await api('/schema');
  update({ schema });
  await Promise.all([refreshDevices(), refreshCounts(), api('/events?limit=50').then((recent) => update({ recent }))]);
  await refreshSignals();
  connect();
}

boot().catch((error) => {
  document.getElementById('app').innerHTML = `<div class="empty-state">Cannot reach the hub API: ${error.message}</div>`;
});
