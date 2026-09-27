#!/usr/bin/env node
// apps/qbasic/tests/run.mjs - QB.EXE ("ARM QuickBASIC") headless tests.
//
//   node apps/qbasic/tests/run.mjs [--only name,...]
//
// Boots ARM-DOS (COMMAND.COM, HIMEM.SYS, MOUSE.COM) with QB.EXE in C:\DOS and
// the sample programs in C:\QB, drives the environment with keys and
// the mouse, and checks the screen (text and colours), the output screen of
// programs run with F5, errors, the Immediate window, stepping and
// breakpoints, help, and the two sample games. Screenshots:
// build/qb-test/*.png (+ .txt).
import fs from 'node:fs';
import path from 'node:path';
import { start, keys, grab, shot, mouse, readFile, checker, ROOT } from '../../tvlib/tests/lib.mjs';

const OUT = path.join(ROOT, 'build', 'qb-test');
const args = process.argv.slice(2);
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
const want = (name) => !ONLY || ONLY.includes(name);
const T = checker();
const ok = T.ok;
const L = (pc) => pc.lines();
const row = (pc, r) => (L(pc)[r] || '').replace(/\s+$/, '');
const attrAt = (pc, r, c) => grab(pc).attrs[r][c];
const findRow = (pc, re) => L(pc).findIndex((l) => re.test(l));
const pos = (pc) => (row(pc, 24).match(/(\d{5}):(\d{3})/) || []).slice(1).map(Number);
const need = ['build/QB.EXE', 'build/COMMAND.COM', 'build/MOUSE.COM', 'build/HIMEM.SYS', 'build/MEM.EXE'];
for (const f of need) if (!fs.existsSync(path.join(ROOT, f))) { console.log(`missing ${f}`); process.exit(1); }
const SAMPLES = path.join(ROOT, 'apps/qbasic/samples');
const sampleFiles = fs.existsSync(SAMPLES) ? fs.readdirSync(SAMPLES).filter((f) => /\.BAS$/i.test(f)) : [];
const dos = (s) => s.replace(/\r?\n/g, '\r\n');
const boot = (files = [], opts = {}) => start(OUT, [
  ...sampleFiles.map((f) => [`QB\\${f.toUpperCase()}`, fs.readFileSync(path.join(SAMPLES, f))]),
  ...files], { mouse: true, exes: ['build/QB.EXE'], dirs: ['QB', ...(opts.dirs || [])], ...opts });
/* the program window's text lines (rows 2..) */
const textRows = (pc) => L(pc).slice(2, 21).map((l) => l.slice(1, 79).replace(/\s+$/, ''));

// ------------------------------------------------------------- the look
if (want('look')) {
  console.log('== start-up, the screen, the menus');
  const pc = await boot();
  keys(pc, 'CLS\rECHO BEFORE QB\r', 300);
  keys(pc, 'QB\r', 2500);
  await shot(pc, OUT, 'look-welcome');
  ok(pc.hasText('Welcome to ARM QuickBASIC') && pc.hasText('< Press Enter to see the Survival Guide >') &&
     pc.hasText('< Press ESC to clear this dialog box >'), 'welcome dialog', pc.screen());
  keys(pc, '{ESC}', 500);
  await shot(pc, OUT, 'look-empty');
  ok(row(pc, 0).startsWith('  File  Edit  View  Search  Run  Debug  Options  ') && row(pc, 0).indexOf('Help') === 74,
    'menu bar: File Edit View Search Run Debug Options ... Help', JSON.stringify(row(pc, 0)));
  ok(/^┌─+ Untitled ─+┐$/.test(row(pc, 1)), 'program window "Untitled"', row(pc, 1));
  ok(/^├─+ Immediate ─+┤$/.test(row(pc, 21)) && row(pc, 22).startsWith('│') && row(pc, 23).startsWith('│'),
    'the Immediate window below it (2 lines)', row(pc, 21));
  ok(/^│←.*→│$/.test(row(pc, 20)) && row(pc, 2).endsWith('↑'), 'scroll bars');
  ok(row(pc, 24).startsWith(' <Shift+F1=Help> <F6=Window> <F2=Subs> <F5=Run> <F8=Step>') && row(pc, 24).endsWith('│ 00001:001'),
    'status line with line:column', row(pc, 24));
  ok(attrAt(pc, 0, 2) === 0x7F && attrAt(pc, 0, 3) === 0x70, 'menu bar black on grey, access keys bright');
  ok(attrAt(pc, 5, 10) === 0x17, 'text grey on blue');
  ok(attrAt(pc, 24, 5) === 0x30, 'status line black on cyan');
  ok(!/MS-DOS|Microsoft|QBasic/.test(pc.screen()), 'no Microsoft branding');
  const menus = [
    ['{ALT+F}', ['New', 'Open...', 'Save', 'Save As...', 'Print...', 'Exit'], 'Removes currently loaded program from memory'],
    ['{RIGHT}', ['Cut', 'Copy', 'Paste', 'Clear', 'New SUB...', 'New FUNCTION...'], 'Deletes selected text and copies it to buffer'],
    ['{RIGHT}', ['SUBs...', 'Split', 'Output Screen'], 'Displays a loaded SUB'],
    ['{RIGHT}', ['Find...', 'Repeat Last Find', 'Change...'], 'Finds specified text'],
    ['{RIGHT}', ['Start', 'Restart', 'Continue'], 'Runs current program'],
    ['{RIGHT}', ['Step', 'Procedure Step', 'Trace On', 'Toggle Breakpoint', 'Clear All Breakpoints', 'Set Next Statement'], 'Executes next program statement'],
    ['{RIGHT}', ['Display...', 'Help Path...'], 'Changes display attributes'],
    ['{RIGHT}', ['Index', 'Contents', 'Topic:', 'Using Help', 'About...'], 'Displays help index'],
  ];
  const names = ['File', 'Edit', 'View', 'Search', 'Run', 'Debug', 'Options', 'Help'];
  for (let i = 0; i < menus.length; i++) {
    const [k, items, hint] = menus[i];
    keys(pc, k, 400);
    if (i === 0 || i === 4 || i === 5) await shot(pc, OUT, `look-menu-${names[i].toLowerCase()}`);
    const m = L(pc).slice(1, 14).join('\n');
    ok(items.every((s) => m.includes('│ ' + s) || m.includes(' ' + s + ' ')) && row(pc, 24).includes(hint),
      `${names[i]} menu: ${items.join(', ')}`, m + '\n' + row(pc, 24));
  }
  keys(pc, '{ESC}', 300);
  keys(pc, '{ALT+R}', 300);
  const rm = L(pc).slice(1, 6).join('\n');
  ok(/Start\s+Shift\+F5/.test(rm) && /Continue\s+F5/.test(rm), 'Run: Start Shift+F5, Continue F5', rm);
  keys(pc, '{ESC}{ALT+D}', 300);
  const dm = L(pc).slice(1, 10).join('\n');
  ok(/Step\s+F8/.test(dm) && /Procedure Step\s+F10/.test(dm) && /Toggle Breakpoint\s+F9/.test(dm), 'Debug: Step F8, Procedure Step F10, Toggle Breakpoint F9', dm);
  keys(pc, '{ESC}{ALT+V}', 300);
  const vm = L(pc).slice(1, 8).join('\n');
  ok(/SUBs\.\.\.\s+F2/.test(vm) && /Output Screen\s+F4/.test(vm), 'View: SUBs... F2, Output Screen F4', vm);
  keys(pc, '{ESC}{ALT+H}A', 600);
  await shot(pc, OUT, 'look-about');
  ok(pc.hasText('ARM QuickBASIC') && pc.hasText('Version 1.00') && pc.hasText('Bywater BASIC'), 'About', pc.screen());
  keys(pc, '{ESC}', 300);
  // Alt alone opens the menu bar
  pc.machine.keyDown('AltLeft'); pc.run(60); pc.machine.keyUp('AltLeft'); pc.run(300);
  ok(attrAt(pc, 0, 2) === 0x0F || attrAt(pc, 0, 3) === 0x07, 'Alt alone: the menu bar is active');
  keys(pc, '{ESC}', 300);
  keys(pc, '{ALT+F}X', 1500);
  ok(pc.hasText('ECHO BEFORE QB') && pc.hasText('C:\\>'), 'File / Exit: back to the DOS screen', pc.screen());
}

// ---------------------------------------------- typing, F5, output screen
if (want('run')) {
  console.log('== typing a program, keywords upper-cased, F5, the output screen, errors');
  const pc = await boot();
  keys(pc, 'QB\r', 2500);
  keys(pc, '{ESC}', 400);
  pc.type('cls\rfor i = 1 to 3\rprint "line"; i\rnext i\rtotal = i * 10\rprint "done"\r');
  pc.waitIdle(); pc.run(500);
  await shot(pc, OUT, 'run-typed');
  const tr = textRows(pc);
  ok(tr[0] === 'CLS' && tr[1] === 'FOR i = 1 TO 3' && tr[2] === 'PRINT "line"; i' && tr[3] === 'NEXT i' && tr[5] === 'PRINT "done"',
    'keywords upper-cased when the cursor leaves the line', tr.slice(0, 6).join('\n'));
  ok(pos(pc).join(':') === '7:1', 'status line: 00007:001', row(pc, 24));
  keys(pc, '{F5}', 1500);
  await shot(pc, OUT, 'run-output');
  ok(row(pc, 0) === 'line 1' && row(pc, 2) === 'line 3' && row(pc, 3) === 'done' && row(pc, 24).startsWith('Press any key to continue'),
    'F5: the output screen, "Press any key to continue" at the bottom', pc.screen());
  keys(pc, ' ', 800);
  ok(row(pc, 0).startsWith('  File  Edit'), 'a key: back to the program');
  // F4: the output screen again
  keys(pc, '{F4}', 600);
  ok(row(pc, 0) === 'line 1' && row(pc, 3) === 'done', 'F4 shows the output screen', pc.screen());
  keys(pc, ' ', 600);
  // the Immediate window: the program's variables
  keys(pc, '{F6}', 300);
  ok(pos(pc).join(':') === '1:1', 'F6: the Immediate window (its own line:column)', row(pc, 24));
  keys(pc, 'print total + 2\r', 1500);
  await shot(pc, OUT, 'run-immediate');
  ok(pc.hasText(' 42') && pc.hasText('Press any key to continue'), 'Immediate: PRINT total + 2 shows 42 on the output screen', pc.screen());
  keys(pc, ' ', 600);
  ok(row(pc, 22) === '│PRINT total + 2' + ' '.repeat(63) + '│' || row(pc, 22).startsWith('│PRINT total + 2'), 'the Immediate line upper-cased', row(pc, 22));
  keys(pc, 'x = 5\r', 1200);
  ok(row(pc, 0).startsWith('  File  Edit'), 'Immediate: X = 5 prints nothing, no output screen');
  // Save As
  keys(pc, '{F6}{ALT+F}A', 800);
  await shot(pc, OUT, 'run-saveas');
  ok(pc.hasText('Save As') && pc.hasText('File Name:'), 'Save As dialog', pc.screen());
  keys(pc, 'LOOP\r', 1000);
  const saved = (readFile(pc, 'LOOP.BAS') || Buffer.alloc(0)).toString('latin1');
  ok(saved.startsWith('CLS\r\nFOR i = 1 TO 3\r\nPRINT "line"; i\r\n'), 'LOOP.BAS saved (.BAS added, CR LF)', JSON.stringify(saved.slice(0, 60)));
  ok(/ LOOP\.BAS /.test(row(pc, 1)), 'title LOOP.BAS', row(pc, 1));
  // a run-time error
  keys(pc, '{CTRL+END}y = 1 / 0\rprint "after"\r', 400);
  keys(pc, '{F5}', 1500);
  keys(pc, ' ', 800);
  await shot(pc, OUT, 'run-error');
  ok(pc.hasText('Division by zero') && pc.hasText('< OK >') && pc.hasText('< Help >'), 'run-time error: a dialog "Division by zero"', pc.screen());
  keys(pc, '{ENTER}', 500);
  ok(pos(pc)[0] === 7, 'the cursor on the statement (line 7)', row(pc, 24));
  keys(pc, '{ESC}', 300);
  // a structure error
  keys(pc, '{CTRL+HOME}{END}\rdo\r', 300);
  keys(pc, '{SHIFT+F5}', 1200);
  await shot(pc, OUT, 'run-structure-error');
  ok(pc.hasText('DO without LOOP'), 'a structure error: "DO without LOOP"', pc.screen());
  keys(pc, '{ENTER}', 400);
  ok(pos(pc)[0] === 2, 'the cursor on the DO line', row(pc, 24));
  // Exit with changes
  keys(pc, '{ALT+F}X', 800);
  await shot(pc, OUT, 'run-exit-unsaved');
  ok(pc.hasText('Loaded file is not saved. Save it now?') && pc.hasText('< Yes >') && pc.hasText('< No >'), 'Exit: "Loaded file is not saved. Save it now?"', pc.screen());
  keys(pc, 'N', 1500);
  ok(pc.hasText('C:\\>') && !pc.hasText('Immediate'), 'No: back to DOS', pc.screen());
  // QB /RUN and File / Open
  keys(pc, 'CLS\rQB /RUN LOOP\r', 3000);
  ok(pc.hasText('line 3') && pc.hasText('Press any key to continue'), 'QB /RUN LOOP runs it at once', pc.screen());
  keys(pc, ' ', 800);
  ok(/ LOOP\.BAS /.test(row(pc, 1)), '... then shows it', row(pc, 1));
  keys(pc, '{ALT+F}O', 800);
  await shot(pc, OUT, 'run-open');
  ok(pc.hasText('File Name:') && pc.hasText('*.BAS') && pc.hasText('LOOP.BAS') && pc.hasText('Dirs/Drives'), 'File / Open lists *.BAS', pc.screen());
  keys(pc, '{ESC}', 400);
}

// ------------------------------------------- break, step, breakpoints
if (want('debug')) {
  console.log('== Ctrl+Break, F5 continue, F8 step, F9 breakpoints, F10, F2 SUBs');
  const P = dos(`DECLARE SUB Show (n)
CLS
FOR i = 1 TO 3
  Show i
NEXT i
cnt = 0
DO
  cnt = cnt + 1
LOOP UNTIL cnt > 5000000
PRINT "end"; cnt

SUB Show (n)
  PRINT "show"; n
END SUB
`);
  const pc = await boot([['D.BAS', P]]);
  keys(pc, 'QB D.BAS\r', 2500);
  keys(pc, '{F8}', 800);
  await shot(pc, OUT, 'debug-step1');
  ok(pos(pc)[0] === 2 && attrAt(pc, 3, 1) === 0x1F, 'F8: stopped at the first statement (CLS), highlighted', row(pc, 24));
  keys(pc, '{F8}', 800);
  ok(pos(pc)[0] === 3, 'F8: FOR', row(pc, 24));
  keys(pc, '{F8}', 800);
  ok(pos(pc)[0] === 4, 'F8: Show i', row(pc, 24));
  keys(pc, '{F8}', 800);
  ok(pos(pc)[0] === 12 || pos(pc)[0] === 13, 'F8: into the SUB', row(pc, 24));
  keys(pc, '{F8}{F8}{F8}', 1500);
  ok(pos(pc)[0] === 5 || pos(pc)[0] === 4, 'F8: out of the SUB again', row(pc, 24));
  keys(pc, '{F10}', 800);
  const at = pos(pc)[0];
  keys(pc, '{F10}{F10}', 1200);
  ok([3, 4, 5].includes(pos(pc)[0]), 'F10: steps over the SUB (stays in the main module)', row(pc, 24) + ' from ' + at);
  // breakpoint on PRINT "end"
  keys(pc, '{CTRL+HOME}' + '{DOWN}'.repeat(9) + '{F9}', 400);
  await shot(pc, OUT, 'debug-breakpoint');
  ok(attrAt(pc, 11, 3) === 0x47, 'F9: the breakpoint line is red', attrAt(pc, 11, 3).toString(16));
  // run it; Ctrl+Break in the long loop
  pc.type('{SHIFT+F5}'); pc.run(1500);
  pc.type('{CTRL+BREAK}'); pc.run(1500);
  await shot(pc, OUT, 'debug-break');
  ok(row(pc, 0).startsWith('  File') && [7, 8, 9].includes(pos(pc)[0]), 'Ctrl+Break: stopped in the loop, the statement highlighted', row(pc, 24));
  // the variable while stopped
  keys(pc, '{F6}print cnt > 100\r', 1500);
  ok(pc.hasText('-1'), 'Immediate while stopped: cnt > 100', pc.screen());
  keys(pc, ' ', 600);
  keys(pc, 'cnt = 4999990\r', 1200);
  keys(pc, '{F5}', 3000);
  await shot(pc, OUT, 'debug-at-breakpoint');
  ok(pos(pc)[0] === 10 && attrAt(pc, 11, 3) === 0x1F, 'F5 continues; stops at the breakpoint (line 10)', row(pc, 24));
  keys(pc, '{F5}', 1500);
  ok(pc.hasText('end 5000001') && pc.hasText('Press any key'), 'F5: the program ends: end 5000001 (the value set in the Immediate window)', pc.screen());
  keys(pc, ' ', 600);
  keys(pc, '{ALT+D}C', 400);
  ok(attrAt(pc, 11, 3) === 0x17, 'Clear All Breakpoints');
  keys(pc, '{F2}', 600);
  await shot(pc, OUT, 'debug-subs');
  ok(pc.hasText('SUBs') && pc.hasText('D.BAS') && pc.hasText('  Show'), 'F2: the SUBs dialog lists D.BAS and Show', pc.screen());
  keys(pc, '{DOWN}\r', 600);
  ok(pos(pc)[0] === 12, 'choosing Show goes to its SUB line', row(pc, 24));
  // New SUB
  keys(pc, '{ALT+E}S', 500);
  keys(pc, 'Beep2\r', 600);
  const tr = textRows(pc);
  ok(tr.includes('SUB Beep2') && tr.includes('END SUB'), 'Edit / New SUB appends SUB Beep2 ... END SUB', tr.join('\n'));
}

// ------------------------------------------------------------ help
if (want('help')) {
  console.log('== help');
  const pc = await boot();
  keys(pc, 'QB\r', 2500);
  keys(pc, '\r', 800);
  await shot(pc, OUT, 'help-survival');
  ok(pc.hasText('HELP: Survival Guide') && /^├|^┌/.test(row(pc, 12)), 'Enter at the welcome: the Survival Guide above the program window', pc.screen());
  keys(pc, '{ESC}', 500);
  ok(/^┌─+ Untitled/.test(row(pc, 1)), 'Esc closes help, the program window is back at the top', row(pc, 1));
  keys(pc, 'print', 300);
  keys(pc, '{F1}', 600);
  await shot(pc, OUT, 'help-print');
  ok(pc.hasText('HELP: PRINT') && pc.hasText('PRINT'), 'F1 on PRINT: its topic', pc.screen());
  keys(pc, '{SHIFT+F1}', 600);
  ok(pc.hasText('HELP: Using Help'), 'Shift+F1: Using Help', pc.screen());
  keys(pc, '{ALT+F1}', 600);
  ok(pc.hasText('HELP: PRINT'), 'Alt+F1: back', pc.screen());
  keys(pc, '{ALT+H}I', 600);
  await shot(pc, OUT, 'help-index');
  ok(pc.hasText('Index of help topics') && pc.hasText('ABS'), 'Help / Index', pc.screen());
  keys(pc, '{TAB}{TAB}\r', 600);
  ok(!pc.hasText('Index of help topics') && /HELP: /.test(pc.screen()), 'Tab, Enter: a cross reference', pc.screen());
  keys(pc, '{ESC}{ALT+R}', 400);
  keys(pc, '{F1}', 800);
  ok(pc.hasText('HELP: Run Menu'), 'F1 in a menu: the menu\'s topic', pc.screen());
  keys(pc, '{ESC}', 400);
}

// ------------------------------------------------------------ mouse
if (want('mouse')) {
  console.log('== the mouse');
  const P = dos('PRINT "hello from the mouse"\r\nPRINT 6 * 7\r\n');
  const pc = await boot([['M.BAS', P]]);
  keys(pc, 'QB M\r', 2500);
  const m = mouse(pc);
  m.home();
  m.click(0, row(pc, 0).indexOf('Run') + 1);
  await shot(pc, OUT, 'mouse-run-menu');
  ok(pc.hasText('Start') && pc.hasText('Continue'), 'a click on Run opens the menu', pc.screen());
  const st = findRow(pc, /│ Start/);
  m.click(st, L(pc)[st].indexOf('Start') + 1);
  pc.run(1000);
  ok(pc.hasText('hello from the mouse') && pc.hasText(' 42') && pc.hasText('Press any key'), 'a click on Start runs the program', pc.screen());
  m.click(12, 40);
  ok(row(pc, 0).startsWith('  File'), 'a click returns from the output screen');
  m.click(0, 75);
  ok(pc.hasText('Using Help') && pc.hasText('About...'), 'a click on Help (right end)', pc.screen());
  pc.type('{ESC}'); pc.run(300);
  m.click(3, 10);
  ok(pos(pc).join(':') === '2:10', 'a click in the text places the cursor', row(pc, 24));
  m.click(22, 5);
  ok(pos(pc).join(':') === '1:1' , 'a click in the Immediate window selects it', row(pc, 24));
}

// ------------------------------------------------------------ samples
/* the mode 13h frame buffer: how many pixels changed */
const vga = (pc) => Buffer.from(pc.cpu.m8.subarray(0xA0000, 0xA0000 + 64000));
const changed = (a, b) => { let n = 0; for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) n++; return n; };

if (want('bananas')) {
  console.log('== BANANAS.BAS');
  const pc = await boot();
  keys(pc, 'CD \\QB\rQB BANANAS\r', 3000);
  ok(/ BANANAS\.BAS /.test(row(pc, 1)) && textRows(pc).some((l) => l.includes('BANANAS.BAS - a banana-throwing duel')), 'QB BANANAS opens it', row(pc, 1));
  pc.type('{F5}'); pc.run(4000);
  await shot(pc, OUT, 'game-bananas-title');
  ok(pc.cpu.m8[0x449] === 0x13, 'F5: the title screen in mode 13h (see game-bananas-title.png)');
  pc.type('\r\r2\r\r'); pc.run(4000);
  await shot(pc, OUT, 'game-bananas-city');
  const city = vga(pc);
  pc.type('0\r25\r'); pc.run(5000);
  await shot(pc, OUT, 'game-bananas-hit-building');
  const after1 = vga(pc);
  ok(changed(city, after1) > 100, `a throw: the banana hits a building (${changed(city, after1)} pixels changed)`);
  pc.type('90\r10\r'); pc.run(9000);
  await shot(pc, OUT, 'game-bananas-point');
  const after2 = vga(pc);
  ok(changed(after1, after2) > 5000, `player 2 hits itself: a point and a new city (${changed(after1, after2)} pixels changed)`);
  pc.type('{ESC}'); pc.run(3000);
  await shot(pc, OUT, 'game-bananas-abandon');
  pc.type('N'); pc.run(3000);
  await shot(pc, OUT, 'game-bananas-end');
  ok(pc.cpu.m8[0x449] === 3 && pc.hasText('Thanks for playing BANANAS.') && pc.hasText('Press any key to continue'),
    'Esc, N: back in text mode, "Thanks for playing", "Press any key to continue"', pc.screen());
  keys(pc, ' ', 800);
  ok(row(pc, 0).startsWith('  File  Edit') && !pc.hasText('< OK >'), 'back in the environment, no error');
}

if (want('serpent')) {
  console.log('== SERPENT.BAS');
  const pc = await boot();
  keys(pc, 'CD \\QB\rQB SERPENT\r', 3000);
  ok(/ SERPENT\.BAS /.test(row(pc, 1)), 'QB SERPENT opens it', row(pc, 1));
  pc.type('{F5}'); pc.run(5000);
  await shot(pc, OUT, 'game-serpent-title');
  ok(pc.hasText('Speed, 1 (slow) to 9 (fast)?') && pc.hasText('Arrow keys steer'), 'F5: the title screen', pc.screen());
  pc.type('3'); pc.run(4000);
  await shot(pc, OUT, 'game-serpent-play');
  ok(/Level +1/.test(row(pc, 0)) && /Score +0/.test(row(pc, 0)), 'playing: the status line (Level 1, Score 0)', row(pc, 0));
  /* steer to the food: a greedy bot reading the half-block screen */
  const D = { 1: [0, -1, '{UP}'], 2: [0, 1, '{DOWN}'], 3: [-1, 0, '{LEFT}'], 4: [1, 0, '{RIGHT}'] };
  const opp = { 1: 2, 2: 1, 3: 4, 4: 3 };
  let dir = 0, last = null, eaten = 0, lastFood = 0;
  for (let t = 0; t < 20000 && eaten < 2; t += 40) {
    const g = grab(pc); const P = {}; let food = null, head = null;
    for (let r = 1; r < 24; r++) for (let c = 0; c < 80; c++) {
      const ch = g.text[r][c], a = g.attrs[r][c], fg = a & 15, bg = (a >> 4) & 7;
      let top, bot;
      if (ch === '▀') { top = fg; bot = bg; } else if (ch === '▄') { top = bg; bot = fg; }
      else if (ch === '█') { top = bot = fg; } else if (/[1-9]/.test(ch)) { food = { x: c + 1, r: r + 1, n: +ch }; top = bot = 0; }
      else { top = bot = bg; }
      const y1 = (r + 1) * 2 - 1;
      P[(c + 1) + ',' + y1] = top; P[(c + 1) + ',' + (y1 + 1)] = bot;
      if (top === 14) head = { x: c + 1, y: y1 }; if (bot === 14) head = { x: c + 1, y: y1 + 1 };
    }
    if (food && food.n !== lastFood) { if (lastFood) eaten++; lastFood = food.n; }
    if (head && food) {
      if (last && (head.x !== last.x || head.y !== last.y)) {
        const dx = head.x - last.x, dy = head.y - last.y;
        dir = dx > 0 ? 4 : dx < 0 ? 3 : dy > 0 ? 2 : 1;
      }
      last = head;
      const fy1 = food.r * 2 - 1;
      const free = (d) => { const [ddx, ddy] = D[d]; let x = head.x, y = head.y;
        for (let k = 1; k <= 2; k++) { x += ddx; y += ddy; if (k === 1 && x === food.x && ((y + 1) >> 1) === food.r) return 2; if (P[x + ',' + y]) return k > 1 ? 0.5 : 0; } return 1; };
      const want = [];
      if (food.x > head.x) want.push(4); if (food.x < head.x) want.push(3);
      if (fy1 > head.y) want.push(2); if (fy1 + 1 < head.y) want.push(1);
      want.push(1, 2, 3, 4);
      let best = null, bestScore = -1;
      for (const d of want) { if (dir && d === opp[dir]) continue; const f = free(d); if (f > bestScore) { best = d; bestScore = f; } }
      if (best && best !== dir) { pc.type(D[best][2]); dir = best; }
    }
    pc.run(40);
  }
  await shot(pc, OUT, 'game-serpent-ate');
  const sc = +((row(pc, 0).match(/Score +(\d+)/) || [])[1] || 0);
  ok(eaten >= 2 && sc >= 30, `the serpent eats: ${eaten} numbers, score ${sc}`, row(pc, 0));
  pc.type('{ESC}'); pc.run(3000);
  await shot(pc, OUT, 'game-serpent-over');
  ok(pc.hasText('Play again? (Y/N)'), 'Esc: "Play again? (Y/N)"', pc.screen());
  pc.type('N'); pc.run(2000);
  ok(pc.hasText('Thanks for playing SERPENT.') && pc.hasText('Press any key to continue'), 'N: the program ends', pc.screen());
  keys(pc, ' ', 800);
  ok(row(pc, 0).startsWith('  File  Edit') && !pc.hasText('< OK >'), 'back in the environment, no error');
}

// -------------------------------- break / continue / restart the samples
if (want('restart')) {
  console.log('== breaks in the samples, then F5 (continue) and Shift+F5 (restart): no stale labels');
  /* a label after an aborted run: the old program's label table made the
   * new "Title:" line a call ("Syntax error" at line 75 of BANANAS.BAS) */
  const LBL = dos('CLS\nn = 0\nTitle:\nn = n + 1\nPRINT "n"; n\nIF n < 3 THEN GOTO Title\nPRINT "done"\n');
  const pc = await boot([['L.BAS', LBL]]);
  keys(pc, 'QB L\r', 2500);
  keys(pc, '{F8}{F8}{F8}', 1500);
  keys(pc, '{SHIFT+F5}', 1500);
  ok(pc.hasText('n 3') && pc.hasText('done'), 'stopped at a label, Shift+F5 restarts and runs to the end', pc.screen());
  keys(pc, ' ', 600);
  for (let i = 0; i < 2; i++) {
    keys(pc, '{SHIFT+F5}', 1500);
    ok(pc.hasText('done') && !pc.hasText('Syntax error'), `run ${i + 3}: the label still works`, pc.screen());
    keys(pc, ' ', 600);
  }
  const cases = [
    ['BANANAS', 1500, '{F5}'], ['BANANAS', 1500, '{SHIFT+F5}'], ['BANANAS', 6000, '{SHIFT+F5}'],
    ['SERPENT', 2000, '{SHIFT+F5}'], ['SERPENT', 7000, '{F5}'], ['SERPENT', 7000, '{SHIFT+F5}'],
  ];
  for (const [game, ms, key] of cases) {
    const p2 = await boot();
    keys(p2, `CD \\QB\rQB ${game}\r`, 3000);
    p2.type('{F5}'); p2.run(ms);
    if (game === 'BANANAS' && ms > 5000) { p2.type('\r\r2\r\r'); p2.run(4000); }
    if (game === 'SERPENT' && ms > 5000) { p2.type('3'); p2.run(2000); }
    p2.type('{CTRL+BREAK}'); p2.run(1500);
    if (!/  File  Edit/.test(row(p2, 0))) { p2.type('\r'); p2.run(1500); }   // a pending break stops after the input line
    const stopped = pos(p2)[0];
    p2.type(key); p2.run(4000);
    /* and once more from the start */
    p2.type('{CTRL+BREAK}'); p2.run(1500);
    if (!/  File  Edit/.test(row(p2, 0))) { p2.type('\r'); p2.run(1500); }
    p2.type('{SHIFT+F5}'); p2.run(4000);
    await shot(p2, OUT, `restart-${game.toLowerCase()}-${ms}-${key.length}`);
    const s = p2.screen();
    ok(!/Syntax error|without|Illegal function call|Undefined/.test(s) && !/  File  Edit/.test(row(p2, 0)),
      `${game}: break after ${ms} ms (at line ${stopped}), ${key}, break, Shift+F5: running, no error`, s);
  }
}

console.log(`\n${T.pass} passed, ${T.fail} failed`);
process.exit(T.fail ? 1 : 0);
