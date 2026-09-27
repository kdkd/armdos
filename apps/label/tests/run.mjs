#!/usr/bin/env node
// apps/label/tests/run.mjs - LABEL.COM against the real MS-DOS 4.00 LABEL.
// The screens were read off the real LABEL.COM in DOSBox-X (disk labelled
// ARMDOS, serial 4069-12FF, apps/mslib/tools/dos400run.sh) for the same keys;
// expected/L1.TXT is its redirected stdout for "LABEL < F.TXT".
import { session, Checker, expected } from '../../dosutil/tests/harness.mjs';

const t = new Checker('LABEL');
const s = await session({ name: 'label', text: { 'T\\F.TXT': 'foo bar\r\n' } });
const L = '\\DOS\\LABEL.COM';
const PROMPT = 'Volume label (11 characters, ENTER for none)?';

// the root directory's volume label entry
function label() {
  const r = s.reader();
  const e = r.readDir(null).find((x) => x.attr & 8);
  return e ? e.name.replace('.', '') : null;   // FatReader shows the 11 bytes as 8.3
}
function bootLabel() {
  const r = s.reader();
  return Buffer.from(r.bs.subarray(0x2B, 0x36)).toString('latin1');
}

t.lines(s.run(L, { steps: [['ENTER for none', 'newlbl\r']] }),
  ['Volume in drive C is ARMDOS', 'Volume Serial Number is 4069-12FF', PROMPT + ' newlbl'], 'LABEL, type a new label');
t.ok(label() === 'NEWLBL', 'new label NEWLBL in the root directory', label());
// 4.00's DOS_Create calls Set_Media_ID for a new label (DOS/CREATE.ASM), so the
// boot record follows too (re-checked on DOSBox-X + the real 4.00: LABEL C:NEWLBL
// leaves NEWLBL at offset 2Bh of the boot sector)
t.ok(bootLabel() === 'NEWLBL     ', 'boot sector label follows (as DOS 4.00: Set_Media_ID)', bootLabel());

// the old label's 11 bytes with their blanks, prompt without CR LF, then CR LF
s.run(`REDIR \\T\\F.TXT \\OUT\\L1.TXT ${L}`);
const l1 = s.file('OUT\\L1.TXT');
t.bytes(l1, expected('label', 'L1.TXT').toString('latin1').replace('ARMDOS', 'NEWLBL'), 'LABEL <F.TXT (stdout bytes)');
t.ok(label() === 'FOO BAR', 'a label with a blank: FOO BAR', label());

t.lines(s.run(`${L} C:ABCDEFGHIJKLMNOP`), [], 'LABEL C:ABCDEFGHIJKLMNOP (silent)');
t.ok(label() === 'ABCDEFGHIJK', 'truncated to 11 characters', label());

// ENTER: delete? "x" is not an answer, "y" deletes
t.lines(s.run(L, { steps: [['ENTER for none', '\r'], ['(Y/N)', 'x'], [null, 'y']] }),
  ['Volume in drive C is ABCDEFGHIJK', 'Volume Serial Number is 4069-12FF', PROMPT, '',
   'Delete current volume label (Y/N)?', 'Delete current volume label (Y/N)?'], 'LABEL, ENTER, x, y');
t.ok(label() === null, 'label deleted');

t.lines(s.run(L, { steps: [['ENTER for none', '\r']] }),
  ['Volume in drive C has no label', 'Volume Serial Number is 4069-12FF', PROMPT], 'LABEL on a disk without label, ENTER');

t.lines(s.run(`${L} Q:`), ['Invalid drive specification'], 'LABEL Q:');

t.lines(s.run(`${L} A*B`, { steps: [['ENTER for none', 'hello world\r']] }),
  ['Volume in drive C has no label', 'Volume Serial Number is 4069-12FF', 'Invalid characters in volume label',
   PROMPT + ' hello world'], 'LABEL A*B, then a label with a blank');
t.ok(label() === 'HELLO WORLD', 'label HELLO WORLD', label());

t.lines(s.run(`${L} a.b`, { steps: [['ENTER for none', '[x]\r'], [null, '\r'], ['(Y/N)', 'n']] }),
  ['Volume in drive C is HELLO WORLD', 'Volume Serial Number is 4069-12FF', 'Invalid characters in volume label',
   PROMPT + ' [x]', 'Invalid characters in volume label', PROMPT, '', 'Delete current volume label (Y/N)?'],
  'LABEL a.b, [x], ENTER, n');
t.ok(label() === 'HELLO WORLD', 'label kept after "n"', label());

t.lines(s.run(`${L} /?`, { steps: [['ENTER for none', 'x\r']] }),
  ['Volume in drive C is HELLO WORLD', 'Volume Serial Number is 4069-12FF', 'Invalid characters in volume label',
   PROMPT + ' x'], 'LABEL /? (not help: an invalid label)');
t.ok(label() === 'X', 'label X', label());
t.done();
