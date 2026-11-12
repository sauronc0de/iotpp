import { html, useEffect, useState } from '/vendor/preact.js';
import { Icon } from './icons.js';
import { toneOf } from '../schema.js';

export const Badge = ({ tone = 'neutral', children, title }) =>
  html`<span class=${`badge tone-${tone}`} title=${title}>${children}</span>`;

// Enum value (string) or flags (array of names).
export function EnumBadges({ enumName, value, empty = 'None' }) {
  const items = Array.isArray(value) ? value : value == null ? [] : [value];
  if (!items.length) return html`<span class="faint">${empty}</span>`;
  return html`<span class="chips">${items.map((v) => html`<${Badge} tone=${toneOf(enumName, v)}>${v}<//>`)}</span>`;
}

export const StatusBadges = ({ status }) => status ? html`<span class="chips">
    <${EnumBadges} enumName="State" value=${status.state} empty="—" />
    ${status.errors?.length ? html`<${EnumBadges} enumName="Error" value=${status.errors} />` : null}
    ${status.notifications?.length ? html`<${EnumBadges} enumName="Notification" value=${status.notifications} />` : null}
  </span>` : html`<span class="faint">—</span>`;

export function Card({ title, icon, actions, children, class: cls = '' }) {
  return html`<section class=${`card ${cls}`}>
    ${title || actions ? html`<div class="card-head">
      ${icon ? html`<${Icon} name=${icon} />` : null}
      ${title ? html`<h2>${title}</h2>` : null}
      <div class="spacer"></div>${actions}
    </div>` : null}
    ${children}
  </section>`;
}

export function Button({ icon, children, onClick, kind = '', disabled, title, busy }) {
  const [running, setRunning] = useState(false);
  const click = async (e) => {
    if (!onClick) return;
    const result = onClick(e);
    if (result?.then) {
      setRunning(true);
      try { await result; } finally { setRunning(false); }
    }
  };
  const spinning = running || busy;
  return html`<button class=${`btn ${kind} ${!children ? 'icon' : ''}`} onClick=${click} disabled=${disabled || spinning} title=${title}>
    ${spinning ? html`<span class="spinner"></span>` : icon ? html`<${Icon} name=${icon} size=${16} />` : null}${children}
  </button>`;
}

export function Segmented({ options, value, onChange }) {
  return html`<div class="seg">${options.map(([v, label]) =>
    html`<button class=${v === value ? 'on' : ''} onClick=${() => onChange(v)}>${label}</button>`)}</div>`;
}

export function Tabs({ tabs, value, onChange }) {
  return html`<div class="tabs">${tabs.map(([v, label]) =>
    html`<button class=${v === value ? 'on' : ''} onClick=${() => onChange(v)}>${label}</button>`)}</div>`;
}

export function Toggle({ checked, onChange, disabled }) {
  return html`<label class="switch"><input type="checkbox" checked=${!!checked} disabled=${disabled}
    onChange=${(e) => onChange(e.target.checked)} /><span></span></label>`;
}

export const Empty = ({ icon = 'info', title, children }) => html`<div class="empty-state">
  <${Icon} name=${icon} size=${36} stroke=${1.5} /><strong>${title}</strong><div class="small">${children}</div></div>`;

export function Modal({ title, children, onClose, actions }) {
  useEffect(() => {
    const key = (e) => e.key === 'Escape' && onClose();
    addEventListener('keydown', key);
    return () => removeEventListener('keydown', key);
  }, []);
  return html`<div class="overlay" onClick=${(e) => e.target === e.currentTarget && onClose()}>
    <div class="modal"><h2>${title}</h2>${children}<div class="row" style="justify-content:flex-end">${actions}</div></div>
  </div>`;
}

// Ask before destructive actions.
export function useConfirm() {
  const [ask, setAsk] = useState(null);
  const confirm = (title, text, action, label = 'Confirm') => setAsk({ title, text, action, label });
  const dialog = ask && html`<${Modal} title=${ask.title} onClose=${() => setAsk(null)} actions=${[
    html`<${Button} kind="ghost" onClick=${() => setAsk(null)}>Cancel<//>`,
    html`<${Button} kind="danger solid" onClick=${async () => { await ask.action(); setAsk(null); }}>${ask.label}<//>`,
  ]}><p class="muted">${ask.text}</p><//>`;
  return [confirm, dialog];
}

// Re-render every `ms` (relative times).
export function useTicker(ms = 5000) {
  const [, set] = useState(0);
  useEffect(() => { const id = setInterval(() => set((v) => v + 1), ms); return () => clearInterval(id); }, [ms]);
}
