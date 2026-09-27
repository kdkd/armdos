#!/usr/bin/env node
// thumb-run.mjs - "make sdk-thumb-test": a program that mixes ARM and Thumb code
// (sdk/tests/thumb.c) links with interworking veneers; elf2exe relocates the
// absolute target word of the ARM->Thumb veneer, and the program runs correctly
// on the machine, loaded wherever DOS puts it.
import fs from 'node:fs';
import { execFileSync } from 'node:child_process';
import { session } from '../../apps/dosutil/tests/harness.mjs';

const exe = process.argv[2], elf = process.argv[3];
let fails = 0;
const ok = (c, what, d) => { console.log(`${c ? '  ok  ' : '  FAIL'} ${what}`); if (!c) { fails++; if (d) console.log('       ' + d); } };
const nm = execFileSync('arm-none-eabi-nm', [elf], { encoding: 'utf8' });
ok(/_from_arm\b/.test(nm), 'the link has an ARM->Thumb veneer (so the test tests something)', nm.split('\n').filter((l) => /from_|veneer/.test(l)).join(' | '));
const s = await session({ name: 'sdk-thumb', files: [{ src: exe, dst: 'T\\THUMB.EXE' }] });
const out = s.run('\\T\\THUMB').join('\n');
ok(/THUMB 1012/.test(out), 'ARM -> Thumb -> newlib strlen and back: THUMB 1012', out);
console.log(`\nsdk-thumb: ${fails ? fails + ' failed' : 'all passed'}`);
process.exit(fails ? 1 : 0);
