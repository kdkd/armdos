// The channels of ARM-DOS Online: each turns a request into a Doc (or a picture) using
// real public web services, through a Fetcher (net.mjs):
//   Encyclopedia      Wikipedia (action API: search, parse; REST: On this day)
//   Dictionary        Wiktionary (REST definition API)
//   Weather           Open-Meteo (geocoding + forecast, no key)
//   Technology News   Hacker News (Firebase API)
// Every function throws ServiceError on failure; the service turns that into the
// channel's "not available right now" message.

import { Doc } from './doc.mjs';
import { ServiceError } from './net.mjs';
import { parseHTML, textOf } from './html.mjs';
import { articleDoc, searchDoc, wikiTitle } from './wiki.mjs';

const WP = 'https://en.wikipedia.org';
const API = WP + '/w/api.php?format=json&formatversion=2&origin=*&';
const enc = encodeURIComponent;

export const CHANNELS = {
  E: 'Encyclopedia', D: 'Dictionary', W: 'Weather', N: 'Technology News', T: 'Today in History', R: 'Encyclopedia', P: 'Pictures',
};
export const UNAVAILABLE = {
  E: 'The Encyclopedia is not available right now.', D: 'The Dictionary is not available right now.',
  W: 'The Weather Center is not available right now.', N: 'Technology News is not available right now.',
  T: 'Today in History is not available right now.', P: 'The Picture Library is not available right now.',
};

// ------------------------------------------------------------------ Encyclopedia
export async function search(net, query) {
  const j = await net.get(API + 'action=query&list=search&srlimit=10&srprop=snippet&srsearch=' + enc(query));
  if (j.error) throw new ServiceError(j.error.info || 'search failed');
  return searchDoc(query, j);
}

export async function article(net, title) {
  const j = await net.get(API + 'action=parse&prop=text%7Cdisplaytitle&redirects=1&disableeditsection=1&disabletoc=1&page=' + enc(title.replace(/ /g, '_')));
  if (j.error) {
    if (j.error.code === 'missingtitle' || j.error.code === 'invalidtitle') return notFoundDoc(title);
    throw new ServiceError(j.error.info || 'parse failed');
  }
  const p = j.parse;
  const shown = textOf(parseHTML(p.displaytitle || p.title)).trim() || p.title;
  return articleDoc({ title: shown, html: p.text || '' });
}

function notFoundDoc(title) {
  const doc = new Doc({ title, channel: 'Encyclopedia', kind: 'E' });
  doc.block({ tight: true });
  doc.text('There is no article called ', 'n'); doc.text('"' + title + '"', 'b'); doc.text('.', 'n');
  doc.end();
  doc.block();
  doc.link('Search the Encyclopedia for "' + title + '"', { kind: 'S', query: title });
  doc.end();
  return doc;
}

export async function randomTitle(net) {
  const j = await net.get(API + 'action=query&list=random&rnnamespace=0&rnlimit=1&_=' + Math.floor(net.now() / 1000), 'json');
  const t = j?.query?.random?.[0]?.title;
  if (!t) throw new ServiceError('no random article');
  return t;
}

const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];

export async function onThisDay(net, month, day) {
  const mm = String(month).padStart(2, '0'), dd = String(day).padStart(2, '0');
  const j = await net.get(`${WP}/api/rest_v1/feed/onthisday/selected/${mm}/${dd}`);
  const ev = (j.selected || j.events || []).slice().sort((a, b) => (b.year || 0) - (a.year || 0));
  const date = `${MONTHS[month - 1]} ${day}`;
  const doc = new Doc({ title: 'Today in History: ' + date, channel: 'Today in History', kind: 'T' });
  doc.block({ tight: true });
  doc.text('On this day, ' + date, 't'); doc.end();
  doc.text('Selected anniversaries, from Wikipedia', 'k'); doc.end();
  if (!ev.length) { doc.block(); doc.text('Nothing is listed for this day.', 'n'); doc.end(); }
  for (const e of ev) {
    const y = e.year < 0 ? -e.year + ' BC' : String(e.year);
    doc.block({ indent: 8, first: 0 });
    doc.raw(y.padStart(6) + '  ', 'y');
    doc.text(e.text || '', 'n');
    doc.end();
    const pages = (e.pages || []).slice(0, 3);
    if (pages.length) {
      doc.block({ indent: 10, first: 8, tight: true });
      doc.raw('► ', 'k');
      pages.forEach((p, i) => {
        if (i) doc.text(' · ', 'k');
        const t = p.titles?.normalized || (p.title || '').replace(/_/g, ' ');
        doc.link(t, { kind: 'A', title: t });
      });
      doc.end();
    }
  }
  return doc;
}

// ------------------------------------------------------------------ Dictionary
export async function define(net, word) {
  const w = word.trim();
  let j = null;
  for (const cand of [w, w.toLowerCase()]) {
    try { j = await net.get('https://en.wiktionary.org/api/rest_v1/page/definition/' + enc(cand.replace(/ /g, '_'))); break; }
    catch (e) { if (!e.notFound) throw e; }
    if (w === w.toLowerCase()) break;
  }
  const doc = new Doc({ title: 'Dictionary: ' + w, channel: 'Dictionary', kind: 'D' });
  doc.block({ tight: true });
  doc.text(w, 't'); doc.end();
  if (!j || !Object.keys(j).length) {
    doc.block();
    doc.text('No definition was found for "' + w + '". Check the spelling, or ', 'n');
    doc.link('look it up in the Encyclopedia', { kind: 'S', query: w });
    doc.text('.', 'n');
    doc.end();
    return doc;
  }
  const langs = Object.keys(j);
  const main = j.en ? ['en'] : langs.slice(0, 2);
  for (const lang of main) {
    const entries = j[lang] || [];
    if (lang !== 'en' && entries[0]) { doc.block(); doc.text(entries[0].language, 'H'); doc.end(); }
    for (const e of entries) {
      doc.block();
      doc.text(e.partOfSpeech || 'Meaning', 'h');
      doc.end();
      let n = 1;
      for (const d of (e.definitions || []).slice(0, 12)) {
        const html = d.definition || '';
        if (!textOf(parseHTML(html)).trim()) continue;
        const num = String(n++).padStart(2) + '. ';
        doc.block({ indent: 6, first: 2, tight: n > 2 });
        doc.raw(num, 'b');
        htmlInline(doc, html, 'n', 'D');
        doc.end();
        const ex = (d.parsedExamples || []).map((x) => x.example).concat(d.parsedExamples ? [] : d.examples || []).slice(0, 2);
        for (const x of ex) { doc.block({ indent: 8, first: 8, tight: true }); doc.text('"', 'i'); htmlInline(doc, x, 'i', 'D'); doc.text('"', 'i'); doc.end(); }
      }
    }
  }
  const others = langs.filter((l) => !main.includes(l)).map((l) => j[l][0]?.language).filter(Boolean);
  if (others.length) {
    doc.block();
    doc.text('Also a word in: ' + others.slice(0, 20).join(', ') + (others.length > 20 ? ', ...' : '') + '.', 'k');
    doc.end();
  }
  doc.block();
  doc.text('From Wiktionary, the free dictionary. CC BY-SA 4.0.', 'k');
  doc.end();
  return doc;
}

/** Simple inline HTML (Wiktionary, Hacker News) into doc; links become `kind` links. */
export function htmlInline(doc, html, style = 'n', kind = 'A') {
  const walk = (node, st, link) => {
    if (typeof node === 'string') { doc.text(node, link ? 'l' : st, link); return; }
    const t = node.tag;
    if (t === 'style' || t === 'script' || (node.attrs.class || '').includes('reference')) return;
    if (t === 'br') { doc.br(); return; }
    if (t === 'p') { if (doc.cells.length > doc.indentCur()) { doc.br(); doc.br(); } for (const c of node.children) walk(c, st, link); return; }
    if (t === 'pre') { doc.br(); for (const l of textOf(node).replace(/\n$/, '').split('\n')) { doc.raw('  ' + l.slice(0, 70), 'c'); doc.br(); } return; }
    if (t === 'a' && !link) {
      const href = node.attrs.href || '';
      let target = null;
      const w = wikiTitle(href) || (/^\/wiki\/([^#?]+)/.exec(href) ? decodeURIComponent(/^\/wiki\/([^#?]+)/.exec(href)[1]).replace(/_/g, ' ') : null);
      if (w && kind === 'D' && !/:/.test(w)) target = { kind: 'D', word: w };
      else if (w && kind === 'A') target = { kind: 'A', title: w };
      else if (/^https?:/.test(href)) target = { kind: 'U', url: href };
      if (target) { const n = doc.addLink(target); for (const c of node.children) walk(c, st, n); return; }
    }
    const ns = t === 'i' || t === 'em' ? 'i' : t === 'b' || t === 'strong' ? 'b' : t === 'code' ? 'c' : st;
    for (const c of node.children) walk(c, ns, link);
  };
  walk(parseHTML(html), style, 0);
}

// ------------------------------------------------------------------ Weather
const WMO = {
  0: ['Clear sky', 'sun'], 1: ['Mainly clear', 'sun'], 2: ['Partly cloudy', 'part'], 3: ['Overcast', 'cloud'], 45: ['Fog', 'fog'], 48: ['Freezing fog', 'fog'],
  51: ['Light drizzle', 'rain'], 53: ['Drizzle', 'rain'], 55: ['Heavy drizzle', 'rain'], 56: ['Freezing drizzle', 'rain'], 57: ['Freezing drizzle', 'rain'],
  61: ['Light rain', 'rain'], 63: ['Rain', 'rain'], 65: ['Heavy rain', 'rain'], 66: ['Freezing rain', 'rain'], 67: ['Freezing rain', 'rain'],
  71: ['Light snow', 'snow'], 73: ['Snow', 'snow'], 75: ['Heavy snow', 'snow'], 77: ['Snow grains', 'snow'],
  80: ['Rain showers', 'rain'], 81: ['Rain showers', 'rain'], 82: ['Violent rain showers', 'rain'], 85: ['Snow showers', 'snow'], 86: ['Heavy snow showers', 'snow'],
  95: ['Thunderstorm', 'storm'], 96: ['Thunderstorm, hail', 'storm'], 99: ['Thunderstorm, heavy hail', 'storm'],
};
// little pictures, 13 columns x 5 lines: [text, style] parts per line
const ART = {
  sun: [['    \\  |  /  ', 'y'], ['     .---.   ', 'y'], ['  --(     )--', 'y'], ["     '---'   ", 'y'], ['    /  |  \\  ', 'y']],
  moon: [['             ', 'n'], ['      _.._   ', 'w'], ['     (   (   ', 'w'], ["      '--'   ", 'w'], ['             ', 'n']],
  part: [['  \\  |  /    ', 'y'], ['   .--.      ', 'y'], ['--(  .--.    ', 'w'], ['  .-(    ).  ', 'w'], [' (___.__)__) ', 'w']],
  cloud: [['             ', 'n'], ['     .--.    ', 'w'], ['  .-(    ).  ', 'w'], [' (___.__)__) ', 'w'], ['             ', 'n']],
  rain: [['     .--.    ', 'w'], ['  .-(    ).  ', 'w'], [' (___.__)__) ', 'w'], ['  / / / / /  ', 'c'], [' / / / / /   ', 'c']],
  snow: [['     .--.    ', 'w'], ['  .-(    ).  ', 'w'], [' (___.__)__) ', 'w'], ['   *  *  *   ', 'w'], ['  *  *  *    ', 'w']],
  storm: [['     .--.    ', 'k'], ['  .-(    ).  ', 'k'], [' (___.__)__) ', 'k'], ['    /_  /_   ', 'y'], ['     /   /   ', 'y']],
  fog: [['             ', 'n'], [' _ - _ - _ - ', 'k'], ['  _ - _ - _  ', 'k'], [' _ - _ - _ - ', 'k'], ['             ', 'n']],
};
const COMPASS = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const f = (c) => Math.round(c * 9 / 5 + 32);
const r1 = (v) => Math.round(v * 100) / 100;

export async function weatherSearch(net, place) {
  const q = place.trim().replace(/,.*$/, '');
  const j = await net.get('https://geocoding-api.open-meteo.com/v1/search?count=6&language=en&format=json&name=' + enc(q));
  let res = j.results || [];
  const hint = place.includes(',') ? place.split(',').slice(1).join(',').trim().toLowerCase() : '';
  if (hint) {
    const m = res.filter((r) => [r.country, r.country_code, r.admin1].some((v) => v && v.toLowerCase().startsWith(hint)));
    if (m.length) res = m.concat(res.filter((r) => !m.includes(r)));
  }
  if (!res.length) {
    const doc = new Doc({ title: 'Weather: ' + place, channel: 'Weather', kind: 'W' });
    doc.block({ tight: true });
    doc.text('The Weather Center does not know a place called "' + place + '".', 'n');
    doc.end();
    doc.block(); doc.text('Try the name of a city, for example Paris, Tokyo, or Springfield, US.', 'k'); doc.end();
    return doc;
  }
  return weather(net, res[0], res.slice(1));
}

const placeName = (p) => [p.name, p.admin1 && p.admin1 !== p.name ? p.admin1 : '', p.country_code || p.country || ''].filter(Boolean).join(', ');

export async function weather(net, p, others = []) {
  const url = 'https://api.open-meteo.com/v1/forecast?latitude=' + r1(p.latitude) + '&longitude=' + r1(p.longitude) +
    '&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,surface_pressure,is_day' +
    '&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset&timezone=auto&forecast_days=7';
  const w = await net.get(url);
  if (!w.current || !w.daily) throw new ServiceError('no forecast');
  const name = placeName(p);
  const doc = new Doc({ title: 'Weather: ' + name, channel: 'Weather', kind: 'W' });
  const c = w.current;
  const [cond, icon0] = WMO[c.weather_code] || ['Unknown', 'cloud'];
  const icon = (icon0 === 'sun' && c.is_day === 0) ? 'moon' : icon0;
  doc.block({ tight: true });
  doc.text(name, 't'); doc.end();
  const local = (c.time || '').replace('T', ' ');
  doc.text(`Conditions at ${local} local time (${w.timezone_abbreviation || w.timezone || 'UTC'})`, 'k'); doc.end();
  doc.blank();
  const dir = COMPASS[Math.round((c.wind_direction_10m || 0) / 22.5) % 16];
  const info = [
    [[`${Math.round(c.temperature_2m)}°C`, 'w'], [` (${f(c.temperature_2m)}°F)  `, 'n'], [cond, 'y']],
    [['Feels like ', 'k'], [`${Math.round(c.apparent_temperature)}°C (${f(c.apparent_temperature)}°F)`, 'n']],
    [['Humidity   ', 'k'], [`${c.relative_humidity_2m}%`, 'n']],
    [['Wind       ', 'k'], [`${dir} ${Math.round(c.wind_speed_10m)} km/h (${Math.round(c.wind_speed_10m / 1.609)} mph)`, 'n']],
    [['Pressure   ', 'k'], [`${Math.round(c.surface_pressure)} hPa (${(c.surface_pressure * 0.02953).toFixed(2)} inHg)`, 'n']],
  ];
  for (let i = 0; i < 5; i++) doc.parts([['  ', 'n'], ART[icon][i], ['    ', 'n'], ...info[i]]);
  const d = w.daily;
  if (d.sunrise?.[0]) { doc.blank(); doc.parts([['  Sunrise ', 'k'], [d.sunrise[0].slice(11), 'n'], ['   Sunset ', 'k'], [d.sunset[0].slice(11), 'n']]); }
  doc.blank();
  doc.parts([['  THE WEEK AHEAD', 'h']]);
  doc.parts([['  ══════════════', 'h']]);
  doc.parts([['  Day        Conditions              High      Low     Rain', 'b']]);
  for (let i = 0; i < d.time.length; i++) {
    const dt = new Date(d.time[i] + 'T12:00:00Z');
    const day = i === 0 ? 'Today' : DAYS[dt.getUTCDay()];
    const [cc] = WMO[d.weather_code[i]] || ['?'];
    const hi = d.temperature_2m_max[i], lo = d.temperature_2m_min[i];
    doc.parts([['  ' + day.padEnd(11), 'n'], [cc.padEnd(22).slice(0, 22), 'y'], ['  ' + `${Math.round(hi)}°C/${f(hi)}°F`.padEnd(10), 'r'],
      [`${Math.round(lo)}°C/${f(lo)}°F`.padEnd(10), 'c'], [d.precipitation_probability_max?.[i] != null ? `${d.precipitation_probability_max[i]}%` : '-', 'n']]);
  }
  if (others.length) {
    doc.block();
    doc.text('Other places with this name: ', 'k');
    others.slice(0, 5).forEach((o, i) => { if (i) doc.text(' · ', 'k'); doc.link(placeName(o), { kind: 'W', lat: o.latitude, lon: o.longitude, place: o }); });
    doc.end();
  }
  doc.block();
  doc.text('Weather data by Open-Meteo.com (CC BY 4.0).', 'k');
  doc.end();
  return doc;
}

// ------------------------------------------------------------------ Technology News
const HN = 'https://hacker-news.firebaseio.com/v0/';
function ago(net, t) {
  const s = Math.max(0, Math.floor(net.now() / 1000 - t));
  if (s < 3600) return `${Math.max(1, Math.floor(s / 60))} min ago`;
  if (s < 86400) { const h = Math.floor(s / 3600); return `${h} hour${h > 1 ? 's' : ''} ago`; }
  const d = Math.floor(s / 86400); return `${d} day${d > 1 ? 's' : ''} ago`;
}
const domain = (u) => { try { return new URL(u).host.replace(/^www\./, ''); } catch { return ''; } };

export async function news(net, count = 15) {
  const ids = await net.get(HN + 'topstories.json');
  if (!Array.isArray(ids)) throw new ServiceError('no stories');
  const items = (await Promise.all(ids.slice(0, count + 5).map((id) => net.get(HN + `item/${id}.json`).catch(() => null)))).filter((x) => x && !x.deleted && !x.dead).slice(0, count);
  if (!items.length) throw new ServiceError('no stories');
  const doc = new Doc({ title: 'Technology News', channel: 'Technology News', kind: 'N' });
  doc.block({ tight: true });
  doc.text('Technology News: the top stories', 't'); doc.end();
  doc.text('From the Hacker News wire, as voted by its readers', 'k'); doc.end();
  items.forEach((it, i) => {
    doc.block({ indent: 5, first: 1 });
    doc.raw(String(i + 1).padStart(2) + '. ', 'b');
    doc.link(it.title || '(untitled)', { kind: 'N', id: it.id });
    const dm = domain(it.url);
    if (dm) doc.text(' (' + dm + ')', 'k');
    doc.end();
    doc.block({ indent: 5, first: 5, tight: true });
    doc.text(`${it.score || 0} points by ${it.by || '?'}, ${ago(net, it.time)}, ${it.descendants || 0} comments`, 'k');
    doc.end();
  });
  return doc;
}

export async function newsItem(net, id) {
  const it = await net.get(HN + `item/${id}.json`);
  if (!it) throw new ServiceError('no such story', { notFound: true });
  const doc = new Doc({ title: it.title || 'Story', channel: 'Technology News', kind: 'N' });
  doc.block({ tight: true });
  doc.text(it.title || '(untitled)', 't'); doc.end();
  doc.text(`${it.score || 0} points by ${it.by || '?'}, ${ago(net, it.time)}, ${it.descendants || 0} comments`, 'k'); doc.end();
  if (it.url) { doc.block(); doc.text('Link: ', 'b'); doc.link(it.url, { kind: 'U', url: it.url }); doc.end(); }
  if (it.text) { doc.block(); htmlInline(doc, it.text, 'n', 'U'); doc.end(); }
  const kids = await Promise.all((it.kids || []).slice(0, 10).map((k) => net.get(HN + `item/${k}.json`).catch(() => null)));
  const cs = kids.filter((k) => k && !k.deleted && !k.dead && k.text).slice(0, 8);
  if (cs.length) {
    doc.heading('Comments', 2);
    for (const k of cs) {
      doc.block({ indent: 2, first: 2 });
      doc.text(k.by || '?', 'y'); doc.text(', ' + ago(net, k.time), 'k');
      doc.end();
      doc.block({ indent: 4, first: 4, tight: true });
      htmlInline(doc, k.text, 'n', 'U');
      doc.end();
      if (doc.full) break;
    }
  }
  return doc;
}
