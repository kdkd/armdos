// A small, forgiving HTML parser for ARM-DOS Online (no DOM needed, so it runs in node
// tests and in the page alike). It builds a light tree:
//   { tag, attrs: {name: value}, children: [...] }   elements (tag lower case)
//   'text'                                             text nodes (entities decoded)
// Mis-nested closing tags close up to the nearest matching open element; stray ones are
// ignored. Good enough for MediaWiki's parser output and Hacker News comment HTML.

const VOID = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'source', 'track', 'wbr', 'param']);
const RAW = new Set(['script', 'style', 'textarea', 'title']);
// elements a new <p>/<li>/... implicitly closes, as browsers do (a simplified version)
const AUTOCLOSE = { p: ['p'], li: ['li'], dt: ['dt', 'dd'], dd: ['dt', 'dd'], tr: ['tr', 'td', 'th'], td: ['td', 'th'], th: ['td', 'th'], option: ['option'] };
const BLOCKS_CLOSE_P = new Set(['div', 'ul', 'ol', 'dl', 'table', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'pre', 'blockquote', 'figure', 'p', 'hr', 'section']);

const NAMED = {
  amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ', ndash: '–', mdash: '—', hellip: '…', lsquo: '‘', rsquo: '’',
  ldquo: '“', rdquo: '”', laquo: '«', raquo: '»', copy: '©', reg: '®', trade: '™', deg: '°', plusmn: '±', times: '×', divide: '÷',
  micro: 'µ', middot: '·', bull: '•', para: '¶', sect: '§', frac12: '½', frac14: '¼', frac34: '¾', sup2: '²', sup3: '³', minus: '−',
  pound: '£', euro: '€', yen: '¥', cent: '¢', shy: '', zwj: '', zwnj: '', thinsp: ' ', ensp: ' ', emsp: ' ', prime: '′', Prime: '″',
  larr: '←', rarr: '→', harr: '↔', le: '≤', ge: '≥', ne: '≠', asymp: '≈', infin: '∞', alpha: 'α', beta: 'β', gamma: 'γ', delta: 'δ',
  pi: 'π', sigma: 'σ', mu: 'μ', omega: 'ω', Omega: 'Ω', Sigma: 'Σ', Delta: 'Δ', theta: 'θ', lambda: 'λ', eacute: 'é', egrave: 'è',
  aacute: 'á', agrave: 'à', iacute: 'í', oacute: 'ó', uacute: 'ú', ntilde: 'ñ', ouml: 'ö', uuml: 'ü', auml: 'ä', szlig: 'ß',
  ccedil: 'ç', Eacute: 'É', Ouml: 'Ö', Uuml: 'Ü', Auml: 'Ä', aring: 'å', Aring: 'Å', aelig: 'æ', AElig: 'Æ', oslash: 'ø', iexcl: '¡', iquest: '¿',
};

export function decodeEntities(s) {
  if (s.indexOf('&') < 0) return s;
  return s.replace(/&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);?/g, (m, e) => {
    if (e[0] === '#') {
      const c = e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10);
      return c > 0 && c < 0x110000 ? String.fromCodePoint(c) : '';
    }
    return NAMED[e] !== undefined ? NAMED[e] : m;
  });
}

function parseAttrs(s) {
  const attrs = {};
  const re = /([^\s"'>\/=]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
  let m;
  while ((m = re.exec(s))) attrs[m[1].toLowerCase()] = decodeEntities(m[2] ?? m[3] ?? m[4] ?? '');
  return attrs;
}

/** HTML string -> root element { tag: '#root', children }. */
export function parseHTML(html) {
  const root = { tag: '#root', attrs: {}, children: [] };
  const stack = [root];
  const top = () => stack[stack.length - 1];
  const closeTo = (i) => { stack.length = i; };
  const n = html.length;
  let i = 0;
  while (i < n) {
    const lt = html.indexOf('<', i);
    if (lt < 0) { top().children.push(decodeEntities(html.slice(i))); break; }
    if (lt > i) top().children.push(decodeEntities(html.slice(i, lt)));
    if (html.startsWith('<!--', lt)) { const e = html.indexOf('-->', lt + 4); i = e < 0 ? n : e + 3; continue; }
    if (html[lt + 1] === '!' || html[lt + 1] === '?') { const e = html.indexOf('>', lt); i = e < 0 ? n : e + 1; continue; }
    const close = html[lt + 1] === '/';
    const m = /^<\/?([A-Za-z][A-Za-z0-9:-]*)/.exec(html.slice(lt, lt + 40));
    if (!m) { top().children.push('<'); i = lt + 1; continue; }
    // find the end of the tag, skipping quoted attribute values
    let j = lt + m[0].length, q = '';
    while (j < n) { const c = html[j]; if (q) { if (c === q) q = ''; } else if (c === '"' || c === "'") q = c; else if (c === '>') break; j++; }
    const tag = m[1].toLowerCase();
    const inner = html.slice(lt + m[0].length, j);
    i = j + 1;
    if (close) {
      for (let k = stack.length - 1; k > 0; k--) if (stack[k].tag === tag) { closeTo(k); break; }
      continue;
    }
    const el = { tag, attrs: parseAttrs(inner), children: [] };
    const ac = AUTOCLOSE[tag];
    if (ac && ac.includes(top().tag)) stack.pop();
    else if (BLOCKS_CLOSE_P.has(tag) && top().tag === 'p') stack.pop();
    top().children.push(el);
    if (VOID.has(tag) || inner.endsWith('/')) continue;
    if (RAW.has(tag)) {
      let e = html.indexOf('</' + tag, i);
      if (e < 0) e = html.toLowerCase().indexOf('</' + tag, i);
      const body = html.slice(i, e < 0 ? n : e);
      if (body) el.children.push(tag === 'textarea' || tag === 'title' ? decodeEntities(body) : body);
      const gt = e < 0 ? n : html.indexOf('>', e);
      i = gt < 0 ? n : gt + 1;
      continue;
    }
    stack.push(el);
  }
  return root;
}

export const hasClass = (el, c) => typeof el === 'object' && (' ' + (el.attrs.class || '') + ' ').includes(' ' + c + ' ');
/** All text inside a node (no layout). */
export function textOf(node) {
  if (typeof node === 'string') return node;
  if (node.tag === 'script' || node.tag === 'style') return '';
  if (node.tag === 'br') return '\n';
  let s = '';
  for (const c of node.children) s += textOf(c);
  return s;
}
/** Depth-first search for elements matching pred. */
export function findAll(node, pred, out = []) {
  if (typeof node !== 'object') return out;
  for (const c of node.children) {
    if (typeof c !== 'object') continue;
    if (pred(c)) out.push(c);
    findAll(c, pred, out);
  }
  return out;
}
export const find = (node, pred) => findAll(node, pred)[0] || null;
