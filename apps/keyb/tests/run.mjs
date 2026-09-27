#!/usr/bin/env node
// apps/keyb/tests/run.mjs - KEYB.COM and KEYBOARD.SYS tests ("make keyb-test").
//
//   node apps/keyb/tests/run.mjs [--verbose]
//
// 1. KEYBOARD.SYS: tools/kdfasm.mjs assembles MS-DOS 4.00's keyboard sources
//    into exactly MS-DOS 4.00's KEYBOARD.SYS (compared when the original is at
//    hand); the committed web/js/kbdlayouts.js is what tools/mklegends.mjs makes.
// 2. KEYB's messages, compared byte for byte with what the genuine KEYB 4.00
//    printed for the same command lines under DOSBox-X ("real"), without and
//    with DISPLAY.SYS.
// 3. Typing: KTEST (tests/ktest.c) prints what INT 16h returns while the keys
//    of a layout are pressed unshifted, shifted and with AltGr (dead keys
//    followed by Space), and the result must equal what the model of KEYB's
//    state processor (tools/kbdsys.mjs) predicts from the same tables, with
//    the BIOS's US translation where KEYB leaves the key to it; plus the
//    tests by hand: German a/o/u umlauts and sharp s, dead keys, AltGr @ on Q,
//    Dvorak, Ctrl+Alt+F1/F2, the layout published on port F6h.

import fs from 'node:fs';
import path from 'node:path';
import { ROOT, B, checker, run, redirected, ktest, press } from './nlskit.mjs';
import { buildKeyboardSys, MS_LINK } from '../tools/kdfasm.mjs';
import { legendsModule, KEYS } from '../tools/mklegends.mjs';
import * as K from '../tools/kbdsys.mjs';
import { rescheck } from '../../ansi/tests/rescheck.mjs';

const T = checker();
const E = (s) => s.split('\n').join('\r\n');
const verbose = process.argv.includes('--verbose');

// ------------------------------------------------------------ 1. the file
{
  console.log('== KEYBOARD.SYS');
  const ms = process.env.ARMDOS_REFS ? path.join(process.env.ARMDOS_REFS, 'msdos400-pcjs/files/KEYBOARD.SYS') : '';   // optional: MS-DOS 4.00's own file
  if (ms && fs.existsSync(ms)) T.check(Buffer.compare(buildKeyboardSys(undefined, MS_LINK), fs.readFileSync(ms)) === 0, 'kdfasm + MS-DOS 4.00 sources = MS-DOS 4.00 KEYBOARD.SYS, byte for byte');
  else console.log('skip (no MS-DOS 4.00 KEYBOARD.SYS to compare with)');
  const built = fs.readFileSync(B('KEYBOARD.SYS'));
  T.check(Buffer.compare(built, buildKeyboardSys()) === 0, 'build/KEYBOARD.SYS is up to date');
  const kbd = K.parse(built);
  T.eq(kbd.langs.map((l) => l.code).join(' '), 'GR SP PO FR DK SG IT UK SF BE NL NO CF SV SU LA DV DL DR US', 'the layouts');
  T.check(fs.readFileSync(path.join(ROOT, 'web/js/kbdlayouts.js'), 'utf8') === legendsModule(built),
    'web/js/kbdlayouts.js is up to date (node apps/keyb/tools/mklegends.mjs)');
  const { from, to, bad } = rescheck(B('obj/KEYB/KEYB.elf'), 'buffer_fill', 'keyb_res_end', ['keyb_res_end']);
  T.check(bad.length === 0, `resident part (${to - from} bytes) refers to nothing beyond keyb_res_end` + (bad.length ? '\n       ' + bad.join('\n       ') : ''));
}

// ------------------------------------------------------------ 2. messages
const NOT_INST = 'KEYB has not been installed\nActive code page not available from CON device\n';
const q = (code, cp, id) => E(`Current keyboard code: ${code}  code page: ${cp}\n`) + (id ? `Current keyboard ID: ${id}\n\r` : '') + E('Active code page not available from CON device\n');
const plain = [
  ['KEYB', E(NOT_INST), 'real'],
  ['KEYB XX', E('Invalid keyboard code specified\n'), 'real'],
  ['KEYB GR,999', E('Invalid code page specified\n'), 'real'],
  ['KEYB GR,850', '', 'real'],
  ['KEYB', q('GR', 850), 'real'],
  ['KEYB DK', '', 'real'],
  ['KEYB', q('DK', 850), 'as real KEYB: no DISPLAY.SYS - the layout\'s first code page (DK: 850, 865)'],
  ['KEYB GR', '', 'real'],
  ['KEYB', q('GR', 437), 'real'],
  ['KEYB FR', '', 'real'],
  ['KEYB', q('FR', 437), 'real'],
  ['KEYB SP /ID:999', E('Invalid keyboard ID specified\n'), 'real'],
  ['KEYB GR /ID:129', '', 'real'],
  ['KEYB US', '', 'real'],
  ['KEYB', q('US', 437), 'real'],
  ['KEYB GR,,C:\\NOFILE.SYS', E('Bad or missing Keyboard Definition File\n'), 'real'],
  ['KEYB GR,850,C:\\DOS\\KEYBOARD.SYS,X', E('Too many parameters - X \n'), 'real'],
  ['KEYB /X', E('Invalid switch -  /X \n'), 'real'],
  ['KEYB GRR', E('Invalid parameter -  GRR \n'), 'real'],
  ['KEYB 12', E('Invalid keyboard ID specified\n'), 'real'],
  ['KEYB GR /ID:', E('Invalid switch - /ID: \n'), 'real'],
  ['KEYB /ID:129', E('Parameter value not allowed -  /ID:129 \n'), 'real'],
  ['KEYB GR /ID:120', E('Keyboard ID specified is inconsistent with the selected keyboard layout\n'), 'real'],
  ['KEYB FR /ID:120', '', 'real'],
  ['KEYB', q('FR', 437, 120), 'real (note the LF CR after the ID)'],
  ['KEYB 129', '', 'real: an ID alone names the layout'],
  ['KEYB', E('Current keyboard ID: 129  code page: 437\nActive code page not available from CON device\n'), 'real'],
  ['KEYB ,850', E('Invalid parameter -  ,\n'), 'real'],
  ['KEYB NO', '', 'real'],
  ['KEYB', q('NO', 850), 'real: NO\'s first code page is 850'],
  ['KEYB SG,,C:\\DOS\\KEYBOARD.SYS', '', 'real'],
  ['KEYB', q('SG', 850), 'real'],
  ['KEYB gr', '', 'real'],
  ['KEYB UK /ID:168', '', 'real'],
  ['KEYB', q('UK', 437, 168), 'real'],
  ['KEYB DV', '', 'ARM-DOS: Dvorak'],
  ['KEYB', q('DV', 437), ''],
];
const withDisplay = [
  ['KEYB', E('KEYB has not been installed\nActive code page not available from CON device\n'), 'real: no code page selected yet'],
  ['MODE CON CP PREPARE=((850,860) C:\\DOS\\EGA.CPI)', E('\nMODE prepare code page function completed\n'), 'real'],
  ['KEYB', E('KEYB has not been installed\nActive code page not available from CON device\n'), 'real'],
  ['MODE CON CP SELECT=850', E('\nMODE select code page function completed\n'), 'real'],
  ['KEYB', E('KEYB has not been installed\nCurrent CON code page: 850\n'), 'real'],
  ['KEYB GR', E('One or more CON code pages invalid for given keyboard code\n'), 'real: GR has no 860'],
  ['KEYB', E('Current keyboard code: GR  code page: 850\nCurrent CON code page: 850\n'), 'real'],
  ['MODE CON CP SEL=860', E('\nCurrent keyboard does not support this code page\n'), 'real'],
  ['KEYB', E('Current keyboard code: GR  code page: 850\nCurrent CON code page: 860\n'), 'real: the screen switched anyway'],
  ['MODE CON CP SEL=437', E('\nMODE select code page function completed\n'), 'real'],
  ['KEYB', E('Current keyboard code: GR  code page: 437\nCurrent CON code page: 437\n'), 'real: KEYB followed'],
  ['KEYB DK', E('Code page requested (437) is not valid for given keyboard code\n'), 'real: DK has no 437'],
  ['KEYB', E('Current keyboard code: GR  code page: 437\nCurrent CON code page: 437\n'), 'real: GR stays'],
  ['KEYB GR,860', E('Invalid code page specified\n'), 'real'],
  ['KEYB PO,860', E('Code page specified is inconsistent with the selected code page\nOne or more CON code pages invalid for given keyboard code\n'), 'as KEYBCMD.ASM: the warning, then 437 is not a PO code page'],
  ['KEYB', E('Current keyboard code: PO  code page: 860\nCurrent CON code page: 437\n'), ''],
];

async function messages(name, config, cases) {
  console.log(`== ${name}`);
  const pc = await run(name, { config, autoexec: redirected(cases.map((c) => c[0])) });
  T.check(pc.done, `${name}: all commands ran`);
  cases.forEach(([cmd, want, note], i) => {
    const got = pc.out(i);
    const ok = got === want;
    T.check(ok, `${cmd}${note ? `   [${note}]` : ''}`, `got  ${JSON.stringify(got)}\n       want ${JSON.stringify(want)}`);
  });
  return pc;
}
await messages('keyb-plain', 'FILES=20\n', plain);
await messages('keyb-display', 'FILES=20\nDEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,2)\n', withDisplay);

// ------------------------------------------------------------ 3. typing

const kbd = K.parse(fs.readFileSync(B('KEYBOARD.SYS')));
const US_LO = '\0\x1b1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;\'`\0\\zxcvbnm,./';
const US_HI = '\0\x1b!@#$%^&*()_+\b\0QWERTYUIOP{}\r\0ASDFGHJKL:"~\0|ZXCVBNM<>?';
function bios(scan, state) {                       // bios/kbd.c translate() for these keys
  if (scan === 0x39) return 0x3920;
  if (state === 'alt') return scan >= 2 && scan <= 13 ? (scan + 0x76) << 8 : scan === 0x56 ? null : scan << 8;
  if (scan >= 0x3A) return null;
  const c = (state === 'shift' ? US_HI : US_LO)[scan];
  return c && c !== '\0' ? (scan << 8) | c.charCodeAt(0) : null;
}
const STATES = { base: [0, '', ''], shift: [K.LEFT_SHIFT, 'ShiftLeft+', 'shift'], altgr: [K.ALT_SHIFT, 'AltRight+', 'alt'] };

/** The keys to press and the words KTEST must print, from the model. */
function expectLayout(code, cp, flags0) {
  const e = kbd.entry(kbd.langs.find((l) => l.code === code).entry);
  const t = K.load(kbd, e, cp);
  const nls = { f1: 0, f2: 0 };
  const keys = [], words = [];
  const step = (scan, fl, name, fallback) => {
    keys.push(name);
    const r = K.processKey(t, scan, fl, nls);
    for (const v of r.out) words.push(v);
    if (!r.exit) { const b = bios(scan, fallback); if (b !== null) words.push(b); }
  };
  for (const [st, [flag, prefix, fb]] of Object.entries(STATES)) {
    for (const [name, scan] of Object.entries(KEYS)) {
      const fl = [flags0[0] | flag, flags0[1], flags0[2], flags0[3] | (st === 'altgr' ? K.R_ALT_SHIFT : 0)];
      step(scan, fl, prefix + name, fb);
      if (nls.f1) step(0x39, [flags0[0], flags0[1], flags0[2], flags0[3]], 'Space', 'base');   // close a dead key with Space
    }
  }
  return { keys, words: words.map((v) => v.toString(16).toUpperCase().padStart(4, '0')) };
}

async function typing(name, layouts, hand) {
  const cmds = [];
  for (const [code, cp] of layouts) cmds.push(`KEYB ${code},${cp}`, `KTEST > \\R\\K${code}${cp}.TXT`);
  if (hand) {
    // by hand: German (437): KeyZ=y KeyY=z [=u umlaut ;=o umlaut '=a umlaut -=sharp s, AltGr+Q=@,
    // dead acute (=) + e, dead grave (Shift+=) + e, dead acute + x (error: the accent, a beep, then x)
    cmds.push('KEYB GR,437', 'KTEST > \\R\\HAND1.TXT');
    // Ctrl+Alt+F1 (US) and Ctrl+Alt+F2 (back); Alt/Ctrl on moved keys use the US key of the letter
    cmds.push('KTEST > \\R\\HAND2.TXT');
    // Dvorak: the QWERTY row gives ',.pyf...
    cmds.push('KEYB DV', 'KTEST > \\R\\HAND3.TXT');
  }
  let auto = '';
  for (const c of cmds) auto += c.startsWith('KEYB') ? `${c} > NUL\n` : `${c}\n`;
  const expects = [];
  const f6 = [];
  const pc = await run(name, {
    config: 'FILES=20\nDEVICE=C:\\DOS\\DISPLAY.SYS CON=(EGA,437,4)\n',
    autoexec: 'MODE CON CP PREPARE=((850,860,863,865) C:\\DOS\\EGA.CPI) > NUL\n' + auto, timeoutMs: 600000,
    boot: process.env.KEYB_NOJIT ? { jit: false } : {},
  }, async (pc) => {
    const m = pc.machine;
    for (const [code, cp] of layouts) {
      if (!pc.until(() => pc.hasText('KTEST READY'), { timeoutMs: 60000 })) break;
      const bda = pc.cpu.m8;
      const ex = expectLayout(code, cp, [bda[0x417], bda[0x418], bda[0x497], bda[0x496] & ~0x02]);
      expects.push([code, cp, ex]);
      f6.push([code, cp, m.nls.keyb]);
      ktest(pc, ex.keys);
    }
    if (!hand) return;
    ktest(pc, ['KeyZ', 'KeyY', 'BracketLeft', 'Semicolon', 'Quote', 'Minus', 'ShiftLeft+BracketLeft', 'ShiftLeft+Quote',
      'AltRight+KeyQ', 'Equal', 'KeyE', 'ShiftLeft+Equal', 'KeyE', 'Equal', 'KeyX']);
    ktest(pc, ['ControlLeft+AltLeft+F1', 'KeyZ', 'ControlLeft+AltLeft+F2', 'KeyZ', 'AltLeft+KeyY', 'ControlLeft+KeyY']);
    f6.push(['DV', 437, null]);
    ktest(pc, ['KeyQ', 'KeyW', 'KeyE', 'KeyR', 'KeyT', 'KeyY', 'KeyA', 'KeyS', 'KeyD', 'KeyF', 'KeyZ', 'KeyX', 'Semicolon', 'Slash',
      'ShiftLeft+KeyP', 'Minus', 'AltLeft+KeyF', 'ControlLeft+KeyI']);
    f6[f6.length - 1][2] = m.nls.keyb;
  });
  if (!T.check(pc.done, `${name}: all ran`)) console.log(pc.screen());
  for (const [code, cp, ex] of expects) {
    const got = (pc.file(`R\\K${code}${cp}.TXT`) || '').trim().split(/\s+/);
    const want = [...ex.words, '011B'];
    const ok = got.join(' ') === want.join(' ');
    let where = '';
    if (!ok) { const i = got.findIndex((w, k) => w !== want[k]); where = `first difference at word ${i}: got ${got.slice(i, i + 6).join(' ')} want ${want.slice(i, i + 6).join(' ')}`; }
    T.check(ok, `${code} ${cp}: ${ex.keys.length} keys (unshifted, Shift, AltGr) give what KEYB's tables say (${want.length - 1} keystrokes)`, where);
  }
  if (hand) {
    const words = (f) => (pc.file(f) || '').trim().split(/\s+/).join(' ');
    T.eq(words('R\\HAND1.TXT'), '2C79 157A 1A81 2794 2884 0CE1 1A9A 288E 1040 0082 008A 0027 2D78 011B',
      'German (437): y z \u00FC \u00F6 \u00E4 \u00DF \u00DC \u00C4, AltGr+Q @, \u00B4+e \u00E9, `+e \u00E8, \u00B4+x = \' x (and a beep)');
    T.eq(words('R\\HAND2.TXT'), '2C7A 2C79 2C00 151A 011B',
      'Ctrl+Alt+F1: US (Z = z), Ctrl+Alt+F2: German again (Z = y); Alt+(physical Y) = Alt+Z, Ctrl+(physical Y) = ^Z');
    T.eq(words('R\\HAND3.TXT'), '1027 112C 122E 1370 1479 1566 1E61 1F6F 2065 2175 2C3B 2D71 2773 357A 194C 0C5B 1600 2E03 011B',
      'Dvorak: the keys Q W E R T Y A S D F Z X ; / give \' , . p y f a o e u ; q s z; Shift+P = L; - = [; Alt+(the u key) = Alt+U; Ctrl+(the c key) = ^C');
  }
  for (const [code, cp, v] of f6) {
    const id = { GR: 1, FR: 4, DK: 5, SP: 2, CF: 13, PO: 3, UK: 8, SG: 6, NO: 12, DV: 17, DL: 18, DR: 19, LA: 16, BE: 10, IT: 7, NL: 11, SV: 14, SF: 9 }[code];
    const cpi = [437, 850, 860, 863, 865].indexOf(cp);
    T.eq(v, id | (cpi << 5), `port F6h after KEYB ${code},${cp}: layout ${id}, code page ${cp}`);
  }
}

console.log('== typing');
// (in several boots: one boot with all of them trips an emulator problem, see README "Known
// issues"; KEYB_ONE_BOOT=1 runs them in one boot to reproduce it, KEYB_NOJIT=1 without the JIT)
const T1 = [['GR', 437], ['GR', 850], ['FR', 850], ['DK', 850], ['SP', 850], ['CF', 863], ['PO', 860]];
const T2 = [['UK', 437], ['SG', 850], ['NO', 865], ['DV', 437], ['DL', 437], ['DR', 437]];
const T3 = [['LA', 437], ['BE', 437], ['IT', 437], ['NL', 437], ['SV', 437], ['SF', 437]];
if (process.env.KEYB_ONE_BOOT) await typing('keyb-typing', [...T1, ...T2, ...T3], true);
else {
  await typing('keyb-typing1', T1);
  await typing('keyb-typing2', T2);
  await typing('keyb-typing3', T3, true);
}

console.log(T.failures ? `\n${T.failures} failure(s)` : '\nall KEYB tests passed');
process.exit(T.failures ? 1 : 0);
