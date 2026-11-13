// Generic editor for any DEX payload type. Values are engineering units; the
// hub converts and validates them against the same schema.
import { html } from '/vendor/preact.js';
import { Toggle } from './ui.js';
import { enumDef } from '../schema.js';

const setIn = (object, key, value) => (Array.isArray(object)
  ? object.map((v, i) => (i === key ? value : v))
  : { ...(object || {}), [key]: value });

export function SchemaForm({ type, value, onChange, field = {}, disabled }) {
  switch (type.kind) {
    case 'struct':
      return html`<div>${type.fields.map((f) => {
        const child = html`<${SchemaForm} type=${f.type} value=${value?.[f.name]} field=${f}
          disabled=${disabled || f.readonly} onChange=${(v) => onChange(setIn(value, f.name, v))} />`;
        return ['struct', 'array'].includes(f.type.kind)
          ? html`<fieldset><legend>${f.label}${f.readonly ? ' · read-only' : ''}</legend>
              ${f.doc ? html`<span class="hint">${f.doc}</span>` : null}${child}</fieldset>`
          : html`<div class="field"><label>${f.label}${f.doc ? html`<span class="hint">${f.doc}</span>` : null}</label>${child}</div>`;
      })}</div>`;
    case 'array':
      return html`<div>${(value || []).map((item, i) => html`<fieldset><legend>${field.label} ${i + 1}</legend>
        <${SchemaForm} type=${type.items} value=${item} field=${field} disabled=${disabled}
          onChange=${(v) => onChange(setIn(value, i, v))} /></fieldset>`)}</div>`;
    case 'bool':
      return html`<${Toggle} checked=${value} disabled=${disabled} onChange=${onChange} />`;
    case 'enum':
      return html`<select value=${value} disabled=${disabled} onChange=${(e) => onChange(e.target.value)}>
        ${enumDef(type.enum).values.map((v) => html`<option value=${v.name}>${v.name}</option>`)}</select>`;
    case 'flags': {
      const set = new Set(value || []);
      return html`<div class="chips">${enumDef(type.enum).values.filter((v) => v.value).map((v) => html`
        <label class="badge tone-${set.has(v.name) ? 'accent' : 'neutral'}" style="cursor:pointer">
          <input type="checkbox" checked=${set.has(v.name)} disabled=${disabled} onChange=${(e) => {
            const next = new Set(set); e.target.checked ? next.add(v.name) : next.delete(v.name); onChange([...next]);
          }} />${v.name}</label>`)}</div>`;
    }
    case 'string':
      return html`<input value=${value || ''} maxlength=${type.maxLength} disabled=${disabled}
        onInput=${(e) => onChange(e.target.value)} />`;
    default: {
      const min = field.min ?? type.min, max = field.max ?? type.max;
      return html`<div class="input-unit">
        <input type="number" value=${value} min=${min} max=${max} step=${type.step ?? 'any'} disabled=${disabled}
          onInput=${(e) => e.target.value !== '' && onChange(Number(e.target.value))} />
        ${field.unit ? html`<span class="unit">${field.unit}</span>` : null}
        ${Number.isFinite(min) && Number.isFinite(max) && max - min < 1e9
          ? html`<span class="faint small">${min} … ${max}</span>` : null}
      </div>`;
    }
  }
}

// Default value for a schema type (used when no current value exists).
export function defaultOf(type) {
  switch (type.kind) {
    case 'struct': return Object.fromEntries(type.fields.map((f) => [f.name, defaultOf(f.type)]));
    case 'array': return Array.from({ length: type.size }, () => defaultOf(type.items));
    case 'bool': return false;
    case 'enum': return enumDef(type.enum).values[0]?.name;
    case 'flags': return [];
    case 'string': return '';
    default: return 0;
  }
}
