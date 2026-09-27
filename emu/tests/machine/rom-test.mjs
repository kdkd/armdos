#!/usr/bin/env node
// Machine test: boots the device test ROM (emu/tests/rom, built on demand),
// drives it with keys, checks screen text, serial output, callbacks, rendered
// pixels in every graphics mode, and the exit port.
import { execFileSync } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../testkit.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const romDir = join(here, '..', 'rom');
const romOut = join(here, '..', '..', '..', 'build', 'emu-tests', 'rom');   // (build/: make clean removes it)
const rom = join(romOut, 'testrom.bin');
const args = process.argv.slice(2);
if (!existsSync(rom) || args.includes('--rebuild')) execFileSync(join(romDir, 'build.sh'), [romOut], { stdio: 'inherit' });
const outDir = args.includes('--png') ? join(romOut, 'png') : null;
if (outDir) mkdirSync(outDir, { recursive: true });

let fails = 0, passes = 0;
const check = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log('FAIL ' + name + (extra ? ': ' + extra : '')); } };

for (const jit of [false, true]) {
  // disk images: HD with a boot signature, 1.44 MB floppy with a marker
  const hd = new Uint8Array(32 * 1024 * 1024);
  hd.set([0xEA, 0x12, 0x34, 0x56], 0); hd[510] = 0x55; hd[511] = 0xAA;
  const fd = new Uint8Array(1474560); fd.set([0x46, 0x4C, 0x4F, 0x50], 0); fd[510] = 0x55; fd[511] = 0xAA;
  const events = { speaker: [], disk: [], modes: [] };
  const pc = await boot({
    rom, hd, fd, jit,
    onSpeaker: (on, f) => events.speaker.push([on, Math.round(f)]),
    onDiskActivity: (d, lba, n, w) => events.disk.push([d, lba, n, w]),
    onModeChange: (mo) => events.modes.push(mo),
  });
  const tag = jit ? '[jit] ' : '[interp] ';
  check(tag + 'reaches key prompt', pc.waitText('ESC continues', { timeoutMs: 5000 }), pc.screen());
  const scr = pc.screen();
  check(tag + 'no FAIL lines', !scr.includes('[FAIL]'), scr);
  check(tag + 'all OK lines', (pc.serial.match(/\[ OK \]/g) || []).length === 17 && !pc.serial.includes('FAIL'), pc.serial);
  check(tag + 'serial transcript', pc.serial.includes('ATA model \'ARM-PC FIXED DISK\''));
  check(tag + 'speaker 440 Hz on/off', events.speaker.some(([on, f]) => on && f === 440) && events.speaker.at(-1)[0] === false, JSON.stringify(events.speaker));
  check(tag + 'disk activity hd read', events.disk.some(([d, l, n, w]) => d === 0x80 && l === 0 && !w));
  check(tag + 'disk activity hd write', events.disk.some(([d, l, n, w]) => d === 0x80 && l === 5 && n === 2 && w));
  check(tag + 'disk activity fd read', events.disk.some(([d, l, n, w]) => d === 0 && l === 0 && n === 2 && !w));
  check(tag + 'hd write landed in image', hd[5 * 512 + 2] === 3 && hd[6 * 512 + 0] === 7);
  // text rendering: 720x400, title bar blue background
  let img = pc.render();
  check(tag + 'text 720x400', img.width === 720 && img.height === 400);
  const at = (x, y) => { const k = (y * img.width + x) * 4; return [img.data[k], img.data[k + 1], img.data[k + 2]]; };
  const abRow = pc.lines().findIndex((l) => l.startsWith(' ab  ab'));
  check(tag + 'colour bar: cell 1 blue bg, cell 6 brown bg', at(4 * 9 + 1, abRow * 16 + 1).join() === '0,0,170' && at(24 * 9 + 1, abRow * 16 + 1).join() === '170,85,0', at(4 * 9 + 1, abRow * 16 + 1) + ' ' + at(24 * 9 + 1, abRow * 16 + 1));
  check(tag + 'cell 1 fg (light brown/yellow) pixels', (() => { for (let y = 0; y < 16; y++) for (let x = 0; x < 9; x++) if (at(5 * 9 + x, abRow * 16 + y).join() === '255,255,85') return true; return false; })());
  if (outDir) await pc.png(join(outDir, `text${jit ? '-jit' : ''}.png`));
  // keys + mouse
  pc.machine.mouseMove(5, -3);
  pc.type('ab');
  check(tag + 'keys echoed', pc.waitText('key 30', { timeoutMs: 2000 }) && pc.hasText('key 1E'));
  check(tag + 'mouse packet', pc.hasText('m08 m05 m03'), pc.screen());
  pc.type('{ESC}');
  check(tag + 'mode 13h', pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 2000 }));
  pc.run(50);
  img = pc.render();
  check(tag + '13h 320x200', img.width === 320 && img.height === 200);
  check(tag + '13h pixel (1,0) = DAC1 magenta', at(1, 0).join() === '255,0,255', at(1, 0).join());
  check(tag + '13h pixel (15,0) = white', at(15, 0).join() === '255,255,255', at(15, 0).join());
  check(tag + '13h pixel (40,0) default palette 40', at(40, 0).join() === '255,0,0', at(40, 0).join());
  check(tag + '13h pixel (20,0) default palette grey', at(20, 0).join() === '56,56,56', at(20, 0).join());
  if (outDir) await pc.png(join(outDir, `mode13${jit ? '-jit' : ''}.png`));
  pc.type(' ');
  check(tag + 'mode 4', pc.until(() => pc.machine.vga.mode === 0x04, { timeoutMs: 2000 }));
  pc.run(50); img = pc.render();
  check(tag + 'mode 4 320x200', img.width === 320 && img.height === 200);
  // x in 0..79 byte columns: col 20-39 -> 0x55 -> colour 1 of palette 1 = cyan
  check(tag + 'mode 4 cyan', at(100, 0).join() === '85,255,255', at(100, 0).join());
  if (outDir) await pc.png(join(outDir, `mode4${jit ? '-jit' : ''}.png`));
  pc.type(' ');
  check(tag + 'mode 6', pc.until(() => pc.machine.vga.mode === 0x06, { timeoutMs: 2000 }));
  pc.run(50); img = pc.render();
  check(tag + 'mode 6 640x200', img.width === 640 && img.height === 200);
  check(tag + 'mode 6 pixels', at(1, 0).join() === '255,255,255' && at(0, 0).join() === '0,0,0', at(0, 0) + ' ' + at(1, 0));
  if (outDir) await pc.png(join(outDir, `mode6${jit ? '-jit' : ''}.png`));
  pc.type(' ');
  check(tag + 'exit port', pc.waitExit({ timeoutMs: 3000 }) && pc.exitCode === 0, 'exit ' + pc.exitCode);
  check(tag + 'final text', pc.hasText('TEST ROM DONE'));
  check(tag + 'mode callbacks', events.modes.join() === '3,19,4,6,3', events.modes.join());
}
console.log(`rom-test: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
