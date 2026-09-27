#!/usr/bin/env node
// apps/diskcopy/tests/run.mjs - DISKCOPY A: A: and DISKCOMP A: A: on the one
// diskette drive, swapping images when the programs ask for them (as a
// visitor does on the web page): a data diskette onto an unformatted one
// ("Formatting while copying", several passes through conventional memory),
// the copy (identical but for the new serial number, fsck.fat -n), then
// DISKCOMP: "Compare OK", and after changing a byte: "Compare error on".
import { hdImage, floppyImage, blankFloppy, start, cmd, exitCode, checkFsck, checkMdir, check, summary } from '../../format/tests/lib.mjs';

const pat = (n, k) => Buffer.from(Array.from({ length: n }, (_, i) => (i * 13 + k) & 255));
const files = {};
for (let i = 0; i < 12; i++) files[`FILE${i}.DAT`] = pat(40000 + i * 9000, i);   // spread over the diskette
const source = floppyImage('SOURCE', files);
const target = blankFloppy();

const hd = hdImage('diskcopy');
const pc = await start(hd, source);
const insert = (img) => { if (pc.machine.floppy !== img) { pc.machine.ejectFloppy(); pc.run(30); pc.machine.insertFloppy(img); pc.run(30); } };
const lastLines = (n = 6) => pc.screen().split('\n').filter((l) => l.trim()).slice(-n);

/** answer the prompts: which = { SOURCE|TARGET|FIRST|SECOND: image }; returns the prompts seen */
const screens = [];
function drive(which, final, answer = 'N') {
  const seen = [];
  screens.length = 0;
  for (let guard = 0; guard < 40; guard++) {
    pc.waitIdle();
    const lines = lastLines();
    const last = lines[lines.length - 1] || '';
    if (last.startsWith(final)) { pc.type(answer); pc.waitIdle(); return seen; }
    if (last !== 'Press any key to continue . . .') { pc.run(200); continue; }
    const ins = [...lines].reverse().find((l) => /^Insert (\w+) diskette in drive A:$/.test(l));
    const who = ins && /^Insert (\w+)/.exec(ins)[1];
    seen.push(who);
    screens.push(pc.screen());
    if (which[who]) insert(which[who]);
    pc.type(' ');
    pc.run(100);
  }
  return seen;
}

// ------------------------------------------------------------ DISKCOPY
cmd(pc, 'C:\\DOS\\DISKCOPY.COM A: A:', { wait: false });
const t0 = pc.timeMs;
const seen = drive({ SOURCE: source, TARGET: target }, 'Copy another diskette (Y/N)?');
pc.waitText('T>', { timeoutMs: 5000 });
const scr = pc.screen();
console.log(`     prompts: ${seen.join(' ')} (${((pc.timeMs - t0) / 1000).toFixed(1)} s emulated)`);
check(seen.length >= 4 && seen.length % 2 === 0 && seen.every((w, i) => w === (i % 2 ? 'TARGET' : 'SOURCE')),
  `SOURCE/TARGET prompts alternate, ${seen.length / 2} passes through conventional memory`, seen.join(' '));
check(/T>C:\\DOS\\DISKCOPY.COM A: A:\n\nInsert SOURCE diskette in drive A:\n\nPress any key to continue \. \. \.\n\nCopying 80 tracks\n18 Sectors\/Track, 2 Side\(s\)\n\nInsert TARGET diskette in drive A:\n\nPress any key to continue \. \. \.\s*$/.test(screens[1]),
  'first pass: SOURCE prompt, "Copying 80 tracks / 18 Sectors/Track, 2 Side(s)", TARGET prompt (4.00 layout)', screens[1]);
check(/Press any key to continue \. \. \.\n\nFormatting while copying\n\nInsert SOURCE diskette/.test(screens[2]), 'unformatted target: "Formatting while copying"', screens[2]);
const serial = (/Volume Serial Number is ([0-9A-F]{4})-([0-9A-F]{4})/.exec(scr) || []);
check(!!serial[0] && /Copy another diskette \(Y\/N\)\? N/.test(scr), 'new serial number shown, "Copy another diskette (Y/N)? N"', scr);
check(exitCode(pc) === 0, 'exit code 0');
{
  const a = Buffer.from(source), b = Buffer.from(target);
  let diff = [];
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) diff.push(i);
  check(diff.every((i) => i >= 0x27 && i <= 0x2A), 'the copy is identical except the serial number (boot record 27h-2Ah)', diff.slice(0, 10).join(','));
  const s = b.readUInt32LE(0x27).toString(16).toUpperCase().padStart(8, '0');
  check(serial[0] && s === serial[1] + serial[2], `target serial ${s} = the one printed`);
  checkFsck(target, 'dcopy', 'fsck.fat -n on the copy: clean');
  checkMdir(target, ['::/'], (dir) => /Volume in drive : is SOURCE/.test(dir), 'the copy: label SOURCE, files listed');
}

// ------------------------------------------------------------ DISKCOMP
insert(source);
cmd(pc, 'C:\\DOS\\DISKCOMP.COM A: A:', { wait: false });
let s2 = drive({ FIRST: source, SECOND: target }, 'Compare another diskette (Y/N) ?');
pc.waitText('T>', { timeoutMs: 5000 });
let out = pc.screen();
console.log(`     prompts: ${s2.join(' ')}`);
check(s2[0] === 'FIRST' && s2[1] === 'SECOND' && s2.length >= 3, 'FIRST, SECOND, then the diskette in the drive is read first (roles swap)', s2.join(' '));
check(/Comparing 80 tracks\n18 sectors per track, 2 side\(s\)\n/.test(out) || /Compare OK/.test(out), '"Comparing 80 tracks / 18 sectors per track, 2 side(s)"');
check(/\nCompare OK\n/.test(out) && !/Compare error/.test(out), 'the DISKCOPY copy: "Compare OK" (serial numbers differ, ignored)', out);

// change one byte in a data file on the copy (side 1, track 40 = LBA (40*2+1)*18)
target[(40 * 2 + 1) * 18 * 512 + 100] ^= 0xFF;
insert(source);
cmd(pc, 'C:\\DOS\\DISKCOMP.COM A: A:', { wait: false });
s2 = drive({ FIRST: source, SECOND: target }, 'Compare another diskette (Y/N) ?');
pc.waitText('T>', { timeoutMs: 5000 });
out = pc.screen();
check(/\nCompare error on\nside 1, track 40\n/.test(out) && !/Compare OK/.test(out), 'one changed byte: "Compare error on / side 1, track 40", no "Compare OK"', out);

// ------------------------------------------------------------ errors
cmd(pc, 'C:\\DOS\\DISKCOPY.COM C: A:');
out = pc.screen();
check(/\nInvalid drive specification\nSpecified drive does not exist\nor is non-removable\n/.test(out), 'DISKCOPY C: A: -> "Invalid drive specification / ... non-removable"', out);
cmd(pc, 'C:\\DOS\\DISKCOPY.COM A:\\X.TXT A:');
out = pc.screen();
check(/Do not specify filename\(s\)\nCommand Format: DISKCOPY d: d: \[\/1\]/.test(out), 'DISKCOPY A:\\X.TXT -> "Do not specify filename(s)"', out);
cmd(pc, 'C:\\DOS\\DISKCOMP.COM A: A: /X');
out = pc.screen();
check(/Invalid switch -  \/X\nDo not specify filename\(s\)\nCommand format: DISKCOMP d: d: \[\/1\]\[\/8\]/.test(out), 'DISKCOMP /X -> "Invalid switch", usage', out);

process.exit(summary() ? 1 : 0);
