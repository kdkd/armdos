// kernel/tests/scenarios.mjs - the kernel test scenarios (see run.mjs)
export const scenarios = [
  {
    name: 'hello',
    script: 'K_HELLO ONE two\nexit 0\n',
    expect: ['T:PASS hello', 'T:EXIT K_HELLO 0 0'],
    expectScreen: ['Hello from printf', 'Hello via AH=09h', 'AH=02h ok'],
  },
];
scenarios.push({
  name: 'files',
  config: 'FILES=40\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  script: 'K_FILES\nexit 0\n',
  expect: ['T:PASS files', 'T:EXIT K_FILES 0 0'],
  timeout: 120,
});
scenarios.push({
  name: 'exec',
  script: 'K_EXEC\nexit 0\n',
  expect: ['T:PASS exec', 'T:EXIT K_EXEC 0 0'],
  expectScreen: ['Divide overflow', 'Exception 06h: undefined instruction', 'Exception 0Dh: data abort',
                 'Exception 0Eh: prefetch abort', 'PSP:50h call ok'],
  rejectScreen: ['NOT TERMINATED'],
  allowFaults: true,
});
scenarios.push({
  name: 'mem',
  script: 'K_MEM\nmem\nexit 0\n',
  expect: ['T:PASS mem', 'T:EXIT K_MEM 0 0'],
});

// ---------------------------------------------------------- console
const waitS = (pc, t, ms = 20000) => pc.waitSerial(t, { timeoutMs: ms });
const lineTests = [
  ['hello world\r', 'hello world'],
  ['{F3}\r', 'hello world'],
  ['{F1}{F1}{F1}XY{F3}\r', 'helXY world'],
  ['{F2}wthere\r', 'helXY there'],
  ['{F4}X{F3}\r', 'XY there'],
  ['abc{ESC}def\r', 'def'],
  ['{INS}ZZ{F3}\r', 'ZZdef'],
  ['{DEL}{DEL}{F3}\r', 'def'],
  ['abcd{BS}{BS}x\r', 'abx'],
  ['one{F5}{F3}\r', 'one'],
  ['{RIGHT}{RIGHT}!\r', 'on!'],
  ['a\tb{BS}{BS}c\r', 'ac'],
  ['x{F6}\r', 'x\x1A'],
  ['y{F7}\r', 'y@'],
  ['{CTRL+H}zz{LEFT}q\r', 'zq'],
];
scenarios.push({
  name: 'con-line',
  script: 'K_CON LINE\nexit 0\n',
  async run({ pc, fail }) {
    if (!waitS(pc, 'T:ROLE LINE')) return fail('no start');
    for (const [keys, want] of lineTests) {
      const before = pc.serial.length;
      pc.type(keys);
      if (!pc.until(() => pc.serial.slice(before).includes('T:LINE ['), { timeoutMs: 20000 })) { fail(`no line for ${JSON.stringify(keys)}`); return; }
      pc.run(50);
      const got = /T:LINE \[([^\]]*)\]/.exec(pc.serial.slice(before))[1];
      if (got !== want) fail(`keys ${JSON.stringify(keys)}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`);
    }
    const long = 'x'.repeat(70);
    let before = pc.serial.length;
    pc.type(long + '\r');
    pc.until(() => pc.serial.slice(before).includes('T:LINE ['));
    pc.run(50);
    if (!/T:LINE \[x{59}\] 59 cr=1/.test(pc.serial.slice(before))) fail('buffer full: ' + pc.serial.slice(before));
    const s = pc.screen();
    if (!s.includes('L>abc\\')) fail('Esc shows backslash:\n' + s);
    if (!s.includes('L>one@')) fail('F5 shows @:\n' + s);
    if (!s.includes('L>x^Z')) fail('F6 echo ^Z:\n' + s);
    before = pc.serial.length;
    pc.type('\r');
    if (!pc.waitExit({ timeoutMs: 20000 })) fail('did not end');
  },
  expect: ['T:EXIT K_CON 0 0'],
});
scenarios.push({
  name: 'con-chars',
  script: 'K_CON CHARS\nexit 0\n',
  async run({ pc, fail }) {
    if (!waitS(pc, 'T:ROLE CHARS')) return fail('no start');
    pc.type('abcd');
    if (!waitS(pc, 'T:READY')) return fail('no READY: ' + pc.serial);
    pc.type('ef');
    pc.run(300);
    pc.type('g{F1}');
    if (!pc.waitExit({ timeoutMs: 20000 })) fail('did not end');
  },
  expect: ['T:C01 61', 'T:C08 62', 'T:C07 63', 'T:C06 64', 'T:C0B 00', 'T:C0C 67', 'T:FKEY 00 3b', 'T:EXIT K_CON 0 0'],
  expectScreen: ['a*'],
});
scenarios.push({
  name: 'con-ctrlc',
  script: 'K_CON CTRLC\nK_CON HANDLER\nK_CON BREAK\nexit 0\n',
  async run({ pc, fail }) {
    if (!waitS(pc, 'T:ROLE CTRLC')) return fail('no start');
    pc.type('ab{CTRL+C}');
    if (!waitS(pc, 'T:EXIT K_CON 0 1')) return fail('^C did not end the program: ' + pc.serial);
    if (!waitS(pc, 'T:ROLE HANDLER')) return fail('no HANDLER');
    pc.type('{CTRL+C}');
    pc.run(300);
    pc.type('x');
    if (!waitS(pc, 'T:HANDLER count=1 char=78')) return fail('handler: ' + pc.serial);
    if (!waitS(pc, 'T:LOOPING')) return fail('no BREAK loop: ' + pc.serial);
    pc.type('{CTRL+C}');
    if (!pc.until(() => (pc.serial.match(/T:EXIT K_CON 0 1/g) || []).length >= 2, { timeoutMs: 20000 })) return fail('BREAK ON ^C: ' + pc.serial);
    if (!pc.waitExit({ timeoutMs: 20000 })) fail('did not end');
  },
  expect: ['T:BREAKON 1'],
  expectScreen: ['ab^C'],
});
scenarios.push({
  name: 'con-ctrlbreak',
  script: 'K_CON CTRLC\nexit 0\n',
  async run({ pc, fail }) {
    if (!waitS(pc, 'T:ROLE CTRLC')) return fail('no start');
    pc.type('xy{CTRL+SCROLL}');   // Ctrl+ScrollLock: Ctrl-Break (the emulator sends E1 for Pause even with Ctrl)
    if (!waitS(pc, 'T:EXIT K_CON 0 1')) return fail('Ctrl-Break did not end the program: ' + pc.serial + '\n' + pc.screen());
    pc.waitExit({ timeoutMs: 20000 });
  },
  expectScreen: ['xy^C'],
});
scenarios.push({
  name: 'con-cooked',
  script: 'K_CON COOKED\nexit 0\n',
  async run({ pc, fail }) {
    if (!waitS(pc, 'T:ROLE COOKED')) return fail('no start');
    pc.type('hello{BS}p\r');
    if (!waitS(pc, 'T:COOKED')) return fail('no cooked');
    pc.type('abc');
    if (!pc.waitExit({ timeoutMs: 20000 })) fail('did not end: ' + pc.serial);
  },
  expect: ['T:COOKED 7 [hellpRN]', 'T:RAW 3 [abc]'],
});
scenarios.push({
  name: 'crit-notready',
  script: 'K_CON CRIT\nexit 0\n',
  expect: ['T:CRIT fail cf=1 ax=3 count=1 ah=1a al=0 di=2', 'T:CRIT exterr 83', 'T:CRIT dev block',
           'T:CRIT df ax=ffff count=3', 'T:EXIT K_CON 0 2'],
  rejectScreen: ['NOT REACHED'],
});

// ----------------------------------------------------------- floppies
import fs from 'node:fs';
import path from 'node:path';
function floppyImage(dir, build, name, label, files, wp) {
  const d = path.join(dir, name);
  fs.mkdirSync(d, { recursive: true });
  const list = [];
  for (const [n, c] of Object.entries(files)) {
    fs.writeFileSync(path.join(d, n), c);
    list.push({ src: path.relative(path.resolve(dir, '../../../..'), path.join(d, n)), dst: n });
  }
  return build({ format: 'fd1440', label, date: '1988-06-17 12:00:00', files: list });
}
function lcgPattern(n, id) {
  const out = Buffer.alloc(n);
  let x = (Math.imul(id, 2654435761) + 12345) >>> 0;
  for (let i = 0; i < n; i++) { x = (Math.imul(x, 1103515245) + 12345) >>> 0; out[i] = x >>> 24; }
  return out;
}
const fileSize = (i) => ((i * 3571) % 17000) + (i % 3) * 511;

scenarios.push({
  name: 'floppy-fill',
  script: 'pause\nK_FLOP FILL\nexit 0\n',
  floppy: (dir, build) => floppyImage(dir, build, 'fd', 'FILLTEST', { 'README.TXT': 'fill me\r\n' }),
  fsckFloppy: true,
  expect: ['T:PASS floppy-fill', 'T:EXIT K_FLOP 0 0'],
  timeout: 120,
  async after({ dir, fail, reader }) {
    const img = fs.readFileSync(path.join(dir, 'after-fd.img'));
    const r = reader(img);
    for (let i = 0; i < 20; i++) {
      const name = `F${String(i).padStart(3, '0')}.DAT`;
      let data;
      try { data = r.readFile(r.lookup(name)); } catch (e) { fail(`${name}: ${e.message}`); continue; }
      if (!data) { fail('FatReader has no readFile'); break; }
      if (!Buffer.from(data).equals(lcgPattern(fileSize(i), i))) fail(`${name} differs on the image`);
    }
  },
});
scenarios.push({
  name: 'floppy-swap',
  script: 'pause\nK_FLOP SWAP\nK_FLOP PHANTOM\nexit 0\n',
  floppy: (dir, build) => floppyImage(dir, build, 'fd1', 'DISKONE', { 'ONE.TXT': 'one\r\n' }),
  async run({ pc, fail, dir }) {
    if (!waitS(pc, 'T:SWAP')) return fail('no SWAP: ' + pc.serial);
    const { build } = await import('../../disk/mkimage.mjs');
    const img2 = floppyImage(dir, (m) => build(m, path.resolve(dir, '../../../..')).img, 'fd2', 'DISKTWO', { 'TWO.TXT': 'two\r\n', 'ONE.DAT': 'x' });
    pc.machine.ejectFloppy();
    pc.run(100);
    pc.machine.insertFloppy(new Uint8Array(img2));
    pc.run(100);
    pc.type('k');
    if (!waitS(pc, 'T:PASS floppy-swap')) return fail('swap: ' + pc.serial);
    if (!waitS(pc, 'T:ROLE PHANTOM')) return fail('no PHANTOM');
    if (!pc.waitText('Insert diskette for drive B: and press any key when ready', { timeoutMs: 20000 })) return fail('no B: prompt\n' + pc.screen());
    pc.type('k');
    if (!pc.waitText('Insert diskette for drive A: and press any key when ready', { timeoutMs: 20000 })) return fail('no A: prompt\n' + pc.screen());
    pc.type('k');
    if (!pc.waitExit({ timeoutMs: 20000 })) fail('did not end');
  },
  expect: ['T:LABEL1 DISKONE', 'T:LABEL2 DISKTWO', 'T:PASS floppy-phantom'],
});
scenarios.push({
  name: 'crit-writeprotect',
  script: 'pause\nK_CON CRITWP\nexit 0\n',
  floppy: (dir, build) => floppyImage(dir, build, 'fd', 'WPDISK', { 'README.TXT': 'read only disk\r\n' }),
  fdWriteProtected: true,
  expect: ['T:WP cf=1 ax=5 count=1 ah=1d al=0 di=0', 'T:WP read cf=0 [read only disk'],
});

// ----------------------------------------------------- CONFIG.SYS
scenarios.push({
  name: 'config-errors',
  // the lines MS-DOS 4.00 prints for these CONFIG.SYS errors
  config: 'FILES=20\nFOO=1\nFILES=3\nSTACKS=1,1\nDEVICE=A:\\NOSUCH.SYS\nBUFFERS=200\nREM hello\nFILESX=9\nLASTDRIVE=Z\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  script: 'exit 0\n',
  expectScreen: [
    '\nUnrecognized command in CONFIG.SYS\nError in CONFIG.SYS line 2\n\nBad command or parameters - 3\nError in CONFIG.SYS line 3\n' +
    '\nInvalid STACK parameters\nError in CONFIG.SYS line 4\n\nBad or missing A:\\NOSUCH.SYS\nError in CONFIG.SYS line 5\n' +
    '\nBad command or parameters -\nError in CONFIG.SYS line 6\n\nUnrecognized command in CONFIG.SYS\nError in CONFIG.SYS line 8\n',
  ],
});
scenarios.push({
  name: 'config-drivers',
  config: 'REM drivers and INSTALL\nDEVICE=\\T\\HIMEM.SYS\nDEVICE=\\T\\K_RAMD.SYS\nDEVICE=\\T\\K_UCON.SYS\n' +
          'FILES=30\nBUFFERS=20\nLASTDRIVE=H\nINSTALL=\\T\\K_TSR.EXE hello\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  script: 'K_DRVT 30 20 H\nK_XMS\nK_XMS\nmem\nexit 0\n',
  expect: ['T:PASS drivers', 'T:TSR installed argc=2 HELLO', 'T:MARKS DDDFXBL', 'T:PASS xms', 'T:EXIT K_XMS 0 0'],
  expectScreen: ['HIMEM: ARM-DOS XMS Driver', 'RAM disk installed as drive D:', 'UPPERCASE CONSOLE DRIVER INSTALLED',
                 'LOWERCASE THROUGH UCON'],
});
scenarios.push({
  name: 'config-noshell',
  config: 'SHELL=\\NOSUCH.EXE\n',
  async run({ pc, fail }) {
    if (!pc.waitText('Bad or missing Command Interpreter', { timeoutMs: 20000 })) fail('no message\n' + pc.screen());
    pc.run(500);
  },
});
scenarios.push({
  name: 'config-default-shell',
  config: null,                       // no CONFIG.SYS: \COMMAND.COM /P on the boot drive
  extraCopies: { 'COMMAND.COM': 'build/ktest/TSHELL.EXE' },
  script: 'exit 0\n',
  expect: ['T:SHELL start argc=2', 'T:ARGV1 /P', 'T:SHELLEXIT'],
});
scenarios.push({
  name: 'memmap',
  script: 'K_MAP\nexit 0\n',
  report: ({ pc }) => { const m = /T:MAP free (\d+) largest (\d+)/.exec(pc.serial); return m ? `free ${m[1]} largest ${m[2]}` : ''; },
});
scenarios.push({
  name: 'idle',
  config: 'SHELL=\\T\\TSHELL.EXE\n',
  async run({ pc, fail, log }) {
    if (!pc.waitText('T>', { timeoutMs: 20000 })) return fail('no prompt');
    pc.run(500);
    const m = pc.machine, h0 = m.haltedNs, t0 = m.timeNs();
    pc.run(3000);
    const ratio = (m.haltedNs - h0) / (m.timeNs() - t0);
    log.push(`halted ${(ratio * 100).toFixed(1)}% while waiting at the prompt`);
    if (ratio < 0.95) fail(`CPU halted only ${(ratio * 100).toFixed(1)}% of the time at the prompt`);
    pc.type('echo still alive\r');
    if (!pc.waitText('still alive', { timeoutMs: 5000 })) fail('typing after idle');
    pc.type('exit 0\r');
    pc.waitExit({ timeoutMs: 5000 });
  },
  report: ({ log }) => log.find(l => l.startsWith('halted')) || '',
});
scenarios.push({ name: 'fcb', script: 'K_FCB\nexit 0\n', expect: ['T:PASS fcb'] });
scenarios.push({ name: 'misc', script: 'K_MISC\nexit 0\n', expect: ['T:PASS misc'] });
scenarios.push({
  name: 'redirect',
  script: 'K_HELLO ONE two > OUT.TXT\nK_CON STDIN < OUT.TXT\necho abc > SMALL.TXT\necho def >> SMALL.TXT\nK_CON STDIN01 < SMALL.TXT\nK_CON STDIN < SMALL.TXT\nexit 0\n',
  expect: ['T:PASS hello', /T:STDIN \d+ \[Hello from printf \(newlib -> AH=40h\)\|\|Hello via AH=09h\|\|AH=02h ok\|\|\]/,
           'T:STDIN01 [abc||def||]', 'T:STDIN 10 [abc||def||]'],
  rejectScreen: ['Hello via AH=09h'],
});
scenarios.push({ name: 'stress-random', config: 'FILES=20\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  script: 'K_STRESS RANDOM 7 600\nK_STRESS RANDOM 99 600\nexit 0\n',
  expect: [/T:PASS stress-random[\s\S]*T:PASS stress-random/], timeout: 300 });
scenarios.push({ name: 'stress-fillhd', script: 'K_STRESS FILLHD\nexit 0\n', expect: ['T:PASS stress-fillhd'], timeout: 600 });

scenarios.push({
  name: 'crit-retry',
  script: 'K_CON CRITRETRY\nexit 0\n',
  async run({ pc, fail, dir }) {
    if (!waitS(pc, 'T:I24WAIT')) return fail('no INT 24h: ' + pc.serial);
    const { build } = await import('../../disk/mkimage.mjs');
    const img = floppyImage(dir, (m) => build(m, path.resolve(dir, '../../../..')).img, 'fd', 'LATE', { 'README.TXT': 'late disk\r\n' });
    pc.machine.insertFloppy(new Uint8Array(img));
    if (!waitS(pc, 'T:RETRYOK cf=0 retries>0=1')) return fail('retry: ' + pc.serial);
    pc.waitExit({ timeoutMs: 20000 });
  },
});
scenarios.push({
  name: 'root-abort',
  script: 'once\ni24abort\necho x > A:\\X.TXT\necho not reached\nexit 1\n',
  expect: ['T:I24 AX=1A00 DI=0002', 'T:SECONDRUN'],
  rejectScreen: ['not reached'],
});
scenarios.push({
  name: 'arena-trashed',
  script: 'K_EXEC TRASH\nexit 0\n',
  async run({ pc, fail }) {
    if (!pc.waitText('Cannot load COMMAND, system halted', { timeoutMs: 20000 })) fail('no halt message\n' + pc.screen());
    if (!pc.hasText('Memory allocation error')) fail('no "Memory allocation error"');
    pc.run(500);
  },
});
// the real COMMAND.COM (skipped when command/ has not built it)
if (fs.existsSync(new URL('../../build/COMMAND.COM', import.meta.url))) scenarios.push({
  name: 'command-com',
  config: 'DEVICE=\\T\\HIMEM.SYS\n',
  withCommand: true,
  async run({ pc, fail, log }) {
    if (!pc.waitText('Enter new date', { timeoutMs: 30000 })) return fail('no date prompt\n' + pc.screen());
    pc.type('\r');
    if (!pc.waitText('Enter new time', { timeoutMs: 10000 })) return fail('no time prompt');
    pc.type('\r');
    if (!pc.waitText('C>', { timeoutMs: 10000 })) return fail('no prompt\n' + pc.screen());
    pc.type('\\T\\K_MAP\r');
    if (!pc.waitSerial('T:MAP free', { timeoutMs: 20000 })) return fail('K_MAP did not run\n' + pc.screen());
    const m = /T:MAP free (\d+) largest (\d+)/.exec(pc.serial);
    log.push(`with COMMAND.COM and HIMEM.SYS: largest free block ${m[2]} bytes (K_MAP itself uses ${24576 + 48} of it)`);
    pc.type('dir \\T\r');
    if (!pc.waitText('File(s)', { timeoutMs: 10000 })) fail('dir\n' + pc.screen());
  },
  report: ({ log }) => log.find(l => l.startsWith('with')) || '',
});

// two hard disks (the web page's C: and D:): the second one's first DOS partition is D:, and
// COMMAND.COM reads, writes and lists it; the BIOS counts two hard disks
if (fs.existsSync(new URL('../../build/COMMAND.COM', import.meta.url))) scenarios.push({
  name: 'two-disks',
  withCommand: true,
  config: '',
  hd2: (dir, build) => {
    fs.writeFileSync(path.join(dir, 'second.txt'), 'from the second disk\r\n');
    return build({ format: 'hd', sizeMB: 16, heads: 16, sectorsPerTrack: 63, label: 'SECOND', date: '1988-06-17 12:00:00',
                   files: [{ src: path.relative(path.resolve(dir, '../../../..'), path.join(dir, 'second.txt')), dst: 'HELLO.TXT' }] });
  },
  async run({ pc, fail }) {
    if (!pc.waitText('Enter new date', { timeoutMs: 30000 })) return fail('no date prompt\n' + pc.screen());
    pc.type('\r'); pc.waitText('Enter new time', { timeoutMs: 10000 }); pc.type('\r');
    if (!pc.waitText('C>', { timeoutMs: 10000 })) return fail('no prompt\n' + pc.screen());
    if (pc.machine.cpu.m8[0x475] !== 2) fail(`BIOS hard disk count ${pc.machine.cpu.m8[0x475]}, want 2`);
    const cmd = (c, want) => { pc.type(c + '\r'); if (!pc.waitText(want, { timeoutMs: 15000 })) fail(`${c}: no "${want}"\n` + pc.screen()); };
    cmd('dir d:\\', 'Volume in drive D is SECOND');
    if (!/HELLO +TXT/.test(pc.screen())) fail('dir d: does not list HELLO.TXT\n' + pc.screen());
    cmd('type d:\\hello.txt', 'from the second disk');
    cmd('copy \\T\\K_HELLO.EXE d:\\copied.exe', '1 File(s) copied');
    cmd('dir c:\\', 'Volume in drive C is KTEST');
    const { FatReader } = await import('../../disk/mkimage.mjs');
    const has = (img, name) => { try { return !!new FatReader(Buffer.from(img)).lookup(name); } catch { return false; } };
    if (!has(pc.machine.ata1.img, 'COPIED.EXE')) fail('COPIED.EXE is not on the second disk');
    if (has(pc.machine.ata.img, 'COPIED.EXE')) fail('COPIED.EXE landed on C:');
  },
});

scenarios.push({
  name: 'tracks',
  script: 'pause\nK_MISC TRACKS\nexit 0\n',
  floppy: (dir, build) => floppyImage(dir, build, 'fd', 'TRACKS', { 'README.TXT': 'x' }),
  fsckFloppy: true,
  expect: ['T:PASS misc'],
});
scenarios.push({
  name: 'config-misc',
  config: 'BREAK=ON\nSWITCHES=/K\nCOUNTRY=049\nFCBS=8,2\nCOUNTRY=001,437\nFILES=5\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  script: 'K_CON BREAKQ\nexit 0\n',
  expect: ['T:BREAKQ 1'],
  // COUNTRY= reads COUNTRY.SYS - there is none on this disk, even for 001
  expectScreen: ['\nBad or missing \\COUNTRY.SYS\nError in CONFIG.SYS line 3\n\nBad or missing \\COUNTRY.SYS\nError in CONFIG.SYS line 5\n\nBad command or parameters - 5\nError in CONFIG.SYS line 6\n'],
});
scenarios.push({
  // national language support: COUNTRY=049 from \DOS\COUNTRY.SYS (the
  // ARM-DOS fallback for the default path), AH=38h/65h/66h, file names in the
  // country's upper case, and no code page / country changes without NLSFUNC
  name: 'nls',
  config: 'COUNTRY=049\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  extraCopies: { 'DOS\\COUNTRY.SYS': 'build/COUNTRY.SYS' },
  dirs: ['DOS'],
  script: 'K_NLS BOOT49\nexit 0\n',
  expect: ['T:PASS nls-boot49'],
});
scenarios.push({
  // ... and with NLSFUNC: CHCP's 6602h, AH=38h set, other countries' information;
  // a bad COUNTRY= still gives NLSFUNC its path (DOS 4 sets it before reading)
  name: 'nls-nlsfunc',
  config: 'COUNTRY=049\nINSTALL=\\T\\NLSFUNC.EXE\nCOUNTRY=999,,\\DOS\\COUNTRY.SYS\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  extraCopies: { 'DOS\\COUNTRY.SYS': 'build/COUNTRY.SYS', 'T\\NLSFUNC.EXE': 'build/NLSFUNC.EXE' },
  dirs: ['DOS'],
  script: 'K_NLS BOOT49F\nK_NLS NLSFUNC\nexit 0\n',
  expect: ['T:PASS nls-boot49f', 'T:PASS nls-nlsfunc'],
  expectScreen: ['\nInvalid country code or code page\nError in CONFIG.SYS line 3\n'],
});

scenarios.push({
  name: 'int28-tsr',
  config: 'INSTALL=\\T\\K_TSR.EXE IDLE\nSHELL=\\T\\TSHELL.EXE\n',
  async run({ pc, fail }) {
    if (!pc.waitText('T>', { timeoutMs: 20000 })) return fail('no prompt');
    pc.run(300);
    pc.type('K_CON STDIN < C:\\IDLE.TXT\r');
    if (!pc.waitSerial('T:STDIN', { timeoutMs: 20000 })) return fail('no STDIN line: ' + pc.serial);
    pc.type('exit 0\r');
    pc.waitExit({ timeoutMs: 5000 });
  },
  expect: ['T:TSR installed argc=2 IDLE', 'T:STDIN 4 [idle]'],
});

scenarios.push({
  name: 'printer',
  config: 'SHELL=\\T\\TSHELL.EXE\n',
  async run({ pc, fail }) {
    if (!pc.waitText('T>', { timeoutMs: 20000 })) return fail('no prompt');
    pc.type('K_CON PRN\r');
    if (!pc.waitSerial('T:PRN done', { timeoutMs: 20000 })) return fail('no PRN run: ' + pc.serial);
    pc.run(200);
    pc.type('{CTRL+P}');
    pc.run(200);
    pc.type('echo echoed\r');
    pc.run(500);
    pc.type('{CTRL+P}');
    pc.run(200);
    pc.type('echo not echoed\r');
    pc.run(500);
    pc.type('exit 0\r');
    pc.waitExit({ timeoutMs: 5000 });
  },
  expect: ['T:PRN done ext-open cf=1 ax=3'],
  expectPrinter: ['Printer via fopen\r\n', 'echoed'],
  rejectPrinter: ['not echoed'],
});

scenarios.push({
  name: 'con-ctrls',
  script: 'K_CON SPEW\nexit 0\n',
  async run({ pc, fail, log }) {
    if (!pc.waitSerial('T:SPEW 100', { timeoutMs: 20000 })) return fail('no output');
    pc.type('{CTRL+S}');
    pc.run(300);
    const a = (pc.serial.match(/T:SPEW \d+/g) || []).length;
    pc.run(1000);
    const b = (pc.serial.match(/T:SPEW \d+/g) || []).length;
    if (b !== a) fail(`output did not stop on ^S (${a} -> ${b})`);
    if (pc.serial.includes('T:SPEW done')) fail('finished before ^S took effect');
    log.push(`paused after ${a} progress lines`);
    pc.type('x');
    if (!pc.waitSerial('T:SPEW done', { timeoutMs: 30000 })) fail('did not resume after a key');
    pc.waitExit({ timeoutMs: 10000 });
  },
  report: ({ log }) => log.find(l => l.startsWith('paused')) || '',
});

scenarios.push({
  name: 'con-exit23',
  script: 'K_CON EXIT23\nK_HELLO ONE two\nexit 0\n',
  async run({ pc, fail }) {
    if (!waitS(pc, 'T:ROLE EXIT23')) return fail('no start');
    pc.type('{CTRL+C}');
    if (!waitS(pc, 'T:EXIT K_CON 42 0')) return fail('exit from INT 23h: ' + pc.serial);
    if (!waitS(pc, 'T:PASS hello')) return fail('system unstable afterwards: ' + pc.serial);
    pc.waitExit({ timeoutMs: 10000 });
  },
  rejectScreen: ['NOT REACHED'],
});

scenarios.push({ name: 'round2', script: 'K_ROUND2\nexit 0\n', expect: ['T:PASS round2'] });
scenarios.push({
  name: 'redirect-both',
  script: 'echo abc > SMALL.TXT\nK_CON STDIN01 <SMALL.TXT>ECHO.TXT\nK_CON STDIN < ECHO.TXT\nexit 0\n',
  expect: ['T:STDIN01 [abc||]', 'T:STDIN 6 [abc||\x1a]'],
});

// a disk with an extended partition: C: as usual, then D: (formatted, FAT12)
// and E: (unformatted: F6h, as FDISK leaves it)
function withLogicalDrives(hd, build) {
  const SPC = 1008;                              // 16 heads x 63 sectors
  const prim = hd.length / 512;                  // the whole old disk (C: fills it)
  const l1 = build({ format: 'hd', sizeMB: 4, label: 'LOGICAL1', date: '1988-06-17 12:00:00', files: [] });
  const l1part = l1.subarray(63 * 512);           // the FAT volume of that image
  const l1sec = l1part.length / 512, l2sec = 3 * SPC - 63;
  const ext = prim, ebr2 = ext + 63 + l1sec;      // cylinder aligned (4 MB = whole cylinders)
  const total = ebr2 + 63 + l2sec;
  const out = Buffer.alloc(total * 512);
  hd.copy(out, 0);
  const entry = (buf, off, type, start, size) => {
    buf[off] = 0; buf[off + 4] = type;
    buf.writeUInt32LE(start, off + 8); buf.writeUInt32LE(size, off + 12);
  };
  entry(out, 0x1BE + 16, 0x05, ext, total - ext);
  const e1 = ext * 512;
  entry(out, e1 + 0x1BE, 0x01, 63, l1sec);
  entry(out, e1 + 0x1BE + 16, 0x05, ebr2 - ext, 63 + l2sec);
  out[e1 + 510] = 0x55; out[e1 + 511] = 0xAA;
  l1part.copy(out, (ext + 63) * 512);
  out.writeUInt32LE(ext + 63, (ext + 63) * 512 + 0x1C);     // hidden sectors of the moved volume
  const e2 = ebr2 * 512;
  entry(out, e2 + 0x1BE, 0x01, 63, l2sec);
  out[e2 + 510] = 0x55; out[e2 + 511] = 0xAA;
  out.fill(0xF6, (ebr2 + 63) * 512);
  return out;
}
scenarios.push({
  name: 'round3',
  config: 'LASTDRIVE=H\nSHELL=\\T\\TSHELL.EXE \\T\\S.TXT\n',
  transformHd: withLogicalDrives,
  script: 'K_ROUND3\nexit 0\n',
  expect: ['T:PASS round3'],
});
scenarios.push({
  name: 'round4',
  script: 'K_ROUND4\nexit 0\n',
  async run({ pc, fail }) {
    if (!pc.waitSerial('T:IN24', { timeoutMs: 20000 })) return fail('no INT 24h: ' + pc.serial);
    pc.run(100);
    pc.type('{CTRL+C}');
    pc.waitExit({ timeoutMs: 20000 });
  },
  expect: ['T:PASS round4'],
});
