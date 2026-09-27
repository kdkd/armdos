#!/usr/bin/env node
// apps/ac/tests/run.mjs - the ARM Commander (AC.EXE + ACMAIN.EXE), headless.
//
// Boots a private C: (COMMAND.COM, HIMEM, MOUSE, MEM, AC, scratch files),
// starts AC from the prompt and drives it with real key presses (and the
// PS/2 mouse through MOUSE.COM): screen layout and colours as the Norton
// Commander 3.0 look it recreates, running commands (MEM: the memory a
// program gets from AC's command line), Ctrl-O, copy / move / rename /
// mkdir / delete on the scratch files (checked on the disk image), viewer
// and editor round trips, menus, info and tree panels, find file, history,
// quick search, the mouse, quitting, and DOOM started from a panel.
// Screenshots go to build/ac-test/.
import fs from 'node:fs';
import { startPC, check, summary, hdEntries, B } from './harness.mjs';

const KEYBAR = '1Help   2Menu   3View   4Edit   5Copy   6RenMov 7Mkdir  8Delete 9PullDn 10Quit';
let longText = '';
for (let i = 1; i <= 60; i++) longText += `Line ${i}\tof the text file\n`;

const pc = await startPC({
  name: 'ac', mouse: true,
  dirs: ['WORK', 'WORK\\SUB', 'WORK\\SUB\\DEEP', 'DEST', 'GAMES', 'GAMES\\DOOM'],
  texts: {
    'WORK\\A.TXT': 'alpha\n', 'WORK\\B.TXT': 'bravo\n', 'WORK\\C.DAT': 'charlie\n',
    'WORK\\README.TXT': longText, 'WORK\\EDIT.TXT': 'first line\nsecond line\n',
    'WORK\\SUB\\S1.TXT': 'sub one\n', 'WORK\\SUB\\DEEP\\FIND.ME': 'deep\n',
  },
  files: fs.existsSync(B('DOOM.EXE')) ? [
    { src: 'build/DOOM.EXE', dst: 'GAMES\\DOOM\\' }, { src: '3rdparty/doom/DOOM1.WAD', dst: 'GAMES\\DOOM\\' },
  ] : [],
});
check(pc.bootOk, 'boots to the C:\\> prompt');

const scr = () => pc.screen();
const has = (t) => pc.hasText(t);
const row = (r) => pc.row(r);
const shot = (f) => pc.shot(f);
const BAR = { Left: 4, Files: 12, Commands: 21, Options: 33, Right: 44 };
/** F9, walk the menu bar to `name`, optionally press `key` */
const menu = (name, key = '') => {
  pc.keys('{F9}');
  for (let i = 0; i < 5 && pc.attr(BAR[name], 0) !== 0x0F; i++) pc.keys('{RIGHT}');
  if (key) pc.keys(key);
};
const largest = () => { const m = [...scr().matchAll(/(\d+) largest executable program size/g)]; return m.length ? +m[m.length - 1][1] : 0; };

// ---- MEM at the bare prompt, for comparison
pc.keys('cls\rmem\r');
const bare = largest();
check(bare > 500000, `MEM at the prompt: ${bare} bytes largest executable program size`);

// ---- start
pc.keys('ac\r');
await shot('start.png');
check(row(0).startsWith('╔') && row(0).includes(' C:\\ ') && row(0).endsWith('╗'), 'both panels framed, path on the top border', row(0));
check(row(1) === '║    Name    │    Name    │    Name    ║║    Name    │    Name    │    Name    ║', 'brief mode: three "Name" columns', row(1));
check(row(20).startsWith('╟────────────┴') && row(22).startsWith('╚'), 'separator line and bottom border', row(20));
check(row(23).startsWith('C:\\>'), 'the DOS prompt below the panels', row(23));
check(row(24) === KEYBAR, 'the function key bar', row(24));
{
  const x = row(0).indexOf(' C:\\ ');
  check(pc.attr(x + 1, 0) === 0x30, 'active panel path black on cyan');
  check(pc.attr(0, 0) === 0x1B && pc.attr(40, 5) === 0x1B, 'blue panels, light cyan double frames');
  check(pc.attr(5, 1) === 0x1E, 'yellow column headers');
  check(pc.attr(1, 2) === 0x30 && pc.attr(41, 2) === 0x1B, 'cursor bar only in the active panel');
  check(pc.attr(0, 24) === 0x07 && pc.attr(1, 24) === 0x30, 'key bar: grey numbers, black on cyan labels');
}
check(row(2).startsWith('║DEST        │') && row(4).startsWith('║GAMES       │'), 'directories upper case, sorted first', row(2));
check(/║autoexec bat│/.test(scr()) && /║command  com│/.test(scr()), 'files lower case, extensions aligned');
check(row(21) === '║DEST         ►SUB-DIR◄  6-01-89 12:00p║║DEST         ►SUB-DIR◄  6-01-89 12:00p║', 'mini status line', row(21));

// ---- running a command
pc.keys('mem\r');
check(row(24) === KEYBAR && row(0).startsWith('╔'), 'after a command the panels come straight back');
pc.keys('{CTRL+O}');
await shot('userscreen.png');
const inAc = largest();
check(has('C:\\>mem') && inAc > 0, 'Ctrl-O shows the user screen with the command and its output');
check(bare - inAc > 0 && bare - inAc <= 12 * 1024, `a program started from AC gets ${inAc} bytes (${bare - inAc} less than at the prompt)`);
check(row(23).startsWith('C:\\>') && row(24) === KEYBAR, 'panels off: command line and key bar stay');
pc.keys('{CTRL+O}');
check(row(1).includes('Name'), 'Ctrl-O brings the panels back');
pc.keys('{CTRL+L}');
{
  const m = scr().match(/([\d,]+) Bytes Free/);
  const free = m ? +m[1].replace(/,/g, '') : 0;
  check(has('Bytes Memory') && Math.abs(free - inAc) < 1024, `Ctrl-L info panel: ${free} bytes free for programs`);
  check(has('total bytes on drive C:') && has('bytes free on drive C:'), 'info panel: disk space');
}
await shot('info.png');
pc.keys('{CTRL+L}');

// ---- panels on WORK (left) and DEST (right)
pc.keys('cd work\r');
check(row(0).includes(' C:\\WORK '), 'cd on the command line changes the panel', row(0));
pc.keys('{TAB}cd dest\r{TAB}');
check(row(0).includes(' C:\\DEST '), 'Tab, cd in the right panel');

// ---- selection
pc.keys('{DOWN}{INS}{INS}');
check(has('11 bytes in 2 selected files') || has(' bytes in 2 selected files'), 'Ins selects, totals on the separator line', row(20));
check(pc.attr(1, 3) === 0x1E, 'selected files in yellow');
pc.keys('{NumpadSubtract}\r');
check(!has('selected file'), 'Gray - unselects');
pc.keys('{NumpadAdd}');
await shot('select.png');
pc.keys('*.TXT\r');
check(has('in 4 selected files'), 'Gray + *.TXT selects the four .TXT files', row(20));
pc.keys('{NumpadMultiply}');
check(has('in 1 selected file'), 'Gray * inverts (files only)', row(20));
pc.keys('{NumpadMultiply}{NumpadSubtract}\r');

// ---- copy
pc.keys('{HOME}{DOWN}{DOWN}{INS}{INS}');       // A.TXT B.TXT (after .., SUB)
pc.keys('{F5}');
await shot('copy.png');
check(has('Copy 2 files to') && has('C:\\DEST\\') && has('[ Copy ]') && has('[ Cancel ]'), 'F5: the copy dialog, the other panel as target');
check(pc.attr(40, 10) === 0x70 || pc.attr(30, 10) === 0x70, 'copy dialog is grey');
pc.keys('\r');
check(pc.readFile('DEST\\A.TXT') === 'alpha\r\n' && pc.readFile('DEST\\B.TXT') === 'bravo\r\n', 'files copied to C:\\DEST');
check(/║a        txt│/.test(row(2) + row(3) + row(4)) || scr().includes('║║a        txt'), 'the other panel is re-read');
check(!has('selected file'), 'selection cleared after the copy');
pc.keys('{HOME}{DOWN}{DOWN}{F5}\r');
check(has('exists') && has('[ Overwrite ]'), 'copying again asks before overwriting');
await shot('overwrite.png');
pc.keys('{RIGHT}{RIGHT}{RIGHT}\r');

// copy a directory tree
pc.keys('{HOME}{DOWN}{F5}\r');
check(pc.readFile('DEST\\SUB\\S1.TXT') === 'sub one\r\n' && pc.readFile('DEST\\SUB\\DEEP\\FIND.ME') === 'deep\r\n', 'F5 on a directory copies the tree');

// ---- rename / move
pc.keys('{HOME}');
for (let i = 0; i < 10 && !row(21).startsWith('║c        dat'); i++) pc.keys('{DOWN}');
pc.keys('{F6}');
check(has('Rename or move "c.dat" to'), 'F6: the rename/move dialog');
pc.keys('\r');
check(pc.readFile('DEST\\C.DAT') === 'charlie\r\n' && pc.readFile('WORK\\C.DAT') === null, 'F6 moves the file to the other panel');
pc.keys('{HOME}');
for (let i = 0; i < 10 && !row(21).startsWith('║edit     txt'); i++) pc.keys('{DOWN}');
pc.keys('{F6}NOTES.TXT\r');
check(pc.readFile('WORK\\NOTES.TXT') === 'first line\r\nsecond line\r\n' && pc.readFile('WORK\\EDIT.TXT') === null, 'F6 with a new name renames in place');
check(row(21).startsWith('║notes    txt'), 'cursor on the renamed file', row(21));

// ---- mkdir
pc.keys('{F7}');
check(has('Create the directory'), 'F7: make directory dialog');
pc.keys('NEWDIR\r');
check(hdEntries(pc, 'WORK').includes('NEWDIR') && row(21).startsWith('║NEWDIR'), 'F7 creates the directory, cursor on it');

// ---- delete
pc.keys('{F8}');
await shot('delete.png');
check(has('Do you wish to delete') && has('NEWDIR') && pc.attr(40, 11) >> 4 === 4, 'F8: the red delete dialog');
pc.keys('\r');
check(!hdEntries(pc, 'WORK').includes('NEWDIR'), 'F8 deletes the empty directory');
pc.keys('{TAB}{HOME}');
for (let i = 0; i < 10 && !row(21).slice(40).startsWith('║SUB'); i++) pc.keys('{DOWN}');
pc.keys('{F8}\r');
check(has('The following directory is not empty'), 'deleting a non-empty directory asks again');
pc.keys('\r');
check(!hdEntries(pc, 'DEST').includes('SUB') && hdEntries(pc, 'DEST').includes('A.TXT'), 'the directory tree is deleted');
pc.keys('{HOME}{DOWN}{F8}\r');
check(pc.readFile('DEST\\A.TXT') === null, 'F8 deletes a file');
pc.keys('{TAB}');

// ---- viewer
pc.keys('{HOME}');
for (let i = 0; i < 10 && !row(21).startsWith('║readme   txt'); i++) pc.keys('{DOWN}');
pc.keys('{F3}');
await shot('view.png');
check(row(0).startsWith('View: c:\\work\\readme.txt') && row(0).includes('Col 0') && row(0).includes('1551 Bytes'), 'F3: the viewer title line', row(0));
check(row(1) === 'Line 1  of the text file' && row(23) === 'Line 23 of the text file', 'text with tabs expanded');
check(pc.attr(0, 1) === 0x1B && pc.attr(0, 0) === 0x30, 'viewer colours');
pc.keys('{END}');
check(row(23) === 'Line 60 of the text file' && row(0).endsWith('100%'), 'End: the last page, 100%');
pc.keys('{F7}Line 42\r');
check(row(1) === 'Line 42 of the text file', 'F7 search', row(1));
pc.keys('{F4}');
check(/^0000[0-9A-F]{4}: /.test(row(1)) && (row(1) + row(2)).replace('-', ' ').includes('4C 69 6E 65 20 34 32'), 'F4: hex mode', row(1));
await shot('viewhex.png');
pc.keys('{ESC}');
check(row(24) === KEYBAR && row(1).includes('Name'), 'Esc leaves the viewer');

// ---- editor
pc.keys('{HOME}');
for (let i = 0; i < 10 && !row(21).startsWith('║notes    txt'); i++) pc.keys('{DOWN}');
pc.keys('{F4}');
await shot('edit.png');
check(row(0).startsWith('Edit: c:\\work\\notes.txt') && row(1) === 'first line' && row(2) === 'second line', 'F4: the editor', row(0));
pc.keys('{DOWN}{END} and more{ENTER}third{UP}{UP}{HOME}X{F2}');
check(pc.readFile('WORK\\NOTES.TXT') === 'Xfirst line\r\nsecond line and more\r\nthird\r\n', 'edit + F2 saves (CR LF lines)');
pc.keys('junk{ESC}');
check(has('The file has been modified.'), 'Esc on a modified file asks');
pc.keys('{RIGHT}\r');
check(pc.readFile('WORK\\NOTES.TXT') === 'Xfirst line\r\nsecond line and more\r\nthird\r\n' && row(1).includes('Name'), '"Don\'t save" leaves the file as it was');
pc.keys('{SHIFT+F4}NEW.TXT\rhello{F10}\r');
check(pc.readFile('WORK\\NEW.TXT') === 'hello\r\n' && row(21).startsWith('║new      txt'), 'Shift-F4 edits a new file');

// ---- menus
pc.keys('{F9}');
await shot('menu.png');
check(row(0).startsWith('    Left    Files    Commands    Options    Right'), 'F9: the menu bar', row(0));
check(has('Brief') && has('eXtension') && has('Drive...     Alt-F1') && pc.attr(3, 0) === 0x0F, 'the Left menu, Left highlighted');
pc.keys('{RIGHT}');
check(has('Rename or move') && has('Select group') && has('Gray +'), 'the Files menu');
pc.keys('{ESC}'); menu('Left', 'f');
check(row(1).includes('Size') && row(1).includes('Date') && row(1).includes('Time'), 'Left > Full: Name Size Date Time');
check(/║\.\.           ►UP--DIR◄/.test(scr()), 'full mode: ►UP--DIR◄');
menu('Left', 't');
check(scr().includes('├──') && scr().includes('Tree'), 'Left > Tree');
await shot('tree.png');
menu('Left', 'b');
menu('Right', 't');
check(row(0).includes('Tree'), 'Right > Tree');
menu('Right', 'b');
pc.keys('{CTRL+F1}');
check(!row(1).startsWith('║    Name'), 'Ctrl-F1 hides the left panel');
pc.keys('{CTRL+F1}');
check(row(1).startsWith('║    Name'), 'Ctrl-F1 shows it again');
pc.keys('{CTRL+U}');
check(row(0).includes('C:\\DEST') && row(0).indexOf('C:\\DEST') < 40, 'Ctrl-U swaps the panels', row(0));
pc.keys('{CTRL+U}');
{
  const x = row(0).indexOf(' C:\\WORK ');
  if (pc.attr(x + 1, 0) !== 0x30) pc.keys('{TAB}');
  check(pc.attr(row(0).indexOf(' C:\\WORK ') + 1, 0) === 0x30, 'Tab back to the left panel');
}

// ---- quick search, Ctrl-Enter, history
pc.keys('{ALT+R}');
check(has('Search: R'), 'Alt+letter: the quick search box', row(21));
await shot('quicksearch.png');
pc.keys('{ESC}');
check(row(21).startsWith('║readme   txt'), 'quick search moved the cursor', row(21));
pc.keys('{CTRL+ENTER}');
check(row(23) === 'C:\\WORK>readme.txt', 'Ctrl-Enter puts the file name on the command line', row(23));
pc.keys('{ESC}');
check(row(23) === 'C:\\WORK>', 'Esc clears the command line');
pc.keys('{ALT+F8}');
await shot('history.png');
check(has('History') && has('mem') && has('cd dest'), 'Alt-F8: the history');
pc.keys('{ESC}{CTRL+E}');
check(row(23).startsWith('C:\\WORK>cd '), 'Ctrl-E recalls the last command', row(23));
pc.keys('{ESC}');

// ---- find file
pc.keys('{ALT+F7}FIND.ME\r');
await shot('find.png');
check(has('C:\\WORK\\SUB\\DEEP\\FIND.ME'), 'Alt-F7 finds the file');
pc.keys('\r');
check(row(0).includes('C:\\WORK\\SUB\\DEEP') && row(21).startsWith('║find     me'), 'Enter goes to the file');
pc.keys('cd \\work\r');

// ---- sorting, user menu
pc.keys('{CTRL+F6}');
check(row(4).startsWith('║readme   txt'), 'Ctrl-F6 sorts by size (largest file first)', row(4));
pc.keys('{CTRL+F3}');
check(row(4).startsWith('║a        txt'), 'Ctrl-F3 sorts by name again', row(4));
pc.keys('{F2}');
await shot('usermenu.png');
check(has('User menu') && has('M  Memory report') && has('T  Directory tree'), 'F2: the user menu from C:\\DOS\\AC.MNU');
pc.keys('{ESC}');

// ---- drive menu
pc.keys('{ALT+F1}');
check(has('Choose left drive:') && /\bA\b.*\bC\b/.test(scr()), 'Alt-F1: the drive menu');
pc.keys('{ESC}');

// ---- the mouse (MOUSE.COM)
{
  const m = pc.m;
  const moveTo = (cx, cy) => {
    for (let i = 0; i < 8; i++) { m.mouseMove(-300, -300); pc.run(20); }
    m.mouseMove(cx * 8 + 4, (cy * 8 + 4) * 2); pc.run(100);
  };
  const click = (btn = 1) => { m.mouseButtons(btn); pc.run(60); m.mouseButtons(0); pc.run(120); };
  pc.keys('{HOME}');
  moveTo(3, 4); pc.run(800); click(); pc.waitIdle();
  const r4 = row(4).slice(1, 13).trim();
  check(row(21).slice(1, 13).trim() === r4, `mouse click moves the cursor (${r4})`);
  click(2); pc.waitIdle();
  check(has('1 selected file'), 'right click selects');
  click(2); pc.waitIdle();
  moveTo(1, 3); pc.run(800); click(); click(); pc.waitIdle();
  check(row(0).includes('C:\\WORK\\SUB'), 'double click enters a directory', row(0));
  pc.keys('{CTRL+PGUP}');
  check(row(0).includes(' C:\\WORK ') && row(21).startsWith('║SUB'), 'Ctrl-PgUp: parent, cursor on the directory left');
  moveTo(75, 24); pc.run(800); click(); pc.waitIdle();
  check(has('Do you want to quit the ARM Commander?'), 'a click on "10Quit" in the key bar');
  pc.keys('{ESC}');
}

// ---- DOOM from a panel (mode 13h and back)
if (fs.existsSync(B('DOOM.EXE'))) {
  pc.keys('cd \\games\\doom\r');
  pc.keys('{HOME}{DOWN}');
  check(row(21).startsWith('║doom     exe'), 'cursor on doom.exe');
  pc.keys('{ENTER}', false);
  const g = pc.until(() => pc.m.vga.mode === 0x13, { timeoutMs: 60000 });
  check(g, 'Enter on DOOM.EXE runs DOOM (mode 13h)');
  pc.run(3000);
  await shot('doom.png');
  pc.type('{F10}'); pc.run(1500); pc.type('y');
  const back = pc.until(() => pc.m.vga.mode === 3 && pc.hasText('1Help'), { timeoutMs: 60000 });
  pc.waitIdle();
  check(back && row(0).includes('C:\\GAMES\\DOOM'), 'after DOOM: text mode and the panels again');
  pc.keys('{CTRL+O}');
  check(has('DOOM: Knee-Deep in the Dead'), 'the user screen keeps ENDOOM');
  pc.keys('{CTRL+O}cd \\\r');
}

// ---- quit
pc.keys('{F10}');
await shot('quit.png');
check(has('Do you want to quit the ARM Commander?') && has('[ Yes ]') && has('[ No ]'), 'F10: the quit dialog');
pc.keys('\r');
check(!has('1Help') && /^C:\\.*>$/.test(pc.lines()[pc.m.cpu.m8[0x451]].trimEnd()), 'Yes: back at the DOS prompt');
pc.keys('cls\rmem\r');
check(largest() === bare, 'all memory is free again after quitting');
check(!pc.faults.length, 'no CPU faults', JSON.stringify(pc.faults.slice(0, 3)));

process.exit(summary() ? 1 : 0);
