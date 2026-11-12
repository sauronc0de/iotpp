// Highly visible notification feedback: animated toasts, screen-edge flash,
// ringing bell, sound and (when the tab is hidden) a desktop notification.
import { html } from '/vendor/preact.js';
import { state, update, useStore } from '../store.js';
import { SEVERITY } from '../schema.js';
import { Icon } from './icons.js';

let audio = null;
function chime(severity) {
  try {
    audio ||= new AudioContext();
    const notes = severity === 'notification' ? [880, 1318.5, 1760] : severity === 'error' ? [440, 330] : [660, 520];
    notes.forEach((freq, i) => {
      const osc = audio.createOscillator(), gain = audio.createGain();
      const start = audio.currentTime + i * 0.13;
      osc.type = severity === 'error' ? 'square' : 'sine';
      osc.frequency.value = freq;
      gain.gain.setValueAtTime(0.0001, start);
      gain.gain.exponentialRampToValueAtTime(severity === 'error' ? 0.05 : 0.18, start + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.0001, start + 0.45);
      osc.connect(gain).connect(audio.destination);
      osc.start(start);
      osc.stop(start + 0.5);
    });
  } catch { /* Audio needs a user gesture first; ignore. */ }
}

function flash(tone) {
  const div = document.createElement('div');
  div.className = 'screen-flash';
  div.style.setProperty('--flash', `var(--${tone})`);
  document.body.appendChild(div);
  setTimeout(() => div.remove(), 1700);
}

export function dismiss(key) {
  update((s) => ({ toasts: s.toasts.map((t) => (t.key === key ? { ...t, leaving: true } : t)) }));
  setTimeout(() => update((s) => ({ toasts: s.toasts.filter((t) => t.key !== key) })), 300);
}

let seq = 0;
export function toast(event) {
  const key = ++seq;
  const sticky = event.severity === 'notification' || event.severity === 'error';
  const ttl = event.ttl ?? (sticky ? 12000 : 5000);
  update((s) => ({ toasts: [...s.toasts.slice(-4), { ...event, key, ttl }] }));
  setTimeout(() => dismiss(key), ttl);
}

export function notifyEvent(event) {
  toast(event);
  if (!['notification', 'error', 'warning'].includes(event.severity)) return;
  const tone = (SEVERITY[event.severity] || SEVERITY.info).tone;
  flash(tone);
  update({ ringing: Date.now() });
  setTimeout(() => update({ ringing: 0 }), 2000);
  if (!state.muted) chime(event.severity);
  if (document.hidden && 'Notification' in window && Notification.permission === 'granted')
    new Notification(`${(SEVERITY[event.severity] || {}).label}: ${event.title}`, { body: event.detail, tag: String(event.id) });
}

export function Toasts() {
  const { toasts } = useStore();
  return html`<div class="toasts">${toasts.map((t) => {
    const sev = SEVERITY[t.severity] || SEVERITY.info;
    return html`<div key=${t.key} class=${`toast ${t.severity} ${t.leaving ? 'leaving' : ''}`}
        style=${`--tone: var(--${sev.tone})`} onClick=${() => { location.hash = '#/events'; dismiss(t.key); }}>
      <div class="ico"><${Icon} name=${sev.icon} size=${20} /></div>
      <div><div class="t">${t.title}</div><div class="d">${t.detail}</div></div>
      <button class="btn ghost icon" onClick=${(e) => { e.stopPropagation(); dismiss(t.key); }}><${Icon} name="x" size=${14} /></button>
      <div class="bar" style=${`animation-duration:${t.ttl}ms`}></div>
    </div>`;
  })}</div>`;
}
