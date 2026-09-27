#!/usr/bin/env node
// ARM-DOS itself on the modem: machine A boots build/rom.bin + build/hd.img and talks to
// its internal modem the DOS way — `ECHO ATDT5551989>COM2` (kernel COM2 device -> BIOS
// INT 14h DX=1 -> the 16550 at 2F8h) and `COPY COM2 CON`. Machine B is a bare machine
// whose COM2 the test drives directly (auto-answer). Checks BIOS 40:02 / INT 11h too.
// Skipped (exit 0) when the images have not been built.
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { boot } from '../../testkit.mjs';
import { Machine } from '../../machine.mjs';
import { PhoneExchange } from '../../phone.mjs';

const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const ROM = ROOT + 'build/rom.bin', HD = ROOT + 'build/hd.img';
if (!existsSync(ROM) || !existsSync(HD)) { console.log('modem-dos: skipped (no build/rom.bin or build/hd.img)'); process.exit(0); }

let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name} ${extra}`); } };

const ex = new PhoneExchange();
const pc = await boot({ rom: ROM, hd: HD, phone: ex, phoneNumber: '555-1000' });
const b = new Machine({ jit: false, rtcBaseMs: 0, phone: ex, phoneNumber: '555-1989' });
b.cpu.halted = 1; b.cpu.i = 1;
const P = 0x2F8;
let brx = '';
const bpoll = () => { while (b.in8(P + 5) & 1) brx += String.fromCharCode(b.in8(P)); };
const bsend = (s) => { for (const c of s) { while (!(b.in8(P + 5) & 0x20)) b.runFor(0.5); b.out8(P, c.charCodeAt(0)); } };
b.out8(P + 3, 0x80); b.out8(P, 12); b.out8(P + 1, 0); b.out8(P + 3, 3); b.out8(P + 2, 0xC7); b.out8(P + 4, 0x0B);
bsend('ATS0=1\r');
const both = (ms) => { for (let t = 0; t < ms; t += 5) { pc.run(5); b.runFor(5); bpoll(); } };
const until = (pred, ms) => { for (let t = 0; t < ms; t += 5) { pc.run(5); b.runFor(5); bpoll(); if (pred()) return true; } return false; };

// the factory disk may boot into DOSSHELL: Shift+F9 is its command prompt
pc.until(() => pc.hasText('C:\\>') || pc.hasText('Shift+F9=Command Prompt'), { timeoutMs: 60000 });
if (!pc.hasText('C:\\>')) { pc.type('{SHIFT+F9}'); pc.run(2000); }
ok('boots to a DOS prompt', pc.until(() => /[A-Z]:\\[^>\n]*>/.test(pc.screen()), { timeoutMs: 30000 }), pc.screen());
const m8 = pc.machine.cpu.m8;
ok('BDA 40:02 = 2F8h', (m8[0x402] | m8[0x403] << 8) === 0x2F8);
ok('equipment word: 2 serial ports', ((m8[0x410] | m8[0x411] << 8) >> 9 & 7) === 2);
brx = '';
pc.type('ECHO ATDT5551989>COM2\r');
ok('the modem goes off hook and dials', until(() => pc.machine.modem.state === 'dialing', 10000));
ok('B rings and answers', until(() => brx.includes('CONNECT 2400'), 30000), JSON.stringify(brx));
ok('A connected', until(() => pc.machine.modem.carrier, 5000));
both(300);
pc.type('COPY COM2 CON\r');
both(1500);
brx = '';
bsend('Welcome to The ARM Pit BBS\r\n\x1A');
ok('COPY COM2 CON shows what B sent', until(() => pc.hasText('Welcome to The ARM Pit BBS'), 10000), pc.screen());
ok('COPY finishes', until(() => /1 file\(s\) copied/i.test(pc.screen()), 5000), pc.screen());
pc.type('ECHO Hi from ARM-DOS>COM2\r');
ok('B receives the DOS side', until(() => brx.includes('Hi from ARM-DOS'), 10000), JSON.stringify(brx));
b.out8(P + 4, 0x0A);   // B drops DTR: hang up
ok('A sees carrier drop', until(() => !pc.machine.modem.carrier && !pc.machine.modem.hook, 5000));

console.log(`modem-dos: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
