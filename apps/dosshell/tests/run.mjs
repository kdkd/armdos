#!/usr/bin/env node
// apps/dosshell/tests/run.mjs - the DOS 4.00 Shell on ARM-DOS, screen by
// screen against the real SHELLC /TEXT captured in DOSBox-X
// (tests/ref, text and attributes of every cell),
// plus the behaviour: transient mode through SHELLB, running programs,
// the command prompt, group maintenance, file operations.
import fs from 'node:fs';
import path from 'node:path';
import { start, grab, dump, save, keys, compare, Checker, OUT, ROOT, makeFloppy } from './lib.mjs';

const t = new Checker('dosshell');
const need = ['build/SHELLB.COM', 'build/SHELLC.EXE', 'build/COMMAND.COM', 'build/IO.SYS', 'build/ARMDOS.SYS', 'build/rom.bin',
  'build/dosshell/SHELL.MEU', 'build/dosshell/SHELL.HLP'];
for (const f of need) if (!fs.existsSync(path.join(ROOT, f))) { console.log(`missing ${f} (make it first)`); process.exit(1); }

// the DOSBox-X mouse pointer sits on these cells of some captures
const pointer = (r, c) => (r === 10 || r === 11) && (c === 40 || c === 41);
const clock = (r, c) => r === 0 && c >= 69 && c <= 77;

function same(pc, ref, name, opts = {}) {
  const s = grab(pc);
  const v = pc.machine.vga, cursor = (v.crtc[0x0A] & 0x20) ? -1 : ((v.crtc[0x0E] << 8) | v.crtc[0x0F]);
  const d = compare(s, ref, { ...opts, cursor, mask: (r, c) => clock(r, c) || (opts.mask ? opts.mask(r, c) : false) });
  fs.writeFileSync(path.join(OUT, name.replace(/[^A-Za-z0-9-]+/g, '_').slice(0, 60) + '.txt'), dump(s));
  t.ok(d.length === 0, `${name}: screen = real Shell (${ref})`, d.length + ' cells differ\n' + d.join('\n'));
}

const GAMES_ROW = ' Games & Fun...';

// ------------------------------------------------------------ Start Programs
{
  const pc = await start();
  t.ok(pc.waitText('Start Programs', { timeoutMs: 30000 }), 'AUTOEXEC.BAT -> DOSSHELL -> Start Programs');
  pc.waitIdle();
  same(pc, '01-start-programs-scheme1.txt', 'start-programs', { patch: { 10: GAMES_ROW }, mask: pointer,
    patchAttr: { 10: [[0, 0, 7, null], [1, 5, 7, 0], [6, 6, 7, null], [7, 7, 7, 0], [8, 8, 7, null], [9, 14, 7, 0], [15, 79, 7, null]] } });
  keys(pc, '{F10}{ENTER}');
  same(pc, '02-program-pulldown.txt', 'program-pulldown', { patch: {}, mask: (r, c) => pointer(r, c) || (r === 10 && c < 41) });
  keys(pc, '{ESC}{ESC}{F10}{RIGHT}{ENTER}');
  same(pc, '03-group-pulldown.txt', 'group-pulldown', { mask: (r, c) => pointer(r, c) || (r === 10 && c < 41) });
  keys(pc, '{ESC}{ESC}{F10}{RIGHT}{RIGHT}{ENTER}');
  same(pc, '04-exit-pulldown.txt', 'exit-pulldown', { mask: (r, c) => pointer(r, c) || (r === 10 && c < 41) });
  keys(pc, '{ESC}{ESC}{DOWN}{DOWN}{ENTER}');
  same(pc, '05-change-colors-1.txt', 'change-colors-1', { mask: pointer });
  keys(pc, '{RIGHT}');
  same(pc, '06-change-colors-2.txt', 'change-colors-2', { mask: pointer });
  keys(pc, '{RIGHT}');
  same(pc, '07-change-colors-3.txt', 'change-colors-3', { mask: pointer });
  keys(pc, '{LEFT}{ENTER}');
  const games = (fg) => ({ 10: [[0, 0, fg >> 4, null], [1, 5, fg >> 4, fg & 15], [6, 6, fg >> 4, null], [7, 7, fg >> 4, fg & 15], [8, 8, fg >> 4, null], [9, 14, fg >> 4, fg & 15], [15, 79, fg >> 4, null]] });
  same(pc, '08-start-programs-scheme2.txt', 'start-programs-2', { patch: { 10: GAMES_ROW }, patchAttr: games(0x0F) });
  keys(pc, '{ENTER}{RIGHT}{ENTER}');
  same(pc, '09-start-programs-scheme3.txt', 'start-programs-3', { patch: { 10: GAMES_ROW }, patchAttr: games(0x0F) });
  keys(pc, '{ENTER}{RIGHT}{ENTER}');
  same(pc, '10-start-programs-scheme4.txt', 'start-programs-4', { patch: { 10: GAMES_ROW }, patchAttr: games(0x1F) });
  keys(pc, '{ENTER}{RIGHT}{ENTER}');         /* back to scheme 1 */
  // DOS Utilities (ARM-DOS has more items: compare the frame)
  keys(pc, '{DOWN}{ENTER}');
  const items = (r) => r >= 5 && r <= 23;
  same(pc, '11-dos-utilities-group.txt', 'dos-utilities', { mask: (r, c) => items(r) });
  t.ok(pc.lines()[6] === ' Set Date and Time'.padEnd(80) && pc.lines()[7].startsWith(' Disk Copy'), 'DOS Utilities: Set Date and Time, Disk Copy, ...');
  keys(pc, '{DOWN}{ENTER}');
  const outside = (r, c) => items(r) && !(r >= 12 && r <= 21 && c >= 27 && c <= 70);
  same(pc, '12-diskcopy-dialog.txt', 'diskcopy-dialog', { mask: outside });
  keys(pc, '{ESC}{DOWN}{ENTER}');
  same(pc, '14-diskcomp-dialog.txt', 'diskcomp-dialog', { mask: outside });
  keys(pc, '{ESC}{DOWN}{ENTER}');
  same(pc, '13-format-dialog.txt', 'format-dialog', { mask: outside });
  keys(pc, '{ESC}{ESC}{F1}');
  same(pc, '15-help-panel.txt', 'help-panel', { mask: (r, c) => r === 10 && c < 41 });
  keys(pc, '{ESC}{ESC}{ESC}{HOME}{F1}{F9}');
  same(pc, 'ref/95-keys.txt', 'F9 in a help panel: Key Assignments', { mask: (r, c) => r >= 7 && r <= 12 && c < 41 });
  keys(pc, '{F1}');
  same(pc, 'ref/96-help-on-help.txt', 'F1 in a help panel: Help on Help (the frame)', { mask: (r, c) => (r >= 7 && r <= 12 && c < 41) || (r >= 16 && r <= 20) });
  keys(pc, '{ESC}');
  t.ok(pc.lines()[14].includes('Command Prompt'), 'Esc on Help on Help (reached through F9) goes back to the item\'s help', pc.lines()[14]);
  keys(pc, '{ESC}{DOWN}{DOWN}{DOWN}{SHIFT+F9}', 1500);
  await save(pc, 'command-prompt');
  same(pc, '16-shift-f9-command-prompt.txt', 'Shift+F9 (the COMMAND.COM banner and prompt are ARM-DOS\'s)', { mask: (r) => r >= 3 && r <= 6 });
  const L = pc.lines();
  t.ok(L[0] === ' When ready to return to the DOS Shell, type EXIT then press enter.'.padEnd(80), 'Shift+F9: row 0 message', L[0]);
  t.ok(L[3].startsWith('ARM-DOS') && L.some((l) => l.startsWith('C:\\DOS>')), 'Shift+F9: COMMAND.COM banner on row 3 and its prompt', L.slice(0, 8).join('\n'));
  keys(pc, 'EXIT\r', 1500);
  t.ok(pc.lines()[2].includes('Main Group') && pc.lines()[9].startsWith(' DOS Utilities...'), 'EXIT: back in Start Programs', pc.screen());
  await save(pc, 'after-prompt');
}

// --------------------------------------------------------------- File System
// the captures show drive B: (DOSBox-X had two diskette drives); here the
// same diskette is in A:, and the machine also has C:
const onA = (r, txt) => {
  txt = txt.replace(/B:(?=\\|│)/g, 'A:').replace(/(Selected {12})B/, '$1A');
  if ((r === 3 || r === 14) && txt.startsWith('│ A  B   ')) txt = '│ A  B  C' + txt.slice(9);
  return txt;
};
const driveBarA = { 3: [[1, 1, 0, null], [2, 2, 0, 7], [3, 3, 0, null], [4, 4, 7, null], [5, 5, 7, 0], [6, 7, 7, null], [8, 8, 7, 0]] };
const fixCell = (r, x, c) => { if ((r === 3 || r === 14) && x === 8 && c.ch === 'C') c.fg = 0; };
{
  const pc = await start();
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  pc.machine.insertFloppy(makeFloppy());
  keys(pc, '{DOWN}{ENTER}');
  t.ok(pc.lines()[0].includes('File System'), 'File System from Start Programs');
  keys(pc, '{CTRL+A}');
  same(pc, '17-file-system-drive-focus.txt', 'fs-drive-focus', { fix: onA, patchAttr: driveBarA });
  keys(pc, '{F1}');
  same(pc, '37-drive-help.txt', 'Drive Help (the frame; ARM-DOS\'s own text)', { mask: (r, c) => r < 13 || (r >= 13 && (c < 15 || c > 63)) || (r >= 16 && r <= 20) });
  keys(pc, '{ESC}');
  keys(pc, '{TAB}{TAB}' + '{DOWN}'.repeat(12) + ' ');
  same(pc, '18-file-system-file-selected.txt', 'fs-file-selected', { fix: onA, fixCell });
  await save(pc, 'fs-selected');
  // the pulldowns, with CHKDSK.COM selected
  keys(pc, ' {HOME}{DOWN}{DOWN} {F10}{ENTER}');
  same(pc, '19-file-pulldown.txt', 'fs-file-pulldown', { fix: onA, patchAttr: {} });
  keys(pc, '{RIGHT}');
  same(pc, '20-options-pulldown.txt', 'fs-options-pulldown', { fix: onA, fixCell });
  keys(pc, '{RIGHT}');
  same(pc, '21-arrange-pulldown.txt', 'fs-arrange-pulldown', { fix: onA, fixCell });
  keys(pc, '{RIGHT}');
  same(pc, '22-fs-exit-pulldown.txt', 'fs-exit-pulldown', { fix: onA, fixCell });
  keys(pc, '{ESC}{ESC}{F10}{RIGHT}{ENTER}{ENTER}');
  same(pc, '23-display-options.txt', 'fs-display-options', { fix: onA, fixCell });
  keys(pc, '{ESC}{F10}{HOME}' + '{DOWN}'.repeat(12) + ' {F10}{RIGHT}{ENTER}f');
  same(pc, '24-file-options.txt', 'fs-file-options', { fix: onA, fixCell });
  keys(pc, '{ESC}{LEFT}{ENTER}l{RIGHT}{ENTER}s');
  same(pc, '25-show-information.txt', 'fs-show-information', { fix: onA, fixCell, mask: (r, c) => r === 17 && c > 20 && c < 30 });  /* Avail: the real TREE.COM is on the diskette */
  keys(pc, '{ESC}{F10}{HOME}{DOWN}{DOWN}{F10}{RIGHT}{RIGHT}{ENTER}t');
  same(pc, '26-system-file-list.txt', 'fs-system-file-list', { fix: onA, fixCell, mask: (r, c) => r === 20 && c > 12 && c < 23 });
  keys(pc, '{F10}{RIGHT}{RIGHT}{ENTER}s{HOME}{TAB}{F10}{RIGHT}{RIGHT}{ENTER}m');
  same(pc, '27-multiple-file-list.txt', 'fs-multiple-file-list', { fix: onA, patchAttr: driveBarA, fixCell });
  keys(pc, '{F10}{RIGHT}{RIGHT}{ENTER}s{TAB}{TAB}{HOME}' + '{DOWN}'.repeat(12) + ' {F10}{ENTER}v');
  same(pc, '28-file-view.txt', 'fs-file-view', { fix: onA });
  keys(pc, '{ESC}{ENTER}c');
  same(pc, '29-copy-file.txt', 'fs-copy', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}d');
  same(pc, '30-delete-file.txt', 'fs-delete', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}r');
  same(pc, '31-rename-file.txt', 'fs-rename', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}h');
  same(pc, '32-change-attribute.txt', 'fs-change-attribute', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}e');
  same(pc, '33-create-directory.txt', 'fs-create-directory', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}m');
  same(pc, '34-move-file.txt', 'fs-move', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}l{F10}{HOME}{DOWN}{DOWN}{ENTER}');
  same(pc, '35-open-file.txt', 'fs-open', { fix: onA, fixCell });
  // run a program: the Shell leaves memory (SHELLB runs it), then asks for Enter
  // (the files on this diskette are filler, but DOS\TREE.COM is the real TREE)
  keys(pc, '{ESC}{TAB}{TAB}{DOWN}{ENTER}{TAB}{DOWN}{ENTER}');
  t.ok(pc.lines()[10].includes('Starting program:  TREE.COM'), 'Open File on A:\\DOS\\TREE.COM', pc.lines()[10]);
  keys(pc, 'A:\\ /F{ENTER}', 3000);
  await save(pc, 'fs-after-program');
  let L = pc.lines();
  t.ok(L[24] === ' Press Enter (<──┘) to return to File System.'.padEnd(80), 'after a program: row 24 asks for Enter', L[24]);
  t.ok(L.some((l) => /ADVENT/.test(l)) && L.some((l) => /SORT\.EXE/.test(l)), 'TREE ran and its output stays on the screen', L.join('\n'));
  const s = grab(pc);
  t.ok(s.attrs[24][1] === 0x07 && s.attrs[24][0] === 0x07, 'the prompt is grey on black (07)');
  same(pc, '36-return-prompt-after-program.txt', 'the return prompt after a program', { mask: (r) => r < 24 });
  keys(pc, '{ENTER}', 1500);
  L = pc.lines();
  await save(pc, 'fs-back');
  t.ok(L[5].startsWith('│ A:\\DOS ') && /TREE    \.COM/.test(L[9]), 'back in the File System at the same place (A:\\DOS, TREE.COM)', L.slice(3, 12).join('\n'));
  const g = grab(pc);
  t.ok(g.attrs[9][40] === 0x07, 'the file list keeps the focus', g.attrs[9][40].toString(16));
}

// ------------------------------------------------ memory while a program runs
{
  const pc = await start({ autoexec: false });
  pc.waitText('C:\\>', { timeoutMs: 30000 });
  keys(pc, 'MEM\r', 1000);
  const plain = +(pc.screen().match(/(\d+) largest executable program size/) || [])[1];
  keys(pc, 'CLS\rDOSSHELL\r', 1500);
  t.ok(pc.lines()[0].includes('Start Programs'), 'DOSSHELL typed at the prompt starts the Shell');
  // DOS Utilities > Display Memory Use (MEM runs while only SHELLB stays resident)
  keys(pc, '{DOWN}{DOWN}{DOWN}{ENTER}');
  const L = pc.lines();
  const row = L.findIndex((l) => l.startsWith(' Display Memory Use'));
  keys(pc, '{DOWN}'.repeat(row - 6) + '{ENTER}');
  t.ok(pc.lines().some((l) => l.includes('Memory Utility')), 'Display Memory Use asks for parameters (its [/t"Memory Utility"...] startup command)');
  keys(pc, '{ENTER}', 2000);
  await save(pc, 'mem-from-shell');
  const inshell = +(pc.screen().match(/(\d+) largest executable program size/) || [])[1];
  t.ok(plain > 400000 && inshell > 0 && plain - inshell < 16 * 1024,
    `a program started from the Shell gets all but ${plain - inshell} bytes (SHELLB and its environment; SHELLC is not in memory)`,
    `at the prompt ${plain}, from the Shell ${inshell}`);
  t.ok(pc.screen().includes('Press any key to continue'), 'the startup command\'s PAUSE line runs after MEM');
  keys(pc, ' ', 1500);
  t.ok(pc.lines()[2].includes('DOS Utilities...'), 'back in the DOS Utilities group after the program', pc.lines()[2]);
  // Exit Shell (F3): the screen is cleared and the batch file goes to :END
  keys(pc, '{F3}', 1500);
  await save(pc, 'after-exit');
  const E = pc.lines();
  t.ok(E.some((l) => l.startsWith('C:\\DOS>')) && !E.some((l) => l.includes('Start Programs')), 'F3 leaves the Shell for the DOS prompt', E.slice(0, 6).join('\n'));
  keys(pc, 'MEM\r', 1000);
  const after = +(pc.screen().match(/(\d+) largest executable program size/) || [])[1];
  t.ok(after === plain, 'after the Shell all the memory is free again', `${after} vs ${plain}`);
}
// ------------------------------------------ group maintenance, file operations
{
  fs.mkdirSync(path.join(OUT, 'work'), { recursive: true });
  const extra = [];
  for (const n of ['A.TXT', 'B.TXT', 'C.TXT']) {
    fs.writeFileSync(path.join(OUT, 'work', n), `This is ${n}\r\n`);
    extra.push({ src: path.relative(ROOT, path.join(OUT, 'work', n)), dst: 'WORK\\' + n });
  }
  const pc = await start({ extra, dirs: ['WORK'] });
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  // Program > Add
  keys(pc, '{F10}{ENTER}a');
  t.ok(pc.lines()[8].includes('Add Program'), 'Program > Add... opens Add Program');
  keys(pc, 'Test Program{TAB}echo hello{F4}pause{TAB}Hello help{F2}');
  t.ok(pc.lines()[11] === ' Test Program'.padEnd(80) && grab(pc).attrs[6][5] === 0x07, 'F2 saves the new title (the selection stays)', pc.lines()[11]);
  keys(pc, '{DOWN}'.repeat(5) + '{F1}');
  t.ok(pc.lines()[14].includes('Test Program') && pc.lines()[16].includes('Hello help'), 'F1 shows the item\'s help text');
  keys(pc, '{ESC}{ENTER}', 2500);
  t.ok(pc.lines().some((l) => l.trimEnd() === 'hello') && pc.screen().includes('Press any key to continue'), 'the item runs its two command lines (ECHO, PAUSE)', pc.screen());
  keys(pc, ' ', 1500);
  t.ok(pc.lines()[11] === ' Test Program'.padEnd(80) && grab(pc).attrs[11][3] === 0x07, 'back in Start Programs on the same item');
  // Group > Reorder: to the top
  keys(pc, '{F10}{RIGHT}{ENTER}r');
  t.ok(pc.lines()[3].includes('To complete the reorder, highlight the new'), 'Reorder: the instruction lines change');
  keys(pc, '{UP}'.repeat(5) + '{ENTER}');
  t.ok(pc.lines()[6] === ' Test Program'.padEnd(80) && pc.lines()[7] === ' Command Prompt'.padEnd(80), 'Reorder moves the title', pc.lines().slice(6, 12).join('\n'));
  // Program > Change
  keys(pc, '{F10}{ENTER}n');
  t.ok(pc.lines()[8].includes('Change Program') && pc.lines()[11].includes('[Test Program'), 'Change Program shows the item');
  keys(pc, '{END}{BS}{BS}{BS}{BS}{BS}{BS}{BS}Item{F2}');
  t.ok(pc.lines()[6] === ' Test Item'.padEnd(80), 'Change Program renames it', pc.lines()[6]);
  // Program > Copy into DOS Utilities
  keys(pc, '{F10}{ENTER}c');
  t.ok(pc.lines()[3].includes('To complete the copy, display the destination group,'), 'Copy: the instruction lines');
  keys(pc, '{DOWN}{DOWN}{DOWN}{DOWN}{ENTER}');
  t.ok(pc.lines()[2].includes('DOS Utilities...'), 'Copy: another group can be shown');
  keys(pc, '{F2}');
  const du = pc.lines().filter((l) => /^ \S/.test(l) && !l.includes('F10=')).length;
  t.ok(pc.lines().some((l) => l === ' Test Item'.padEnd(80)), 'F2 copies the title into DOS Utilities', pc.screen());
  keys(pc, '{ESC}{HOME}{F10}{ENTER}d');
  t.ok(pc.lines()[8].includes('Delete Item'), 'Program > Delete asks');
  keys(pc, '{ENTER}');
  t.ok(pc.lines()[6] === ' Command Prompt'.padEnd(80), 'Delete Item removes it', pc.lines()[6]);
  // Group > Add
  keys(pc, '{F10}{RIGHT}{ENTER}a');
  t.ok(pc.lines()[8].includes('Add Group'), 'Group > Add opens Add Group');
  keys(pc, 'My Group{TAB}mygroup{F2}');
  t.ok(pc.lines()[11] === ' My Group...'.padEnd(80), 'the new group is listed with "..."', pc.lines()[11]);
  keys(pc, '{END}{ENTER}');
  t.ok(pc.lines()[2].includes('My Group...') && pc.lines()[6].startsWith(' Group is empty.'), 'an empty group says "Group is empty."');
  keys(pc, '{ESC}');
  // the files: the Shell's own .MEU format
  const { FatReader } = await import('../../../disk/mkimage.mjs');
  const { parseMeu } = await import('../tools/mkmeu.mjs');
  let rd = new FatReader(Buffer.from(pc.machine.ata.img));
  const meu = (p) => { try { return parseMeu(Buffer.from(rd.readFile(rd.lookup(p)))); } catch { return null; } };
  const main = meu('DOS\\SHELL.MEU'), dos = meu('DOS\\DOSUTIL.MEU');
  t.ok(main && main.map((i) => i.title).join('|') === 'Command Prompt|File System|Change Colors|DOS Utilities...|Games & Fun...|My Group...',
    'SHELL.MEU on the disk has the changes', main && main.map((i) => i.title).join('|'));
  t.ok(main && main[5].cmd === 'MYGROUP.MEU' && !main[5].isProg, 'the group item names MYGROUP.MEU');
  t.ok(meu('DOS\\MYGROUP.MEU') && meu('DOS\\MYGROUP.MEU').length === 0, 'MYGROUP.MEU was created, empty');
  const copied = dos && dos.find((i) => i.title === 'Test Item');
  t.ok(copied && copied.cmd === 'echo hello\xBApause' && copied.help === 'Hello help', 'DOSUTIL.MEU got the copy, with the F4 marker between its commands', JSON.stringify(copied));

  // ---- File System operations on C:\WORK
  keys(pc, '{HOME}{DOWN}{ENTER}', 800);
  keys(pc, '{CTRL+C}{TAB}{HOME}{DOWN}{DOWN}{ENTER}{TAB}');
  t.ok(pc.lines()[5].startsWith('│ C:\\WORK') && pc.lines()[8].includes('A       .TXT'), 'File System: C:\\WORK listed', pc.lines().slice(3, 12).join('\n'));
  // select just the file called name (the list starts at row 8)
  const bar = () => grab(pc).attrs[1][3] === 0x0F;      /* the action bar is active */
  const pick = (name) => {
    if (bar()) keys(pc, '{F10}');
    if (pc.lines().some((l) => l[36] === '►')) keys(pc, '{F10}{ENTER}l{F10}');
    keys(pc, '{HOME}');
    const i = pc.lines().findIndex((l) => l.slice(38, 50) === name);
    keys(pc, '{DOWN}'.repeat(Math.max(0, i - 8)) + ' ');
  };
  pick('A       .TXT');
  keys(pc, '{F10}{ENTER}c');
  keys(pc, '{END}' + '{BS}'.repeat(12) + 'D.TXT{ENTER}', 800);
  pick('B       .TXT');
  keys(pc, '{F10}{ENTER}r');
  t.ok(pc.lines()[10].includes('Current filename:  B.TXT'), 'Rename File: B.TXT', pc.lines()[10]);
  keys(pc, 'E.TXT{ENTER}', 800);
  await save(pc, 'fs-ops-1');
  pick('C       .TXT');
  keys(pc, '{F10}{ENTER}d{ENTER}');
  t.ok(pc.lines()[10].includes('Deleting file:') && pc.lines()[10].includes('C.TXT') && pc.lines()[12].includes('Select an option.'),
    'Delete asks for each file (confirm on delete)', pc.lines().slice(7, 21).join('\n'));
  keys(pc, '{DOWN}{ENTER}', 800);
  t.ok(bar(), 'after a File action the action bar is active again (File highlighted)');
  keys(pc, '{ENTER}e');
  keys(pc, 'NEWDIR{ENTER}', 800);
  pick('A       .TXT');
  keys(pc, '{F10}{ENTER}m');
  keys(pc, '{END}' + '{BS}'.repeat(20) + 'C:\\WORK\\NEWDIR{ENTER}', 800);
  pick('E       .TXT');
  keys(pc, '{F10}{ENTER}h{ENTER}');
  t.ok(pc.screen().includes('Hidden') && pc.screen().includes('Read only') && pc.screen().includes('Archive'), 'Change attribute: the attribute list', pc.screen());
  keys(pc, '{DOWN} {ENTER}', 800);
  await save(pc, 'fs-ops-2');
  rd = new FatReader(Buffer.from(pc.machine.ata.img));
  const has = (p) => { try { return !!rd.lookup(p); } catch { return false; } };
  t.ok(has('WORK\\D.TXT') && rd.readFile(rd.lookup('WORK\\D.TXT')).toString() === 'This is A.TXT\r\n', 'Copy: A.TXT copied to D.TXT');
  t.ok(!has('WORK\\B.TXT') && has('WORK\\E.TXT'), 'Rename: B.TXT is now E.TXT');
  t.ok(!has('WORK\\C.TXT'), 'Delete: C.TXT is gone');
  t.ok(has('WORK\\NEWDIR') && has('WORK\\NEWDIR\\A.TXT') && !has('WORK\\A.TXT'), 'Create directory and Move: A.TXT moved into NEWDIR');
  const e = rd.lookup('WORK\\E.TXT');
  t.ok(e && (e.attr & 1), 'Change attribute: E.TXT is read-only', e && e.attr.toString(16));
  t.ok(pc.lines().some((l) => l.includes('NEWDIR')), 'the tree shows NEWDIR');
}


// ------------------------------------ more screens of the real Shell (tests/ref)
// captured for this work with the same DOSBox-X harness: dialogs, messages,
// the help index, File View, confirmations, the other colour schemes
{
  const pc = await start();
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  pc.machine.insertFloppy(makeFloppy());
  const games = (r, c) => r === 10 && c < 41;
  keys(pc, '{F10}{ENTER}a');
  same(pc, 'ref/50-add-program.txt', 'add-program', { mask: (r, c) => r === 10 && c < 16 });
  keys(pc, '{ESC}{F10}{RIGHT}{ENTER}a');
  same(pc, 'ref/51-add-group.txt', 'add-group', { mask: (r, c) => r === 10 && c < 16 });
  keys(pc, '{ESC}{DOWN}{DOWN}{DOWN}{ENTER}{F10}{ENTER}n');
  const items = (r, c, box) => r >= 6 && r <= 23 && !box(r, c);
  same(pc, 'ref/53-change-program.txt', 'change-program', { mask: (r, c) => items(r, c, (r, c) => r >= 7 && r <= 22 && c >= 16 && c <= 59) });
  keys(pc, '{ESC}{DOWN}{F10}{ENTER}d');
  same(pc, 'ref/52-delete-item.txt', 'delete-item', { mask: (r, c) => items(r, c, (r, c) => r >= 7 && r <= 14 && c >= 10 && c <= 67) });
  keys(pc, '{ESC}{F10}{RIGHT}{ENTER}r');
  same(pc, 'ref/54-reorder.txt', 'reorder', { mask: (r, c) => r >= 6 && r <= 23 });
  keys(pc, '{ESC}{ESC}');
  // a program with a password
  keys(pc, '{F10}{ENTER}aMy Program{TAB}chkdsk{F4}pause{TAB}{TAB}abc{F2}');
  keys(pc, '{END}{ENTER}');
  same(pc, 'ref/55-password.txt', 'password', { mask: (r, c) => r >= 10 && r <= 11 && c < 5 });
  keys(pc, '{ENTER}');
  same(pc, 'ref/56-password-incorrect.txt', 'password-incorrect', { mask: (r, c) => r >= 10 && r <= 11 && (c < 10 || r === 11) });
  keys(pc, '{ESC}{ESC}{F1}');
  same(pc, 'ref/59-help-no-text.txt', 'help-no-text', { mask: (r, c) => r >= 10 && r <= 12 });
  keys(pc, '{ESC}{F10}{ENTER}cabc{ENTER}');
  same(pc, 'ref/57-copy-program.txt', 'copy-program', { mask: (r, c) => r >= 6 && r <= 23 });
  keys(pc, '{F3}{F10}{RIGHT}{ENTER}aTest Group{TAB}TESTGRP{F2}{END}{ENTER}');
  same(pc, 'ref/58-group-is-empty.txt', 'group-is-empty');
  keys(pc, '{ESC}{HOME}{F1}{F11}');
  same(pc, 'ref/60-help-index.txt', 'help-index', { mask: (r, c) => r >= 6 && r <= 12 });
  keys(pc, '{DOWN}'.repeat(6));
  same(pc, 'ref/61-help-index-scrolled.txt', 'help-index-scrolled', { mask: (r, c) => r >= 6 && r <= 12 });
  keys(pc, '{ENTER}');
  same(pc, 'ref/62-help-topic.txt', 'help-topic (the frame; ARM-DOS\'s own text)', { mask: (r, c) => (r >= 6 && r <= 12) || (r >= 16 && r <= 20 && c > 15 && c < 63) || (r === 15 && c === 59) });
  keys(pc, '{ESC}');
  t.ok(!pc.lines()[14].includes('Indexed'), 'Esc on a topic from the index closes all the help');
  // the File System on the diskette
  keys(pc, '{HOME}{DOWN}{ENTER}{CTRL+A}{TAB}{TAB}' + '{DOWN}'.repeat(12) + ' {F10}{ENTER}c{END}WORK{ENTER}');
  same(pc, 'ref/63-replace-confirm.txt', 'replace-confirm', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}c{ENTER}');
  same(pc, 'ref/64-copy-to-itself.txt', 'copy-to-itself', { fix: onA, fixCell });
  keys(pc, '{ESC}{ENTER}d{ENTER}');
  same(pc, 'ref/65-delete-confirm.txt', 'delete-confirm', { fix: onA, fixCell });
  keys(pc, '{ESC}');
  same(pc, 'ref/66-bar-after-action.txt', 'bar-after-action', { fix: onA, fixCell });
  // View: the real CHKDSK.COM is on the captured diskette, filler here: the frame
  keys(pc, '{ENTER}l{F10}{HOME}{DOWN}{DOWN} {F10}{ENTER}v');
  same(pc, 'ref/67-view-binary-ascii.txt', 'view (ASCII)', { fix: onA, mask: (r, c) => r >= 11 && r <= 23 });
  keys(pc, '{F9}');
  same(pc, 'ref/68-view-hex.txt', 'view (hex)', { fix: onA, mask: (r, c) => r >= 11 && r <= 23 && ((c >= 12 && c <= 57) || (c >= 59 && c <= 78)) });
  keys(pc, '{ESC}{ESC}{F10}{RIGHT}{ENTER}{ENTER}{TAB}{DOWN}{DOWN}');
  same(pc, 'ref/69-display-options-date.txt', 'display options: sort by Date', { fix: onA, fixCell, mask: (r, c) => r === 7 && c === 75 });
  keys(pc, '{ENTER}');
  same(pc, 'ref/70-sorted-by-date.txt', 'sorted by date (then the last on the disk first); the action bar is active again', { fix: onA, fixCell, mask: (r, c) => (r === 7 && c === 75) || c === 36 });
  keys(pc, '{ENTER}{ENTER}{TAB}{UP}{UP}{ENTER}{F10}');
  keys(pc, '{TAB}{TAB}{DOWN}{ENTER}');
  same(pc, 'ref/72-tree-enter.txt', 'tree: Enter lists the highlighted directory', { fix: onA, fixCell, mask: (r, c) => r === 9 && c >= 50 && c <= 62 });
  keys(pc, '{TAB}{DOWN} {F10}{ENTER}a');
  same(pc, 'ref/71-associate.txt', 'associate', { fix: onA, fixCell, mask: (r, c) => r === 9 && c >= 50 && c <= 62 });
  keys(pc, '{ESC}{ESC}{F3}');
}

// set the date dialog (/l"8"), Down wraps in Start Programs, the directory dialogs, Change Attribute
{
  const pc = await start();
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  pc.machine.insertFloppy(makeFloppy());
  keys(pc, '{DOWN}{DOWN}{DOWN}{ENTER}{ENTER}');
  same(pc, 'ref/89-date-dialog.txt', 'Set Date and Time: /l"8" gives an 8-character field', { mask: (r, c) => r >= 9 && r <= 23 && !(r >= 12 && r <= 21 && c >= 27 && c <= 70) });
  keys(pc, '{ESC}{ESC}' + '{DOWN}'.repeat(4));
  t.ok(grab(pc).attrs[8][3] === 0x07, 'Start Programs: Down wraps around (DOS Utilities + 4 = Change Colors, as the real Shell)');
  keys(pc, '{ENTER}');
  same(pc, '05-change-colors-1.txt', 'change-colors (reached by wrapping)', { mask: pointer });
  keys(pc, '{ESC}{UP}{ENTER}{CTRL+A}{TAB}{DOWN}{ENTER}{F10}{ENTER}r');
  same(pc, 'ref/90-rename-directory.txt', 'Rename Directory', { fix: onA, fixCell, mask: (r, c) => r === 9 && c >= 50 && c <= 62 });
  keys(pc, '{ESC}{ENTER}d');
  same(pc, 'ref/91-delete-directory.txt', 'Delete Directory', { fix: onA, fixCell, mask: (r, c) => r === 9 && c >= 50 && c <= 62 });
  keys(pc, '{ESC}{F10}{UP}{ENTER}{TAB}{HOME}' + '{DOWN}'.repeat(12) + ' {F10}{ENTER}h{ENTER}');
  same(pc, 'ref/92-attributes.txt', 'Change Attribute: the attributes of one file', { fix: onA, fixCell });
  keys(pc, '{DOWN} ');
  same(pc, 'ref/93-attributes-read-only.txt', 'Change Attribute: Space marks Read only', { fix: onA, fixCell });
}

// Associate's second step; the Multiple file list under Tab
{
  const pc = await start();
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  pc.machine.insertFloppy(makeFloppy());
  keys(pc, '{DOWN}{ENTER}{CTRL+A}{TAB}{TAB}{HOME}{DOWN}{DOWN} {F10}{ENTER}aTXT{ENTER}');
  same(pc, 'ref/97-associate-options.txt', 'Associate: prompt for options?', { fix: onA, fixCell });
  keys(pc, '{DOWN}{ENTER}{ENTER}l{F10}{HOME}{TAB}{F10}{RIGHT}{RIGHT}{ENTER}m{TAB}{TAB}');
  same(pc, 'ref/98-multiple-files1.txt', 'Multiple file list: Tab to the first file list', { fix: onA, fixCell });
  keys(pc, '{TAB}');
  same(pc, 'ref/99-multiple-drives2.txt', 'Multiple file list: Tab to the second drive row', { fix: onA, fixCell, patchAttr: { 14: [[1, 1, 0, null], [2, 2, 0, 7], [3, 3, 0, null], [4, 4, 7, null], [5, 5, 7, 0], [6, 7, 7, null], [8, 8, 7, 0]] } });
  keys(pc, '{TAB}');
  same(pc, 'ref/100-multiple-tree2.txt', 'Multiple file list: Tab to the second tree', { fix: onA, fixCell });
  const { FatReader } = await import('../../../disk/mkimage.mjs');
  const rd = new FatReader(Buffer.from(pc.machine.ata.img));
  const asc = rd.readFile(rd.lookup('DOS\\SHELL.ASC')).toString();
  t.ok(/^CHKDSK\.COM=TXT\r\n$/.test(asc), 'Associate wrote SHELL.ASC (CHKDSK.COM for .TXT, no options prompt)', JSON.stringify(asc));
}

// the other colour schemes
{
  const pc = await start();
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  pc.machine.insertFloppy(makeFloppy());
  keys(pc, '{DOWN}{DOWN}{ENTER}{LEFT}');
  same(pc, 'ref/73-change-colors-4.txt', 'change colors 4', { mask: pointer });
  keys(pc, '{ENTER}{F10}{ENTER}');
  same(pc, 'ref/74-program-pulldown-4.txt', 'scheme 4: pulldown', { patch: { 10: GAMES_ROW }, patchAttr: { 10: [[0, 79, 1, null], [1, 5, 1, 15], [7, 7, 1, 15], [9, 14, 1, 15]] } });
  keys(pc, '{ESC}{ESC}{F1}');
  same(pc, 'ref/75-help-4.txt', 'scheme 4: help', { patch: { 10: GAMES_ROW }, patchAttr: { 10: [[0, 79, 1, null], [1, 5, 1, 15], [7, 7, 1, 15], [9, 14, 1, 15]] } });
  keys(pc, '{ESC}{SHIFT+F9}', 1500);
  const s = grab(pc);
  t.ok(s.attrs[0].every((a) => a === 0x70), 'scheme 4: the command prompt message is black on grey (70)');
  same(pc, 'ref/86-command-prompt-4.txt', 'scheme 4: the command prompt message', { mask: (r) => r > 2 });
  keys(pc, 'EXIT\r', 1500);
  keys(pc, '{UP}{ENTER}{CTRL+A}');
  same(pc, 'ref/76-file-system-4.txt', 'scheme 4: File System', { fix: onA, fixCell: (r, x, c) => { if (r === 3 && x === 8 && c.ch === 'C') c.fg = 15; }, patchAttr: { 3: [[1, 1, 7, null], [2, 2, 7, 0], [3, 3, 7, null], [4, 4, 1, null], [5, 5, 1, 15], [6, 7, 1, null], [8, 8, 1, 15]] } });
  keys(pc, '{TAB}{TAB}' + '{DOWN}'.repeat(12) + ' ');
  same(pc, 'ref/77-file-selected-4.txt', 'scheme 4: selected file', { fix: onA, fixCell: (r, x, c) => { if (r === 3 && x === 8 && c.ch === 'C') c.fg = 15; } });
  keys(pc, '{F3}{DOWN}{ENTER}{RIGHT}{RIGHT}{ENTER}{F10}{ENTER}');
  same(pc, 'ref/78-program-pulldown-2.txt', 'scheme 2: pulldown', { patch: { 10: GAMES_ROW }, patchAttr: { 10: [[0, 79, 0, null], [1, 5, 0, 15], [7, 7, 0, 15], [9, 14, 0, 15]] }, mask: (r, c) => r === 8 });
  keys(pc, '{ESC}{ESC}{F10}{ENTER}a');
  same(pc, 'ref/79-add-program-2.txt', 'scheme 2: dialog', { mask: (r, c) => r === 10 && c < 16 });
  keys(pc, '{ESC}{F1}');
  same(pc, 'ref/80-help-2.txt', 'scheme 2: help', { patch: { 10: GAMES_ROW }, patchAttr: { 10: [[0, 79, 0, null], [1, 5, 0, 15], [7, 7, 0, 15], [9, 14, 0, 15]] } });
  keys(pc, '{ESC}{UP}{ENTER}{CTRL+A}{TAB}');
  same(pc, 'ref/81-file-system-2.txt', 'scheme 2: File System', { fix: onA, fixCell: (r, x, c) => { if (r === 3 && x === 8 && c.ch === 'C') c.fg = 10; }, patchAttr: { 3: [[1, 1, 7, null], [2, 2, 7, 0], [3, 3, 7, null], [4, 4, 0, null], [5, 5, 0, 10], [6, 7, 0, null], [8, 8, 0, 10]] } });
  keys(pc, '{TAB}{TAB}{HOME}{ENTER}');
  same(pc, 'ref/82-open-file-2.txt', 'scheme 2: Open File', { fix: onA, mask: (r, c) => r < 7 || r > 20 || c < 14 || c > 63 });
  keys(pc, '{ESC}{SHIFT+F9}', 1500);
  same(pc, 'ref/87-command-prompt-2.txt', 'scheme 2: the command prompt message', { mask: (r) => r > 2 });
  keys(pc, 'EXIT\r', 1500);
  keys(pc, '{F3}{DOWN}{ENTER}{RIGHT}{ENTER}{F10}{ENTER}');
  same(pc, 'ref/83-program-pulldown-3.txt', 'scheme 3: pulldown', { patch: { 10: GAMES_ROW }, patchAttr: { 10: [[0, 79, 0, null], [1, 5, 0, 15], [7, 7, 0, 15], [9, 14, 0, 15]] } });
  keys(pc, '{ESC}{ESC}{F1}');
  same(pc, 'ref/84-help-3.txt', 'scheme 3: help', { patch: { 10: GAMES_ROW }, patchAttr: { 10: [[0, 79, 0, null], [1, 5, 0, 15], [7, 7, 0, 15], [9, 14, 0, 15]] } });
  keys(pc, '{ESC}{UP}{ENTER}{CTRL+A}');
  same(pc, 'ref/85-file-system-3.txt', 'scheme 3: File System', { fix: onA, fixCell: (r, x, c) => { if (r === 3 && x === 8 && c.ch === 'C') c.fg = 15; } });
}

// ------------------------------------------------------ Games & Fun: Zork I
if (fs.existsSync(path.join(ROOT, 'build/ZORK1.EXE'))) {
  const pc = await start({ extra: [{ src: 'build/ZORK1.EXE', dst: 'GAMES\\ZORK\\' }, { src: 'apps/zork/data/ZORK1.DAT', dst: 'GAMES\\ZORK\\' }],
    dirs: ['GAMES', 'GAMES\\ZORK'] });
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  keys(pc, '{END}{ENTER}');
  const L = pc.lines();
  t.ok(L[2].includes('Games & Fun...') && L.some((l) => l.startsWith(' Zork I: The Great Underground Empire')), 'Games & Fun group', L.slice(2, 16).join('\n'));
  const z = L.findIndex((l) => l.startsWith(' Zork I:'));
  keys(pc, '{DOWN}'.repeat(z - 6) + '{ENTER}', 3000);
  t.ok(pc.screen().includes('West of House'), 'Zork I starts (CD \\GAMES\\ZORK, ZORK1)', pc.screen());
  keys(pc, 'quit\r', 800);
  keys(pc, 'y\r', 2500);
  const M = pc.lines(), g = grab(pc);
  t.ok(M[2].includes('Games & Fun...') && g.attrs[z][3] === 0x07, 'after the game: back in the group, on Zork I', M.slice(0, 12).join('\n'));
}

// --------------------------------------------------------------------- mouse
if (fs.existsSync(path.join(ROOT, 'build/MOUSE.COM'))) {
  const pc = await start({ autoexec: true, mouse: true });
  pc.waitText('Start Programs', { timeoutMs: 30000 });
  pc.waitIdle();
  let at = { r: 12, c: 40 };                    // the driver centres the pointer on reset
  const to = (r, c) => { pc.machine.mouseMove((c - at.c) * 8, (r - at.r) * 16); pc.run(150); at = { r, c }; };
  const click = () => { pc.machine.mouseButtons(1); pc.run(60); pc.machine.mouseButtons(0); pc.run(250); };
  const dbl = () => { pc.machine.mouseButtons(1); pc.run(40); pc.machine.mouseButtons(0); pc.run(40);
    pc.machine.mouseButtons(1); pc.run(40); pc.machine.mouseButtons(0); pc.run(400); };
  const g0 = grab(pc);
  t.ok(g0.attrs[12][40] !== 0x70, 'mouse: the pointer is shown (a highlighted cell)', g0.attrs[12][40].toString(16));
  to(9, 5); click();
  t.ok(grab(pc).attrs[9][10] === 0x07, 'mouse: a click selects DOS Utilities...');
  dbl(); pc.waitIdle();
  t.ok(pc.lines()[2].includes('DOS Utilities...'), 'mouse: a double click opens the group', pc.lines()[2]);
  to(24, 16); click(); pc.waitIdle();
  t.ok(pc.lines()[2].includes('Main Group'), 'mouse: clicking Esc=Cancel on the bottom line goes back', pc.lines()[2]);
  to(1, 20); click(); pc.waitIdle();
  t.ok(pc.lines()[3].includes('Exit Shell'), 'mouse: clicking Exit on the action bar opens its pulldown', pc.lines()[3]);
  to(12, 60); click(); pc.waitIdle();
  t.ok(!pc.lines()[3].includes('Exit Shell'), 'mouse: a click outside closes the pulldown');
  to(7, 3); dbl(); pc.waitIdle(); pc.run(500);
  t.ok(pc.lines()[0].includes('File System'), 'mouse: double-clicking File System starts it', pc.lines()[0]);
  await save(pc, 'mouse-fs');
  to(9, 40); click(); pc.waitIdle();
  const L = pc.lines();
  t.ok(L[9][36] === '►', 'mouse: clicking a file selects it', L[9]);
  to(1, 28); click(); pc.waitIdle();
  to(3, 30); click(); pc.waitIdle(); pc.run(500);
  t.ok(pc.lines()[0].includes('Start Programs'), 'mouse: Exit > Exit File System goes back to Start Programs', pc.lines()[0]);
}
t.done();
