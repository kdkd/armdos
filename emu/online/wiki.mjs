// Wikipedia article HTML (the action API's parser output) -> an ARM-DOS Online document:
// headings, paragraphs, lists, quotes, simple tables, the infobox as "quick facts" after
// the introduction, pictures as picture links, wiki links as numbered links. Footnote
// markers, navigation boxes, edit links, maintenance banners and the reference sections
// are left out. Stops at the document size limit and says what was left out.

import { parseHTML, hasClass, textOf, find, findAll } from './html.mjs';
import { Doc } from './doc.mjs';

const SKIP_CLASS = ['reference', 'mw-editsection', 'navbox', 'navbox-styles', 'metadata', 'ambox', 'mbox-small', 'sistersitebox',
  'noprint', 'reflist', 'references', 'mw-references-wrap', 'shortdescription', 'portalbox', 'side-box', 'sidebar', 'toc',
  'mw-jump-link', 'mw-cite-backlink', 'Z3988', 'authority-control', 'catlinks', 'printfooter', 'vertical-navbox', 'navbar',
  'mw-kartographer-maplink', 'box-Multiple_issues', 'dablink-empty', 'ext-phonos', 'mw-indicators', 'geo-nondefault', 'nomobile'];
const SKIP_TAG = new Set(['style', 'script', 'link', 'meta', 'noscript', 'input', 'button', 'svg', 'audio', 'video', 'map']);
const END_SECTIONS = /^(references|notes|footnotes|citations|sources|bibliography|external links|further reading|notes and references|references and notes|works cited|general references|explanatory notes|literature)$/i;
const BAD_NS = /^(File|Image|Help|Special|Category|Template|Template talk|Wikipedia|Portal|Talk|User|User talk|Draft|Module|MediaWiki|TimedText|Book):/i;

const skip = (el) => SKIP_TAG.has(el.tag) || SKIP_CLASS.some((c) => hasClass(el, c)) ||
  /display:\s*none/.test(el.attrs.style || '') || el.attrs.role === 'navigation' || el.attrs.typeof === 'mw:Extension/templatestyles';

/** "/wiki/ARM_Holdings#History" -> "ARM Holdings" (null for other namespaces). */
export function wikiTitle(href) {
  const m = /^(?:https?:)?(?:\/\/en\.wikipedia\.org)?\/wiki\/([^?#]+)/.exec(href || '') || /^\.\/([^?#]+)/.exec(href || '');
  if (!m) return null;
  let t;
  try { t = decodeURIComponent(m[1]); } catch { t = m[1]; }
  t = t.replace(/_/g, ' ');
  return BAD_NS.test(t) ? null : t;
}

/** Best image URL for a ~320 pixel display from an <img>. */
export function imageURL(img) {
  let src = img.attrs.src || '';
  const w = +img.attrs.width || 0;
  const set = (img.attrs.srcset || '').split(',').map((s) => s.trim().split(/\s+/)).filter((p) => p[0]);
  if (w < 300 && set.length) src = set[set.length - 1][0];
  if (src.startsWith('//')) src = 'https:' + src;
  return src || null;
}
const fileName = (a) => { const t = /\/wiki\/(File:[^?#]+)/.exec(a?.attrs.href || ''); try { return t ? decodeURIComponent(t[1]).replace(/_/g, ' ') : ''; } catch { return t[1]; } };

export function articleDoc({ title, html, maxBytes = 40 * 1024, description = '' }) {
  const doc = new Doc({ title, channel: 'Encyclopedia', kind: 'E', maxBytes });
  const root = parseHTML(html);
  const body = find(root, (e) => hasClass(e, 'mw-parser-output')) || root;
  doc.block({ tight: true });
  doc.text(title, 't');
  doc.end();
  doc.raw('═'.repeat(Math.min(76, doc.plain[doc.plain.length - 1].length)), 't');
  doc.end();
  const sd = find(body, (e) => hasClass(e, 'shortdescription'));
  const desc = description || (sd ? textOf(sd).trim() : '');
  if (desc) { doc.text(desc, 'k'); doc.end(); }
  const st = { doc, infobox: null, stopped: false, left: [], pictures: 0, headings: 0 };
  for (const c of body.children) { block(c, st, 0); if (st.stopped && st.left.length > 40) break; }
  flushInfobox(st);
  if (st.stopped) {
    doc.block();
    doc.text('(This article continues, but the rest is too long to send over the line.', 'k');
    if (st.left.length) doc.text(' Sections not shown: ' + st.left.slice(0, 12).join(', ') + (st.left.length > 12 ? ', ...' : '') + '.', 'k');
    doc.text(')', 'k');
    doc.end();
  }
  doc.block();
  doc.text('From Wikipedia, the free encyclopedia. Text under CC BY-SA 4.0.', 'k');
  doc.end();
  return doc;
}

function headingOf(el) {
  if (/^h[1-6]$/.test(el.tag)) return el;
  if (el.tag === 'div' && hasClass(el, 'mw-heading')) return find(el, (e) => /^h[1-6]$/.test(e.tag));
  return null;
}

function block(node, st, depth) {
  const doc = st.doc;
  if (typeof node === 'string') { if (node.trim()) { inline(node, st, 'n', 0); } return; }
  if (skip(node)) return;
  const h = headingOf(node);
  if (h) {
    const level = +h.tag[1];
    const text = textOf(h).trim();
    if (level <= 2 && END_SECTIONS.test(text)) { st.stopped = true; st.ended = true; return; }
    if (st.ended) return;
    if (st.stopped || doc.full) { st.stopped = true; if (level <= 2) st.left.push(text); return; }
    flushInfobox(st);
    doc.heading(text, level);
    st.headings++;
    return;
  }
  if (st.stopped || st.ended) return;
  if (doc.full) { st.stopped = true; return; }
  const t = node.tag;
  if (t === 'table') {
    if (hasClass(node, 'infobox') || hasClass(node, 'vcard')) { if (!st.infobox) st.infobox = node; return; }
    table(node, st);
    return;
  }
  if (t === 'p') { doc.block(); inlineKids(node, st, 'n', 0); doc.end(); return; }
  if (t === 'figure' || hasClass(node, 'thumb') || (t === 'div' && hasClass(node, 'thumbinner'))) { picture(node, st, depth); return; }
  if (t === 'ul' && hasClass(node, 'gallery')) { for (const li of findAll(node, (e) => hasClass(e, 'gallerybox'))) picture(li, st, depth); return; }
  if (t === 'div' && hasClass(node, 'hatnote')) { doc.block({ indent: 2, first: 2 }); inlineKids(node, st, 'k', 0); doc.end(); return; }
  if (t === 'ul' || t === 'ol') { list(node, st, depth, t === 'ol'); return; }
  if (t === 'dl') { deflist(node, st, depth); return; }
  if (t === 'blockquote') {
    doc.block({ indent: depth + 4, first: depth + 4 });
    for (const c of node.children) {
      if (typeof c === 'object' && c.tag === 'p') { inlineKids(c, st, 'i', 0); doc.br(); } else if (typeof c === 'object' && /^(div|ul|ol)$/.test(c.tag)) inlineKids(c, st, 'i', 0); else inline(c, st, 'i', 0);
    }
    doc.end(); return;
  }
  if (t === 'pre') { doc.block(); for (const l of textOf(node).replace(/\n$/, '').split('\n')) doc.line('  ' + l, 'c'); return; }
  if (t === 'hr') { doc.rule(); return; }
  if (t === 'math' || hasClass(node, 'mwe-math-element')) { doc.block({ indent: 4, first: 4 }); inline(node, st, 'n', 0); doc.end(); return; }
  // containers
  if (/^(div|section|center|span|body|main|article|dd|tbody|font|small|big|a|b|i)$/.test(t) || t === '#root') {
    // a div of inline content (rare) - treat it as a paragraph
    const inl = node.children.every((c) => typeof c === 'string' || /^(a|b|i|span|em|strong|sup|sub|small|code|abbr|br|img|math)$/.test(c.tag));
    if (inl && node.children.some((c) => (typeof c === 'string' ? c.trim() : true))) {
      if (t === 'span' || t === 'a' || t === 'b' || t === 'i') { inline(node, st, 'n', 0); return; }
      doc.block(); inlineKids(node, st, 'n', 0); doc.end(); return;
    }
    for (const c of node.children) block(c, st, depth);
  }
}

function list(node, st, depth, ordered) {
  const doc = st.doc;
  let n = +node.attrs.start || 1;
  let firstItem = true;
  for (const li of node.children) {
    if (typeof li !== 'object' || li.tag !== 'li' || skip(li)) continue;
    const mark = ordered ? `${n++}. ` : '• ';
    const ind = Math.min(depth, 12) + 2;
    doc.block({ indent: ind + mark.length, first: ind, tight: !firstItem || depth > 0 });
    firstItem = false;
    doc.raw(mark, ordered ? 'b' : 'n');
    for (const c of li.children) {
      if (typeof c === 'object' && (c.tag === 'ul' || c.tag === 'ol')) { doc.end(); list(c, st, ind + mark.length - 2 + 2, c.tag === 'ol'); }
      else if (typeof c === 'object' && (c.tag === 'dl')) { doc.end(); deflist(c, st, ind + 2); }
      else if (typeof c === 'object' && (c.tag === 'p' || c.tag === 'div') && !skip(c)) { inlineKids(c, st, 'n', 0); doc.br(); }
      else inline(c, st, 'n', 0);
    }
    doc.end();
    if (doc.full) { st.stopped = true; return; }
  }
}

function deflist(node, st, depth) {
  const doc = st.doc;
  for (const c of node.children) {
    if (typeof c !== 'object' || skip(c)) continue;
    if (c.tag === 'dt') { doc.block({ indent: depth + 2, first: depth, tight: true }); inlineKids(c, st, 'b', 0); doc.end(); }
    else if (c.tag === 'dd') {
      const sub = c.children.find((x) => typeof x === 'object' && (x.tag === 'ul' || x.tag === 'ol' || x.tag === 'dl'));
      if (sub && c.children.every((x) => typeof x === 'object' || !x.trim())) { for (const x of c.children) if (typeof x === 'object') block(x, st, depth + 4); continue; }
      doc.block({ indent: depth + 4, first: depth + 4, tight: true }); inlineKids(c, st, 'n', 0); doc.end();
    }
  }
}

function table(node, st) {
  const doc = st.doc;
  const rows = findAll(node, (e) => e.tag === 'tr');
  if (!rows.length) return;
  const cap = find(node, (e) => e.tag === 'caption');
  doc.block();
  if (cap) { doc.text(textOf(cap).trim(), 'b'); doc.end(); }
  const max = 24;
  rows.slice(0, max).forEach((tr) => {
    const cells = tr.children.filter((c) => typeof c === 'object' && (c.tag === 'td' || c.tag === 'th'));
    if (!cells.length) return;
    doc.block({ indent: 4, first: 2, tight: true });
    cells.forEach((td, i) => {
      if (i) doc.raw(' │ ', 'k');
      inlineKids(td, st, td.tag === 'th' ? 'b' : 'n', 0);
    });
    doc.end();
  });
  if (rows.length > max) { doc.block({ indent: 2, first: 2, tight: true }); doc.text(`(${rows.length - max} more rows not shown)`, 'k'); doc.end(); }
}

function picture(node, st, depth) {
  const doc = st.doc;
  const img = find(node, (e) => e.tag === 'img');
  if (!img) return;
  const w = +img.attrs['data-file-width'] || +img.attrs.width || 0;
  if ((+img.attrs.width || 99) < 40) return;           // icons
  const cap = find(node, (e) => e.tag === 'figcaption' || hasClass(e, 'thumbcaption') || hasClass(e, 'gallerytext'));
  const a = find(node, (e) => e.tag === 'a' && /\/wiki\/File:/.test(e.attrs.href || ''));
  const file = fileName(a) || img.attrs.alt || 'Picture';
  let caption = cap ? textOf(cap).replace(/\s+/g, ' ').trim() : (img.attrs.alt || '').trim();
  if (!caption) caption = file.replace(/^File:/, '').replace(/\.[a-z]+$/i, '');
  const src = imageURL(img);
  if (!src) return;
  st.pictures++;
  doc.block({ indent: depth + 4, first: depth + 2 });
  const short = caption.length > 150 ? caption.slice(0, 147) + '...' : caption;
  doc.link('[Picture: ' + short + ']', { kind: 'P', src, caption, file, w, h: +img.attrs['data-file-height'] || 0 }, 'p');
  doc.end();
}

function flushInfobox(st) {
  const box = st.infobox;
  if (!box || st.infoboxDone) return;
  st.infoboxDone = true;
  const doc = st.doc;
  const cap = find(box, (e) => e.tag === 'caption' || hasClass(e, 'infobox-title') || hasClass(e, 'infobox-above'));
  doc.block();
  doc.parts([['─── ', 'k'], ['Quick facts' + (cap ? ': ' + textOf(cap).replace(/\s+/g, ' ').trim() : ''), 'b'], [' ' + '─'.repeat(60), 'k']]);
  let rows = 0;
  for (const tr of findAll(box, (e) => e.tag === 'tr')) {
    if (skip(tr)) continue;
    const th = tr.children.find((c) => typeof c === 'object' && c.tag === 'th');
    const td = tr.children.find((c) => typeof c === 'object' && c.tag === 'td');
    if (td && find(td, (e) => e.tag === 'img') && !th) { picture(td, st, 0); continue; }
    if (th && td) {
      const label = textOf(th).replace(/\s+/g, ' ').trim();
      if (!label) continue;
      doc.block({ indent: 4, first: 2, tight: true });
      doc.text(label + ': ', 'b');
      inlineKids(td, st, 'n', 0, true);
      doc.end();
      if (++rows >= 30) break;
    } else if (th && !td && rows) {
      doc.block({ tight: true, indent: 2, first: 2 }); doc.text(textOf(th).replace(/\s+/g, ' ').trim(), 'H'); doc.end();
    }
  }
  doc.parts([['─'.repeat(76), 'k']]);
}

// ------------------------------------------------------------------ inline content
function inlineKids(node, st, style, link, commas = false) {
  for (let i = 0; i < node.children.length; i++) {
    const c = node.children[i];
    if (commas && typeof c === 'object' && (c.tag === 'ul' || c.tag === 'ol')) {
      const items = c.children.filter((x) => typeof x === 'object' && x.tag === 'li');
      items.forEach((li, k) => { if (k) st.doc.text(', ', style); inlineKids(li, st, style, link); });
      continue;
    }
    if (commas && typeof c === 'object' && c.tag === 'br') { st.doc.text(', ', style); continue; }
    inline(c, st, style, link, commas);
  }
}

function inline(node, st, style, link, commas = false) {
  const doc = st.doc;
  if (typeof node === 'string') { doc.text(node, link ? (style === 'n' || style === 'b' || style === 'i' ? 'l' : style) : style, link); return; }
  if (skip(node)) return;
  const t = node.tag;
  if (t === 'br') { if (commas) doc.text(', ', style); else doc.br(); return; }
  if (t === 'img') return;
  if (t === 'sup') { if (!hasClass(node, 'reference')) { doc.text('^', style, link); inlineKids(node, st, style, link); } return; }
  if (t === 'math' || hasClass(node, 'mwe-math-element')) {
    const m = t === 'math' ? node : find(node, (e) => e.tag === 'math');
    let tex = m ? m.attrs.alttext || textOf(m) : textOf(node);
    tex = tex.replace(/^\{\\displaystyle\s*/, '').replace(/\}$/, '').replace(/\\(mathrm|text|mathbf|operatorname)\{([^}]*)\}/g, '$2').replace(/\\,|\\;|\\!/g, ' ').trim();
    doc.text(' ' + tex + ' ', 'c', 0);
    return;
  }
  if (t === 'a') {
    const href = node.attrs.href || '';
    if (link) { inlineKids(node, st, style, link); return; }
    const wt = wikiTitle(href);
    if (wt && !href.startsWith('#')) {
      const n = doc.addLink({ kind: 'A', title: wt.replace(/#.*$/, '') });
      inlineKids(node, st, 'nbik'.includes(style) ? 'l' : style, n);
      return;
    }
    if (/^https?:|^\/\//.test(href) && hasClass(node, 'external')) {
      const n = doc.addLink({ kind: 'U', url: href.startsWith('//') ? 'https:' + href : href });
      inlineKids(node, st, 'l', n);
      return;
    }
    inlineKids(node, st, style, link);
    return;
  }
  if (t === 'b' || t === 'strong') { inlineKids(node, st, style === 'n' ? 'b' : style, link, commas); return; }
  if (t === 'i' || t === 'em' || t === 'cite' || t === 'var' || t === 'dfn') { inlineKids(node, st, style === 'n' ? 'i' : style, link, commas); return; }
  if (t === 'code' || t === 'kbd' || t === 'samp' || t === 'tt') { inlineKids(node, st, 'c', link, commas); return; }
  if (t === 'ul' || t === 'ol' || t === 'dl' || t === 'table' || t === 'figure') { if (commas) inlineKids(node, st, style, link, true); return; }
  if (t === 'div' || t === 'p') { if (commas) { inlineKids(node, st, style, link, true); doc.text(' ', style); } else { inlineKids(node, st, style, link); doc.text(' ', style); } return; }
  inlineKids(node, st, style, link, commas);
}

// ------------------------------------------------------------------ search results
/** action=query&list=search JSON -> the results list. */
export function searchDoc(query, json) {
  const doc = new Doc({ title: 'Search: ' + query, channel: 'Encyclopedia', kind: 'L' });
  const q = json?.query || {};
  const hits = q.search || [];
  const total = q.searchinfo?.totalhits ?? hits.length;
  doc.block({ tight: true });
  doc.text('Encyclopedia search for ', 'n'); doc.text('"' + query + '"', 'b');
  doc.end();
  doc.text(hits.length ? `${total.toLocaleString('en-US')} articles found. The best ${hits.length}:` : 'No articles were found.', 'k');
  doc.end();
  if (!hits.length && q.searchinfo?.suggestion) {
    doc.block();
    doc.text('Did you mean ', 'n'); doc.link(q.searchinfo.suggestion, { kind: 'S', query: q.searchinfo.suggestion }); doc.text('?', 'n');
    doc.end();
  }
  hits.forEach((h, i) => {
    const num = String(i + 1).padStart(2) + '. ';
    doc.block({ indent: 6, first: 2 });
    doc.raw(num, 'b');
    doc.link(h.title, { kind: 'A', title: h.title });
    doc.end();
    const snip = textOf(parseHTML(h.snippet || '')).replace(/\s+/g, ' ').trim();
    if (snip) { doc.block({ indent: 6, first: 6, tight: true }); doc.text(snip + (snip.endsWith('.') ? '' : ' ...'), 'k'); doc.end(); }
  });
  return doc;
}
