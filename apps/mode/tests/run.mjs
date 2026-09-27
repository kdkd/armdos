#!/usr/bin/env node
// apps/mode/tests/run.mjs - MODE.COM tests ("make mode-test").
//
//   node apps/mode/tests/run.mjs [--keep] [--verbose]
//
// Runs MODE commands under the kernel's test shell with standard output
// redirected to files, then reads the files back from the disk image and
// compares them byte for byte with what the genuine MS-DOS 4.00 MODE printed
// for the same command lines under DOSBox-X (captured in the comments as
// "real"). Where the machines differ (the ARM-PC's COM2 is its modem card, a
// ready printer and no monochrome adapter) the expectation says so. The last group
// boots the Hercules card option (MONO works there, the colour modes do not).
//
// One difference is the test shell's, not MODE's: COMMAND.COM leaves the
// blank before ">" in the command tail, and MODE 4.00 shows such trailing
// blanks after the last operand in its "Invalid parameter - X " messages;
// the test shell strips them, so the expectations here have none.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { boot } from '../../../emu/testkit.mjs';
import { build as buildImage, FatReader } from '../../../disk/mkimage.mjs';
import { rescheck } from '../../ansi/tests/rescheck.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../../..');
const B = (p) => path.join(ROOT, 'build', p);
const OUT = B('mode-test');
fs.mkdirSync(OUT, { recursive: true });
const args = process.argv.slice(2);
const verbose = args.includes('--verbose');

let failures = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) failures++; return ok; };

const CP = 'Code page operation not supported on this device\r\n';
const STATUS_CON = '\r\nStatus for device CON:\r\n----------------------\r\n';
const statusLpt = (n) => `\r\nStatus for device LPT${n}:\r\n-----------------------\r\n`;
const STATUS_ALL = statusLpt(1) + 'LPT1: not rerouted\r\nRETRY=NONE\r\n' + CP +
  statusLpt(2) + 'LPT2: not rerouted\r\n' + statusLpt(3) + 'LPT3: not rerouted\r\n' + STATUS_CON + CP;
const E = (msg) => `\r\n${msg}\r\n`;

// [command tail, expected stdout, note]
const plain = [
  ['CON', STATUS_CON + CP, 'real'],
  ['', STATUS_ALL, 'real (DOSBox-X has LPT1 only too)'],
  ['/STATUS', STATUS_ALL, 'real'],
  ['CON RATE=20 DELAY=2', '', 'real'],
  ['CON RATE=20', E('RATE and DELAY must be specified together'), 'real'],
  ['CON RATE=40 DELAY=2', E('Invalid parameter - RATE=40'), 'real'],
  ['CON DELAY=5 RATE=1', E('Invalid parameter - DELAY=5'), 'real'],
  ['CON RATE=0 DELAY=1', E('Invalid parameter - RATE=0'), 'real'],
  ['XYZ', E('Invalid parameter - XYZ'), 'real'],
  ['CON LINES=25', E('ANSI.SYS must be installed to perform requested function'), 'real'],
  ['CON COLS=80', '', 'real'],
  ['CON: COLS=80', '', 'real'],
  ['CO80 X', E('Invalid parameter - X'), 'real'],
  ['MONO', E('Function not supported on this computer - MONO'), 'ARM-PC: no monochrome mode (real VGA MODE says the same without mono support)'],
  ['80,R', E('      Unable to shift screen right'), 'real'],
  [',L', E('      Unable to shift screen left'), 'real (,R)'],
  ['80,25', E('ANSI.SYS must be installed to perform requested function'), 'real'],
  ['CO80,43', E('ANSI.SYS must be installed to perform requested function'), 'real'],
  ['COM1:9600,N,8,1', E('COM1: 9600,n,8,1,-'), 'real'],
  ['COM1:96', E('COM1: 9600,e,7,1,-'), 'real'],
  ['COM1:110', E('COM1: 110,e,7,2,-'), 'real'],
  ['COM1:2400 , N', E('COM1: 2400,n,7,1,-'), 'real'],
  ['COM1:19200', E('Function not supported on this computer - 19200'), 'real'],
  ['COM1:9600,X,8,1', E('Invalid parameter - X'), 'real'],
  ['COM1:9601', E('Invalid parameter - 9601'), 'real'],
  ['COM1:9600,N,9', E('Invalid parameter - 9'), 'real'],
  ['COM1:9600,N,8,3', E('Invalid parameter - 3'), 'real'],
  ['COM1:9600,N,8,1,P,X', E('Invalid number of parameters'), 'real'],
  ['COM1:,N', E('Invalid parameter -  '), 'real'],
  ['COM5:9600', E('Invalid parameter - COM5'), 'real'],
  ['COM2:1200,O,7,2', E('COM2: 1200,o,7,2,-'), 'real (COM2 = the internal modem card)'],
  ['COM3:1200', E('Illegal device name - COM3'), 'real on a PC without COM3'],
  ['COM1', '\r\nStatus for device COM1:\r\n-----------------------\r\nRETRY=NONE\r\n', 'real'],
  ['COM1 /STATUS', '\r\nStatus for device COM1:\r\n-----------------------\r\nRETRY=NONE\r\n', 'real'],
  ['COM1 BAUD=2400', E('COM1: 2400,e,7,1,-'), 'real'],
  ['COM1 BAUD=2400 PARITY=O DATA=7 STOP=1', E('COM1: 2400,o,7,1,-'), 'real'],
  ['COM1 RETRY=R', E('Baud rate required'), 'real'],
  ['LPT1:', E('LPT1: not rerouted') + E('No retry on parallel printer time-out'), 'real'],
  ['LPT1', E('LPT1: not rerouted') + E('No retry on parallel printer time-out'), 'real'],
  ['LPT1:132,8', E('LPT1: not rerouted') + E('LPT1: set for 132') + E('Printer lines per inch set') +
    E('No retry on parallel printer time-out'), 'ARM-PC printer is ready (real, printer off: "Printer error")'],
  ['LPT1: 80', E('LPT1: not rerouted') + E('LPT1: set for 80') + E('No retry on parallel printer time-out'), 'ARM-PC printer ready'],
  ['LPT1:132,6,X', E('Invalid parameter - X'), 'real'],
  ['LPT1 COLS=100', E('Invalid parameter - COLS=100'), 'real'],
  ['LPT2:', E('LPT2: not rerouted'), 'real'],
  ['LPT4:', E('Invalid parameter - LPT4'), 'real'],
  ['PRN', CP, 'real'],
  ['LPT1 /STATUS', statusLpt(1) + 'LPT1: not rerouted\r\nRETRY=NONE\r\n' + CP, 'real'],
  ['LPT1:=COM3', E('Illegal device name - COM3'), 'real'],
  ['CON CP /STATUS', CP, 'real'],
  ['CON CP SELECT=850', 'Device error during select\r\n', 'real (re-checked 2026-09-25: without DISPLAY.SYS the select fails as a device error)'],
  ['CON CP PREPARE=((850) C:\\DOS\\EGA.CPI)', CP, 'real'],
  ['CON CODEPAGE', CP, 'real'],
  ['LPT1 CP SELECT=850', 'Device error during select\r\n', 'real'],
  ['/X', E('Invalid switch - /X'), 'real'],
  ['CON COLS=', E('Parameter format not correct - COLS='), 'real'],
  ['CON COLS=81', E('Invalid parameter - COLS=81'), 'real'],
  ['XYZ:', E('Invalid parameter - XYZ'), 'real'],
];

const resident = [
  ['COM1:1200,N,8,1,B', E('Resident portion of MODE loaded') + E('COM1: 1200,n,8,1,b'), 'real'],
  ['COM1', '\r\nStatus for device COM1:\r\n-----------------------\r\nRETRY=B\r\n', 'real'],
  ['COM1 BAUD=2400 PARITY=O DATA=7 STOP=1', E('COM1: 2400,o,7,1,b'), 'real (keyword form keeps the retry)'],
  ['COM1:110', E('COM1: 110,e,7,2,-'), 'real (positional form resets it)'],
  ['COM1:9600,E,7,2,P', E('COM1: 9600,e,7,2,p'), 'real (without the resident message: already loaded)'],
  ['LPT1:=COM1', E('LPT1: rerouted to COM1:'), 'real'],
  ['LPT1 /STATUS', statusLpt(1) + 'LPT1: rerouted to COM1:\r\nRETRY=NONE\r\n' + CP, 'status of a redirected printer'],
  ['LPT1:,,P', E('LPT1: not rerouted') + E('Infinite retry on parallel printer time-out'), 'real'],
  ['LPT1 RETRY=E', E('LPT1: not rerouted') + E('Infinite retry on parallel printer time-out'), 'real'],
  ['LPT1 RETRY=NONE', E('LPT1: not rerouted') + E('No retry on parallel printer time-out'), 'real'],
  ['LPT1', E('LPT1: not rerouted') + E('No retry on parallel printer time-out'), 'real'],
  ['LPT1=COM1', E('LPT1: rerouted to COM1:'), 'the colon is optional'],
];

const withAnsi = [
  ['CON', STATUS_CON + 'COLUMNS=80\r\nLINES=25\r\n' + CP, 'real'],
  ['CON LINES=50', '', 'real'],
  ['CON', STATUS_CON + 'COLUMNS=80\r\nLINES=50\r\n' + CP, 'real'],
  ['CO80', '', 'real'],
  ['CON', STATUS_CON + 'COLUMNS=80\r\nLINES=25\r\n' + CP, 'real (a mode set goes back to 25 lines)'],
  ['40', '', 'real'],
  ['CON', STATUS_CON + 'COLUMNS=40\r\nLINES=25\r\n' + CP, 'real'],
  ['80', '', 'real'],
  ['CON COLS=40', '', 'real'],
  ['CON', STATUS_CON + 'COLUMNS=40\r\nLINES=25\r\n' + CP, 'real'],
  ['CON LINES=43 COLS=80', '', 'real (LINES=43)'],
  ['CON', STATUS_CON + 'COLUMNS=80\r\nLINES=43\r\n' + CP, ''],
  ['CON LINES=30', E('Invalid parameter - LINES=30'), 'real'],
  ['80,50', '', 'old form with lines'],
  ['CON', STATUS_CON + 'COLUMNS=80\r\nLINES=50\r\n' + CP, ''],
  ['CON LINES=25 COLS=80', '', 'real'],
  ['CON', STATUS_CON + 'COLUMNS=80\r\nLINES=25\r\n' + CP, ''],
];

function makeImage(name, config, script) {
  const dir = path.join(OUT, name);
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });
  const files = [
    { src: 'build/IO.SYS', attr: 'HSR', first: 1 },
    { src: 'build/ARMDOS.SYS', attr: 'HSR', first: 2 },
    { src: 'build/MODE.COM', dst: 'DOS\\' },
    { src: 'build/ANSI.SYS', dst: 'DOS\\' },
    { src: 'build/EGA.CPI', dst: 'DOS\\' },
    { src: 'build/ktest/TSHELL.EXE', dst: 'T\\' },
  ];
  const put = (dst, text) => {
    const host = path.join(dir, dst.replace(/[\\/]/g, '_'));
    fs.writeFileSync(host, text.replace(/\n/g, '\r\n'));
    files.push({ src: path.relative(ROOT, host), dst });
  };
  put('CONFIG.SYS', `${config}SHELL=C:\\T\\TSHELL.EXE C:\\T\\S.TXT\n`);
  put('T\\S.TXT', script);
  const { img } = buildImage({
    format: 'hd', sizeMB: 32, heads: 16, sectorsPerTrack: 63, label: 'MODETEST',
    date: '1988-06-17 12:00:00', boot: { src: 'build/bootsect.bin' }, files, dirs: ['DOS', 'T', 'R'],
  }, ROOT);
  if (args.includes('--keep')) fs.writeFileSync(path.join(OUT, name + '.img'), img);
  fs.rmSync(dir, { recursive: true, force: true });
  return img;
}

async function runGroup(name, config, cases, extra = '', bootOpts = {}) {
  console.log(`== ${name}`);
  const script = cases.map(([tail], i) => `C:\\DOS\\MODE.COM ${tail} > C:\\R\\O${i}.TXT`).join('\n') +
    `\n${extra}echo T-DONE\nhalt\n`;
  const pc = await boot({ rom: B('rom.bin'), hd: makeImage(name, config, script), ...bootOpts });
  if (!check(pc.until(() => pc.serial.includes('T-DONE') || pc.hasText('T-DONE'), { timeoutMs: 120000 }), `${name}: all commands ran`)) {
    console.log(pc.screen());
    return pc;
  }
  pc.run(200);
  const fat = new FatReader(pc.machine.ata.img);
  cases.forEach(([tail, want, note], i) => {
    let got;
    try { got = Buffer.from(fat.readFile(fat.lookup(`R\\O${i}.TXT`))).toString('latin1'); } catch (e) { got = `<${e.message}>`; }
    const ok = got === want;
    check(ok, `MODE ${tail}${note ? `   [${note}]` : ''}` + (ok && !verbose ? '' : `\n       got  ${JSON.stringify(got)}\n       want ${JSON.stringify(want)}`));
  });
  return pc;
}

{
  console.log('== resident');
  const { from, to, bad } = rescheck(B('obj/MODE/MODE.elf'), 'copyregs', 'mode_res_end');
  check(bad.length === 0, `resident portion (${to - from} bytes) refers to nothing beyond mode_res_end` +
    (bad.length ? '\n       ' + bad.join('\n       ') : ''));
}

await runGroup('plain', '', plain);
{
  // the resident portion, and a printer redirected to COM1 for real
  const pc = await runGroup('resident', '', resident, 'mem\nC:\\DOS\\MODE.COM LPT1:=COM1\necho PRINTED-THROUGH-COM1 > PRN\nmem\n');
  check(pc.serial.includes('PRINTED-THROUGH-COM1'), 'LPT1:=COM1: text written to PRN comes out of COM1');
  const mems = pc.serial.split(/\r?\n/).filter((l) => l.startsWith('T:MEM')).map((l) => +l.split(' ')[1]);
  check(mems.length === 2 && mems[0] === mems[1], `no further memory used by later MODE commands (${mems.join(', ')})`);
}
{
  const pc = await runGroup('ansi', 'DEVICE=C:\\DOS\\ANSI.SYS\n', withAnsi);
  check(pc.cpu.m8[0x484] === 24 && pc.cpu.m8[0x449] === 3, 'back in 80x25');
}

{
  // the modem card taken out (Machine({ modem: false }), the page's "open the box"): no COM2
  const noModem = [
    ['COM2:1200,O,7,2', E('Illegal device name - COM2'), 'real on a PC with one serial port'],
    ['COM1:9600,N,8,1', E('COM1: 9600,n,8,1,-'), 'real'],
  ];
  const pc = await runGroup('no modem', '', noModem, '', { modem: false });
  const m8 = pc.cpu.m8;
  check((m8[0x402] | m8[0x403] << 8) === 0 && ((m8[0x410] | m8[0x411] << 8) >> 9 & 7) === 1, 'no modem: BDA COM2 = 0, one serial port in the equipment word');
}

{
  // the Hercules card + mono monitor option (emu/dev/hercules.mjs): MONO works, the
  // colour modes are refused the way MODE 4.00 refuses them on an MDA-only PC
  const herc = [
    ['CO80', E('Function not supported on this computer - CO80'), 'real MODE on an MDA/HGC-only PC'],
    ['BW40', E('Function not supported on this computer - BW40'), 'real MODE on an MDA/HGC-only PC'],
    ['80', E('Function not supported on this computer - 80'), ''],
    ['MONO', '', 'real'],
  ];
  const pc = await runGroup('hercules', '', herc, '', { video: 'hercules' });
  const m8 = pc.cpu.m8;
  check(m8[0x449] === 7 && (m8[0x410] & 0x30) === 0x30 && m8[0x460] === 0x0C && m8[0x461] === 0x0B,
    `Hercules: mode 7, equipment 80x25 mono, cursor 0B0Ch (${m8[0x449]}, ${m8[0x410].toString(16)}, ${m8[0x461].toString(16)}${m8[0x460].toString(16)})`);
}

console.log(failures ? `\n${failures} failure(s)` : '\nall MODE tests passed');
process.exit(failures ? 1 : 0);
