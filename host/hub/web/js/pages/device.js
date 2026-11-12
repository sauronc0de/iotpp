// Device detail. Every panel is generated from the peripheral type schema:
// a new DEX peripheral (status/config/action/signals) shows up here as-is.
import { html, useEffect, useState } from '/vendor/preact.js';
import { useStore } from '../store.js';
import { api } from '../api.js';
import { navigate, parse, setQuery } from '../router.js';
import { Icon } from '../components/icons.js';
import { Badge, Button, Card, EnumBadges, Empty, Segmented, StatusBadges, Tabs, useConfirm, useTicker } from '../components/ui.js';
import { Value, progressOf } from '../components/value.js';
import { SchemaForm, defaultOf } from '../components/form.js';
import { rangeOf } from '../components/chart.js';
import { ago, typeInfo, signalsOf, peripheralLabel } from '../schema.js';
import { LiveTile, SignalCharts, run } from './common.js';

const humanize = (name) => name.replace(/([a-z])([A-Z])/g, '$1 $2').replace(/^./, (c) => c.toUpperCase());

function Header({ device }) {
  const [editing, setEditing] = useState(false);
  const [name, setName] = useState(device.name);
  const [confirm, dialog] = useConfirm();
  const save = async () => {
    await api(`/devices/${encodeURIComponent(device.key)}`, { method: 'PATCH', body: { name } });
    setEditing(false);
  };
  return html`<div class="card">
    <div class="row between">
      <div class="row">
        <span class=${`dot ${device.online ? 'on' : 'off'}`}></span>
        ${editing ? html`<input value=${name} onInput=${(e) => setName(e.target.value)} onKeyDown=${(e) => e.key === 'Enter' && save()} autofocus />
          <${Button} icon="check" onClick=${save} />`
        : html`<h1>${device.name || device.key}</h1>
          <button class="btn ghost icon" title="Rename" onClick=${() => { setName(device.name); setEditing(true); }}><${Icon} name="edit" size=${15} /></button>`}
        <${StatusBadges} status=${device.status} />
      </div>
      <div class="row">
        <${Button} icon="refresh" onClick=${() => api(`/devices/${encodeURIComponent(device.key)}/refresh`, { method: 'POST' })}>Refresh all<//>
        <${Button} icon="info" onClick=${() => run(device.key, 'Info', null, undefined, 'Info updated')}>Info<//>
        <${Button} icon="activity" onClick=${() => run(device.key, 'Status', null, undefined, 'Status updated')}>Status<//>
        <${Button} icon="power" kind="danger" onClick=${() => confirm('Reset device?', `${device.name || device.key} will restart now.`,
          () => run(device.key, 'Reset', null, undefined, 'Reset sent'), 'Reset')}>Reset<//>
        <${Button} icon="trash" kind="ghost" title="Forget device" onClick=${() => confirm('Forget device?',
          'It is removed from the hub (stored samples are kept). It reappears if it publishes again.',
          async () => { await api(`/devices/${encodeURIComponent(device.key)}`, { method: 'DELETE' }); navigate('/devices'); }, 'Forget')} />
      </div>
    </div>
    <div class="row small muted" style="margin-top:10px;gap:22px">
      <span class="mono">${device.host}:${device.port}</span>
      <span>Firmware <strong>${device.firmware || '—'}</strong></span>
      <span>Seen ${ago(device.lastSeen)}</span>
      <span>${device.publications.toLocaleString()} publications · ${device.samples.toLocaleString()} samples</span>
      ${device.failures ? html`<${Badge} tone="warn">${device.failures} failed polls<//>` : null}
    </div>
    ${dialog}
  </div>`;
}

function StatusCard({ device, p, type }) {
  const schema = useStore().schema;
  const statusType = type?.status || schema.peripheralStatus;
  const progress = progressOf(p.status);
  return html`<${Card} title="Status" icon="activity" actions=${html`
      ${p.statusAt ? html`<span class="small faint">${ago(p.statusAt)}</span>` : null}
      <${Button} icon="refresh" title="Read status" onClick=${() => run(device.key, 'PeripheralStatus', p.id)} />
      <${Button} icon="pause" title="Pause" onClick=${() => run(device.key, 'PeripheralPause', p.id, undefined, 'Paused')} />
      <${Button} icon="play" title="Resume" onClick=${() => run(device.key, 'PeripheralResume', p.id, undefined, 'Resumed')} />`}>
    <div class="stack">
      <div class="row small">
        <span class="muted">Version ${p.version}</span>
        <${EnumBadges} enumName=${schema.capabilities} value=${p.capabilities} />
        ${p.lastError ? html`<${Badge} tone="err" title="Last command result">${p.lastError}<//>` : null}
      </div>
      ${progress != null && progress < 100 && p.status?.total ? html`<div><div class="row between small"><span>Progress</span><span>${progress.toFixed(0)}%</span></div>
        <div class="progress"><div style=${`width:${progress}%`}></div></div></div>` : null}
      ${p.status ? html`<${Value} type=${statusType} value=${p.status} />`
        : html`<span class="faint">${p.statusUnsupported ? 'This peripheral does not report its status.' : 'No status yet.'}</span>`}
    </div>
  <//>`;
}

function ConfigCard({ device, p, type }) {
  const [draft, setDraft] = useState(p.config);
  const [dirty, setDirty] = useState(false);
  useEffect(() => { if (!dirty) setDraft(p.config); }, [JSON.stringify(p.config)]);
  const apply = async () => {
    const result = await run(device.key, 'PeripheralConfig', p.id, draft, 'Configuration applied');
    if (result.ok) setDirty(false);
  };
  return html`<${Card} title="Configuration" icon="settings" actions=${html`
      ${p.configAt ? html`<span class="small faint">read ${ago(p.configAt)}</span>` : null}
      <${Button} icon="download" onClick=${async () => { setDirty(false); await run(device.key, 'PeripheralConfig', p.id); }}>Read<//>`}>
    ${draft == null ? html`<span class="faint">${p.configUnsupported ? 'Configuration not supported by the device.' : 'Not read yet.'}</span>`
    : html`<${SchemaForm} type=${type.config} value=${draft} onChange=${(v) => { setDraft(v); setDirty(true); }} />
      <div class="row" style="justify-content:flex-end;margin-top:12px">
        ${dirty ? html`<${Button} kind="ghost" onClick=${() => { setDraft(p.config); setDirty(false); }}>Discard<//>` : null}
        <${Button} kind="primary" icon="send" disabled=${!dirty} onClick=${apply}>Apply<//>
      </div>`}
  <//>`;
}

function ActionsCard({ device, p, type }) {
  const [draft, setDraft] = useState(type.actionDefault ?? defaultOf(type.action));
  const fields = type.action.fields || [];
  const enumField = fields.length === 1 && fields[0].type.kind === 'enum' ? fields[0] : null;
  const schema = useStore().schema;
  return html`<${Card} title="Actions" icon="zap">
    ${enumField ? html`<div class="row">${schema.enums[enumField.type.enum].values.map((v) => html`
        <${Button} kind="primary big" icon="zap" onClick=${() => run(device.key, 'PeripheralAction', p.id, { [enumField.name]: v.name }, `${humanize(v.name)} started`)}>
          ${humanize(v.name)}<//>`)}</div>`
    : html`<${SchemaForm} type=${type.action} value=${draft} onChange=${setDraft} />
      <div class="row" style="justify-content:flex-end"><${Button} kind="primary" icon="zap"
        onClick=${() => run(device.key, 'PeripheralAction', p.id, draft, 'Action executed')}>Execute<//></div>`}
  <//>`;
}

function MonitoringCard({ device, p }) {
  useStore();
  const [range, setRange] = useState(parse().query.get('range') || '5m');
  const [hidden, setHidden] = useState(new Set());
  const signals = signalsOf(device.key, p.id);
  const shown = signals.filter((s) => !hidden.has(s.id));
  const toggle = (id) => setHidden((h) => { const n = new Set(h); n.has(id) ? n.delete(id) : n.add(id); return n; });
  return html`<${Card} title="Monitoring" icon="activity" actions=${html`
      ${p.monitoringAt ? html`<span class="small faint">last publication ${ago(p.monitoringAt)}</span>` : null}
      <${Segmented} value=${range} onChange=${(r) => { setRange(r); setQuery({ range: r }); }}
        options=${[['5m', 'Live'], ['15m', '15m'], ['1h', '1h'], ['6h', '6h'], ['24h', '24h'], ['7d', '7d']]} />`}>
    ${signals.length ? html`<div class="stack">
        <div class="live-values">${signals.map((s) => html`<${LiveTile} signal=${s} selected=${!hidden.has(s.id)} onClick=${() => toggle(s.id)} />`)}</div>
        <div class="small faint">Click a value to hide/show it in the charts. Drag on a chart to zoom, double-click to reset.</div>
        <${SignalCharts} signals=${shown} range=${rangeOf(range)} compactLabels=${true} />
      </div>`
    : html`<${Empty} icon="activity" title="Waiting for publications">Signals appear when this peripheral publishes monitoring data.<//>`}
  <//>`;
}

function PeripheralPanel({ device, p }) {
  const type = typeInfo(p.type);
  if (!type)
    return html`<${Card} title=${peripheralLabel(p)}><${Empty} icon="alert" title="Unknown peripheral type">
      Type ${p.type ?? '?'} is not described in this hub build. Add it to <code>dex::Peripherals</code> and rebuild.<//>
      <pre class="code">${JSON.stringify(p, null, 2)}</pre><//>`;
  return html`<div class="stack">
    <div class="row muted small"><strong>${type.name}</strong> — ${type.description}</div>
    <div class="grid two">
      <${StatusCard} device=${device} p=${p} type=${type} />
      <div class="stack">
        ${type.features.config ? html`<${ConfigCard} key=${`${device.key}-${p.id}`} device=${device} p=${p} type=${type} />` : null}
        ${type.features.action ? html`<${ActionsCard} device=${device} p=${p} type=${type} />` : null}
      </div>
    </div>
    ${type.features.monitoring ? html`<${MonitoringCard} key=${`${device.key}-${p.id}-m`} device=${device} p=${p} />` : null}
  </div>`;
}

export function DevicePage({ deviceKey }) {
  const s = useStore();
  useTicker(5000);
  const device = s.devices[deviceKey];
  const [tab, setTab] = useState(Number(parse().query.get('p')) || null);
  if (!device) return html`<${Card}><${Empty} icon="cpu" title="Unknown device">${deviceKey}<//><//>`;
  const current = device.peripherals.find((p) => p.id === tab) || device.peripherals[0];
  return html`
    <${Header} device=${device} />
    ${device.peripherals.length ? html`
      <${Tabs} value=${current?.id} onChange=${(id) => { setTab(id); setQuery({ p: id }); }}
        tabs=${device.peripherals.map((p) => [p.id, html`<${Icon} name="layers" size=${15} />${peripheralLabel(p)}
          ${p.status?.notifications?.length ? html`<span class="count">!</span>` : null}`])} />
      ${current ? html`<${PeripheralPanel} key=${current.id} device=${device} p=${current} />` : null}`
    : html`<${Card}><${Empty} icon="layers" title="No peripherals">Query Info to list this device's peripherals.<//><//>`}`;
}
