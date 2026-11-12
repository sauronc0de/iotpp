// DEX console (any command, schema-generated or raw payloads) and a protocol
// reference generated from the very schema the hub was compiled with.
import { html, useState } from '/vendor/preact.js';
import { useStore } from '../store.js';
import { command, rawCommand } from '../api.js';
import { Badge, Button, Card, Empty, Segmented, Tabs, Toggle } from '../components/ui.js';
import { Value } from '../components/value.js';
import { SchemaForm, defaultOf } from '../components/form.js';
import { typeInfo, commandDef } from '../schema.js';
import { devicesSorted } from './common.js';

const hex = (n, w = 2) => '0x' + Number(n).toString(16).padStart(w, '0').toUpperCase();

function Console() {
  const s = useStore();
  const devices = devicesSorted();
  const [chosen, setKey] = useState('');
  const key = s.devices[chosen] ? chosen : devices[0]?.key || '';
  const [cmd, setCmd] = useState('Info');
  const [pid, setPid] = useState(null);
  const [write, setWrite] = useState(false);
  const [raw, setRaw] = useState(false);
  const [payload, setPayload] = useState('');
  const [draft, setDraft] = useState(null);
  const [log, setLog] = useState([]);
  const device = s.devices[key];
  const def = commandDef(cmd);
  const peripheral = device?.peripherals.find((p) => p.id === pid) || (def?.peripheral ? device?.peripherals[0] : null);
  const type = peripheral ? typeInfo(peripheral.type) : null;
  const payloadType = def?.payload === 'config' ? (def.peripheral ? type?.config : s.schema.device.config)
    : def?.payload === 'action' ? type?.action : null;
  const needsWrite = def?.payload === 'config';
  const showForm = !raw && payloadType && (!needsWrite || write);
  const value = draft ?? (def?.payload === 'config' ? peripheral?.config : def?.payload === 'action' ? type?.actionDefault : null)
    ?? (payloadType ? defaultOf(payloadType) : null);

  const send = async () => {
    const started = new Date();
    let result;
    try {
      result = raw ? await rawCommand(key, cmd, payload)
        : await command(key, cmd, def.peripheral ? peripheral?.id : null, showForm ? value : undefined);
    } catch (error) { result = { ok: false, error: error.message, command: cmd }; }
    setLog((l) => [{ ...result, at: started, device: key, typeId: peripheral?.type, cmd }, ...l].slice(0, 30));
  };
  const responseType = (entry) => {
    const t = typeInfo(entry.typeId);
    return { Status: s.schema.device.status, PeripheralStatus: t?.status, PeripheralConfig: t?.config, PeripheralMonitoring: t?.monitoring }[entry.cmd];
  };

  if (!devices.length) return html`<${Card}><${Empty} icon="terminal" title="No devices to talk to" /><//>`;
  return html`<div class="grid two" style="align-items:start">
    <${Card} title="Request" icon="send">
      <div class="field"><label>Device</label><select value=${key} onChange=${(e) => { setKey(e.target.value); setDraft(null); }}>
        ${devices.map((d) => html`<option value=${d.key}>${d.name || d.key} (${d.host})</option>`)}</select></div>
      <div class="field"><label>Command</label><select value=${cmd} onChange=${(e) => { setCmd(e.target.value); setDraft(null); setWrite(false); }}>
        ${s.schema.commands.map((c) => html`<option value=${c.name}>${c.name} (${hex(c.value)})</option>`)}</select></div>
      ${def?.peripheral && !raw ? html`<div class="field"><label>Peripheral</label>
        <select value=${peripheral?.id} onChange=${(e) => { setPid(Number(e.target.value)); setDraft(null); }}>
          ${device?.peripherals.map((p) => html`<option value=${p.id}>${p.typeName || 'Unknown'} #${p.id}</option>`)}</select></div>` : null}
      ${needsWrite && !raw ? html`<div class="field"><label>Mode</label><${Segmented} value=${write} onChange=${setWrite}
        options=${[[false, 'Read (GET)'], [true, 'Write (SET)']]} /></div>` : null}
      <div class="field"><label>Raw payload<span class="hint">Hex DATA after CMD (includes peripheral id)</span></label>
        <${Toggle} checked=${raw} onChange=${setRaw} /></div>
      ${raw ? html`<div class="field"><label>DATA</label><input class="mono" value=${payload} placeholder="02 00" onInput=${(e) => setPayload(e.target.value)} /></div>` : null}
      ${showForm ? html`<fieldset><legend>Payload</legend><${SchemaForm} type=${payloadType} value=${value} onChange=${setDraft} /></fieldset>` : null}
      <div class="row" style="justify-content:flex-end;margin-top:12px">
        <${Button} kind="primary" icon="send" onClick=${send} disabled=${def?.peripheral && !raw && !peripheral}>Send<//></div>
    <//>
    <${Card} title="Exchanges" icon="terminal" actions=${log.length ? html`<button class="btn ghost small" onClick=${() => setLog([])}>Clear</button>` : null}>
      ${log.length ? html`<div class="stack">${log.map((e) => html`<div class="subcard stack" style="gap:8px">
        <div class="row between"><strong>${e.cmd}</strong>
          <span class="row small">${e.ok ? html`<${Badge} tone="ok">OK<//>` : html`<${Badge} tone="err">${e.timeout ? 'Timeout' : (e.result || []).join(' | ') || 'Error'}<//>`}
            <span class="faint">${e.elapsedMs ?? '—'} ms · ${e.at.toLocaleTimeString()}</span></span></div>
        ${e.request ? html`<div class="small"><span class="faint">→ </span><span class="hex">${e.request}</span></div>` : null}
        ${e.response ? html`<div class="small"><span class="faint">← </span><span class="hex">${e.response}</span></div>` : null}
        ${e.error || e.decodeError ? html`<div class="small" style="color:var(--err)">${e.error || e.decodeError}</div>` : null}
        ${e.data != null ? (responseType(e) ? html`<${Value} type=${responseType(e)} value=${e.data} />`
          : html`<pre class="code">${JSON.stringify(e.data, null, 2)}</pre>`) : null}
      </div>`)}</div>` : html`<${Empty} icon="terminal" title="Send a command">Requests and responses are shown here in hex and decoded.<//>`}
    <//>
  </div>`;
}

function TypeTree({ type, depth = 0 }) {
  if (!type) return html`<span class="faint">—</span>`;
  if (type.kind === 'struct')
    return html`<table class="small" style=${depth ? 'margin-top:4px' : ''}><tbody>${type.fields.map((f) => html`<tr>
      <td class="mono" style=${`padding-left:${10 + depth * 16}px`}>${f.name}</td>
      <td>${f.label}${f.doc ? html`<div class="faint">${f.doc}</div>` : null}</td>
      <td>${describe(f.type)}${f.unit ? html` <${Badge} tone="accent">${f.unit}<//>` : null}
        ${f.scale ? html` <span class="faint">×${f.scale}</span>` : null}
        ${f.min != null || f.max != null ? html` <span class="faint">[${f.min ?? '−∞'}, ${f.max ?? '∞'}]</span>` : null}
        ${['struct', 'array'].includes(f.type.kind) ? html`<${TypeTree} type=${f.type.kind === 'array' ? f.type.items : f.type} depth=${depth + 1} />` : null}</td>
    </tr>`)}</tbody></table>`;
  return describe(type);
}
const describe = (t) => t.kind === 'number' ? html`<code>${t.type}</code>` : t.kind === 'array' ? html`<code>array[${t.size}]</code>`
  : t.kind === 'string' ? html`<code>char[${t.maxLength + 1}]</code>` : t.enum ? html`<code>${t.kind} ${t.enum}</code>`
  : t.kind === 'struct' ? html`<code>${t.name} (${t.size} B)</code>` : html`<code>${t.kind}</code>`;

function Reference() {
  const { schema } = useStore();
  return html`<div class="stack" style="gap:16px">
    <${Card} title="Commands" icon="terminal"><table><thead><tr><th>CMD</th><th>Name</th><th>Scope</th><th>Request DATA</th></tr></thead>
      <tbody>${schema.commands.map((c) => html`<tr><td class="mono">${hex(c.value)}</td><td><strong>${c.name}</strong></td>
        <td>${c.peripheral ? 'Peripheral (id byte + DATA)' : 'Device'}</td><td>${c.payload}</td></tr>`)}</tbody></table><//>
    ${schema.types.map((t) => html`<${Card} title=${`${t.name} · type ${t.type}`} icon="layers"
        actions=${html`<span class="chips">${Object.entries(t.features).filter(([, v]) => v).map(([k]) => html`<${Badge} tone="accent">${k}<//>`)}</span>`}>
      <p class="muted" style="margin-top:0">${t.description}</p>
      <div class="grid two">
        ${['status', 'config', 'action'].filter((k) => t[k]).map((k) => html`<div><h3>${k}</h3><${TypeTree} type=${t[k]} /></div>`)}
        ${t.signals ? html`<div><h3>Monitoring signals (${t.signals.length})</h3><table class="small"><tbody>${t.signals.map((sig) =>
          html`<tr><td class="mono">${sig.key}</td><td>${sig.label}</td><td>${sig.unit || sig.enum || sig.kind}</td></tr>`)}</tbody></table></div>` : null}
      </div>
    <//>`)}
    <${Card} title="Enums" icon="book"><div class="subcards">${Object.entries(schema.enums).map(([name, e]) => html`<div class="subcard">
      <h4>${name}${e.flags ? ' · flags' : ''}</h4><table class="small"><tbody>${e.values.map((v) => html`<tr><td class="mono">${e.flags ? hex(v.value, 1) : v.value}</td><td>${v.name}</td></tr>`)}</tbody></table>
    </div>`)}</div><//>
  </div>`;
}

export function Protocol() {
  const [tab, setTab] = useState('console');
  return html`<${Tabs} value=${tab} onChange=${setTab} tabs=${[['console', 'Console'], ['reference', 'Reference']]} />
    ${tab === 'console' ? html`<${Console} />` : html`<${Reference} />`}`;
}
