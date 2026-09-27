#!/usr/bin/env node
// tools/fetch-3rdparty.mjs - fetch the third-party data ARM-DOS's disks carry but the
// repository does not (shareware game data, freeware programs, music, the MIDI sound set).
//
//   node tools/fetch-3rdparty.mjs            download what is missing, unpack, verify
//   node tools/fetch-3rdparty.mjs --check    only verify what is there (exit 1 if incomplete)
//   node tools/fetch-3rdparty.mjs --list     list the items, their sources and licences
//
// Usually run through tools/fetch-3rdparty.sh (or ./build.sh, which runs it first).
//
// What to fetch is in 3rdparty/manifest.json (tracked); everything else under 3rdparty/ is
// generated and git-ignored:
//   3rdparty/downloads/<file>   the original archives, exactly as downloaded (sha256-checked)
//   3rdparty/<dst>              the files the build uses, unpacked from them (sha256-checked)
// Idempotent: a file that is present with the right hash is left alone; nothing is downloaded
// twice. Unpacking needs no external tools: zip (deflate), LHA (-lh0-/-lh5-/-lh6-/-lh7-) and
// id Software's .SHR installer archives (PKWARE DCL "implode") are read here.
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { inflateRawSync } from 'node:zlib';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const TP = path.join(ROOT, '3rdparty');
const DL = path.join(TP, 'downloads');
const MANIFEST = path.join(TP, 'manifest.json');
const args = process.argv.slice(2);
const CHECK = args.includes('--check'), LIST = args.includes('--list');

const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');
const rel = (p) => path.relative(ROOT, p) || '.';
const fileSha = (p) => (fs.existsSync(p) ? sha256(fs.readFileSync(p)) : null);
const mb = (n) => (n / 1048576).toFixed(1) + ' MB';

// ------------------------------------------------------------------ zip
function zipEntries(buf) {
  let eocd = -1;
  for (let i = buf.length - 22; i >= Math.max(0, buf.length - 65557); i--)
    if (buf.readUInt32LE(i) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error('not a zip archive');
  const n = buf.readUInt16LE(eocd + 10);
  // a self-extracting zip (an .EXE stub in front) may give offsets relative to the zip part
  const shift = eocd - buf.readUInt32LE(eocd + 12) - buf.readUInt32LE(eocd + 16);
  let p = buf.readUInt32LE(eocd + 16) + shift;
  const out = new Map();
  for (let k = 0; k < n; k++) {
    if (buf.readUInt32LE(p) !== 0x02014b50) throw new Error('bad zip central directory');
    const method = buf.readUInt16LE(p + 10), csize = buf.readUInt32LE(p + 20);
    const nl = buf.readUInt16LE(p + 28), xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32);
    let lho = buf.readUInt32LE(p + 42);
    if (buf.readUInt32LE(lho) !== 0x04034b50 && buf.readUInt32LE(lho + shift) === 0x04034b50) lho += shift;
    const name = buf.subarray(p + 46, p + 46 + nl).toString('latin1');
    out.set(name, { method, csize, lho });
    p += 46 + nl + xl + cl;
  }
  return out;
}
function unzip(buf, member) {
  const e = zipEntries(buf).get(member);
  if (!e) throw new Error(`"${member}" is not in the zip archive`);
  const p = e.lho;
  if (buf.readUInt32LE(p) !== 0x04034b50) throw new Error('bad zip local header');
  const data = buf.subarray(p + 30 + buf.readUInt16LE(p + 26) + buf.readUInt16LE(p + 28));
  if (e.method === 0) return Buffer.from(data.subarray(0, e.csize));
  if (e.method === 8) return inflateRawSync(data.subarray(0, e.csize));
  throw new Error(`zip method ${e.method} not supported`);
}

// ------------------------------------------------------------------ LHA (-lh5-, -lh6-, -lh7-, -lh0-)
function lhaEntries(buf) {
  // skip an SFX stub: the first level-0/1 header, "-lh?-" at +2
  const out = [];
  let p = -1;
  for (let i = 0; i + 21 < buf.length; i++)
    if (buf[i + 2] === 0x2d && buf[i + 3] === 0x6c && buf[i + 4] === 0x68 && buf[i + 6] === 0x2d && buf[i + 20] <= 1 && buf[i] > 20) { p = i; break; }
  if (p < 0) throw new Error('not an LHA archive');
  while (p + 21 < buf.length && buf[p] !== 0) {
    const hsize = buf[p], method = buf.subarray(p + 2, p + 7).toString('latin1');
    let packed = buf.readUInt32LE(p + 7);
    const size = buf.readUInt32LE(p + 11), level = buf[p + 20];
    const name = buf.subarray(p + 22, p + 22 + buf[p + 21]).toString('latin1');
    let data = p + 2 + hsize;
    if (level === 1) {                       // extended headers follow; the packed size includes them
      let x = buf.readUInt16LE(data - 2);
      while (x) { packed -= x; data += x; x = buf.readUInt16LE(data - 2); }
    } else if (level !== 0) throw new Error(`LHA header level ${level} not supported`);
    out.push({ name, method, size, data: buf.subarray(data, data + packed) });
    p = data + packed;
  }
  return out;
}
function canonical(lens) {                   // canonical Huffman code (LHA's make_table order)
  const count = new Array(17).fill(0);
  for (const l of lens) count[l]++;
  count[0] = 0;
  const offs = new Array(17).fill(0);
  for (let l = 1; l < 17; l++) offs[l] = offs[l - 1] + count[l - 1];
  const sym = [];
  for (let s = 0; s < lens.length; s++) if (lens[s]) sym[offs[lens[s]]++] = s;
  return { count, sym };
}
function lhaDecode(src, size, dicbit) {
  const out = Buffer.alloc(size);
  let bp = 0, bitbuf = 0, bitcnt = 0;
  const bit = () => {
    if (!bitcnt) { bitbuf = bp < src.length ? src[bp] : 0; bp++; bitcnt = 8; }
    bitcnt--; return (bitbuf >> bitcnt) & 1;
  };
  const bits = (n) => { let v = 0; while (n--) v = (v << 1) | bit(); return v; };
  const dec = (h) => {
    if (h.single !== undefined) return h.single;
    let code = 0, first = 0, index = 0;
    for (let l = 1; l < 17; l++) {
      code |= bit();
      const c = h.count[l];
      if (code - first < c) return h.sym[index + code - first];
      index += c; first = (first + c) << 1; code <<= 1;
    }
    throw new Error('bad LHA Huffman code');
  };
  const NP = dicbit + 1, NT = 19, NC = 510, PBIT = dicbit <= 13 ? 4 : 5;
  const readPt = (nn, nbit, special) => {
    const n = bits(nbit);
    if (!n) return { single: bits(nbit) };
    const len = new Array(nn).fill(0);
    let i = 0;
    while (i < n) {
      let c = bits(3);
      if (c === 7) while (bit()) c++;
      len[i++] = c;
      if (i === special) { let z = bits(2); while (z-- > 0) len[i++] = 0; }
    }
    return canonical(len);
  };
  let blocksize = 0, pt, cc, pp, o = 0;
  while (o < size) {
    if (!blocksize) {
      blocksize = bits(16);
      pt = readPt(NT, 5, 3);
      const n = bits(9);
      if (!n) cc = { single: bits(9) };
      else {
        const len = new Array(NC).fill(0);
        let i = 0;
        while (i < n) {
          let c = dec(pt);
          if (c <= 2) {
            c = c === 0 ? 1 : c === 1 ? bits(4) + 3 : bits(9) + 20;
            while (c-- > 0) len[i++] = 0;
          } else len[i++] = c - 2;
        }
        cc = canonical(len);
      }
      pp = readPt(NP, PBIT, -1);
    }
    blocksize--;
    const c = dec(cc);
    if (c < 256) { out[o++] = c; continue; }
    const n = c - 253;
    let d = dec(pp);
    if (d) d = (1 << (d - 1)) + bits(d - 1);
    let from = o - d - 1;
    for (let k = 0; k < n && o < size; k++) out[o++] = out[from++];
  }
  return out;
}
function unlha(buf, member) {
  const e = lhaEntries(buf).find((x) => x.name === member);
  if (!e) throw new Error(`"${member}" is not in the LHA archive`);
  if (e.method === '-lh0-') return Buffer.from(e.data.subarray(0, e.size));
  const dicbit = { '-lh5-': 13, '-lh6-': 15, '-lh7-': 16 }[e.method];
  if (!dicbit) throw new Error(`LHA method ${e.method} not supported`);
  return lhaDecode(e.data, e.size, dicbit);
}

// ------------------------------------------------------------------ id Software .SHR (PKWARE DCL implode)
// A port of zlib's contrib/blast/blast.c (Mark Adler). Returns [output, bytes consumed].
function blast(src, start) {
  let p = start, bitbuf = 0, bitcnt = 0;
  const bits = (need) => {
    let val = bitbuf;
    while (bitcnt < need) { if (p >= src.length) throw new Error('SHR: unexpected end of data'); val |= src[p++] << bitcnt; bitcnt += 8; }
    bitbuf = val >>> need; bitcnt -= need;
    return val & ((1 << need) - 1);
  };
  const construct = (rep) => {
    const length = [];
    for (const b of rep) { const len = b & 15; for (let k = (b >> 4) + 1; k; k--) length.push(len); }
    const count = new Array(14).fill(0);
    for (const l of length) count[l]++;
    const offs = [0, 0];
    for (let l = 1; l < 13; l++) offs[l + 1] = offs[l] + count[l];
    const symbol = [];
    for (let s = 0; s < length.length; s++) if (length[s]) symbol[offs[length[s]]++] = s;
    return { count, symbol };
  };
  const decode = (h) => {
    let code = 0, first = 0, index = 0;
    for (let len = 1; len <= 13; len++) {
      code |= bits(1) ^ 1;
      const count = h.count[len];
      if (code < first + count) return h.symbol[index + (code - first)];
      index += count; first += count; first <<= 1; code <<= 1;
    }
    throw new Error('SHR: bad code');
  };
  const litcode = construct([11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45, 44, 173]);
  const lencode = construct([2, 35, 36, 53, 38, 23]);
  const distcode = construct([2, 20, 53, 230, 247, 151, 248]);
  const base = [3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264];
  const extra = [0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8];
  const lit = bits(8), dict = bits(8);
  if (lit > 1 || dict < 4 || dict > 6) throw new Error('SHR: bad implode header');
  const out = [];
  for (;;) {
    if (bits(1)) {
      let symbol = decode(lencode);
      const len = base[symbol] + bits(extra[symbol]);
      if (len === 519) break;
      symbol = len === 2 ? 2 : dict;
      let dist = decode(distcode) << symbol;
      dist += bits(symbol);
      dist++;
      if (dist > out.length) throw new Error('SHR: distance too far back');
      for (let k = 0; k < len; k++) out.push(out[out.length - dist]);
    } else out.push(lit ? decode(litcode) : bits(8));
  }
  return [Buffer.from(out), p - start];
}
function unshr(buf, member) {
  // a 3Ah-byte archive header, then per file: a A8h-byte header (file name at +0,
  // packed size at +88h) and the imploded data
  let p = 0x3a;
  while (p + 0xa8 < buf.length) {
    const name = buf.subarray(p, buf.indexOf(0, p)).toString('latin1');
    const [data, used] = blast(buf, p + 0xa8);
    if (name === member) return data;
    p += 0xa8 + used;
  }
  throw new Error(`"${member}" is not in the SHR archive`);
}

// ------------------------------------------------------------------ manifest
function extract(buf, steps) {
  for (const step of steps) {
    const i = step.indexOf(':'), kind = step.slice(0, i), member = step.slice(i + 1);
    if (kind === 'zip') buf = unzip(buf, member);
    else if (kind === 'lha') buf = unlha(buf, member);
    else if (kind === 'shr') buf = unshr(buf, member);
    else throw new Error(`unknown unpack step "${step}"`);
  }
  return buf;
}

async function download(d) {
  const dst = path.join(DL, d.file);
  if (fs.existsSync(dst)) {
    if (!d.sha256 || fileSha(dst) === d.sha256) return true;
    console.log(`  ${d.file}: the copy in ${rel(DL)} has the wrong contents; downloading it again`);
    fs.rmSync(dst);
  }
  fs.mkdirSync(DL, { recursive: true });
  for (const url of d.urls) {
    process.stdout.write(`  downloading ${d.file}${d.size ? ` (${mb(d.size)})` : ''} from ${url} ... `);
    try {
      const r = await fetch(url, { redirect: 'follow', signal: AbortSignal.timeout(600000), headers: { 'user-agent': 'armdos-fetch-3rdparty (+https://github.com/kdkd/armdos)' } });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const buf = Buffer.from(await r.arrayBuffer());
      if (d.sha256 && sha256(buf) !== d.sha256) throw new Error(`the file is not the expected one (sha256 ${sha256(buf)})`);
      fs.writeFileSync(dst + '.part', buf);
      fs.renameSync(dst + '.part', dst);
      console.log('ok');
      return true;
    } catch (e) {
      console.log(`failed: ${e.message}`);
    }
  }
  return false;
}

function help(item, d) {
  return [
    `  Could not get ${d.file} (for ${item.title}).`,
    `  You can download it by hand: open one of these links in a web browser,`,
    ...d.urls.map((u) => `      ${u}`),
    `  save the file as`,
    `      ${path.join(DL, d.file)}`,
    `  (exactly that name, in that folder${d.size ? `; it is ${d.size.toLocaleString('en-US')} bytes` : ''}), and run ./build.sh again.`,
  ].join('\n');
}

const manifest = JSON.parse(fs.readFileSync(MANIFEST, 'utf8'));

if (LIST) {
  for (const it of manifest.items) {
    console.log(`${it.id}: ${it.title}\n  licence: ${it.licence}`);
    for (const d of it.downloads) console.log(`  ${d.file}${d.size ? ` (${mb(d.size)})` : ''}: ${d.urls[0]}`);
    console.log(`  -> ${it.files.length} file(s) in 3rdparty/${[...new Set(it.files.map((f) => f.dst.split('/')[0]))].join(', 3rdparty/')}`);
  }
  process.exit(0);
}

let bad = 0;
for (const it of manifest.items) {
  const missing = it.files.filter((f) => fileSha(path.join(TP, f.dst)) !== f.sha256);
  if (!missing.length) { console.log(`fetch-3rdparty: ${it.id}: ok (${it.files.length} file${it.files.length > 1 ? 's' : ''})`); continue; }
  if (CHECK) { console.log(`fetch-3rdparty: ${it.id}: ${missing.length} of ${it.files.length} file(s) missing or different, e.g. 3rdparty/${missing[0].dst}`); bad++; continue; }
  console.log(`fetch-3rdparty: ${it.id}: ${it.title}`);
  const need = new Set(missing.map((f) => f.src));
  let ok = true;
  for (const d of it.downloads) {
    if (!need.has(d.file)) continue;
    if (!(await download(d))) { console.error('\n' + help(it, d) + '\n'); ok = false; }
  }
  if (!ok) { bad++; continue; }
  const cache = new Map();
  for (const f of missing) {
    const out = path.join(TP, f.dst);
    try {
      let buf;
      if (f.derive) {
        const input = path.join(DL, f.src);
        fs.mkdirSync(path.dirname(out), { recursive: true });
        const cmd = f.derive.map((a) => a.replace('$IN', input).replace('$OUT', out + '.part'));
        console.log(`  making ${f.dst}: ${cmd.join(' ').replaceAll(ROOT + '/', '')}`);
        const r = spawnSync(cmd[0] === 'node' ? process.execPath : cmd[0], cmd.slice(1), { cwd: ROOT, stdio: ['ignore', 'ignore', 'inherit'] });
        if (r.status !== 0) throw new Error(`${cmd[0]} failed`);
        buf = fs.readFileSync(out + '.part');
        fs.rmSync(out + '.part');
      } else {
        const key = f.src + '|' + (f.path || []).slice(0, -1).join('|');
        let base = cache.get(key);
        if (!base) { base = extract(fs.readFileSync(path.join(DL, f.src)), (f.path || []).slice(0, -1)); cache.set(key, base); }
        buf = (f.path || []).length ? extract(base, f.path.slice(-1)) : base;
      }
      if (sha256(buf) !== f.sha256) throw new Error(`unpacked file has the wrong contents (sha256 ${sha256(buf)})`);
      fs.mkdirSync(path.dirname(out), { recursive: true });
      fs.writeFileSync(out, buf);
      if (f.date) { const t = new Date(f.date); fs.utimesSync(out, t, t); }
    } catch (e) {
      console.error(`  ${f.dst}: ${e.message}`);
      console.error(`  (delete ${path.join(rel(DL), f.src)} and run ./build.sh again; if that does not help, please report it)`);
      ok = false;
    }
  }
  if (ok) console.log(`  ${it.id}: ${missing.length} file(s) ready`);
  else bad++;
}
if (bad) {
  console.error(`\nfetch-3rdparty: ${bad} item(s) ${CHECK ? 'are not complete (run tools/fetch-3rdparty.sh)' : 'could not be fetched - see the messages above'}.`);
  process.exit(1);
}
console.log('fetch-3rdparty: all third-party files are present and verified.');
