#!/usr/bin/env node
// Host Link (555-0100) end to end: a machine dials it through its COM2 modem at 2400 bps; the
// "terminal program" is this test, forwarding the UART to lrzsz (rz for [D]ownload, sz for
// [U]pload), so the Host Link's ZMODEM is checked against a known implementation over the real
// paced modem path. Skipped when lrzsz is missing.
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Machine } from '../../machine.mjs';
import { PhoneExchange } from '../../phone.mjs';
import { HostLink, dosName } from '../../hostlink.mjs';

if (spawnSync('sh', ['-c', 'command -v sz && command -v rz']).status !== 0) { console.log('hostlink: skipped (lrzsz not installed)'); process.exit(0); }
let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };
const dir = mkdtempSync(join(tmpdir(), 'hl-'));

const rate = +(process.argv[2] || 2400);
const ex = new PhoneExchange();
const picked = { name: 'readme from host.txt', data: new Uint8Array(3000).map((_, i) => (i * 31 + (i >> 7)) & 0xFF), mtime: 600000000 };
const saved = [];
const host = new HostLink({ pickFiles: async () => [picked], saveFile: (f) => saved.push(f) });
ex.register('555-0100', host);

const m = new Machine({ jit: false, rtcBaseMs: 0, phone: ex, phoneNumber: '555-2000' });
m.cpu.halted = 1; m.cpu.i = 1;
const B = 0x2F8;
m.out8(B + 3, 0x80); m.out8(B, 6); m.out8(B + 1, 0); m.out8(B + 3, 3); m.out8(B + 2, 0xC7); m.out8(B + 4, 0x0B);   // 19200 DTE
const txq = [];
let rx = '', sink = null;
function poll() {
  while (txq.length && (m.in8(B + 5) & 0x20)) m.out8(B, txq.shift());
  while (m.in8(B + 5) & 1) { const b = m.in8(B); if (sink) sink(b); else rx += String.fromCharCode(b); }
}
const send = (s) => { for (const c of s) txq.push(typeof c === 'number' ? c : c.charCodeAt(0)); };
const tickAll = (ms) => { m.runFor(ms); host.tick(m.timeMs()); poll(); };
async function until(cond, maxMs, what) {
  const end = m.timeMs() + maxMs;
  let n = 0;
  while (m.timeMs() < end) {
    tickAll(2);
    if (cond()) return true;
    if (sink || ++n % 8 === 0) await new Promise((r) => setImmediate(r));
  }
  console.log(`(timeout waiting for ${what}; rx tail ${JSON.stringify(rx.slice(-200))})`);
  return false;
}

/** Run an lrzsz program with the "terminal" connected to it until it exits. */
async function lrzsz(cmd, args, cwd, maxMs) {
  const p = spawn(cmd, args, { cwd, stdio: ['pipe', 'pipe', 'ignore'] });
  p.stdin.on('error', () => {});
  let exited = null;
  p.on('exit', (c) => { exited = c; });
  p.stdout.on('data', (d) => { for (const b of d) txq.push(b); });
  sink = (b) => { try { p.stdin.write(Buffer.from([b])); } catch {} };
  const t0 = m.timeMs();
  await until(() => exited !== null, maxMs, cmd);
  sink = null;
  if (exited === null) p.kill();
  return { code: exited, ms: m.timeMs() - t0 };
}

send(`AT&B${rate}DT555-0100\r`);
ok('host link answers', await until(() => /CONNECT \d+\r\n/.test(rx), 30000, 'CONNECT'), JSON.stringify(rx));
ok(`CONNECT ${rate}`, rx.includes(`CONNECT ${rate}`));
ok('menu', await until(() => rx.includes('Your choice:'), 20000, 'menu'));
ok('menu text', rx.includes('ARM-DOS HOST LINK') && rx.includes('[\x1b[1;33mD') && rx.includes('Download a file from your computer to ARM-DOS'));
rx = '';
send('D');
ok('host starts ZMODEM send', await until(() => rx.includes('**\x18B00'), 20000, 'ZRQINIT'));
const got = mkdtempSync(join(dir, 'rz-'));
const r1 = await lrzsz('rz', ['-b', '-y', '-q'], got, 120000);
const want = dosName(picked.name);
ok('rz exit 0', r1.code === 0, `code ${r1.code}`);
ok('dosName', want === 'README_F.TXT', want);
ok('file arrived intact over the modem', existsSync(join(got, want)) && Buffer.compare(readFileSync(join(got, want)), Buffer.from(picked.data)) === 0);
const cps = 3000 / (r1.ms / 1000);
ok(`paced at the line rate (~${rate / 10} cps)`, cps < rate / 10 * 1.02 && cps > rate / 10 * 0.4, `${cps.toFixed(0)} cps`);
rx = '';
ok('back to the menu', await until(() => rx.includes('Your choice:'), 20000, 'menu 2'));
ok('transfer complete message', rx.includes('Transfer complete.'));
rx = '';
send('U');
ok('upload prompt', await until(() => rx.includes('Start your ZMODEM upload now'), 10000, 'upload prompt'));
const up = new Uint8Array(2500).map((_, i) => (i * 7 + 3) & 0xFF);
writeFileSync(join(dir, 'ARMDOS.BIN'), up);
const r2 = await lrzsz('sz', ['-b', '-q', join(dir, 'ARMDOS.BIN')], dir, 120000);
ok('sz exit 0', r2.code === 0, `code ${r2.code}`);
ok('host saved the upload', saved.length === 1 && saved[0].name === 'ARMDOS.BIN' && Buffer.compare(Buffer.from(saved[0].data), Buffer.from(up)) === 0, JSON.stringify(saved.map((f) => [f.name, f.data.length])));
rx = '';
ok('menu after upload', await until(() => rx.includes('Your choice:'), 20000, 'menu 3'));
ok('received message', rx.includes('1 file(s) received'));
// a ZMODEM upload started straight from the menu (no U first)
rx = '';
writeFileSync(join(dir, 'AUTO.TXT'), 'auto-detected upload\r\n');
const r3 = await lrzsz('sz', ['-b', '-q', join(dir, 'AUTO.TXT')], dir, 60000);
ok('auto-detected upload', r3.code === 0 && saved.length === 2 && saved[1].name === 'AUTO.TXT');
await until(() => rx.includes('Your choice:'), 20000, 'menu 4');
rx = '';
send('G');
ok('goodbye hangs up: NO CARRIER', await until(() => rx.includes('NO CARRIER'), 20000, 'NO CARRIER'), JSON.stringify(rx));
ok('modem on hook', !m.modem.hook && host.state === 'idle');
rmSync(dir, { recursive: true, force: true });
console.log(`hostlink (${rate} bps): ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
