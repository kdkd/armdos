#!/usr/bin/env node
// apps/fdisk/tests/run.mjs - FDISK.EXE (Microsoft's MS-DOS 4.0 FDISK compiled
// for ARM) against the real MS-DOS 4.00 FDISK, screen by screen: the same
// keys typed into both, the real screens captured in DOSBox-X
// (apps/mslib/tools/dos400run.sh KEYS=tests/real/keys-*.txt, decoded to
// tests/expected/*.txt: 25 text rows + attribute runs), ours read from the
// ARM-PC's text buffer.  The banner differs on purpose (ARM-DOS branding).
// The partition table that FDISK writes is checked on the disk image.
//
//   node apps/fdisk/tests/run.mjs [scenario ...] [--show]
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { session, Checker, need, B } from '../../mslib/tests/harness.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
need(['build/FDISK.EXE', 'build/ktest/TSHELL.EXE', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin']);
const args = process.argv.slice(2);
const SHOW = args.includes('--show');
const only = args.filter((a) => !a.startsWith('--'));
const t = new Checker('fdisk');

const BRAND = [['MS-DOS Version 4.00', 'ARM-DOS Version 4.00'],
  ['(C)Copyright Microsoft Corp. 1983, 1988', '(C)Copyright Europa Micro Systems 1988']];

function expected(name) {
  const [text, attrs] = fs.readFileSync(path.join(HERE, 'expected', name + '.txt'), 'utf8').split('--attributes\n');
  let rows = text.split('\n').slice(0, 25);
  rows = rows.map((r) => BRAND.reduce((s, [a, b]) => s.replace(a, b), r));
  // attribute runs "NN: a-b=BG/FG ..." -> per cell fg (null for blank cells)
  const fg = [];
  for (const line of (attrs || '').trim().split('\n')) {
    const m = line.match(/^(\d\d): (.*)$/);
    if (!m) continue;
    const row = new Array(80).fill(null);
    for (const run of m[2].split(' ')) {
      const r = run.match(/^(\d+)-(\d+)=([0-9A-F])\/([0-9A-F-])$/);
      if (!r) continue;
      for (let c = +r[1]; c <= +r[2]; c++) row[c] = r[4] === '-' ? null : { bg: parseInt(r[3], 16), fg: parseInt(r[4], 16) };
    }
    fg[+m[1]] = row;
  }
  return { rows, fg };
}

const CP437 = Buffer.from(Array.from({ length: 256 }, (_, i) => i)).toString('latin1').replace(/[\x80-\xff]/g, (x) =>
  '\u00c7\u00fc\u00e9\u00e2\u00e4\u00e0\u00e5\u00e7\u00ea\u00eb\u00e8\u00ef\u00ee\u00ec\u00c4\u00c5\u00c9\u00e6\u00c6\u00f4\u00f6\u00f2\u00fb\u00f9\u00ff\u00d6\u00dc\u00a2\u00a3\u00a5\u20a7\u0192\u00e1\u00ed\u00f3\u00fa\u00f1\u00d1\u00aa\u00ba\u00bf\u2310\u00ac\u00bd\u00bc\u00a1\u00ab\u00bb\u2591\u2592\u2593\u2502\u2524\u2561\u2562\u2556\u2555\u2563\u2551\u2557\u255d\u255c\u255b\u2510\u2514\u2534\u252c\u251c\u2500\u253c\u255e\u255f\u255a\u2554\u2569\u2566\u2560\u2550\u256c\u2567\u2568\u2564\u2565\u2559\u2558\u2552\u2553\u256b\u256a\u2518\u250c\u2588\u2584\u258c\u2590\u2580\u03b1\u00df\u0393\u03c0\u03a3\u03c3\u00b5\u03c4\u03a6\u0398\u03a9\u03b4\u221e\u03c6\u03b5\u2229\u2261\u00b1\u2265\u2264\u2320\u2321\u00f7\u2248\u00b0\u2219\u00b7\u221a\u207f\u00b2\u25a0\u00a0'[x.charCodeAt(0) - 128]);
function screen(pc) {
  const m8 = pc.cpu.m8, rows = [], attr = [];
  for (let r = 0; r < 25; r++) {
    let s = '';
    const a = [];
    for (let c = 0; c < 80; c++) {
      const off = 0xB8000 + (r * 80 + c) * 2;
      s += m8[off] < 32 ? ' ' : CP437[m8[off]];
      a.push(m8[off + 1]);
    }
    rows.push(s.trimEnd());
    attr.push(a);
  }
  return { rows, attr };
}

function compare(pc, name, what, subst = []) {
  const e = expected(name), s = screen(pc);
  e.rows = e.rows.map((r) => subst.reduce((x, [a, b]) => x.replace(a, b), r));
  const textOk = e.rows.every((r, i) => r === s.rows[i]);
  let attrBad = 0, firstBad = '';
  for (let r = 0; r < 25; r++) for (let c = 0; c < 80; c++) {
    const x = e.fg[r] && e.fg[r][c];
    if (!x) continue;                                  // blank cell in the capture
    const a = s.attr[r][c];
    if ((a & 0x0F) !== x.fg || ((a >> 4) & 7) !== (x.bg & 7)) { if (!attrBad++) firstBad = `row ${r} col ${c}: ${a.toString(16)} vs ${x.bg.toString(16)}${x.fg.toString(16)}`; }
  }
  const diff = e.rows.map((r, i) => r === s.rows[i] ? null : `  ${String(i).padStart(2)} real |${r}|\n     ours |${s.rows[i]}|`).filter(Boolean).join('\n');
  t.ok(textOk, `${what}: screen text as the real FDISK (${name})`, diff);
  t.ok(!attrBad, `${what}: colours as the real FDISK (${name})`, `${attrBad} cells differ, first ${firstBad}`);
  if (SHOW) console.log(s.rows.map((r, i) => `${String(i).padStart(2)}|${r}`).join('\n'));
}

// the disks have the verified 32 MB disk's geometry (65 cylinders x 16 x 63)

async function fdisk(name, { blank = false, hd = null, steps, end = 'exit' }) {
  const s = await session({
    name, outDir: B('apps-test'), programs: ['build/FDISK.EXE'],
    script: ['FDISK.EXE', 'echo FDISK-ENDED'], work: false,
    ...(blank || hd ? { floppy: true, hd: hd || Buffer.alloc(65 * 16 * 63 * 512) } : { cylinders: 65 }),
    drive: async (pc) => {
      if (!pc.until(() => screen(pc).rows[4].includes('FDISK Options'), { timeoutMs: 30000 })) {
        t.ok(false, `${name}: FDISK main menu appears`, pc.screen());
        return false;
      }
      pc.run(300);
      for (const st of steps) {
        if (st.keys) { pc.type(st.keys); pc.run(st.ms || 1500); }
        if (st.check) compare(pc, st.check, `${name}: ${st.what || st.check}`, st.subst);
        if (st.fn) await st.fn(pc);
      }
      if (end === 'exit') return pc.waitExit({ timeoutMs: 30000 });
      return true;
    },
  });
  return s;
}

// the partition table FDISK wrote vs the real one's (expected/*-mbr.bin), and
// the boot code: the ARM-DOS MBR (disk/mbr.S) instead of the x86 one
async function checkMbr(name, img, want) {
  const { MBR_BOOT } = await import('../../../disk/mkimage.mjs');
  const real = fs.readFileSync(path.join(HERE, 'expected', want));
  const hex = (b) => [0, 1, 2, 3].map((i) => b.subarray(0x1BE + 16 * i, 0x1CE + 16 * i).toString('hex')).join(' ');
  t.ok(img.subarray(0x1BE, 0x200).equals(real.subarray(0x1BE, 0x200)), `${name}: partition table and 55AA as the real FDISK wrote them`,
    `real ${hex(real)}\nours ${hex(img)}`);
  t.ok(img.subarray(0, MBR_BOOT.length).equals(MBR_BOOT) && !img.subarray(MBR_BOOT.length, 0x1BE).some((x) => x),
    `${name}: boot code = the ARM-DOS MBR (disk/mbr.S)`);
}

const scenarios = {
  // tests/real/keys-a.txt on the partitioned disk: look around, change nothing
  async display() {
    const before = (await import('../../../disk/mkimage.mjs'));
    void before;
    const s = await fdisk('display', {
      steps: [
        { check: 'a1-main', what: 'main menu' },
        { keys: '4', ms: 600, check: 'a2-choice4', what: 'typed 4' },
        { keys: '\r', check: 'a3-display', what: 'Display partition information' },
        { keys: '{ESC}', check: 'a4-main-again', what: 'Esc: back to the options' },
        { keys: '1\r', check: 'a5-create-menu', what: 'Create menu' },
        { keys: '1\r', check: 'a6-pri-exists', what: 'Create primary: already exists' },
        { keys: '{ESC}', check: 'a7-main-again', what: 'Esc' },
        { keys: '{ESC}', ms: 3000 },
      ],
    });
    t.ok(s.finished, 'display: FDISK ended and the script ran on');
    t.ok(s.serial.includes('T:EXIT FDISK.EXE 0'), 'display: exit code 0', s.serial.slice(-300));
  },

  // tests/real/keys-b1.txt: blank disk, primary partition of maximum size,
  // restart; then the disk alone boots through the MBR FDISK wrote
  async create() {
    let restarted = false;
    const s = await fdisk('create', {
      blank: true, end: 'reboot',
      steps: [
        { check: 'b1-main', what: 'main menu (blank disk)' },
        { keys: '1\r', check: 'b1-create-menu', what: 'Create menu' },
        { keys: '1\r', check: 'b1-max-yn', what: 'Create primary: maximum size? [Y]' },
        { keys: '\r', ms: 3000, check: 'b1-restart', what: 'System will now restart' },
        { keys: ' ', ms: 200, fn: async (pc) => {
          // reboot() = warm boot through the reset vector: POST, A: again, FDISK again
          const n0 = (pc.serial.match(/T:SHELL start/g) || []).length;
          restarted = pc.until(() => (pc.serial.match(/T:SHELL start/g) || []).length > n0, { timeoutMs: 30000 });
          pc.until(() => screen(pc).rows[4].includes('FDISK Options'), { timeoutMs: 30000 });
        } },
      ],
    });
    t.ok(restarted, 'create: the machine restarted (warm boot) and ran the script again');
    const img = s.after;
    await checkMbr('create', img, 'b1-mbr.bin');
    // boot the disk alone: the ARM MBR loads the (unformatted: F6 filled)
    // partition's first sector and says so, like the x86 original
    const { boot } = await import('../../../emu/testkit.mjs');
    const pc = await boot({ rom: B('rom.bin'), hd: new Uint8Array(img) });
    const ok = pc.waitText('Missing operating system', { timeoutMs: 20000 });
    t.ok(ok, 'create: booting the new disk runs the ARM MBR FDISK wrote ("Missing operating system")', pc.screen());
  },

  // tests/real/keys-b2.txt: 10 MB primary, extended partition, set active
  async partitions() {
    const s = await fdisk('partitions', {
      blank: true, end: 'reboot',
      steps: [
        { keys: '1\r' },
        { keys: '1\r' },
        { keys: 'n\r', check: 'b2-size', what: 'primary: size prompt' },
        { keys: '10\r', ms: 3000, check: 'b2-pri-created', what: '10 Mbytes: created' },
        { keys: '{ESC}', check: 'b2-main-warning', what: 'main menu: no partition active' },
        { keys: '1\r' },
        { keys: '2\r', check: 'b2-ext-size', what: 'extended: size prompt' },
        { keys: '\r', ms: 3000, check: 'b2-ext-created', what: 'extended: created' },
        { keys: '\r', ms: 600, check: 'b2-ext-created', what: 'Enter is not Esc' },
        { keys: '{ESC}', check: 'b2-logical', what: 'logical drive screen' },
        { keys: '{ESC}', check: 'b2-main-warning2', what: 'main menu' },
        { keys: '2\r', check: 'b2-active-prompt', what: 'set active: prompt' },
        { keys: '1', ms: 600, check: 'b2-active-typed', what: 'set active: typed 1' },
        { keys: '\r', ms: 2000, check: 'b2-made-active', what: 'partition 1 made active' },
        { keys: '{ESC}', check: 'b2-main', what: 'main menu' },
        { keys: '4\r', check: 'b2-display', what: 'display' },
        { keys: 'y\r', ms: 600, check: 'b2-display', what: 'display waits for Esc' },
        { keys: '{ESC}', check: 'b2-main2', what: 'main menu' },
        { keys: '{ESC}', ms: 3000, check: 'b2-restart', what: 'System will now restart' },
      ],
    });
    await checkMbr('partitions', s.after, 'b2-mbr.bin');
    images.partitions = s.after;
  },

  // tests/real/keys-c.txt: a logical drive in that extended partition
  async logical() {
    if (!images.partitions) await scenarios.partitions();
    const s = await fdisk('logical', {
      hd: images.partitions, end: 'reboot',
      steps: [
        { check: 'c-main', what: 'main menu' },
        { keys: '1\r', check: 'c-create-menu', what: 'create menu' },
        { keys: '3\r', check: 'c-logical-prompt', what: 'logical drive: size prompt' },
        { keys: '10', ms: 800, check: 'c-logical-typed', what: 'logical drive: typed 10' },
        { keys: '\r', ms: 3000, check: 'c-logical-created', what: 'logical drive created' },
        { keys: '{ESC}', check: 'c-main2', what: 'main menu' },
        { keys: '{ESC}', ms: 3000, check: 'c-restart', what: 'System will now restart' },
      ],
    });
    await checkMbr('logical', s.after, 'c-mbr.bin');
    const ebr = s.after.subarray(21168 * 512, 21169 * 512), real = fs.readFileSync(path.join(HERE, 'expected/c-ebr.bin'));
    t.ok(ebr.subarray(0x1BE).equals(real.subarray(0x1BE)), 'logical: the extended boot record as the real FDISK wrote it',
      `real ${real.subarray(0x1BE).toString('hex')}\nours ${ebr.subarray(0x1BE).toString('hex')}`);
    images.logical = s.after;
  },

  // tests/real/keys-d.txt: delete the logical drive, the extended and the primary partition
  async delete() {
    if (!images.logical) await scenarios.logical();
    const s = await fdisk('delete', {
      hd: images.logical, end: 'reboot',
      steps: [
        { check: 'd-main', what: 'main menu' },
        { keys: '3\r', check: 'd-delete-menu', what: 'delete menu' },
        { keys: '3\r', check: 'd-logical', what: 'delete logical drive' },
        { keys: 'd\r', check: 'd-label', what: 'drive D: volume label prompt' },
        { keys: '\r', check: 'd-sure', what: 'are you sure' },
        { keys: 'y\r', ms: 3000, check: 'd-drive-deleted', what: 'drive deleted' },
        { keys: '{ESC}', check: 'd-no-logical', what: 'no logical drives' },
        { keys: '{ESC}', check: 'd-main2', what: 'main menu' },
        { keys: '3\r', check: 'd-delete-menu2', what: 'delete menu' },
        { keys: '2\r', check: 'd-ext-sure', what: 'delete extended: continue?' },
        { keys: 'y\r', ms: 3000, check: 'd-ext-deleted', what: 'extended deleted' },
        { keys: '{ESC}', check: 'd-main3', what: 'main menu' },
        { keys: '3\r', check: 'd-delete-menu3', what: 'delete menu' },
        { keys: '1\r', check: 'd-pri-sure', what: 'delete primary: continue?' },
        { keys: 'y\r', ms: 3000, check: 'd-pri-deleted', what: 'primary deleted' },
        { keys: '{ESC}', check: 'd-main4', what: 'main menu' },
        { keys: '{ESC}', ms: 3000, check: 'd-restart', what: 'System will now restart' },
      ],
    });
    await checkMbr('delete', s.after, 'd-mbr.bin');
  },

  // tests/real: FDISK's command line (/PRI: /EXT: /LOG: /Q) and its parse
  // errors, on a blank disk - no screens, errorlevels and the tables it wrote
  async cmdline() {
    const cmds = ['/X', '3', '/PRI:5', '1 /PRI', '1 /PRI:10 /EXT:20 /LOG:10 /Q', '1 /PRI:10 /Q', '1 /Q', '1 /PRI:10 /EXT:5000 /Q'];
    const s = await session({
      name: 'cmdline', outDir: B('apps-test'), programs: ['build/FDISK.EXE'], work: false,
      floppy: true, hd: Buffer.alloc(65 * 16 * 63 * 512),
      script: ['echo ERRSTART', ...cmds.map((c) => 'FDISK.EXE ' + c), 'echo ERREND'],
    });
    const scr = s.pc.screen().split('\n');
    const got = scr.slice(scr.indexOf('ERRSTART') + 1, scr.indexOf('ERREND')).join('\n');
    const want = fs.readFileSync(path.join(HERE, 'expected/e-cmdline.txt'), 'utf8').trim();
    t.same(Buffer.from(got), Buffer.from(want), 'cmdline: messages as the real FDISK');
    const codes = s.exits.map((e) => e.code);
    t.ok(codes[4] === 0 && codes[5] === 0 && codes[6] === 1, 'cmdline: errorlevels 0, 0, 1 for /Q runs (as real)', codes.join(' '));
    await checkMbr('cmdline', s.after, 'e-mbr.bin');
    const ebr = s.after.subarray(21168 * 512, 21169 * 512), real = fs.readFileSync(path.join(HERE, 'expected/e-ebr.bin'));
    t.ok(ebr.subarray(0x1BE).equals(real.subarray(0x1BE)), 'cmdline: the logical drive (/LOG:10) as the real FDISK wrote it',
      `real ${real.subarray(0x1BE).toString('hex')}\nours ${ebr.subarray(0x1BE).toString('hex')}`);
  },
};
const images = {};

for (const [n, f] of Object.entries(scenarios)) if (!only.length || only.includes(n)) await f();
t.done();
