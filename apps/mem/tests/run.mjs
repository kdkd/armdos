#!/usr/bin/env node
// apps/mem/tests/run.mjs - MEM.EXE (Microsoft's MS-DOS 4.0 MEM compiled for
// ARM) on ARM-DOS, checked against the real MS-DOS 4.00 MEM.EXE:
//  - the fixed text (headers, the first map lines, the device driver list of
//    MEM /DEBUG, the summary wording) byte for byte against
//    tests/expected/MEM*.TXT (the real output, DOS 4.00 in DOSBox-X);
//  - every map line in the real format; the map itself against ARM-DOS's
//    real memory (contiguous MCB chain, IO.SYS at 0700h, the ARMDOS.SYS
//    kernel, the CONFIG.SYS sub-blocks, the programs);
//  - the parse errors on the screen against the real ones (expected/ERRORS.TXT,
//    tests/errors.bat) and the exit codes (102 as the real MEM, 1 on errors).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { session, Checker, need, B } from '../../mslib/tests/harness.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const exp = (f) => fs.readFileSync(path.join(HERE, 'expected', f), 'latin1');
need(['build/MEM.EXE', 'build/ktest/TSHELL.EXE', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin']);
const t = new Checker('mem');
const hex = (s) => parseInt(s, 16);
const lines = (b) => b.toString('latin1').split('\r\n');

// the parse errors (as tests/errors.bat, which made expected/ERRORS.TXT)
const errCmds = fs.readFileSync(path.join(HERE, 'errors.bat'), 'latin1').split(/\r?\n/)
  .filter((l) => /^MEM /.test(l)).map((l) => l.replace(/^MEM/, 'MEM.EXE'));

const MAIN = /^  ([0-9A-F]{6})      (.{8})     ([0-9A-F]{6})     (.*)$/;
const SUB = /^                (.{8})   ([0-9A-F]{6})      (.{10}) $/;
const DEV = /^                  (.{8})              (.{10,}) $/;

function checkMap(name, buf, { debug, subs, himem }) {
  const L = lines(buf);
  const real = lines(Buffer.from(exp(debug ? 'MEMDEBUG.TXT' : 'MEMPROG.TXT'), 'latin1'));
  t.same(Buffer.from(L.slice(0, 7).join('\r\n')), Buffer.from(real.slice(0, 7).join('\r\n')), `${name}: title and the fixed first lines as the real MEM`);
  t.ok(/^  000700      IO           [0-9A-F]{6}     System Program$/.test(L[7]), `${name}: IO System Program at 000700h`, L[7]);
  let i = 8;
  if (debug) {
    const realDev = real.slice(8, 20), dev = L.slice(8, 20);
    t.same(Buffer.from(dev.join('\r\n')), Buffer.from(realDev.join('\r\n')), `${name}: the 12 IO.SYS device drivers as on real DOS 4.00`);
    i = 20;
  }
  const io = L[7].match(MAIN);
  t.ok(L[i] === '', `${name}: blank line after IO`);
  const dos = L[i + 1].match(MAIN);
  t.ok(dos && dos[2] === 'ARMDOS  ' && dos[4] === 'System Program', `${name}: ARMDOS System Program line`, L[i + 1]);
  t.ok(dos && hex(dos[1]) === 0x700 + hex(io[3]), `${name}: ARMDOS.SYS right after IO.SYS`);
  t.ok(L[i + 2] === '', `${name}: blank line after ARMDOS`);
  i += 3;
  // the MCB chain
  const blocks = [];
  let subTotal = 0, subList = [];
  for (; i < L.length && L[i] !== ''; i++) {
    const m = L[i].match(MAIN), s = L[i].match(SUB);
    if (m) blocks.push({ addr: hex(m[1]), name: m[2], size: hex(m[3]), type: m[4] });
    else if (s) { subList.push({ owner: s[1], size: hex(s[2]), type: s[3] }); subTotal += hex(s[2]) + 16; }
    else t.ok(false, `${name}: map line format`, JSON.stringify(L[i]));
  }
  t.ok(blocks.length >= 5, `${name}: ${blocks.length} memory blocks`);
  t.ok(blocks[0].addr === hex(dos[1]) + hex(dos[3]) && blocks[0].name === 'IO      ' && blocks[0].type === 'System Data',
    `${name}: first MCB = IO System Data after the kernel`, JSON.stringify(blocks[0]));
  let contiguous = true;
  for (let k = 0; k + 1 < blocks.length; k++) if (blocks[k].addr + blocks[k].size + 16 !== blocks[k + 1].addr) contiguous = false;
  t.ok(contiguous, `${name}: the MCB chain is contiguous`);
  const last = blocks[blocks.length - 1];
  t.ok(last.addr + last.size + 16 === 0xA0000 && last.type === '-- Free --' && last.name === 'ARMDOS  ', `${name}: ends with free memory up to 640 KB`, JSON.stringify(last));
  t.ok(subTotal === blocks[0].size, `${name}: the CONFIG.SYS sub-blocks fill IO System Data`, `${subTotal} vs ${blocks[0].size}`);
  t.ok(JSON.stringify(subList.map((s) => s.type.trim())) === JSON.stringify(subs), `${name}: sub-blocks ${subs.join(' ')}`, JSON.stringify(subList));
  if (himem) t.ok(subList[0].owner === 'HIMEM   ', `${name}: DEVICE= sub-block owned by HIMEM`);
  t.ok(subList.filter((s) => s.type.trim() !== 'DEVICE=').every((s) => s.owner === '        '), `${name}: SYSINIT sub-blocks have no owner name (as real)`);
  const names = blocks.slice(1).map((b) => `${b.name.trim()}:${b.type.trim()}`);
  t.ok(names.includes('TSHELL:Program') && names.includes('TSHELL:Environment') && names.includes('MEM:Program') && names.includes('MEM:Environment'),
    `${name}: programs and environments named`, names.join(' '));
  // the summary after the map
  const memProg = blocks.find((b) => b.name === 'MEM     ' && b.type.trim() === 'Program');
  return { L: L.slice(i), largest: memProg.size + last.size + 16 };
}

function checkSummary(name, L, largest, extAvail) {
  const want = ['', '', '    655360 bytes total memory', '    655360 bytes available',
    `${String(largest).padStart(10)} largest executable program size`, '',
    '  15663104 bytes total extended memory', `${String(extAvail).padStart(10)} bytes available extended memory`, ''];
  t.same(Buffer.from(L.join('\r\n')), Buffer.from(want.join('\r\n')), `${name}: summary (largest = MEM's block + the free block)`);
}

// ------------------------------------------------------ plain CONFIG.SYS
{
  const s = await session({
    name: 'mem', outDir: B('apps-test'), programs: ['build/MEM.EXE'],
    script: ['MEM.EXE >\\OUT\\MEM.TXT', 'MEM.EXE /PROGRAM >\\OUT\\MEMPROG.TXT', 'MEM.EXE /DEBUG >\\OUT\\MEMDEBUG.TXT',
      'echo ERRSTART', ...errCmds, 'echo ERREND'],
  });
  t.ok(s.finished, 'script ran to the end');
  const p = checkMap('MEM /PROGRAM', s.read('OUT\\MEMPROG.TXT'), { subs: ['FILES=', 'FCBS=', 'BUFFERS=', 'LASTDRIVE='] });
  checkSummary('MEM /PROGRAM', p.L, p.largest, 15663104);
  const d = checkMap('MEM /DEBUG', s.read('OUT\\MEMDEBUG.TXT'), { debug: true, subs: ['FILES=', 'FCBS=', 'BUFFERS=', 'LASTDRIVE='] });
  checkSummary('MEM /DEBUG', d.L, d.largest, 15663104);
  checkSummary('MEM', lines(s.read('OUT\\MEM.TXT')), p.largest, 15663104);
  // the real MEM.TXT, with this machine's numbers
  const realMem = exp('MEM.TXT').replace('    585920', String(p.largest).padStart(10)).replace(/   3145728/g, '  15663104');
  t.same(s.read('OUT\\MEM.TXT'), Buffer.from(realMem, 'latin1'), 'MEM: the real output with this machine\'s numbers');
  // errors on the screen
  const scr = s.pc.screen().split('\n');
  const a = scr.findIndex((l) => l === 'ERRSTART'), b = scr.findIndex((l) => l === 'ERREND');
  const got = scr.slice(a + 1, b).join('\n'), want = exp('ERRORS.TXT').split('\n').filter((l) => l).join('\n');
  t.same(Buffer.from(got), Buffer.from(want), 'parse errors on the screen as the real MEM');
  const codes = s.exits.map((e) => e.code);
  t.ok(codes.slice(0, 3).every((c) => c === 102), 'exit code 102 like the real MEM (MEM, /PROGRAM, /DEBUG)', codes.join(' '));
  t.ok(codes.slice(3).every((c) => c === 1), 'exit code 1 after a parse error', codes.join(' '));
  t.ok(!s.pc.faults.length, 'no CPU faults', JSON.stringify(s.pc.faults.slice(0, 3)));
}

// ---------------------------------------- HIMEM.SYS, FCBS, LASTDRIVE=Z
{
  const s = await session({
    name: 'mem-himem', outDir: B('apps-test'), programs: ['build/MEM.EXE', 'build/HIMEM.SYS'],
    config: ['FILES=30', 'BUFFERS=15', 'DEVICE=C:\\T\\HIMEM.SYS', 'FCBS=8,4', 'LASTDRIVE=Z'],
    script: ['MEM.EXE /DEBUG >\\OUT\\MEMDEBUG.TXT'],
  });
  t.ok(s.finished, 'HIMEM: script ran to the end');
  const d = checkMap('HIMEM: MEM /DEBUG', s.read('OUT\\MEMDEBUG.TXT'), { debug: true, himem: true, subs: ['DEVICE=', 'FILES=', 'FCBS=', 'BUFFERS=', 'LASTDRIVE='] });
  checkSummary('HIMEM: MEM /DEBUG', d.L, d.largest, 0);
  t.ok(!s.pc.faults.length, 'HIMEM: no CPU faults');
}
t.done();
