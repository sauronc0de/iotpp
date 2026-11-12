// Renders any decoded DEX value from its schema type (see /api/schema).
import { html } from '/vendor/preact.js';
import { EnumBadges, Badge } from './ui.js';
import { formatNumber } from '../schema.js';

export function Num({ value, unit, scaled }) {
  const f = formatNumber(value, unit, scaled);
  return html`<span class="num">${f.text}${f.unit ? html`<span class="unit">${f.unit}</span>` : null}</span>`;
}

export function Value({ type, value, field = {} }) {
  if (!type) return html`<code>${JSON.stringify(value)}</code>`;
  switch (type.kind) {
    case 'struct':
      return html`<${Struct} type=${type} value=${value} />`;
    case 'array':
      return html`<${ArrayValue} type=${type} value=${value} field=${field} />`;
    case 'enum':
    case 'flags':
      return html`<${EnumBadges} enumName=${type.enum} value=${value} />`;
    case 'bool':
      return value ? html`<${Badge} tone="ok">Yes<//>` : html`<${Badge}>No<//>`;
    case 'string':
      return value ? html`<code>${value}</code>` : html`<span class="faint">—</span>`;
    default:
      return html`<${Num} value=${value} unit=${field.unit} scaled=${!!field.scale} />`;
  }
}

function Struct({ type, value }) {
  if (!type.fields.length) return html`<span class="faint">No fields</span>`;
  const simple = type.fields.filter((f) => !['struct', 'array'].includes(f.type.kind));
  const nested = type.fields.filter((f) => ['struct', 'array'].includes(f.type.kind));
  return html`<div class="stack">
    ${simple.length ? html`<dl class="kv">${simple.map((f) => html`
      <dt title=${f.doc || ''}>${f.label}</dt>
      <dd><${Value} type=${f.type} value=${value?.[f.name]} field=${f} /></dd>`)}</dl>` : null}
    ${nested.map((f) => f.type.kind === 'array'
      ? html`<${Value} type=${f.type} value=${value?.[f.name]} field=${f} />`
      : html`<div class="subcard"><h4>${f.label}</h4><${Value} type=${f.type} value=${value?.[f.name]} field=${f} /></div>`)}
  </div>`;
}

function ArrayValue({ type, value = [], field }) {
  if (type.items.kind === 'struct')
    return html`<div class="subcards">${(value || []).map((item, i) => html`
      <div class="subcard"><h4>${field.label || 'Item'} ${i + 1}</h4><${Value} type=${type.items} value=${item} /></div>`)}</div>`;
  return html`<span class="chips">${(value || []).map((item) => html`<${Value} type=${type.items} value=${item} field=${field} />`)}</span>`;
}

// Progress bars for "x of total" pairs are a nice touch for any status with them.
export function progressOf(value) {
  if (!value || typeof value.downloaded !== 'number' || !value.total) return null;
  return Math.min(100, (100 * value.downloaded) / value.total);
}
