// Hash router: #/path/param?query=1
import { useEffect, useState } from '/vendor/preact.js';

export function parse() {
  const [path, query = ''] = location.hash.replace(/^#/, '').split('?');
  return { parts: path.split('/').filter(Boolean).map(decodeURIComponent), query: new URLSearchParams(query) };
}

export function useRoute() {
  const [route, setRoute] = useState(parse());
  useEffect(() => {
    const onChange = () => setRoute(parse());
    addEventListener('hashchange', onChange);
    return () => removeEventListener('hashchange', onChange);
  }, []);
  return route;
}

export function navigate(path, query) {
  const q = query && Object.keys(query).length ? '?' + new URLSearchParams(query) : '';
  location.hash = '#' + path + q;
}

// Update the query string without adding history entries.
export function setQuery(values) {
  const { parts, query } = parse();
  for (const [k, v] of Object.entries(values)) v == null || v === '' ? query.delete(k) : query.set(k, v);
  const q = query.toString();
  history.replaceState(null, '', '#/' + parts.map(encodeURIComponent).join('/') + (q ? '?' + q : ''));
}
