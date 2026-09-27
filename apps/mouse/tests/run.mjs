#!/usr/bin/env node
// apps/mouse/tests/run.mjs - MOUSE.COM tests ("make mouse-test").
//
//   node apps/mouse/tests/run.mjs [--keep]
//
// Boots a disk with MOUSE.COM, MTEST.EXE and the kernel's test shell, moves
// the (emulated PS/2) mouse and clicks through the 8042, and checks the
// INT 33h answers (COM1 log of MTEST), the text-mode cursor in screen memory,
// the graphics cursor in mode 13h, the TSR's size and MOUSE OFF.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage } from '../../../disk/mkimage.mjs';
import { rescheck } from '../../ansi/tests/rescheck.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const OUT = B('mouse-test');
fs.mkdirSync(OUT, { recursive: true });
const args = process.argv.slice(2);

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };

function makeImage(name, script) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/MOUSE.COM', dst: 'DOS\\' },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\' },
    { src: 'build/mouse-test/MTEST.EXE', dst: 'T\\' },
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', 'SHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n');
  put('T\\S.TXT', script);
  const { img } = buildImage({
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'MOUSETEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'T'],
  }, ROOT);
  if (args.includes('--keep')) fs.writeFileSync(path.join(OUT, name + '.img'), img);
  fs.rmSync(dir, { recursive: true, force: true });
  return img;
}

const cellAt = (pc, r, c) => { const a = 0xB8000 + (r * 80 + c) * 2; return [pc.cpu.m8[a], pc.cpu.m8[a + 1]]; };
const serialLine = (pc, tag) => (pc.serial.split(/\r?\n/).find((l) => l.startsWith(tag)) || '').trim();

// ---------------------------------------------------------------- resident
{
  console.log('== resident');
  const { from, to, bad } = rescheck(B('obj/MOUSE/MOUSE.elf'), 'm_int10', 'mouse_res_end');
  check(bad.length === 0, `resident part (${to - from} bytes) refers to nothing beyond mouse_res_end` +
    (bad.length ? '\n       ' + bad.join('\n       ') : ''));
}

// ---------------------------------------------------------------- main
{
  console.log('== install, text cursor, clicks, mode 13h, MOUSE OFF');
  const pc = await boot({
    rom: B('rom.bin'),
    hd: makeImage('main', 'mem\nC:\\DOS\\MOUSE.COM\nmem\nC:\\T\\MTEST.EXE\nC:\\DOS\\MOUSE.COM\n' +
      'C:\\DOS\\MOUSE.COM OFF\nC:\\T\\MTEST.EXE /N\nmem\nhalt\n'),
  });
  check(pc.waitText('Mouse driver installed', { timeoutMs: 30000 }), 'banner: Mouse driver installed');
  check(pc.hasText('ARM-DOS Mouse Driver Version 1.00'), 'banner: version line');
  check(pc.waitSerial('T:READY', { timeoutMs: 20000 }), 'MTEST: cursor shown');
  check(serialLine(pc, 'T:RESET') === 'T:RESET FFFF 2', `AX=0000h: ${serialLine(pc, 'T:RESET')}`);
  check(serialLine(pc, 'T:VERSION') === 'T:VERSION 0100 0400', `AX=0024h: ${serialLine(pc, 'T:VERSION')}`);
  check(serialLine(pc, 'T:POS') === 'T:POS 0 320 96', `AX=0003h after reset: ${serialLine(pc, 'T:POS')}`);
  pc.run(100);
  const c0 = cellAt(pc, 12, 40);
  check(c0[1] === 0x70, `text cursor at row 12, column 40 inverts the cell (attribute ${c0[1].toString(16)})`);

  // 80 mickeys right = 80 pixels (8 per 8), 32 down = 16 pixels (16 per 8)
  pc.machine.mouseMove(40, 16); pc.run(50);
  pc.machine.mouseMove(40, 16); pc.run(200);
  const c1 = cellAt(pc, 14, 50), c0b = cellAt(pc, 12, 40);
  check(c1[1] === 0x70 && c0b[1] === 0x07, `cursor moved to row 14, column 50; old cell restored (${c1[1].toString(16)} ${c0b[1].toString(16)})`);
  await pc.png(path.join(OUT, 'text.png'));
  pc.machine.mouseButtons(1); pc.run(100);
  pc.machine.mouseButtons(0); pc.run(200);
  check(pc.waitSerial('T:CLICK', { timeoutMs: 5000 }), 'left click seen through AX=0005h');
  check(serialLine(pc, 'T:CLICK') === 'T:CLICK 400 112', `click position: ${serialLine(pc, 'T:CLICK')}`);
  check(serialLine(pc, 'T:MICKEYS') === 'T:MICKEYS 80 32', `AX=000Bh mickeys: ${serialLine(pc, 'T:MICKEYS')}`);
  check(serialLine(pc, 'T:HANDLER') === 'T:HANDLER 1 2 1 400 112', `event handler (AX=000Ch) called: ${serialLine(pc, 'T:HANDLER')}`);
  check(pc.waitText('Left click at 400,112 (column 50, row 14)', { timeoutMs: 5000 }), 'MTEST printed the click');

  check(pc.waitSerial('T:GFX', { timeoutMs: 5000 }), 'mode 13h: cursor shown');
  pc.run(100);
  check(serialLine(pc, 'T:GFX') === 'T:GFX 320 100', `mode 13h position: ${serialLine(pc, 'T:GFX')}`);
  const px = (x, y) => pc.cpu.m8[0xA0000 + y * 320 + x];
  check(px(160, 100) === 15 && px(161, 101) === 15 && px(159, 100) === 0 && px(160, 99) === 0,
    `arrow drawn with its tip at (160,100): ${px(160, 100)} ${px(161, 101)} ${px(159, 100)} ${px(160, 99)}`);
  await pc.png(path.join(OUT, 'mode13.png'));
  pc.machine.mouseMove(-50, -20); pc.run(200);
  check(px(160, 100) === 0, 'mode 13h: background restored after moving');
  pc.machine.mouseButtons(2); pc.run(100);
  pc.machine.mouseButtons(0); pc.run(100);
  check(pc.waitSerial('T:DONE', { timeoutMs: 5000 }), 'right click ends MTEST');
  check(pc.waitText('Mouse driver already installed', { timeoutMs: 20000 }), 'second MOUSE: already installed');
  check(pc.waitText('Mouse driver removed', { timeoutMs: 20000 }), 'MOUSE OFF: removed');
  check(pc.waitSerial('T:NODRIVER', { timeoutMs: 20000 }) && serialLine(pc, 'T:NODRIVER') === 'T:NODRIVER 0000',
    `no INT 33h driver afterwards: ${serialLine(pc, 'T:NODRIVER')}`);
  // resident size: the free memory before/after (TSHELL "mem" -> T:MEM lines)
  const mems = pc.serial.split(/\r?\n/).filter((l) => l.startsWith('T:MEM')).map((l) => l.trim());
  console.log('     ' + mems.join('\n     '));
  const free = mems.map((l) => +(/(\d+)/.exec(l) || [0, 0])[1]);
  if (free.length >= 3) {
    check(free[0] - free[1] > 0 && free[0] - free[1] <= 6 * 1024, `resident size ${free[0] - free[1]} bytes (<= 6 KB)`);
    check(free[2] === free[0], 'MOUSE OFF gave all the memory back');
  }
}

console.log(failures ? `\n${failures} failure(s)` : '\nall MOUSE.COM tests passed');
process.exit(failures ? 1 : 0);
