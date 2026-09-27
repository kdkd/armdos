#!/usr/bin/env node
// apps/basic/tests/run.mjs - BASIC.EXE regression checks (headless).
//   node apps/basic/tests/run.mjs
// A program exercising fixed bugs (three-number LOCATE/COLOR, strings with
// CHR$(0) in comparisons and SELECT CASE, VAL("")) plus a few basics, run as
// BASIC PROG; the screen is checked. Screenshot: build/basic-test/.
import path from 'node:path';
import { start, keys, shot, checker, ROOT } from '../../tvlib/tests/lib.mjs';

const OUT = path.join(ROOT, 'build', 'basic-test');
const T = checker();
const ok = T.ok;
const P = `10 CLS
20 PRINT "VAL:"; VAL(""); VAL("12")
30 K$ = CHR$(0) + "H"
40 IF K$ <> "" THEN PRINT "NONEMPTY" ELSE PRINT "EMPTY"
50 IF K$ = CHR$(0) + "P" THEN PRINT "EQ-BAD" ELSE PRINT "EQ-OK"
60 IF K$ = CHR$(0) + "H" THEN PRINT "EQ-SAME"
70 IF "AB" < "ABC" THEN PRINT "LT-OK"
80 LOCATE 10, 5, 0: PRINT "AT10"
90 LOCATE 11, 5, 1: PRINT "AT11"
100 COLOR 14, 1, 0: PRINT "YELLOW": COLOR 7, 0
110 PRINT 7 \\ 2; 2 + 3
120 SYSTEM
`.replace(/\n/g, '\r\n');
const pc = await start(OUT, [['T.BAS', P]], { exes: ['build/BASIC.EXE'] });
keys(pc, 'BASIC T\r', 3000);
await shot(pc, OUT, 'regress');
const s = pc.screen(), L = pc.lines();
ok(/VAL: 0 {2}12/.test(s), 'VAL("") = 0 (was Illegal function call)', s);
ok(s.includes('NONEMPTY') && s.includes('EQ-OK') && s.includes('EQ-SAME') && s.includes('LT-OK'),
  'strings with CHR$(0) compare by length (INKEY$-style CHR$(0)+"H")', s);
ok(L[9].startsWith('    AT10') && L[10].startsWith('    AT11'), 'LOCATE row, col, cursor (3 numbers)', s);
const yr = L.findIndex((l) => l.startsWith('YELLOW'));
ok(yr >= 0 && (pc.cpu.m8[0xB8000 + yr * 160 + 1] === 0x1E), 'COLOR fg, bg, border (3 numbers)', s);
ok(/ 3 {2}5/.test(s), '7 \\ 2 = 3, arithmetic', s);
ok(!/Illegal function call|Syntax error/.test(s) && pc.hasText('C:\\>'), 'no errors; SYSTEM back to DOS', s);
console.log(`\n${T.pass} passed, ${T.fail} failed`);
process.exit(T.fail ? 1 : 0);
