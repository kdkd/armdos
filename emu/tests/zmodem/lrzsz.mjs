#!/usr/bin/env node
// Validates emu/zmodem.mjs against lrzsz: our receiver <- `sz`, our sender -> `rz`, over pipes.
// Includes line noise injection (bytes flipped on the way) to exercise ZRPOS recovery.
// Skipped when sz/rz are not installed (apt install lrzsz).
import { spawn, spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, readFileSync, readdirSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { ZReceiver, ZSender } from '../../zmodem.mjs';

if (spawnSync('sh', ['-c', 'command -v sz && command -v rz']).status !== 0) { console.log('zmodem: skipped (lrzsz not installed)'); process.exit(0); }
let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };
const dir = mkdtempSync(join(tmpdir(), 'zm-'));
let seed = 12345;
const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296;
function blob(n) { const b = new Uint8Array(n); for (let i = 0; i < n; i++) b[i] = rnd() < 0.3 ? [0x18, 0x11, 0x13, 0x0D, 0x7F, 0xFF, 0x2A, 0x10][i % 8] : (rnd() * 256) | 0; return b; }
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

function withTimeout(p, ms, what) { return Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error('timeout: ' + what)), ms))]); }

// noise: corrupt one byte every `every` bytes (0 = clean)
function noisy(every) { let n = 0; return (buf) => { if (!every) return buf; const b = Buffer.from(buf); for (let i = 0; i < b.length; i++) if (++n % every === 0) b[i] ^= 0x55; return b; }; }

async function receiveFromSz(files, noiseEvery = 0) {
  const paths = files.map((f) => { const p = join(dir, f.name); writeFileSync(p, f.data); return p; });
  const sz = spawn('sz', ['-q', '-b', ...paths], { stdio: ['pipe', 'pipe', 'ignore'] });
  const got = [];
  const noise = noisy(noiseEvery);
  const done = new Promise((resolve) => {
    const rx = new ZReceiver({
      send: (bytes) => { try { sz.stdin.write(Buffer.from(bytes)); } catch {} },
      onFile: (f) => got.push(f),
      onDone: (okk, why) => resolve({ ok: okk, why }),
      timeoutMs: 1500,
    });
    sz.stdout.on('data', (d) => rx.feed(noise(d), Date.now()));
    const t = setInterval(() => rx.tick(Date.now()), 200);
    sz.on('exit', () => setTimeout(() => { clearInterval(t); rx.finish(true, 'sz exited'); }, 300));
    rx.start(Date.now());
  });
  const r = await withTimeout(done, 60000, 'sz');
  const code = await new Promise((res) => (sz.exitCode !== null ? res(sz.exitCode) : sz.on('exit', res)));
  return { ...r, got, code };
}

async function sendToRz(files, noiseEvery = 0) {
  const out = mkdtempSync(join(dir, 'rz-'));
  const rz = spawn('rz', ['-q', '-b', '-y'], { cwd: out, stdio: ['pipe', 'pipe', 'ignore'] });
  const noise = noisy(noiseEvery);
  let t;
  const done = new Promise((resolve) => {
    const tx = new ZSender({
      files,
      send: (bytes) => { try { rz.stdin.write(noise(Buffer.from(bytes))); } catch {} },
      onDone: (okk, why) => { setTimeout(() => clearInterval(t), 200); resolve({ ok: okk, why }); },
      timeoutMs: 1500,
    });
    if (process.env.ZDEBUG) { const oh = tx.header.bind(tx); tx.header = (h) => { console.log('<', h.type, h.pos, tx.state, tx.pos, tx.tries); oh(h); }; const ot = tx.tick.bind(tx); tx.tick = (n) => { const a = tx.state + tx.tries; ot(n); if (tx.state + tx.tries !== a) console.log('tick', a, '->', tx.state + tx.tries); }; }
    rz.stdout.on('data', (d) => tx.feed(d, Date.now()));
    // pump data as a line would: a few KB at a time
    t = setInterval(() => {
      tx.tick(Date.now());
      const chunk = tx.produce(256);
      if (chunk.length) { try { rz.stdin.write(noise(Buffer.from(chunk))); } catch {} }
    }, 2);
    tx.start(Date.now());
    rz.on('exit', () => setTimeout(() => clearInterval(t), 100));
  });
  const r = await withTimeout(done, 60000, 'rz');
  const code = await new Promise((res) => (rz.exitCode !== null ? res(rz.exitCode) : rz.on('exit', res)));
  const recv = readdirSync(out).map((n) => ({ name: n, data: new Uint8Array(readFileSync(join(out, n))) }));
  return { ...r, code, recv };
}

const f1 = { name: 'HELLO.TXT', data: new TextEncoder().encode('Hello from the host link!\r\n'), mtime: 600000000 };
const f2 = { name: 'BLOB.BIN', data: blob(70000), mtime: 600000000 };
const f3 = { name: 'EMPTY.DAT', data: new Uint8Array(0), mtime: 600000000 };

{
  const r = await receiveFromSz([f1, f2, f3]);
  ok('sz -> ZReceiver completes', r.ok && r.code === 0, JSON.stringify({ ok: r.ok, why: r.why, code: r.code }));
  ok('3 files', r.got.length === 3, String(r.got.length));
  ok('file 1 intact', r.got[0] && r.got[0].name === 'HELLO.TXT' && same(r.got[0].data, f1.data));
  ok('file 2 intact (70000 bytes of escapes)', r.got[1] && same(r.got[1].data, f2.data));
  ok('empty file', r.got[2] && r.got[2].data.length === 0 && r.got[2].name === 'EMPTY.DAT');
}
{
  const r = await receiveFromSz([f2], 3001);
  ok('sz -> ZReceiver with line noise recovers', r.ok && r.got[0] && same(r.got[0].data, f2.data), JSON.stringify({ ok: r.ok, why: r.why, n: r.got.length }));
}
{
  const r = await sendToRz([f1, f2, f3]);
  ok('ZSender -> rz completes', r.ok && r.code === 0, JSON.stringify({ ok: r.ok, why: r.why, code: r.code }));
  const by = Object.fromEntries(r.recv.map((f) => [f.name, f.data]));
  ok('rz got HELLO.TXT', by['HELLO.TXT'] && same(by['HELLO.TXT'], f1.data), Object.keys(by).join(','));
  ok('rz got BLOB.BIN', by['BLOB.BIN'] && same(by['BLOB.BIN'], f2.data));
  ok('rz got EMPTY.DAT', by['EMPTY.DAT'] && by['EMPTY.DAT'].length === 0);
}
{
  const r = await sendToRz([f2], 2999);
  const got = r.recv.find((f) => f.name === 'BLOB.BIN');
  ok('ZSender -> rz with line noise recovers', r.ok && got && same(got.data, f2.data), JSON.stringify({ ok: r.ok, why: r.why, code: r.code, len: got?.data.length }));
}
rmSync(dir, { recursive: true, force: true });
console.log(`zmodem: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
