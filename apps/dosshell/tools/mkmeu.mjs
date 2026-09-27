#!/usr/bin/env node
// tools/mkmeu.mjs - write the Start Programs group files (SHELL.MEU, ...) in
// the DOS 4.00 Shell's .MEU format (see shellc/meu.c for the layout), and
// SHELL.CLR.
//
//   node mkmeu.mjs menus.json OUTDIR [--have "PROG.EXE PROG.COM ..."]
//   node mkmeu.mjs --clr OUT        (SHELL.CLR: colour scheme 1)
//   node mkmeu.mjs --dump FILE.MEU  (show a .MEU file, e.g. the shipped ones)
import fs from 'node:fs';
import path from 'node:path';

const HLEN = 480, ILEN = 556;
const helpOff = (i) => (i === 0 ? 0x138 : 0x5C8 + (i - 1) * (HLEN + ILEN));
const itemOff = (i) => 0x39C + i * (HLEN + ILEN);

export function buildMeu(items) {
  const n = items.length;
  if (n > 16) throw new Error(`a group holds at most 16 items (${n})`);
  const size = n ? itemOff(n - 1) + ILEN : 0x39C;
  const b = Buffer.alloc(size);
  [0x1234, 0x14, 0xFE, 0, 0xA4, 0x10, 0xF7, 0, 0x318, 0x10].forEach((v, i) => b.writeUInt16LE(v, i * 2));
  b.writeUInt16LE(n, 0xA4); b.writeUInt16LE(1, 0xA6);
  b.writeUInt16LE(n, 0x318); b.writeUInt16LE(n, 0x31A);
  b.fill(0x20, 0x128, 0x128 + Math.min(n, 16));
  items.forEach((it, i) => {
    b.writeUInt16LE(i + 1, 0xA8 + i * 8); b.writeUInt16LE(helpOff(i), 0xA8 + i * 8 + 4); b.writeUInt16LE(HLEN, 0xA8 + i * 8 + 6);
    b.writeUInt16LE(i + 1, 0x31C + i * 8); b.writeUInt16LE(itemOff(i), 0x31C + i * 8 + 4); b.writeUInt16LE(ILEN, 0x31C + i * 8 + 6);
    const h = helpOff(i), r = itemOff(i);
    b.fill(0x20, h, h + HLEN); b[h] = 0xC6; b[h + HLEN - 1] = 0xC6;
    const help = Buffer.from(it.help || '', 'latin1');
    if (help.length > HLEN - 2) throw new Error(`help of "${it.title}" is ${help.length} chars (max ${HLEN - 2})`);
    help.copy(b, h + 1);
    b.fill(0x20, r, r + ILEN); b[r] = 0;
    if (it.title.length > 40) throw new Error(`title too long: ${it.title}`);
    b.write(it.title, r + 1, 'latin1');
    const isProg = !it.group;
    b[r + 41] = isProg ? 1 : 0;
    b.write((it.password || '').slice(0, 8), r + 42, 'latin1');
    b.writeUInt16LE(i + 1, r + 50);
    if (isProg) {
      b.writeUInt16LE(50, r + 52); b.writeUInt16LE(10, r + 54);
      const cmd = it.builtin ? Buffer.from([parseInt(it.builtin, 16)])
        : Buffer.from(it.cmd.join('\xBA') + '\xBA', 'latin1');
      if (cmd.length > 500) throw new Error(`command of "${it.title}" too long`);
      // the limits SHELLC checks (psc.c): title/instruction 40, prompt 20
      for (const [, k, v] of (it.cmd || []).join(' ').matchAll(/\/([tip])"([^"]*)"/gi)) {
        const max = /p/i.test(k) ? 20 : 40;
        if (v.length > max) throw new Error(`"${it.title}": /${k}"${v}" is longer than ${max}`);
      }
      cmd.copy(b, r + 56);
    } else b.write(it.group, r + 52, 'latin1');
  });
  return b;
}

export function parseMeu(b) {
  const rd = (o) => b.readUInt16LE(o);
  const trim = (s) => s.replace(/[ \0\xC6]+$/, '');
  const hidx = rd(8), iidx = rd(16), n = rd(iidx), nh = rd(hidx);
  const helps = {};
  for (let k = 0; k < nh; k++) helps[rd(hidx + 4 + k * 8)] = trim(b.toString('latin1', rd(hidx + 4 + k * 8 + 4) + 1, rd(hidx + 4 + k * 8 + 4) + HLEN));
  const items = [];
  for (let i = 0; i < n; i++) {
    const r = rd(iidx + 4 + i * 8 + 4);
    const isProg = b[r + 41] === 1;
    items.push({
      title: trim(b.toString('latin1', r + 1, r + 41)), isProg,
      password: trim(b.toString('latin1', r + 42, r + 50)),
      cmd: isProg ? trim(b.toString('latin1', r + 56, r + ILEN)) : trim(b.toString('latin1', r + 52, r + 64)),
      help: helps[rd(r + 50)] || '',
    });
  }
  return items;
}

// SHELL.CLR: ARM-DOS's own small format (the real one is a 4,406-byte table of
// panel colour records); "ADSHCLR", 0, then the scheme number 1-4
export function buildClr(scheme = 1) {
  const b = Buffer.alloc(16);
  b.write('ADSHCLR', 0, 'latin1');
  b[8] = scheme;
  return b;
}

if (process.argv[1] && import.meta.url === `file://${path.resolve(process.argv[1])}`) {
  const a = process.argv.slice(2);
  if (a[0] === '--clr') {
    fs.writeFileSync(a[1], buildClr(1));
  } else if (a[0] === '--dump') {
    for (const it of parseMeu(fs.readFileSync(a[1]))) console.log(JSON.stringify(it));
  } else {
    const spec = JSON.parse(fs.readFileSync(a[0], 'utf8'));
    const out = a[1];
    const hi = a.indexOf('--have');
    const have = hi >= 0 ? new Set(a[hi + 1].split(/\s+/).filter(Boolean).map((s) => s.toUpperCase())) : null;
    for (const [file, items] of Object.entries(spec)) {
      if (file === 'comment') continue;
      const keep = items.filter((it) => !it.requires || !have || have.has(it.requires.toUpperCase()));
      fs.mkdirSync(out, { recursive: true });
      // written only when it changes: make runs this every time (the programs
      // that exist decide the items), the disk images follow only real changes
      const dst = path.join(out, file), data = buildMeu(keep.slice(0, 16));
      if (!fs.existsSync(dst) || !fs.readFileSync(dst).equals(data)) fs.writeFileSync(dst, data);
    }
  }
}
