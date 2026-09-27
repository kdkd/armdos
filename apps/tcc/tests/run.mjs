#!/usr/bin/env node
// apps/tcc/tests/run.mjs - the Tiny C Compiler on ARM-DOS, headless.
//
//   node apps/tcc/tests/run.mjs [--only samples,session,errors,suite,run] [--keep] [--verbose]
//
// Everything is compiled BY TCC.EXE RUNNING ON ARM-DOS (C:\DOS\TCC.EXE with
// C:\TC from build/tcc/disk/TC, exactly what goes on the hard disk image):
//
//   samples  C:\TC\SAMPLES\BUILD.BAT compiles the five samples; each one runs
//            and is checked (text output, speaker notes, mode 13h pixels,
//            screenshots in build/tcc/test/); compile times are measured.
//   session  what a visitor types: COPY CON HELLO.C, TCC HELLO.C, HELLO;
//            TCC with no arguments prints the usage banner.
//   errors   "FILE.C:LINE: error: ..." messages, exit code, no .EXE left.
//   suite    TinyCC's tests/tests2 programs (tests/tests2: those GCC compiles too) plus
//            our own (tests/progs: soft-float, packed structs, ...) are
//            compiled on ARM-DOS by TCC.EXE and run; their output must be
//            identical to the same programs compiled by the host GCC SDK
//            and run in the same machine.
//   run      TCC -run: compile in memory and run.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { makeImage, start, cmd, readFile, ROOT, B } from './lib.mjs';

const HERE = path.dirname(new URL(import.meta.url).pathname);
const OUT = B('tcc/test');
const argv = process.argv.slice(2);
const opt = (n, d) => { const i = argv.indexOf(n); return i >= 0 ? argv[i + 1] : d; };
const ONLY = opt('--only', '') ? opt('--only', '').split(',') : null;
const VERBOSE = argv.includes('--verbose');
const TESTS2 = path.join(HERE, 'tests2');   // TinyCC's tests/tests2 (LGPL), the ones used here

let failures = 0, passes = 0;
const check = (ok, what, detail) => {
  if (ok) passes++; else failures++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`);
  if ((!ok || VERBOSE) && detail) console.log(String(detail).split('\n').map((l) => '     ' + l).join('\n'));
  return ok;
};
const timings = [];

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });

// the C: drive as the hard disk image has it, plus extra files
function disk(name, files = [], dirs = [], autoexec) {
  return makeImage(path.join(OUT, name), {
    files: [{ dst: 'DOS\\TCC.EXE', src: 'build/TCC.EXE' }, { dst: 'DOS\\EDLIN.COM', src: 'build/EDLIN.COM' }, ...files],
    trees: [{ src: 'build/tcc/disk/TC', dst: 'TC' }],
    dirs: ['TC', ...dirs],
    ...(autoexec ? { autoexec } : {}),
  });
}
const text = (b) => (b ?? Buffer.alloc(0)).toString('latin1').replace(/\r\n/g, '\n');
const screenTail = (pc, n = 12) => pc.screen().split('\n').filter((l) => l.trim()).slice(-n).join('\n');

// ----------------------------------------------------------------- GCC side
// the same C program built with the ARM-DOS SDK (GCC + newlib-nano)
function gccBuild(src, exe, extraFlags = []) {
  const o = exe.replace(/\.EXE$/i, '.o'), elf = exe.replace(/\.EXE$/i, '.elf');
  const arch = ['-marm', '-march=armv5te', '-mfloat-abi=soft', '--specs=nano.specs'];
  execFileSync('arm-none-eabi-gcc', [...arch, '-O1', '-std=gnu11', '-w', '-fno-builtin', '-isystem', path.join(ROOT, 'sdk/include'),
    ...extraFlags, '-c', src, '-o', o], { stdio: 'pipe' });
  execFileSync('arm-none-eabi-gcc', [...arch, '-nostartfiles', '-nostdlib', '-T', path.join(ROOT, 'sdk/link.ld'),
    '-Wl,-q', '-Wl,--gc-sections', '-Wl,--target2=rel', '-Wl,-u,_printf_float', '-Wl,-u,_scanf_float', '-o', elf,
    path.join(ROOT, 'build/sdk/crt0.o'), o, '-Wl,--start-group', path.join(ROOT, 'build/sdk/libdos.a'), '-lc', '-lm', '-lgcc',
    '-Wl,--end-group'], { stdio: 'pipe' });
  execFileSync('node', [path.join(ROOT, 'sdk/elf2exe.mjs'), '--stack', '16384', elf, '-o', exe], { stdio: 'pipe' });
}

// ------------------------------------------------------------------ samples
async function samples() {
  console.log('== samples: C:\\TC\\SAMPLES\\BUILD.BAT with TCC.EXE on ARM-DOS');
  const speaker = [];
  let pc = null;
  pc = await start(disk('samples'), { onSpeaker: (on, hz) => speaker.push([pc ? pc.timeMs : 0, on, hz]) });
  cmd(pc, 'CD \\TC\\SAMPLES');
  const t = cmd(pc, 'BUILD', 600000);
  const scr = pc.screen();
  check(scr.includes('Done. Run HELLO'), `BUILD.BAT ran (${(t / 1000).toFixed(1)} s emulated for 5 programs)`, screenTail(pc));
  timings.push(['BUILD.BAT (5 samples)', t]);
  for (const n of ['HELLO', 'MODE13', 'TUNE', 'ASMDEMO', 'MANDEL']) {
    const exe = readFile(pc, `TC\\SAMPLES\\${n}.EXE`);
    check(exe && exe[0] === 0x4D && exe[1] === 0x5A && exe.includes('AR1\0'), `${n}.EXE written (${exe?.length ?? 0} bytes, MZ + AR1)`);
  }
  check(!/error|warning/i.test(scr), 'no errors or warnings while compiling the samples', scr);

  // compile time of HELLO.C alone
  let th = cmd(pc, 'TCC HELLO.C');
  timings.push(['TCC HELLO.C', th]);
  th = cmd(pc, 'TCC MANDEL.C');
  timings.push(['TCC MANDEL.C', th]);

  cmd(pc, 'CLS');
  cmd(pc, 'HELLO');
  check(pc.screen().includes('HELLO\nHello, world!'), 'HELLO prints "Hello, world!"', screenTail(pc, 4));

  cmd(pc, 'CLS');
  cmd(pc, 'ASMDEMO');
  const a = pc.screen();
  check(a.includes('CPU ID register  : 41069265  (implementer \'A\', part 926, revision 5)'), 'ASMDEMO: mrc p15 reads the ARM926 ID', a);
  check(/CPSR +: [0-9A-F]{6}[1-9A-F]F  \(SYS mode, IRQs on\)/.test(a), 'ASMDEMO: mrs reads CPSR (SYS mode, IRQs on)', a);
  check(a.includes('clz(0x00F00000)  : 8') && a.includes('0x87654321 * 1000: 0x210E38E38E8'), 'ASMDEMO: clz and umull', a);
  check(a.includes('And this line was printed by svc #0x21 with AH=09h.'), 'ASMDEMO: svc #0x21 from inline assembler', a);
  await pc.png(path.join(OUT, 'asmdemo.png'));

  // MANDEL: same output as MANDEL.C built with GCC
  cmd(pc, 'MANDEL > MANDEL.OUT');
  gccBuild(path.join(HERE, '../samples/MANDEL.C'), path.join(OUT, 'MANDELG.EXE'));
  const pcg = await start(makeImage(path.join(OUT, 'mandelg'), { files: [{ dst: 'MANDELG.EXE', src: path.relative(ROOT, path.join(OUT, 'MANDELG.EXE')) }] }));
  cmd(pcg, 'MANDELG > MANDEL.OUT');
  const m1 = text(readFile(pc, 'TC\\SAMPLES\\MANDEL.OUT')), m2 = text(readFile(pcg, 'MANDEL.OUT'));
  check(m1.length > 1500 && m1 === m2, 'MANDEL (soft-float doubles) prints exactly what the GCC-built MANDEL prints',
    `TCC:\n${m1}\nGCC:\n${m2}`);
  cmd(pc, 'CLS');
  cmd(pc, 'MANDEL');
  await pc.png(path.join(OUT, 'mandel.png'));

  // TUNE: the notes of Ode to Joy on the speaker
  speaker.length = 0;
  cmd(pc, 'CLS');
  const tt = cmd(pc, 'TUNE', 60000);
  const notes = speaker.filter((e) => e[1] && e[2] > 0).map((e) => Math.round(e[2]));
  const expect = [330, 330, 349, 392, 392, 349, 330, 294, 262, 262, 294, 330, 330, 294, 294];
  check(notes.length === 30 && expect.every((f, i) => Math.abs(notes[i] - f) <= 1),
    `TUNE plays 30 notes on the PC speaker (${notes.slice(0, 8).join(' ')} Hz ...), ${(tt / 1000).toFixed(1)} s`, JSON.stringify(notes));
  check(pc.screen().includes('Ode to Joy on the PC speaker'), 'TUNE prints its notes', screenTail(pc, 8));
  await pc.png(path.join(OUT, 'tune.png'));

  // MODE13: 320x200x256 through int86(0x10), palette cycling until a key
  cmd(pc, 'CLS');
  pc.type('MODE13\r');
  check(pc.until(() => pc.machine.vga.mode === 0x13, { timeoutMs: 10000 }), 'MODE13 switches to mode 13h with int86(0x10, ...)');
  pc.run(1500);
  const vram = pc.cpu.m8 ? pc.cpu.m8.subarray(0xA0000, 0xA0000 + 64000) : null;
  const px = (x, y) => vram[y * 320 + x];
  const ring = (x, y) => ((((x - 160) ** 2 + (y - 100) ** 2) >> 5) + ((x ^ y) >> 2)) & 0xFF;
  let good = 0;
  for (let i = 0; i < 400; i++) { const x = (i * 37) % 320, y = (i * 53) % 200; if (px(x, y) === ring(x, y)) good++; }
  check(vram && good === 400, `MODE13 drew the pattern into 0xA0000 (${good}/400 sampled pixels right)`);
  await pc.png(path.join(OUT, 'mode13.png'));
  pc.run(700);
  await pc.png(path.join(OUT, 'mode13-cycled.png'));
  pc.type(' ');
  pc.until(() => pc.machine.vga.mode === 3, { timeoutMs: 5000 });
  cmd(pc, '');
  check(pc.machine.vga.mode === 3 && /Back in text mode after \d+ palette cycles\./.test(pc.screen()), 'MODE13 returns to text mode on a key', screenTail(pc, 4));
  check(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ------------------------------------------------------------------ session
async function session() {
  console.log('== session: COPY CON HELLO.C / TCC HELLO.C / HELLO');
  const pc = await start(disk('session', [
    { dst: 'UTIL.C', data: 'int twice(int x)\r\n{\r\n    return 2 * x;\r\n}\r\n' },
    { dst: 'MAIN.C', data: '#include <stdio.h>\r\nint twice(int);\r\nint main(void)\r\n{\r\n    printf("twice 21 = %d\\n", twice(21));\r\n    return 0;\r\n}\r\n' }]));
  cmd(pc, 'TCC');
  let s = pc.screen();
  check(s.includes('Tiny C Compiler for ARM-DOS  Version 0.9.28rc') && s.includes('Syntax is: TCC [ options ] file[s]'),
    'TCC alone prints the usage banner', screenTail(pc, 18));
  cmd(pc, 'CLS');
  pc.type('COPY CON HELLO.C\r');
  pc.run(300);
  for (const l of ['#include <stdio.h>', '', 'int main(void)', '{', '    int i;', '    for (i = 1; i <= 3; i++)',
    '        printf("Hello from ARM-DOS, line %d\\n", i);', '    return 0;', '}'])
    pc.type(l + '\r');
  pc.type('{F6}\r');
  pc.waitIdle({ timeoutMs: 20000 });
  check(/1 File\(s\) copied/.test(pc.screen()), 'COPY CON HELLO.C', screenTail(pc, 4));
  const t = cmd(pc, 'TCC HELLO.C');
  timings.push(['TCC HELLO.C (typed at the prompt)', t]);
  check(readFile(pc, 'HELLO.EXE') !== null && /C:\\>TCC HELLO\.C\n\nC:\\>$/.test(pc.screen().trimEnd()), `TCC HELLO.C: silent, HELLO.EXE made (${(t / 1000).toFixed(2)} s)`, screenTail(pc, 4));
  cmd(pc, 'HELLO');
  s = pc.screen();
  check(s.includes('Hello from ARM-DOS, line 1\nHello from ARM-DOS, line 2\nHello from ARM-DOS, line 3'), 'HELLO runs', screenTail(pc, 6));
  cmd(pc, 'DIR HELLO.*');
  check(/HELLO +EXE +\d+/.test(pc.screen()), 'DIR shows HELLO.EXE', screenTail(pc, 8));
  await pc.png(path.join(OUT, 'session.png'));
  // -o, -c + link, -E
  cmd(pc, 'CLS');
  cmd(pc, 'TCC -c HELLO.C');
  check(readFile(pc, 'HELLO.O')?.subarray(0, 4).toString('latin1') === '\x7fELF', 'TCC -c HELLO.C writes the object HELLO.O');
  cmd(pc, 'TCC -o HI.EXE HELLO.O');
  cmd(pc, 'HI');
  check(pc.screen().includes('Hello from ARM-DOS, line 3'), 'TCC -o HI.EXE HELLO.O links an object', screenTail(pc, 5));
  cmd(pc, 'TCC -v');
  check(pc.screen().includes('tcc version 0.9.28rc (ARM eabi soft-float ARM-DOS)'), 'TCC -v', screenTail(pc, 3));

  // a library: TCC -ar, then -L. -lNAME
  cmd(pc, 'CLS');
  cmd(pc, 'TCC -c UTIL.C');
  cmd(pc, 'TCC -ar rcs LIBUTIL.A UTIL.O');
  cmd(pc, 'TCC -L. -lutil MAIN.C');
  cmd(pc, 'MAIN');
  check(readFile(pc, 'LIBUTIL.A')?.subarray(0, 8).toString('latin1') === '!<arch>\n' && pc.screen().includes('twice 21 = 42'),
    'TCC -ar rcs LIBUTIL.A UTIL.O; TCC -L. -lutil MAIN.C', screenTail(pc, 8));

  // EDLIN: write a program with the line editor (it ends the file with ^Z)
  cmd(pc, 'CLS');
  pc.type('EDLIN SQUARES.C\r');
  pc.waitIdle({ timeoutMs: 10000 });
  pc.type('I\r');
  pc.waitIdle({ timeoutMs: 10000 });
  for (const l of ['#include <stdio.h>', 'int main(void)', '{', '    int n;', '    for (n = 1; n <= 5; n++)',
    '        printf("%d squared is %d\\n", n, n * n);', '    return 0;', '}'])
    pc.type(l + '\r');
  pc.type('{CTRL+C}');
  pc.waitIdle({ timeoutMs: 10000 });
  pc.type('E\r');
  pc.waitIdle({ timeoutMs: 10000 });
  const sq = readFile(pc, 'SQUARES.C');
  check(sq && sq.includes('printf("%d squared is %d\\n", n, n * n);\r\n'), `EDLIN SQUARES.C wrote the program (${sq?.length ?? 0} bytes${sq && sq[sq.length - 1] === 0x1A ? ', ends with ^Z' : ''})`, screenTail(pc, 14));
  cmd(pc, 'TCC SQUARES.C');
  cmd(pc, 'SQUARES');
  check(pc.screen().includes('1 squared is 1\n2 squared is 4\n3 squared is 9\n4 squared is 16\n5 squared is 25'), 'TCC SQUARES.C, SQUARES', screenTail(pc, 8));
  await pc.png(path.join(OUT, 'edlin.png'));
  check(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ------------------------------------------------------------------- errors
async function errors() {
  console.log('== errors: messages as FILE.C:LINE: error: ...');
  const bad = '#include <stdio.h>\r\n\r\nint main(void)\r\n{\r\n    printf("oops\\n")\r\n    return 0;\r\n}\r\n';
  const warn = 'int main(void)\r\n{\r\n    int x;\r\n    return y;\r\n}\r\n';
  const pc = await start(disk('errors', [{ dst: 'BAD.C', data: bad }, { dst: 'UNDEF.C', data: warn },
    { dst: 'LINKERR.C', data: 'void nosuch(void);\r\nint main(void) { nosuch(); return 0; }\r\n' }]));
  cmd(pc, 'TCC BAD.C');
  check(pc.screen().includes('BAD.C:6: error: \';\' expected (got \'return\')'), 'syntax error: BAD.C:6: error: \';\' expected', screenTail(pc, 3));
  check(readFile(pc, 'BAD.EXE') === null, 'no BAD.EXE after an error');
  cmd(pc, 'IF ERRORLEVEL 1 ECHO exit code 1');
  check(pc.screen().includes('\nexit code 1'), 'TCC exits with ERRORLEVEL 1', screenTail(pc, 3));
  cmd(pc, 'TCC UNDEF.C');
  check(pc.screen().includes("UNDEF.C:4: error: 'y' undeclared"), "undeclared identifier: UNDEF.C:4: error: 'y' undeclared", screenTail(pc, 3));
  cmd(pc, 'TCC LINKERR.C');
  check(pc.screen().includes("tcc: error: undefined symbol 'nosuch'") || pc.screen().includes("unresolved reference to 'nosuch'"),
    'link error names the missing function', screenTail(pc, 3));
  cmd(pc, 'TCC NOFILE.C');
  check(/NOFILE\.C.*(not found|No such file)/i.test(pc.screen()), 'missing source file', screenTail(pc, 3));
}

// -------------------------------------------------------------------- suite
// TinyCC's own tests2 programs, where GCC can build them too (so both can be
// compared); skipped: x86/arm64 asm, threads/TLS, bounds checker, tests of
// tcc's own diagnostics, multi-file tests and ones needing files.
const SKIP2 = new Set(['34_array_assignment', '98_al_ax_extend', '99_fastcall', '85_asm-outside-function', '127_asm_goto',
  '113_btdll', '112_backtrace', '114_bound_signal', '115_bound_setjmp', '116_bound_setjmp2', '117_builtins',
  '126_bound_global', '132_bound_test', '148_linker_symbols', '106_versym', '144_tls', '146_tls_extern',
  '138_arm64_encoding', '139_arm64_errors', '140_arm64_extasm', '141_riscv_asm', '145_winarm64_interlocked',
  '60_errors_and_warnings', '96_nodata_wanted', '125_atomic_misc', '104_inline', '120_alias', '108_constructor',
  '124_atomic_counter', '136_atomic_gcc_style', '128_run_atexit', '42_function_pointer', '18_include',
  '95_bitfields_ms', '119_random_stuff', '46_grep', '129_scopes', '112_backtrace', '101_cleanup', '147_crash_on_const_expr',
  // not comparable with GCC: 118 prints long longs with %ld (varargs padding),
  // 70 has a __TINYC__-only part, 86 checks __ILP32__ (GCC does not define it),
  // 95_bitfields #includes itself by its long name
  '118_switch', '70_floating_point_literals', '86_memory-model', '95_bitfields']);
const ARGS2 = { '31_args': 'arg1 arg2 arg3 arg4 arg5' };

async function suite() {
  console.log('== suite: tests2 + tests/progs compiled by TCC.EXE on ARM-DOS vs the same programs built by GCC');
  const work = path.join(OUT, 'suite');
  fs.mkdirSync(work, { recursive: true });
  const progs = [];
  for (const f of fs.readdirSync(path.join(HERE, 'progs')).sort()) if (f.endsWith('.c')) progs.push([f.slice(0, -2), path.join(HERE, 'progs', f)]);
  if (fs.existsSync(TESTS2))
    for (const f of fs.readdirSync(TESTS2).sort()) {
      const n = f.slice(0, -2);
      if (/^\d+_.*\.c$/.test(f) && !f.includes('+') && !SKIP2.has(n)) progs.push([n, path.join(TESTS2, f)]);
    }
  else console.log(`     (${TESTS2} not found: only tests/progs)`);
  const files = [], list = [], gccFail = [];
  let k = 0;
  for (const [name, src] of progs) {
    const id = String(k++).padStart(3, '0');
    try { gccBuild(src, path.join(work, `G${id}.EXE`)); }
    catch (e) { gccFail.push(name); continue; }
    list.push({ id, name });
    files.push({ dst: `S\\T${id}.C`, data: fs.readFileSync(src) });
    files.push({ dst: `S\\G${id}.EXE`, src: path.relative(ROOT, path.join(work, `G${id}.EXE`)) });
  }
  if (VERBOSE) console.log(`     not buildable with GCC (skipped): ${gccFail.join(' ')}`);
  let bat = '@ECHO OFF\r\n';
  for (const { id, name } of list)
    bat += `TCC T${id}.C > T${id}.CMP\r\nT${id} ${ARGS2[name] ?? ''} > T${id}.OUT\r\nG${id} ${ARGS2[name] ?? ''} > G${id}.OUT\r\n`;
  bat += 'ECHO SUITE DONE\r\n';
  files.push({ dst: 'S\\RUNALL.BAT', data: bat });
  const pc = await start(disk('suite', files, ['S']));
  cmd(pc, 'CD \\S');
  const t = cmd(pc, 'RUNALL', 1800000);
  check(pc.screen().includes('SUITE DONE'), `RUNALL.BAT finished (${list.length} programs compiled on ARM-DOS in ${(t / 1000).toFixed(1)} s emulated, including running both versions)`, screenTail(pc));
  const bad = [];
  let same = 0, warnings = 0;
  for (const { id, name } of list) {
    const exe = readFile(pc, `S\\T${id}.EXE`);
    const cmpOut = text(readFile(pc, `S\\T${id}.CMP`));
    if (/warning/.test(cmpOut)) warnings++;
    const a = text(readFile(pc, `S\\T${id}.OUT`)), b = text(readFile(pc, `S\\G${id}.OUT`));
    if (exe && a === b) same++;
    else bad.push(`${name}: ${exe ? 'output differs' : 'not compiled: ' + cmpOut.split('\n').filter(Boolean).slice(-1)[0]}` +
      (exe && VERBOSE ? `\n  TCC: ${JSON.stringify(a.slice(0, 300))}\n  GCC: ${JSON.stringify(b.slice(0, 300))}` : ''));
  }
  check(bad.length === 0, `${same}/${list.length} programs compiled on ARM-DOS by TCC.EXE print exactly what their GCC builds print`, bad.join('\n'));
  timings.push([`suite: ${list.length} x (TCC compile + 2 runs)`, t]);
  check(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

// ---------------------------------------------------------------------- run
async function run() {
  console.log('== run: TCC -run');
  const src = '#include <stdio.h>\r\n#include <math.h>\r\nint main(int argc, char **argv)\r\n{\r\n    int i;\r\n' +
    '    for (i = 0; i < argc; i++) printf("argv[%d] = %s\\n", i, argv[i]);\r\n' +
    '    printf("sqrt(2) = %.6f\\n", sqrt(2.0));\r\n    return 3;\r\n}\r\n';
  const pc = await start(disk('run', [{ dst: 'ARGS.C', data: src }]));
  const t = cmd(pc, 'TCC -run ARGS.C one two');
  const s = pc.screen();
  check(s.includes('argv[0] = ARGS.C\nargv[1] = one\nargv[2] = two\nsqrt(2) = 1.414214'), `TCC -run ARGS.C one two (${(t / 1000).toFixed(2)} s)`, screenTail(pc, 6));
  timings.push(['TCC -run ARGS.C', t]);
  cmd(pc, 'IF ERRORLEVEL 3 ECHO exit code 3');
  check(pc.screen().includes('\nexit code 3'), "the program's exit code is TCC's", screenTail(pc, 3));
  check(readFile(pc, 'ARGS.EXE') === null, 'no .EXE written');
}

// ----------------------------------------------------------------- selfhost
// TCC.EXE compiles TinyCC itself (tcc.c + everything it includes, ~83,000
// lines) on ARM-DOS. The result must be byte-identical to what the Linux
// cross compiler makes of the same sources. It is too big to run in 640 KB,
// but "TCC -run TCC.C" compiles it into extended memory and runs it there:
// that TCC compiles HELLO.C.
async function selfhost() {
  console.log('== selfhost: TCC.EXE compiles TCC.C');
  const SRC = path.join(HERE, '../src');
  const names = ['tcc.c', 'libtcc.c', 'tccpp.c', 'tccgen.c', 'tccdbg.c', 'tccelf.c', 'tccasm.c', 'tccrun.c', 'tcctools.c',
    'arm-gen.c', 'arm-link.c', 'arm-asm.c', 'arm-tok.h', 'tcc.h', 'libtcc.h', 'tcctok.h', 'elf.h', 'stab.h', 'stab.def', 'dwarf.h', 'config.h'];
  const files = names.map((n) => ({ dst: 'SRC\\' + n.toUpperCase(), data: fs.readFileSync(path.join(SRC, n)) }));
  files.push({ dst: 'SRC\\TCCDEFS_.H', data: fs.readFileSync(B('tcc/gen/tccdefs_.h')) });
  const hostsyms = 'const struct { const char *name; void *addr; } tcc_armdos_hostsyms[] = { { 0, 0 } };\n';
  files.push({ dst: 'SRC\\HOSTSYMS.C', data: hostsyms });
  files.push({ dst: 'SRC\\HELLO.C', data: fs.readFileSync(path.join(HERE, '../samples/HELLO.C')) });
  const pc = await start(disk('selfhost', files, ['SRC']));
  cmd(pc, 'CD \\SRC');
  const t = cmd(pc, 'TCC -bench -DTCC_HOST_ARMDOS -o TCC2.EXE TCC.C HOSTSYMS.C', 900000);
  const m = pc.screen().match(/# (\d+) idents, (\d+) lines, (\d+) bytes/);
  const dos = readFile(pc, 'SRC\\TCC2.EXE');
  check(dos && m, `TCC.EXE compiled TCC.C: ${m ? m[2] : '?'} lines in ${(t / 1000).toFixed(1)} s, TCC2.EXE ${dos?.length ?? 0} bytes`, screenTail(pc, 6));
  timings.push([`TCC TCC.C (${m ? m[2] : '?'} lines)`, t]);
  // the same with the Linux cross compiler
  fs.writeFileSync(path.join(OUT, 'hostsyms.c'), hostsyms);
  execFileSync(B('tcc/host/armdos-tcc'), ['-DTCC_HOST_ARMDOS', '-I' + SRC, '-I' + B('tcc/gen'), '-o', path.join(OUT, 'TCC2.EXE'),
    path.join(SRC, 'tcc.c'), path.join(OUT, 'hostsyms.c')]);
  const cross = fs.readFileSync(path.join(OUT, 'TCC2.EXE'));
  check(dos && Buffer.compare(dos, cross) === 0, 'TCC2.EXE is byte-identical to the cross compiler\'s build of the same sources');
  cmd(pc, 'TCC2');
  check(pc.screen().includes('Program too big to fit in memory'), 'TCC2.EXE (unoptimised) does not fit in 640 KB: "Program too big to fit in memory"', screenTail(pc, 2));
  cmd(pc, 'CLS');
  const t2 = cmd(pc, 'TCC -DTCC_HOST_ARMDOS HOSTSYMS.C -run TCC.C -o HELLO2.EXE HELLO.C', 900000);
  cmd(pc, 'HELLO2');
  check(pc.screen().includes('HELLO2\nHello, world!'), `TCC -run TCC.C ... HELLO.C: TinyCC compiled on ARM-DOS, run from extended memory, compiles HELLO.C (${(t2 / 1000).toFixed(1)} s)`, screenTail(pc, 5));
  timings.push(['TCC -run TCC.C -o HELLO2.EXE HELLO.C', t2]);
  check(pc.faults.length === 0, 'no CPU faults', JSON.stringify(pc.faults));
}

const suites = { samples, session, errors, suite, run, selfhost };
for (const [name, fn] of Object.entries(suites)) {
  if (ONLY && !ONLY.includes(name)) continue;
  try { await fn(); } catch (e) { check(false, `${name}: ${e.message}`, e.stack); }
}
if (timings.length) {
  console.log('== timings (emulated time at 100 MHz, Enter to prompt)');
  for (const [w, t] of timings) console.log(`     ${w.padEnd(44)} ${(t / 1000).toFixed(2)} s`);
}
console.log(`${passes} passed, ${failures} failed`);
process.exit(failures ? 1 : 0);
