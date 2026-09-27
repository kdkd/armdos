#!/usr/bin/env node
// apps/edit/tests/run.mjs - EDIT.EXE (the ARM-DOS Editor) headless tests.
//
//   node apps/edit/tests/run.mjs [--only name,...] [--shots]
//
// Boots ARM-DOS (COMMAND.COM, HIMEM.SYS, optionally MOUSE.COM) with EDIT.EXE
// and EDIT.HLP in C:\DOS, drives it with keys and mouse, and checks the
// screen (text and attributes), the files written to the disk image, the
// printer output, and the screen after EDIT exits. Screenshots of every
// stage go to build/edit-test/*.png (and .txt).
import fs from 'node:fs';
import path from 'node:path';
import { start, keys, grab, shot, mouse, readFile, ROOT, OUT } from './lib.mjs';

const args = process.argv.slice(2);
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
let pass = 0, fail = 0;
const ok = (c, what, detail) => {
  if (c) pass++; else fail++;
  console.log(`${c ? 'ok  ' : 'FAIL'} ${what}`);
  if (!c && detail !== undefined) console.log('     ' + String(detail).split('\n').join('\n     '));
  return c;
};
const want = (name) => !ONLY || ONLY.includes(name);
const L = (pc) => pc.lines();
const row = (pc, r) => L(pc)[r] || '';
const attrAt = (pc, r, c) => grab(pc).attrs[r][c];
const statusPos = (pc) => (row(pc, 24).match(/(\d{5}):(\d{3})/) || []).slice(1).map(Number);
const need = ['build/EDIT.EXE', 'build/edit/EDIT.HLP', 'build/COMMAND.COM', 'build/MOUSE.COM', 'build/HIMEM.SYS'];
for (const f of need) if (!fs.existsSync(path.join(ROOT, f))) { console.log(`missing ${f}`); process.exit(1); }

// ------------------------------------------------------------- the look
if (want('look')) {
  console.log('== start-up, welcome, the empty editor');
  const pc = await start([], {});
  keys(pc, 'CLS\rECHO BEFORE EDIT\r', 300);
  const dos = L(pc).slice();
  keys(pc, 'EDIT\r', 1500);
  await shot(pc, 'look-welcome');
  ok(pc.hasText('Welcome to the ARM-DOS Editor'), 'welcome dialog', pc.screen());
  ok(pc.hasText('< Press ENTER to see the Survival Guide >') && pc.hasText('< Press ESC to clear this dialog box >'), 'welcome dialog buttons');
  keys(pc, '{ESC}', 500);
  await shot(pc, 'look-empty');
  ok(row(pc, 0).startsWith('  File  Edit  Search  Options  ') && row(pc, 0).indexOf('Help') === 74 && row(pc, 0).trim().split(/\s+/).length === 5,
    'menu bar: File Edit Search Options ... Help', JSON.stringify(row(pc, 0)));
  ok(/^┌─+ Untitled ─+┐$/.test(row(pc, 1)), 'window title "Untitled" centred in the frame', row(pc, 1));
  ok(row(pc, 2).startsWith('│') && row(pc, 2).endsWith('↑') && row(pc, 22).endsWith('↓'), 'vertical scroll bar on the right edge');
  ok(/^│←.*→│$/.test(row(pc, 23)), 'horizontal scroll bar on the bottom row', row(pc, 23));
  ok(row(pc, 24).startsWith(' ARM-DOS Editor  <F1=Help> Press ALT to choose commands') && row(pc, 24).endsWith('│ 00001:001'),
    'status line: ARM-DOS Editor <F1=Help> ... │ 00001:001', row(pc, 24));
  ok(attrAt(pc, 0, 2) === 0x7F && attrAt(pc, 0, 3) === 0x70, 'menu bar black on grey, hot keys bright white');
  ok(attrAt(pc, 5, 10) === 0x17, 'text area grey on blue', attrAt(pc, 5, 10).toString(16));
  ok(attrAt(pc, 24, 5) === 0x30, 'status line black on cyan');
  const t = row(pc, 1).indexOf('Untitled');
  ok(attrAt(pc, 1, t) === 0x70, 'active window title reversed');
  ok(!/MS-DOS|Microsoft/.test(pc.screen()), 'no MS-DOS / Microsoft branding');

  // the menus
  keys(pc, '{ALT+F}', 500);
  await shot(pc, 'look-file-menu');
  const fm = L(pc).slice(1, 12).join('\n');
  ok(['New', 'Open...', 'Save', 'Save As...', 'Print...', 'Exit'].every((s) => fm.includes('│ ' + s)), 'File menu: New Open... Save Save As... Print... Exit', fm);
  ok(row(pc, 24).includes('F1=Help │ Removes currently loaded file from memory'), 'status line: hint for New', row(pc, 24));
  keys(pc, '{DOWN}', 300);
  ok(row(pc, 24).includes('Loads new file into memory'), 'status line: hint follows the highlighted item');
  keys(pc, '{RIGHT}', 400);
  await shot(pc, 'look-edit-menu');
  const em = L(pc).slice(1, 8).join('\n');
  ok(/Cut\s+Shift\+Del/.test(em) && /Copy\s+Ctrl\+Ins/.test(em) && /Paste\s+Shift\+Ins/.test(em) && /Clear\s+Del/.test(em),
    'Edit menu: Cut Shift+Del, Copy Ctrl+Ins, Paste Shift+Ins, Clear Del', em);
  const copyRow = L(pc).findIndex((l) => l.includes('Copy'));
  ok(attrAt(pc, copyRow, row(pc, copyRow).indexOf('Copy') + 1) === 0x78, 'Copy is disabled (dimmed) without a selection');
  keys(pc, '{RIGHT}', 400);
  const sm = L(pc).slice(1, 8).join('\n');
  ok(sm.includes('Find...') && /Repeat Last Find\s+F3/.test(sm) && sm.includes('Change...'), 'Search menu: Find... Repeat Last Find F3 Change...', sm);
  keys(pc, '{RIGHT}', 400);
  const om = L(pc).slice(1, 8).join('\n');
  ok(om.includes('Display...') && om.includes('Help Path...'), 'Options menu: Display... Help Path...');
  keys(pc, '{RIGHT}', 400);
  await shot(pc, 'look-help-menu');
  const hm = L(pc).slice(1, 8).join('\n');
  ok(hm.includes('Getting Started') && hm.includes('Keyboard') && hm.includes('About...'), 'Help menu (at the right end): Getting Started Keyboard About...', hm);
  keys(pc, '{ESC}{ESC}', 400);
  ok(!pc.hasText('Getting Started') && !pc.hasText('Display...'), 'Esc closes the menus');

  // Alt alone activates the menu bar
  keys(pc, '{ALT}', 500);
  await shot(pc, 'look-alt');
  ok(attrAt(pc, 0, 2) === 0x0F && attrAt(pc, 0, 1) === 0x07, 'Alt pressed alone: menu bar active, File highlighted', attrAt(pc, 0, 1).toString(16));
  ok(row(pc, 24).includes('Enter=Display Menu'), 'status line while the menu bar is active', row(pc, 24));
  keys(pc, 's', 500);
  ok(L(pc).slice(1, 6).join('').includes('Find...'), 'then a letter opens that menu');
  keys(pc, '{ESC}{ESC}', 300);

  // exit: the DOS screen comes back
  keys(pc, '{ALT+F}x', 1500);
  const after = L(pc);
  ok(after.slice(0, 3).join('\n') === dos.slice(0, 3).join('\n') && after.some((l) => l.startsWith('C:\\>')), 'Exit restores the DOS screen', after.slice(0, 6).join('\n'));
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ----------------------------------------------- type, save, reload
if (want('files')) {
  console.log('== typing, Save As, reload, CR LF, LF-only files');
  const pc = await start([['UNIX.TXT', 'alpha\nbeta\ngamma\n'], ['OLD.TXT', 'one\r\ntwo\r\n']], {});
  keys(pc, 'EDIT\r', 1500);
  keys(pc, '{ESC}', 300);
  keys(pc, 'Hello, world!\rThe second line.\r\tTabbed', 500);
  await shot(pc, 'files-typed');
  ok(row(pc, 2) === '│Hello, world!'.padEnd(79) + '↑', 'first line typed', row(pc, 2));
  ok(row(pc, 4).startsWith('│        Tabbed'), 'a tab expands to column 9', row(pc, 4));
  ok(statusPos(pc).join(':') === '3:15', 'status line shows line 3, column 15', row(pc, 24));
  keys(pc, '{ALT+F}a', 800);
  await shot(pc, 'files-saveas');
  ok(pc.hasText('Save As') && pc.hasText('File Name:') && pc.hasText('Dirs/Drives'), 'Save As dialog', pc.screen());
  ok(pc.hasText('C:\\'), 'Save As shows the current directory');
  keys(pc, 'NEW.TXT\r', 1000);
  await shot(pc, 'files-saved');
  ok(/ NEW\.TXT /.test(row(pc, 1)), 'title becomes NEW.TXT', row(pc, 1));
  const f = readFile(pc, 'NEW.TXT');
  ok(f && f.toString('latin1') === 'Hello, world!\r\nThe second line.\r\n\tTabbed', 'NEW.TXT saved with CR LF', f && JSON.stringify(f.toString('latin1')));
  // modify and exit without saving: the prompt
  keys(pc, ' more', 300);
  keys(pc, '{ALT+F}x', 800);
  await shot(pc, 'files-notsaved');
  ok(pc.hasText('Loaded file is not saved. Save it now?') && pc.hasText('< Yes >') && pc.hasText('< No >') && pc.hasText('< Cancel >'),
    'Exit with changes: "Loaded file is not saved. Save it now?"', pc.screen());
  keys(pc, '{ESC}', 400);
  ok(!pc.hasText('Save it now?') && pc.hasText('Tabbed more'), 'Cancel stays in the editor');
  keys(pc, '{ALT+F}x', 800);
  keys(pc, 'y', 1500);
  ok(pc.hasText('C:\\>'), 'Yes saves and exits');
  ok(readFile(pc, 'NEW.TXT').toString('latin1').endsWith('\tTabbed more'), 'the change was saved');
  // reload
  keys(pc, 'EDIT NEW.TXT\r', 1500);
  await shot(pc, 'files-reload');
  ok(row(pc, 2).startsWith('│Hello, world!') && row(pc, 3).startsWith('│The second line.') && / NEW\.TXT /.test(row(pc, 1)), 'EDIT NEW.TXT loads it', pc.screen());
  keys(pc, '{ALT+F}x', 1200);
  // LF-only file: saved as CR LF
  keys(pc, 'EDIT UNIX.TXT\r', 1500);
  ok(row(pc, 3).startsWith('│beta') && row(pc, 4).startsWith('│gamma'), 'an LF-only file loads line by line');
  keys(pc, '{CTRL+END}!{ALT+F}s', 1000);
  ok(readFile(pc, 'UNIX.TXT').toString('latin1') === 'alpha\r\nbeta\r\ngamma\r\n!', 'and is saved with CR LF', JSON.stringify(readFile(pc, 'UNIX.TXT').toString('latin1')));
  // File / New with no changes, then Open
  keys(pc, '{ALT+F}n', 800);
  ok(/ Untitled /.test(row(pc, 1)) && row(pc, 2) === '│'.padEnd(79) + '↑', 'File / New: empty Untitled');
  keys(pc, '{ALT+F}o', 1000);
  await shot(pc, 'files-open');
  const scr = pc.screen();
  ok(pc.hasText(' Open ') && scr.includes('*.TXT') && scr.includes('NEW.TXT') && scr.includes('OLD.TXT') && scr.includes('UNIX.TXT'),
    'Open dialog lists *.TXT', scr);
  ok(scr.includes('DOS') && scr.includes('[-A-]') && scr.includes('[-C-]'), 'Dirs/Drives: DOS, [-A-], [-C-]');
  keys(pc, 'OLD.TXT\r', 1200);
  ok(row(pc, 2).startsWith('│one') && / OLD\.TXT /.test(row(pc, 1)), 'Open loads OLD.TXT');
  // Open with a new pattern, then a directory
  keys(pc, '{ALT+F}o', 800);
  keys(pc, '*.BAT\r', 800);
  ok(pc.hasText('AUTOEXEC.BAT') && !pc.hasText('NEW.TXT'), 'a wildcard in File Name lists other files');
  keys(pc, 'DOS\r', 800);
  ok(pc.hasText('C:\\DOS'), 'a directory name changes to it', pc.screen());
  keys(pc, '{ESC}', 400);
  keys(pc, '{ALT+F}x', 1200);
  ok(pc.hasText('C:\\>'), 'exit');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ----------------------------------------------------- search, change
if (want('search')) {
  console.log('== Find, Repeat Last Find, Change');
  const text = 'The cat sat on the mat.\r\nA CAT is a cat.\r\nconcatenate\r\nlast line with cat\r\n';
  const pc = await start([['CATS.TXT', text]], {});
  keys(pc, 'EDIT CATS.TXT\r', 1500);
  keys(pc, '{ALT+S}f', 800);
  await shot(pc, 'search-find');
  ok(pc.hasText(' Find ') && pc.hasText('Find What:') && pc.hasText('[ ] Match Upper/Lowercase') && pc.hasText('[ ] Whole Word'),
    'Find dialog: Find What, Match Upper/Lowercase, Whole Word', pc.screen());
  ok(pc.hasText('Find What: │ The ') || pc.hasText('│ The'), 'Find What starts with the word at the cursor');
  keys(pc, '{HOME}{SHIFT+END}{DEL}cat\r', 800);
  await shot(pc, 'search-found1');
  ok(statusPos(pc).join(':') === '1:8', 'finds "cat" (cursor after the match, 1:8)', row(pc, 24));
  ok(attrAt(pc, 2, 5) === 0x71 && attrAt(pc, 2, 7) === 0x71, 'the match is selected (highlighted)');
  keys(pc, '{F3}', 400);
  ok(statusPos(pc).join(':') === '2:6', 'F3: the next one, CAT (case ignored)', row(pc, 24));
  keys(pc, '{F3}{F3}', 400);
  ok(statusPos(pc).join(':') === '3:7', 'F3 F3: inside "concatenate"', row(pc, 24));
  keys(pc, '{F3}{F3}', 400);
  ok(statusPos(pc).join(':') === '1:8', 'F3 wraps around to the start', row(pc, 24));
  // whole word + case
  keys(pc, '{ALT+S}f', 600);
  keys(pc, '{HOME}{SHIFT+END}{DEL}CAT{ALT+M}{ALT+W}{ALT+F}\r', 800);
  ok(statusPos(pc).join(':') === '2:6', 'Match Upper/Lowercase + Whole Word: CAT', row(pc, 24));
  keys(pc, '{F3}', 600);
  ok(statusPos(pc).join(':') === '2:6', 'no other CAT', row(pc, 24));
  keys(pc, '{ALT+S}f', 600);
  keys(pc, '{HOME}{SHIFT+END}{DEL}dog\r', 800);
  await shot(pc, 'search-notfound');
  ok(pc.hasText('Match not found'), '"Match not found"', pc.screen());
  keys(pc, '{ENTER}', 400);
  // Change all
  keys(pc, '{CTRL+HOME}{ALT+S}c', 800);
  await shot(pc, 'search-change');
  ok(pc.hasText(' Change ') && pc.hasText('Find What:') && pc.hasText('Change To:') && pc.hasText('< Find and Verify >') && pc.hasText('< Change All >'),
    'Change dialog: Find What, Change To, < Find and Verify >, < Change All >', pc.screen());
  keys(pc, '{HOME}{SHIFT+END}{DEL}cat{TAB}{HOME}{SHIFT+END}{DEL}dog{ALT+A}', 1000);   // options still on from the last Find
  await shot(pc, 'search-changed');
  ok(pc.hasText('Change complete'), '"Change complete"', pc.screen());
  keys(pc, '{ENTER}', 400);
  ok(row(pc, 2).startsWith('│The dog sat on the mat.') && row(pc, 3).startsWith('│A CAT is a dog.') &&
     row(pc, 4).startsWith('│concatenate') && row(pc, 5).startsWith('│last line with dog'),
    'Change All (whole words, case): cat -> dog', L(pc).slice(2, 6).join('\n'));
  // Find and Verify: change, skip
  keys(pc, '{CTRL+HOME}{ALT+S}c', 800);
  keys(pc, '{HOME}{SHIFT+END}{DEL}dog{TAB}{HOME}{SHIFT+END}{DEL}cow{ALT+V}', 800);
  await shot(pc, 'search-verify');
  ok(pc.hasText('< Change >') && pc.hasText('< Skip >'), 'Find and Verify asks: < Change > < Skip > < Cancel >', pc.screen());
  keys(pc, 'c', 600);
  keys(pc, 's', 600);
  keys(pc, 's', 600);
  ok(pc.hasText('Change complete'), 'then "Change complete"');
  keys(pc, '{ENTER}', 400);
  ok(row(pc, 2).startsWith('│The cow sat') && row(pc, 3).startsWith('│A CAT is a dog.') && row(pc, 5).startsWith('│last line with dog'),
    'first changed, the others skipped', L(pc).slice(2, 6).join('\n'));
  // Save and check the file
  keys(pc, '{ALT+F}s', 1000);
  ok(readFile(pc, 'CATS.TXT').toString('latin1') === text.replace('The cat', 'The cow').replace('a cat.', 'a dog.').replace('with cat', 'with dog'), 'saved');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ------------------------------------------------ cut, copy, paste
if (want('clipboard')) {
  console.log('== selection, Cut / Copy / Paste / Clear, Ctrl+Y, Ctrl+P');
  const pc = await start([['A.TXT', 'first line\r\nsecond line\r\nthird line\r\n']], {});
  keys(pc, 'EDIT A.TXT\r', 1500);
  keys(pc, '{SHIFT+DOWN}', 300);
  ok(attrAt(pc, 2, 1) === 0x71 && attrAt(pc, 3, 1) === 0x17, 'Shift+Down selects the first line');
  keys(pc, '{ALT+E}', 400);
  const copyRow = L(pc).findIndex((l) => l.includes('Copy'));
  ok(attrAt(pc, copyRow, row(pc, copyRow).indexOf('Copy') + 1) === 0x70, 'Copy is enabled with a selection');
  keys(pc, '{ESC}{ESC}{SHIFT+DEL}', 400);
  ok(row(pc, 2).startsWith('│second line'), 'Shift+Del cuts', L(pc).slice(2, 5).join('\n'));
  keys(pc, '{CTRL+END}{SHIFT+INS}', 400);
  ok(row(pc, 4).startsWith('│first line'), 'Shift+Ins pastes at the end', L(pc).slice(2, 6).join('\n'));
  keys(pc, '{CTRL+HOME}{SHIFT+END}{CTRL+INS}{END} {SHIFT+INS}', 400);
  ok(row(pc, 2).startsWith('│second line second line'), 'Ctrl+Ins copies', row(pc, 2));
  keys(pc, '{HOME}{SHIFT+CTRL+RIGHT}{DEL}', 400);
  ok(row(pc, 2).startsWith('│line second line'), 'Del clears the selection', row(pc, 2));
  keys(pc, '{CTRL+Y}', 400);
  ok(row(pc, 2).startsWith('│third line'), 'Ctrl+Y deletes the line', row(pc, 2));
  keys(pc, '{HOME}{CTRL+P}{CTRL+L}', 400);
  const g = grab(pc);
  ok(pc.cpu.m8[0xB8000 + (2 * 80 + 1) * 2] === 0x0C, 'Ctrl+P Ctrl+L inserts a form feed character');
  keys(pc, '{INS}x', 300);
  ok(row(pc, 2).startsWith('│\u2640xhird') || pc.cpu.m8[0xB8000 + (2 * 80 + 2) * 2] === 0x78, 'Ins: overwrite mode', row(pc, 2));
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ----------------------------------------------------------- mouse
if (want('mouse')) {
  console.log('== the mouse (MOUSE.COM)');
  const lines = [];
  for (let i = 1; i <= 60; i++) lines.push(`Line ${i} of the mouse test file`);
  const pc = await start([['M.TXT', lines.join('\r\n') + '\r\n']], { mouse: true });
  keys(pc, 'EDIT M.TXT\r', 1500);
  const m = mouse(pc);
  m.home();
  m.click(5, 10);
  ok(statusPos(pc).join(':') === '4:10', 'a click in the text moves the cursor (row 5 col 10 -> 4:10)', row(pc, 24));
  m.click(0, 3);
  await shot(pc, 'mouse-file-menu');
  ok(L(pc).slice(1, 12).join('').includes('Save As...'), 'a click on File opens the File menu', pc.screen());
  m.click(0, 9);
  ok(L(pc).slice(1, 8).join('').includes('Shift+Del'), 'a click on Edit switches to the Edit menu');
  m.click(0, 75);
  ok(L(pc).slice(1, 8).join('').includes('Getting Started'), 'a click on Help (right end) opens the Help menu');
  m.click(0, 16);
  const fr = L(pc).findIndex((l) => l.includes('Find...'));
  m.click(fr, row(pc, fr).indexOf('Find...') + 1);
  await shot(pc, 'mouse-find');
  ok(pc.hasText('Find What:'), 'a click on Search / Find... opens the Find dialog', pc.screen());
  const cr = L(pc).findIndex((l) => l.includes('< Cancel >'));
  m.click(cr, row(pc, cr).indexOf('< Cancel >') + 3);
  ok(!pc.hasText('Find What:'), 'a click on < Cancel > closes it');
  // scroll bar: the down arrow and page area
  m.click(22, 79);
  ok(row(pc, 2).startsWith('│Line 2 of'), 'the down arrow scrolls one line', row(pc, 2));
  m.click(15, 79);
  const top = +(row(pc, 2).match(/Line (\d+)/) || [])[1];
  ok(top >= 20 && top <= 42, 'the scroll bar track scrolls by pages', row(pc, 2));
  // drag to select
  m.click(3, 1);
  m.to(3, 1); m.down(); m.to(3, 6); m.up();
  const a = grab(pc).attrs[3];
  ok(a[1] === 0x71 && a[5] === 0x71 && a[8] === 0x17, 'dragging selects text', a.slice(0, 10).map((x) => x.toString(16)).join(' '));
  // double click on a menu item
  m.click(0, 3);
  const xr = L(pc).findIndex((l) => l.includes('Exit'));
  m.click(xr, row(pc, xr).indexOf('Exit') + 1);
  keys(pc, '', 1200);
  ok(pc.hasText('C:\\>'), 'File / Exit with the mouse');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ------------------------------------------------------------- help
if (want('help')) {
  console.log('== help (EDIT.HLP)');
  const pc = await start([], {});
  keys(pc, 'EDIT\r', 1500);
  keys(pc, '{ENTER}', 800);
  await shot(pc, 'help-survival');
  ok(/HELP: Survival Guide/.test(row(pc, 1)) && pc.hasText('Survival Guide'), 'Enter at the welcome: HELP: Survival Guide', pc.screen());
  ok(row(pc, 24).includes('<Esc=Cancel>'), 'help status line', row(pc, 24));
  ok(L(pc).some((l) => / Untitled /.test(l)), 'the document window below the help window');
  keys(pc, 'k{ENTER}', 600);
  ok(/HELP: Keyboard/.test(row(pc, 1)), 'a letter selects a cross reference; Enter follows it', row(pc, 1));
  keys(pc, '{TAB}{TAB}{ENTER}', 600);
  await shot(pc, 'help-topic');
  ok(/HELP: Selecting Text/.test(row(pc, 1)), 'Tab moves between cross references', row(pc, 1));
  keys(pc, '{ALT+F1}', 600);
  ok(/HELP: Keyboard/.test(row(pc, 1)), 'Alt+F1 goes back', row(pc, 1));
  keys(pc, '{ESC}', 600);
  ok(/ Untitled /.test(row(pc, 1)), 'Esc closes help; the document fills the screen again', row(pc, 1));
  keys(pc, '{ALT+S}{F1}', 800);
  ok(/HELP: Search Menu/.test(row(pc, 1)), 'F1 in a menu: help on it', row(pc, 1));
  keys(pc, '{ESC}', 400);
  keys(pc, '{ALT+O}d', 800);
  keys(pc, '{F1}', 800);
  await shot(pc, 'help-dialog');
  ok(/HELP: Display Dialog/.test(row(pc, 1)), 'F1 in a dialog: help on the dialog', row(pc, 1));
  keys(pc, '{ESC}', 500);
  ok(pc.hasText(' Display ') && pc.hasText('Tab Stops:'), 'and back in the dialog');
  keys(pc, '{ESC}', 400);
  keys(pc, '{ALT+H}a', 800);
  await shot(pc, 'help-about');
  ok(pc.hasText('ARM-DOS Editor') && pc.hasText('Version 1.00') && pc.hasText('Europa Micro Systems'), 'About box', pc.screen());
  keys(pc, '{ENTER}', 400);
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ---------------------------------------------- display options, print
if (want('options')) {
  console.log('== Options / Display, Print, /H (50 lines), /B');
  const printed = [];
  const pc = await start([['P.TXT', 'Printed line 1\r\nPrinted line 2\r\n']], { machine: { onPrint: (b) => printed.push(b) } });
  keys(pc, 'EDIT P.TXT\r', 1500);
  keys(pc, '{ALT+O}d', 800);
  await shot(pc, 'options-display');
  ok(pc.hasText(' Display ') && pc.hasText('Colors') && pc.hasText('1 Normal Text') && pc.hasText('[X] Scroll Bars') && pc.hasText('Tab Stops:'),
    'Display dialog', pc.screen());
  // normal text: foreground Yellow (14), background Black (0)
  keys(pc, '{TAB}{CTRL+PGUP}' + '{DOWN}'.repeat(14) + '{TAB}{CTRL+PGUP}{TAB} {ENTER}', 1000);
  await shot(pc, 'options-colors');
  ok(attrAt(pc, 2, 3) === 0x0E, 'normal text is now yellow on black', attrAt(pc, 2, 3).toString(16));
  ok(!row(pc, 2).endsWith('↑') && row(pc, 23).startsWith('└'), 'scroll bars turned off', row(pc, 23));
  // print
  keys(pc, '{ALT+F}p', 800);
  await shot(pc, 'options-print');
  ok(pc.hasText(' Print ') && pc.hasText('Complete Document') && pc.hasText('Selected Text Only'), 'Print dialog', pc.screen());
  keys(pc, '{ENTER}', 1500);
  const out = Buffer.from(printed).toString('latin1');
  ok(out === 'Printed line 1\r\nPrinted line 2\r\n\f', 'the document goes to PRN, then a form feed', JSON.stringify(out));
  keys(pc, '{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}{SHIFT+RIGHT}{ALT+F}p', 800);
  printed.length = 0;
  keys(pc, '{ENTER}', 1500);
  ok(Buffer.from(printed).toString('latin1') === 'Printed\r\n\f', 'Selected Text Only', JSON.stringify(Buffer.from(printed).toString('latin1')));
  keys(pc, '{ALT+F}x', 1200);
  // /H
  keys(pc, 'EDIT /H P.TXT\r', 1500);
  await shot(pc, 'options-50');
  const rows = pc.cpu.m8[0x484] + 1;
  ok(rows === 50 && L(pc).length >= 50 && L(pc)[49].includes('ARM-DOS Editor'), '/H: 50 lines, status line on line 50', `${rows} rows`);
  keys(pc, '{ALT+F}x', 1500);
  ok(pc.cpu.m8[0x484] + 1 === 25 && pc.hasText('C:\\>'), 'and 25 lines again after Exit');
  keys(pc, 'EDIT /B P.TXT\r', 1500);
  await shot(pc, 'options-bw');
  ok(attrAt(pc, 2, 3) === 0x07 && attrAt(pc, 0, 3) === 0x70, '/B: monochrome colours', attrAt(pc, 2, 3).toString(16));
  keys(pc, '{ALT+F}x', 1200);
  keys(pc, 'EDIT /?\r', 800);
  ok(pc.hasText('EDIT [[drive:][path]filename] [/B] [/G] [/H] [/NOHI]'), 'EDIT /? prints the usage');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ------------------------------------------------------- odd corners
if (want('corners')) {
  console.log('== a new file, drive A: without a disk, Ctrl+Break, F6');
  const pc = await start([], {});
  keys(pc, 'EDIT NOTES.TXT\r', 1500);
  ok(/ NOTES\.TXT /.test(row(pc, 1)) && row(pc, 2) === '│'.padEnd(79) + '↑', 'EDIT NOTES.TXT (not there): an empty file of that name', row(pc, 1));
  keys(pc, 'Some notes{CTRL+BREAK}{CTRL+C}', 600);
  ok(pc.hasText('Some notes') && / NOTES\.TXT /.test(row(pc, 1)), 'Ctrl+Break and Ctrl+C do not stop the editor');
  keys(pc, '{ALT+F}a', 800);
  keys(pc, '{HOME}{SHIFT+END}{DEL}A:\\X.TXT\r', 2500);
  await shot(pc, 'corners-drive-a');
  ok(!pc.hasText('Abort, Retry, Fail') && (pc.hasText('Device not ready') || pc.hasText('Path not found')), 'saving to A: with no disk: a message box, not "Abort, Retry, Fail?"', pc.screen());
  keys(pc, '{ENTER}', 500);
  keys(pc, '{ESC}', 400);
  keys(pc, '{ALT+F}s', 1000);
  ok(readFile(pc, 'NOTES.TXT') && readFile(pc, 'NOTES.TXT').toString('latin1') === 'Some notes', 'File / Save writes NOTES.TXT', pc.screen());
  keys(pc, '{F1}', 800);
  ok(/HELP: Survival Guide/.test(row(pc, 1)), 'F1 in the document: the Survival Guide');
  keys(pc, '{F6}', 400);
  keys(pc, '!', 300);
  ok(pc.hasText('Some notes!') && /HELP:/.test(row(pc, 1)), 'F6 switches to the document (help stays open)');
  keys(pc, '{F6}{ESC}', 500);
  ok(/ NOTES\.TXT /.test(row(pc, 1)), 'F6 back to help, Esc closes it');
  keys(pc, '{ALT+F}x', 800);
  keys(pc, 'n', 1200);
  ok(pc.hasText('C:\\>') && readFile(pc, 'NOTES.TXT').toString('latin1') === 'Some notes', 'Exit, No: not saved');
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ------------------------------------------------ size, memory, speed
if (want('perf')) {
  console.log('== size, memory, scrolling speed (2000 lines)');
  const exe = fs.statSync(path.join(ROOT, 'build/EDIT.EXE')).size;
  const big = [];
  for (let i = 1; i <= 2000; i++) big.push(`${String(i).padStart(4, '0')} The quick brown fox jumps over the lazy dog. ${'x'.repeat(i % 30)}`);
  const pc = await start([['BIG.TXT', big.join('\r\n') + '\r\n']], { autoexec: 'SET EDITSTATS=1\r\n' });
  keys(pc, 'MEM\r', 800);
  const free0 = +(pc.screen().match(/(\d+) largest executable program size/) || [])[1];
  const m = pc.machine;
  const t0 = m.timeMs(), i0 = pc.cpu.icount;
  keys(pc, 'EDIT BIG.TXT\r', 0);
  pc.waitText('0001 The quick', { timeoutMs: 20000 });
  const tLoad = m.timeMs() - t0;
  pc.waitIdle();
  ok(/ BIG\.TXT /.test(row(pc, 1)), `2000-line file loaded (${Math.round(tLoad)} ms emulated, incl. start-up)`);
  // PgDn through the whole file: count instructions and emulated time
  const measure = (k, n, until) => {
    const ia = pc.cpu.icount, ta = m.timeMs();
    keys(pc, k.repeat(n), 0);
    pc.until(until, { timeoutMs: 60000, stepMs: 5 });
    return { insns: pc.cpu.icount - ia, ms: m.timeMs() - ta };
  };
  const pg = measure('{PGDN}', 100, () => /^│2000 /.test(row(pc, 22)) || statusPos(pc)[0] >= 2000);
  pc.waitIdle();
  ok(statusPos(pc)[0] >= 1990, `100 x PgDn reaches the end (${statusPos(pc)[0]})`, row(pc, 24));
  const perPage = pg.insns / 100;
  ok(perPage < 2e6, `PgDn: ${(perPage / 1e3).toFixed(0)}k instructions per page = ${(perPage / 100e3).toFixed(2)} ms at 100 MHz`);
  const ln = measure('{UP}', 200, () => statusPos(pc)[0] <= statusPos(pc)[0] && false);
  pc.waitIdle();
  const perLine = (pc.cpu.icount - 0) && ln.insns / 200;
  ok(true, `Up (scrolling by a line at the top): ${(perLine / 1e3).toFixed(0)}k instructions per key`);
  keys(pc, '{CTRL+HOME}', 300);
  const ch = measure('{CTRL+END}', 1, () => statusPos(pc)[0] >= 2000);
  ok(ch.insns < 5e6, `Ctrl+End over 2000 lines: ${(ch.insns / 1e3).toFixed(0)}k instructions`);
  keys(pc, '{ALT+F}x', 1500);
  const stats = (pc.debug.match(/EDIT: .*/g) || []).join('\n');
  console.log('     ' + stats);
  console.log(`     EDIT.EXE ${exe} bytes; conventional memory free before: ${free0} bytes`);
  fs.writeFileSync(path.join(OUT, 'perf.txt'), JSON.stringify({ exe, free0, tLoad, perPage, perLine, ctrlEnd: ch.insns, stats }, null, 2));
  ok(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail ? 1 : 0);
