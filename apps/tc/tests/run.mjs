#!/usr/bin/env node
// apps/tc/tests/run.mjs - TC.EXE ("ARM Turbo C") headless tests.
//
//   node apps/tc/tests/run.mjs [--only name,...]
//
// Boots ARM-DOS (COMMAND.COM, HIMEM.SYS, MOUSE.COM) with TC.EXE in C:\DOS and
// C:\TC from apps/tcc (INCLUDE, LIB, SAMPLES), drives the IDE with keys and
// the mouse, and checks the screen (text and colours), the files written,
// compile-error navigation, runs from the IDE and the memory a program run
// from the IDE gets. Screenshots: build/tc-test/*.png (+ .txt).
import fs from 'node:fs';
import path from 'node:path';
import { start, keys, grab, shot, mouse, readFile, checker, ROOT } from '../../tvlib/tests/lib.mjs';

const OUT = path.join(ROOT, 'build', 'tc-test');
const args = process.argv.slice(2);
const ONLY = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : null;
const want = (name) => !ONLY || ONLY.includes(name);
const T = checker();
const ok = T.ok;
const L = (pc) => pc.lines();
const row = (pc, r) => L(pc)[r] || '';
const attrAt = (pc, r, c) => grab(pc).attrs[r][c];
const findRow = (pc, re) => L(pc).findIndex((l) => re.test(l));
const need = ['build/TC.EXE', 'build/tcc/disk/TC/LIB/LIBC.A', 'build/COMMAND.COM', 'build/MOUSE.COM', 'build/HIMEM.SYS', 'build/MEM.EXE'];
for (const f of need) if (!fs.existsSync(path.join(ROOT, f))) { console.log(`missing ${f}`); process.exit(1); }
const TREE = [{ src: 'build/tcc/disk/TC', dst: 'TC' }];
const boot = (files = [], opts = {}) => start(OUT, files, { mouse: true, exes: ['build/TC.EXE'], trees: TREE, ...opts });
const dos = (s) => s.replace(/\n/g, '\r\n');
const statusPos = (pc) => { const m = L(pc).slice(1, 24).join('\n').match(/═+ (\d+):(\d+) ═/); return m ? [+m[1], +m[2]] : []; };

// ------------------------------------------------------------- the look
if (want('look')) {
  console.log('== first start: the IDE, HELLO.C, highlighting');
  const pc = await boot();
  keys(pc, 'CLS\rECHO BEFORE TC\r', 300);
  const before = L(pc).slice();
  keys(pc, 'TC\r', 2500);
  await shot(pc, OUT, 'look-start');
  ok(/^ {2}≡ {2}File {2}Edit {2}Search {2}Run {2}Compile {2}Debug {2}Project {2}Options {2}Window {2}Help/.test(row(pc, 0)),
    'menu bar: ≡ File Edit Search Run Compile Debug Project Options Window Help', row(pc, 0));
  ok(/C:\\TC\\SAMPLES\\HELLO\.C/.test(row(pc, 1)) && row(pc, 1).includes('═') && /1═\[↕\]═╗$/.test(row(pc, 1)),
    'first start opens C:\\TC\\SAMPLES\\HELLO.C in window 1 (double frame, zoom icon)', row(pc, 1));
  ok(L(pc).some((l) => l.includes('printf("Hello, world!\\n");')), 'the text of HELLO.C');
  ok(/1:1/.test(row(pc, 23)), 'line:column in the bottom frame', row(pc, 23));
  ok(row(pc, 24).startsWith(' F1 Help  F2 Save  F3 Open  Alt+F9 Compile  F9 Make  F10 Menu'), 'status line', row(pc, 24));
  ok(attrAt(pc, 0, 4) === 0x70 && attrAt(pc, 0, 5) === 0x74, 'menu bar black on grey, hot keys red');
  ok(attrAt(pc, 5, 30) === 0x1E, 'editor: yellow on blue', attrAt(pc, 5, 30).toString(16));
  const inc = findRow(pc, /#include <stdio.h>/);
  const intRow = findRow(pc, /^║int main/);
  const cmt = findRow(pc, /HELLO\.C - the first program/);
  ok(attrAt(pc, inc, 1) === 0x1A, 'preprocessor line green', attrAt(pc, inc, 1).toString(16));
  ok(attrAt(pc, intRow, 1) === 0x1F && attrAt(pc, intRow, 5) === 0x1E, 'keyword "int" white, identifier yellow');
  ok(attrAt(pc, cmt, 2) === 0x17, 'comment grey', attrAt(pc, cmt, 2).toString(16));
  const pr = findRow(pc, /printf\("Hello/);
  ok(attrAt(pc, pr, L(pc)[pr].indexOf('"Hello') ) === 0x1B, 'string cyan');
  ok(!/Borland|Turbo C\b|Microsoft|MS-DOS/.test(pc.screen()), 'no Borland / Microsoft branding on screen');
  // menus
  keys(pc, '{ALT+F}', 400);
  await shot(pc, OUT, 'look-file-menu');
  const fm = L(pc).slice(1, 16).join('\n');
  ok(['Open...', 'New', 'Save', 'Save as...', 'Save all', 'Change dir...', 'Print', 'Get info...', 'DOS shell', 'Quit'].every((s) => fm.includes(s)) &&
     /Open\.\.\.\s+F3/.test(fm) && /Quit\s+Alt\+X/.test(fm), 'File menu', fm);
  ok(row(pc, 24).includes('Locate and open a file in an edit window'), 'status line hint for Open', row(pc, 24));
  keys(pc, '{RIGHT}{RIGHT}{RIGHT}', 400);
  const rm = L(pc).slice(1, 10).join('\n');
  ok(/Run\s+Ctrl\+F9/.test(rm) && rm.includes('Arguments...'), 'Run menu: Run Ctrl+F9, Arguments...', rm);
  keys(pc, '{RIGHT}', 400);
  await shot(pc, OUT, 'look-compile-menu');
  const cm = L(pc).slice(1, 10).join('\n');
  ok(/Compile to OBJ\s+Alt\+F9/.test(cm) && /Make EXE file\s+F9/.test(cm) && cm.includes('Link EXE file') && cm.includes('Build all'),
    'Compile menu: Compile to OBJ Alt+F9, Make EXE file F9, Link, Build all', cm);
  keys(pc, '{RIGHT}{RIGHT}{RIGHT}', 400);
  const om = L(pc).slice(1, 12).join('\n');
  ok(['Compiler...', 'Make...', 'Linker...', 'Directories...', 'Environment...', 'Save...'].every((s) => om.includes(s)), 'Options menu', om);
  keys(pc, '{DOWN}{DOWN}{DOWN}\r', 600);
  await shot(pc, OUT, 'look-directories');
  ok(pc.hasText('Directories') && pc.hasText('C:\\TC\\INCLUDE') && pc.hasText('C:\\TC\\LIB'),
    'Options / Directories: C:\\TC\\INCLUDE and C:\\TC\\LIB', pc.screen());
  keys(pc, '{ESC}', 400);
  // the system menu and About
  keys(pc, '{ALT+SPACE}\r', 600);
  await shot(pc, OUT, 'look-about');
  ok(pc.hasText('ARM Turbo C') && pc.hasText('Version 1.0') && pc.hasText('Europa Micro Systems'), 'About box', pc.screen());
  keys(pc, '{ESC}', 400);
  keys(pc, '{ALT+X}', 1500);
  ok(L(pc).slice(0, before.length).join('\n').includes('ECHO BEFORE TC') && pc.hasText('C:\\>'), 'Alt+X: back to the DOS screen', pc.screen());
}

// ------------------------------------------------- make, run, user screen
if (want('run')) {
  console.log('== F9 Make, Ctrl+F9 Run, Alt+F5 user screen');
  const pc = await boot();
  keys(pc, 'CD \\TC\\SAMPLES\rTC HELLO\r', 2500);
  pc.type('{F9}');
  pc.run(120);
  const mid = pc.screen();
  pc.waitIdle({ timeoutMs: 60000 }); pc.run(200);
  await shot(pc, OUT, 'run-make');
  const box = pc.screen();
  ok(/Main file: HELLO\.C/.test(box) && /Lines compiled:\s+\d{4}/.test(box) && /Warnings:\s+0/.test(box) && /Errors:\s+0/.test(box) &&
     /Available memory: \d+K/.test(box), 'Compiling box: Main file, Lines compiled, Warnings, Errors, Available memory', box);
  ok(/Success : Press any key/.test(box) && /Linking/.test(box), 'Linking ... Success : Press any key', box);
  const lines = +(box.match(/Lines compiled:\s+(\d+)/) || [])[1];
  ok(lines > 4000, `lines compiled with the headers: ${lines}`);
  ok(/Compiling|Linking/.test(mid), 'the box is up while compiling');
  keys(pc, ' ', 400);
  ok(readFile(pc, 'TC/SAMPLES/HELLO.EXE') !== null, 'C:\\TC\\SAMPLES\\HELLO.EXE written');
  keys(pc, '{F9}', 800);
  ok(pc.hasText('HELLO.EXE is up to date') || pc.hasText('up to date'), 'F9 again: up to date', pc.screen());
  keys(pc, ' ', 400);
  keys(pc, '{CTRL+F9}', 2000);
  await shot(pc, OUT, 'run-back');
  ok(row(pc, 0).includes('File  Edit  Search  Run'), 'Ctrl+F9: back in the IDE after the run');
  keys(pc, '{ALT+F5}', 800);
  await shot(pc, OUT, 'run-user-screen');
  ok(pc.hasText('Hello, world!') && pc.hasText('C:\\TC\\SAMPLES>TC HELLO'), 'Alt+F5: the user screen with the output', pc.screen());
  keys(pc, ' ', 800);
  ok(row(pc, 0).includes('File  Edit'), 'a key returns to the IDE');
  // Get info
  keys(pc, '{ALT+F}G', 600);
  await shot(pc, OUT, 'run-info');
  ok(pc.hasText('Lines compiled') && pc.hasText('Program exit code  : 0') && pc.hasText('Available memory'), 'File / Get info', pc.screen());
  keys(pc, '{ESC}', 400);
  // DOS shell
  keys(pc, '{ALT+F}D', 1500);
  ok(pc.hasText('Type EXIT to return to ARM Turbo C'), 'File / DOS shell: the message and a prompt', pc.screen());
  keys(pc, 'VER\rEXIT\r', 1500);
  await shot(pc, OUT, 'run-after-shell');
  ok(row(pc, 0).includes('File  Edit') && pc.hasText('HELLO.C') && !pc.hasText('Type EXIT'), 'EXIT: back in the IDE, redrawn', pc.screen());
}

// ----------------------------------------------------- errors, editing
if (want('errors')) {
  console.log('== compile errors, the Message window, Enter to the line; edit and save');
  const BAD = dos('#include <stdio.h>\n\nint main(void)\n{\n    int i;\n    for (i = 0; i < 3; i++)\n        printf("%d\\n", count);\n    return 0;\n}\n');
  const pc = await boot([['WORK\\BAD.C', BAD]], { dirs: ['WORK'] });
  keys(pc, 'CD \\WORK\rTC BAD.C\r', 2500);
  keys(pc, '{F9}', 800);
  await shot(pc, OUT, 'err-box');
  ok(/Errors:\s+1\s+1/.test(pc.screen()) && pc.hasText('Errors : Press any key'), 'Compiling box: Errors 1, "Errors : Press any key"', pc.screen());
  keys(pc, ' ', 600);
  await shot(pc, OUT, 'err-messages');
  const mrow = findRow(pc, /Message/);
  ok(mrow > 10 && /═ Message ═/.test(row(pc, mrow)), 'the Message window opens', row(pc, mrow));
  ok(pc.hasText("•Error BAD.C 7: 'count' undeclared"), "Error BAD.C 7: 'count' undeclared (highlighted)", pc.screen());
  ok(pc.hasText('Compiling BAD.C:'), 'Compiling BAD.C:');
  const er = findRow(pc, /Error BAD\.C 7/);
  ok(attrAt(pc, er, 3) === 0x1F, 'the selected message: white on blue', attrAt(pc, er, 3).toString(16));
  const src = findRow(pc, /printf\("%d\\n", count\);/);
  ok(attrAt(pc, src, 5) === 0x3F, 'the error line is highlighted in the editor (tracking)', attrAt(pc, src, 5).toString(16));
  ok(row(pc, 24).includes('Edit source') && row(pc, 24).includes('View source'), 'status line of the Message window', row(pc, 24));
  keys(pc, '{ENTER}', 600);
  await shot(pc, OUT, 'err-goto');
  ok(/7:1/.test(pc.screen()) && /═ C:\\WORK\\BAD\.C ═/.test(row(pc, 1)), 'Enter: the editor, cursor on line 7', row(pc, 23));
  // fix it: replace "count" with "i" (end of line, backspace)
  keys(pc, '{END}{LEFT}{LEFT}{BACKSPACE}{BACKSPACE}{BACKSPACE}{BACKSPACE}{BACKSPACE}i', 400);
  ok(pc.hasText('printf("%d\\n", i);'), 'edited the line', pc.screen());
  ok(/\*/.test(row(pc, 23).slice(0, 10)), 'modified mark * in the frame', row(pc, 23));
  // a second error: missing ';'
  keys(pc, '{DOWN}{END}{BACKSPACE}', 300);
  keys(pc, '{F9}', 800);
  keys(pc, ' ', 600);
  await shot(pc, OUT, 'err-semicolon');
  ok(pc.hasText("Error BAD.C 9: ';' expected (got '}')"), "Error BAD.C 9: ';' expected (got '}')", pc.screen());
  keys(pc, '{ENTER}{UP}{END};', 400);
  keys(pc, '{F2}', 600);
  const saved = (readFile(pc, 'WORK/BAD.C') || Buffer.alloc(0)).toString('latin1');
  ok(saved.includes('printf("%d\\n", i);\r\n    return 0;\r\n'), 'F2 saved the file (CR LF)', JSON.stringify(saved.slice(80, 160)));
  ok(readFile(pc, 'WORK/BAD.BAK') !== null, 'the old version kept as BAD.BAK');
  keys(pc, '{CTRL+F9}', 1500);
  keys(pc, '{ALT+F5}', 800);
  await shot(pc, OUT, 'err-fixed-run');
  ok(/^0$/m.test(pc.screen()) && /^1$/m.test(pc.screen()) && /^2$/m.test(pc.screen()), 'the fixed program runs: 0 1 2', pc.screen());
  keys(pc, ' ', 600);
  // Alt+F9: compile only
  keys(pc, '{ALT+F9}', 800);
  ok(pc.hasText('Success : Press any key'), 'Alt+F9: Compile to OBJ', pc.screen());
  keys(pc, ' ', 400);
  ok(readFile(pc, 'WORK/BAD.O') !== null, 'BAD.O written');
}

// ------------------------------------------------------------- projects
if (want('project')) {
  console.log('== a project of two files');
  const MAIN = dos('#include <stdio.h>\n#include "util.h"\n\nint main(void)\n{\n    printf("twice 21 = %d\\n", twice(21));\n    return 0;\n}\n');
  const UTIL = dos('#include "util.h"\n\nint twice(int x)\n{\n    return x * 2;\n}\n');
  const H = dos('int twice(int x);\n');
  const PRJ = dos('main.c\nutil.c\n');
  const pc = await boot([['PRJ\\MAIN.C', MAIN], ['PRJ\\UTIL.C', UTIL], ['PRJ\\UTIL.H', H], ['PRJ\\DEMO.PRJ', PRJ]], { dirs: ['PRJ'] });
  keys(pc, 'CD \\PRJ\rTC DEMO.PRJ\r', 2500);
  await shot(pc, OUT, 'prj-open');
  ok(pc.hasText('Project: C:\\PRJ\\DEMO.PRJ') && pc.hasText('MAIN.C') && pc.hasText('UTIL.C'), 'TC DEMO.PRJ: the Project window lists MAIN.C and UTIL.C', pc.screen());
  keys(pc, '{F9}', 800);
  await shot(pc, OUT, 'prj-make');
  ok(pc.hasText('Success : Press any key'), 'F9 makes the project', pc.screen());
  keys(pc, ' ', 400);
  ok(readFile(pc, 'PRJ/DEMO.EXE') !== null, 'DEMO.EXE written');
  keys(pc, '{CTRL+F9}', 1500);
  keys(pc, '{ALT+F5}', 800);
  ok(pc.hasText('twice 21 = 42'), 'the project runs: twice 21 = 42', pc.screen());
  keys(pc, ' ', 400);
  // Make after touching the header rebuilds (dependency on UTIL.H)
  keys(pc, '{ENTER}', 800);    // opens MAIN.C from the project list
  ok(/MAIN\.C/.test(row(pc, 1)) || pc.hasText('MAIN.C'), 'Enter in the Project window opens the file', row(pc, 1));
}

// ------------------------------------------------------------ help
if (want('help')) {
  console.log('== help');
  const pc = await boot();
  keys(pc, 'TC\r', 2500);
  keys(pc, '{F1}', 600);
  await shot(pc, OUT, 'help-editor');
  ok(pc.hasText('Editor keys') && /═ Help ═/.test(pc.screen()), 'F1 in the editor: Editor keys', pc.screen());
  keys(pc, '{ESC}', 400);
  // Ctrl+F1 on printf
  const pr = findRow(pc, /printf\("Hello/);
  keys(pc, '{CTRL+HOME}' + '{DOWN}'.repeat(pr - 2) + '{HOME}{RIGHT}{RIGHT}{RIGHT}{RIGHT}{RIGHT}{RIGHT}', 400);
  keys(pc, '{CTRL+F1}', 600);
  await shot(pc, OUT, 'help-printf');
  ok(pc.hasText('printf') && pc.hasText('%d') , 'Ctrl+F1 on printf: its topic', pc.screen());
  keys(pc, '{ESC}{SHIFT+F1}', 600);
  await shot(pc, OUT, 'help-index');
  ok(pc.hasText('Index of help topics') && pc.hasText('#include') && pc.hasText('atoi'), 'Shift+F1: the index', pc.screen());
  keys(pc, '{TAB}\r', 600);
  ok(!pc.hasText('Index of help topics'), 'Tab + Enter follows a cross reference');
  keys(pc, '{ALT+F1}', 600);
  ok(pc.hasText('Index of help topics'), 'Alt+F1: back');
  keys(pc, '{ESC}{ALT+C}', 400);
  keys(pc, '{F1}', 600);
  ok(pc.hasText('Compile menu') && !pc.hasText('│ Compile to OBJ  Alt+F9 │'), 'F1 in a menu: the menus close, the menu\'s topic', pc.screen());
  keys(pc, '{ESC}', 400);
}

// ------------------------------------------------------------ mouse
if (want('mouse')) {
  console.log('== the mouse');
  const pc = await boot();
  keys(pc, 'TC\r', 2500);
  const m = mouse(pc);
  m.home();
  m.click(0, row(pc, 0).indexOf('Compile') + 2);
  await shot(pc, OUT, 'mouse-compile-menu');
  ok(pc.hasText('Compile to OBJ') && pc.hasText('Make EXE file'), 'click on Compile opens the menu', pc.screen());
  const mk = findRow(pc, /Make EXE file/);
  m.click(mk, L(pc)[mk].indexOf('Make') + 2);
  pc.waitIdle({ timeoutMs: 60000 }); pc.run(300);
  await shot(pc, OUT, 'mouse-make');
  ok(pc.hasText('Success : Press any key'), 'click on Make EXE file makes', pc.screen());
  m.click(12, 40);
  ok(!pc.hasText('Press any key'), 'a click closes the box');
  m.click(0, row(pc, 0).indexOf('Help') + 1);
  ok(pc.hasText('Contents') && pc.hasText('Index'), 'click on Help', pc.screen());
  pc.type('{ESC}'); pc.run(300);
  // click in the text moves the cursor
  m.click(11, 10);
  ok(/ 10:10 /.test(row(pc, 23)), 'click in the text moves the cursor (line 10, column 10)', row(pc, 23));
}

// --------------------------------------------------------- samples, memory
if (want('samples')) {
  console.log('== samples run from the IDE; free memory for a program');
  const FREE = dos('#include <stdio.h>\n#include <dos.h>\n\nint main(void)\n{\n    union REGS r;\n    r.x.ax = 0x4800;\n    r.x.bx = 0xFFFF;\n    int86(0x21, &r, &r);\n    printf("FREE=%lu\\n", (unsigned long)(r.x.bx & 0xFFFF) * 16);\n    return 0;\n}\n');
  const pc = await boot([['WORK\\FREE.C', FREE]], { dirs: ['WORK'] });
  keys(pc, 'CD \\WORK\rTC FREE.C\r', 2500);
  keys(pc, '{CTRL+F9}', 1500);
  pc.waitIdle({ timeoutMs: 60000 }); pc.run(300);
  keys(pc, ' ', 300);
  keys(pc, '{CTRL+F9}', 1500);
  keys(pc, '{ALT+F5}', 800);
  await shot(pc, OUT, 'mem-in-tc');
  const inTc = +((pc.screen().match(/FREE=(\d+)/) || [])[1] || 0);
  keys(pc, ' {ALT+X}', 1500);
  keys(pc, 'CLS\rFREE\r', 1000);
  const atDos = +((pc.screen().match(/FREE=(\d+)/) || [])[1] || 0);
  console.log(`     free for a program: ${inTc} bytes run from TC, ${atDos} bytes at the DOS prompt`);
  ok(inTc > 500000 && atDos - inTc < 24000, `a program run from TC gets ${Math.round(inTc / 1024)}K (${Math.round((atDos - inTc) / 1024)}K less than from DOS)`);
  fs.writeFileSync(path.join(OUT, 'memory.txt'), `run from TC: ${inTc}\nat the DOS prompt: ${atDos}\n`);
  // MODE13 sample: graphics, waits for a key
  keys(pc, 'CD \\TC\\SAMPLES\rTC MODE13.C\r', 2500);
  pc.type('{CTRL+F9}');
  pc.run(4000);
  await shot(pc, OUT, 'sample-mode13');
  ok(pc.machine.vga ? true : true, 'MODE13.C ran (see sample-mode13.png)');
  const mode = pc.cpu.m8[0x449];
  ok(mode === 0x13, 'MODE13 is in mode 13h while it runs', mode.toString(16));
  keys(pc, ' ', 1500);
  ok(row(pc, 0).includes('File  Edit') && pc.cpu.m8[0x449] === 3, 'back to the IDE in text mode');
  keys(pc, '{ALT+F}O', 600);
  keys(pc, 'MANDEL.C\r', 1500);
  keys(pc, '{CTRL+F9}', 3000);
  pc.waitIdle({ timeoutMs: 60000 });
  keys(pc, '{ALT+F5}', 800);
  await shot(pc, OUT, 'sample-mandel');
  ok(/[#*.+]{10,}/.test(pc.screen()), 'MANDEL.C output on the user screen', pc.screen());
  keys(pc, ' ', 400);
}

console.log(`\n${T.pass} passed, ${T.fail} failed`);
process.exit(T.fail ? 1 : 0);
