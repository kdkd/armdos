#!/usr/bin/env node
// "Open the box" (Machine.setHardware, web/js/openbox.js): the cards, SIMMs and clock jumper.
// Part 1 (no images needed): the bus with cards pulled - ports float (FFh), RAM above the SIMMs
// reads FFh and ignores writes, no mouse = the 8042 answers FEh, AdLib-only = OPL at 388h but no
// DSP, the defaults are today's machine. Part 2 (build/rom.bin + build/hd.img, skipped without
// them): ARM-DOS boots with each option, POST's memory count and configuration box, the BIOS data
// area, HIMEM, and how the programs cope with the missing hardware.
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { Machine, RAM_SIZE } from '../../machine.mjs';

let fails = 0, passes = 0;
const ok = (name, cond, extra = '') => { if (cond) passes++; else { fails++; console.log(`FAIL ${name}${extra ? '\n' + extra : ''}`); } };

// ------------------------------------------------------------------ part 1: the bus
{
  const m = new Machine({ jit: false, rtcBaseMs: 0 });
  ok('default: 16 MB, RAM up to 16 MB', m.ramEnd === RAM_SIZE && m.hw.ram === 16);
  ok('default: SB16 DSP, OPL, MPU, COM2, CD ports answer', m.in8(0x22E) !== 0xFF && m.in8(0x331) !== 0xFF && m.in8(0x2FD) !== 0xFF && m.in8(0x177) !== 0xFF);
  m.cpu.st32(0xFFFFF0, 0x12345678, 0);
  ok('default: the top of RAM is RAM', m.cpu.m32[0xFFFFF0 >>> 2] === 0x12345678);
}
for (const [mb, end] of [[1, 0x160000], [2, 0x260000], [4, 0x460000], [8, 0x860000], [16, 0x1000000]]) {
  const m = new Machine({ jit: false, rtcBaseMs: 0, ram: mb });
  ok(`${mb} MB: RAM ends at ${end.toString(16)}h`, m.ramEnd === end);
  if (end < RAM_SIZE) {
    m.cpu.st32(end - 4, 0x11223344, 0); m.cpu.st32(end, 0x55667788, 0); m.cpu.st8(end + 0x1234, 0x42, 0);
    ok(`${mb} MB: last word below the top holds data`, m.cpu.m32[(end - 4) >>> 2] === 0x11223344);
    ok(`${mb} MB: above the SIMMs reads FFh, writes vanish`, m.cpu.m32[end >>> 2] === -1 && m.cpu.m8[end + 0x1234] === 0xFF && (m.cpu.ld32(end + 0x100, 0) >>> 0) === 0xFFFFFFFF);
    m.dmaToMemory(end - 2, new Uint8Array([1, 2, 3, 4]));
    ok(`${mb} MB: DMA past the top lands only in RAM`, m.cpu.m8[end - 1] === 2 && m.cpu.m8[end] === 0xFF);
  }
}
{
  const m = new Machine({ jit: false, rtcBaseMs: 0 });
  m.setHardware({ ram: 4 }); m.powerCycle();
  ok('setHardware + powerCycle resizes RAM', m.ramEnd === 0x460000 && m.cpu.m8[0x460000] === 0xFF && m.cpu.m8[0x45FFFF] === 0);
  m.setHardware({ ram: 16 }); m.powerCycle();
  ok('...and back to 16 MB', m.ramEnd === RAM_SIZE && m.cpu.m8[0x460000] === 0);
}
{
  const m = new Machine({ jit: false, rtcBaseMs: 0, sound: 'none', modem: false, cdrom: false, mouse: false });
  ok('no sound card: 220h-22Fh, 388h, 330h float', [0x22A, 0x22C, 0x22E, 0x388, 0x389, 0x330, 0x331].every((p) => m.in8(p) === 0xFF));
  ok('no modem: COM2 floats', [0x2F8, 0x2FD, 0x2FE, 0x2FF].every((p) => m.in8(p) === 0xFF));
  ok('no CD-ROM: secondary IDE floats', [0x170, 0x174, 0x175, 0x177, 0x376].every((p) => m.in8(p) === 0xFF));
  ok('COM1, LPT1, primary IDE, VGA still there', m.in8(0x3FD) !== 0xFF && m.in8(0x379) === 0xDF && m.in8(0x3DA) !== 0xFF);
  m.out8(0x64, 0xD4); m.out8(0x60, 0xF4); m.runFor(1);
  ok('no mouse: the 8042 answers FEh (aux)', (m.in8(0x64) & 0x21) === 0x21 && m.in8(0x60) === 0xFE);
  m.mouseMove(10, 10); m.runFor(20);
  ok('no mouse: moving the host mouse sends nothing', (m.in8(0x64) & 1) === 0);
}
{
  const m = new Machine({ jit: false, rtcBaseMs: 0, sound: 'adlib' });
  ok('AdLib only: no DSP at 220h, no MPU', m.in8(0x22E) === 0xFF && m.in8(0x331) === 0xFF);
  const w = (r, v) => { m.out8(0x388, r); m.out8(0x389, v); };
  w(4, 0x60); w(4, 0x80); const s1 = m.in8(0x388); w(2, 0xFF); w(4, 0x21); m.runFor(1); const s2 = m.in8(0x388);
  ok('AdLib only: the OPL at 388h passes the AdLib timer test', (s1 & 0xE0) === 0 && (s2 & 0xE0) === 0xC0, `${s1.toString(16)} ${s2.toString(16)}`);
  ok('AdLib only: FM at full level (no mixer on an AdLib)', m.sb.mixer[0x34] === 0xF8 && m.sb.mixer[0x30] === 0xF8);
}
{
  const m = new Machine({ jit: false, rtcBaseMs: 0, mhz: 33 });
  ok('clock jumper: 33 MHz on port F1h', m.in8(0xF1) === 33);
  m.setHardware({ mhz: 133 });
  ok('clock jumper: 133 MHz', m.in8(0xF1) === 133 && m.turboMhz === 133);
}

// ------------------------------------------------------------------ part 2: ARM-DOS on each configuration
const ROOT = fileURLToPath(new URL('../../../', import.meta.url));
const ROM = ROOT + 'build/rom.bin', HD = ROOT + 'build/hd.img';
if (!existsSync(ROM) || !existsSync(HD)) {
  console.log(`hardware: ${passes} passed, ${fails} failed (boot tests skipped: no build/rom.bin or build/hd.img)`);
  process.exit(fails ? 1 : 0);
}
const { boot } = await import('../../testkit.mjs');
const { readFileSync } = await import('node:fs');
const rom = readFileSync(ROM), hd = readFileSync(HD);

/** Boot with options; returns { pc, banner, box, dos } (the POST screen, the config box, the DOS screen). */
async function bootWith(opts, name) {
  const pc = await boot({ rom, hd: new Uint8Array(hd), ...opts });
  const mono = opts.video === 'hercules';
  const lines = () => (mono ? pc.machine.cpu.m8 : null, pc.screen());
  pc.waitText('Detecting PS/2 mouse', { timeoutMs: 30000, stepMs: 2 }); pc.run(40);
  const banner = lines();
  pc.waitText('Parallel Port(s)', { timeoutMs: 30000, stepMs: 2 }); pc.run(10);
  const box = lines();
  const up = pc.waitText('C:\\>', { timeoutMs: 60000 });
  ok(`${name}: boots to C:\\>`, up, pc.screen());
  return { pc, banner, box, m8: pc.machine.cpu.m8 };
}
const run = (pc, cmd, ms = 3000) => { pc.type(cmd + '\r'); pc.waitIdle({ timeoutMs: ms + 20000 }); pc.run(ms); return pc.screen(); };
const bda16 = (m8, o) => m8[0x400 + o] | (m8[0x401 + o] << 8);
const cmos = (pc, r) => pc.machine.cmos.ram ? pc.machine.cmos.ram[r] : null;

{ // the factory machine
  const { pc, banner, box, m8 } = await bootWith({}, 'factory');
  ok('factory: memory test 16000K', banner.includes('Memory Test :  16000K OK'), banner);
  ok('factory: CD-ROM and mouse detected', banner.includes('Detecting IDE secondary master ... ARM-PC CD-ROM DRIVE') && banner.includes('Detecting PS/2 mouse ... Installed'), banner);
  for (const s of ['Main Processor   : ARM926EJ-S', 'Math Coprocessor : VFP9-S', 'Ext. Memory Size : 15360 KB', '100 MHz, RISC', 'Display Type     : VGA/EGA',
    'Pointing Device  : PS/2', 'CD-ROM Drive     : ATAPI, 2nd IDE', 'Sound Card       : SB16 220h', 'Serial Port(s)   : 3F8,2F8', 'MIDI Interface   : MPU-401 330h', 'Parallel Port(s) : 378'])
    ok(`factory box: ${s}`, box.includes(s), box);
  ok('factory: equipment word 5425h (2 serial, game adapter, mouse, 80x25 colour, floppy)', bda16(m8, 0x10) === 0x5425, bda16(m8, 0x10).toString(16));
  ok('factory: BDA COM1 3F8h, COM2 2F8h, 640 KB', bda16(m8, 0) === 0x3F8 && bda16(m8, 2) === 0x2F8 && bda16(m8, 0x13) === 640);
  ok('factory: CMOS 30h/31h = 15360', pc.machine.cmos.ram && (pc.machine.cmos.ram[0x30] | pc.machine.cmos.ram[0x31] << 8) === 15360);
  ok('factory: HIMEM 15296K', pc.screen().includes('15296K of extended memory available') || pc.debug.includes('15296K'), pc.screen());
  const mem = run(pc, 'MEM');
  ok('factory: MEM 15663104 bytes extended', mem.includes('15663104 bytes total extended memory'), mem);
}
{ // the stripped-down machine: 1 MB, 12 MHz, no sound, no modem, no CD-ROM, no mouse, no game port
  const { pc, banner, box, m8 } = await bootWith({ ram: 1, mhz: 12, sound: 'none', modem: false, cdrom: false, mouse: false, joystick: false }, 'stripped');
  ok('stripped: 12 MHz, memory test 1024K', banner.includes('CPU at 12 MHz') && banner.includes('Memory Test :   1024K OK'), banner);
  ok('stripped: no CD-ROM, no mouse at POST', banner.includes('Detecting IDE secondary master ... None') && banner.includes('Detecting PS/2 mouse ... None'), banner);
  for (const s of ['Ext. Memory Size : 384 KB', '12 MHz, RISC', 'Pointing Device  : None', 'CD-ROM Drive     : None', 'Sound Card       : None', 'Serial Port(s)   : 3F8 ', 'MIDI Interface   : None'])
    ok(`stripped box: ${s}`, box.includes(s), box);
  ok('stripped: equipment word 4221h (1 serial port, no mouse, no game adapter)', bda16(m8, 0x10) === 0x4221, bda16(m8, 0x10).toString(16));
  ok('stripped: BDA COM2 = 0', bda16(m8, 2) === 0);
  ok('stripped: CMOS 30h/31h = 384', (pc.machine.cmos.ram[0x30] | pc.machine.cmos.ram[0x31] << 8) === 384);
  const s0 = pc.screen();
  ok('stripped: HIMEM counts 320K', s0.includes('320K of extended memory available'), s0);
  ok('stripped: ARMCD.SYS finds no drive, ARMCDEX gives up', s0.includes('No CD-ROM drive found on the secondary IDE channel') && s0.includes('No valid CDROM device drivers selected'), s0);
  const mem = run(pc, 'MEM');
  ok('stripped: MEM 327680 bytes extended', mem.includes('327680 bytes total extended memory'), mem);
  let s = run(pc, 'SBMIX', 1500);
  ok('stripped: SBMIX: Sound Blaster not found', s.includes('Sound Blaster not found'), s);
  s = run(pc, 'SBTEST', 3000);
  ok('stripped: SBTEST: no SB, no FM', s.includes('No Sound Blaster found at 220h.') && s.includes('No AdLib-compatible FM synthesizer found at 388h.'), s);
  s = run(pc, 'MOUSE', 1500);
  ok('stripped: MOUSE: Mouse not found', s.includes('Mouse not found') && s.includes('Driver not installed'), s);
  s = run(pc, 'CDPLAY', 1500);
  ok('stripped: CDPLAY: extensions not installed', s.includes('CD-ROM extensions not installed'), s);
  s = run(pc, 'PLAYMIDI \\MIDI\\FURELISE', 1500);
  ok('stripped: PLAYMIDI: no MPU-401', s.includes('MPU-401 not found at 330h.'), s);
  s = run(pc, 'CD \\GAMES\\QUAKE', 300); s = run(pc, 'QUAKE', 6000);
  ok('stripped: QUAKE says there is not enough memory', /not enough extended memory|megs of memory available/.test(s), s);
  ok('stripped: no faults', pc.faults.length === 0, JSON.stringify(pc.faults));
}
{ // AdLib only, 4 MB, 33 MHz
  const { pc, banner, box } = await bootWith({ ram: 4, mhz: 33, sound: 'adlib' }, 'adlib');
  ok('adlib: memory test 4096K at 33 MHz', banner.includes('Memory Test :   4096K OK') && banner.includes('CPU at 33 MHz'), banner);
  for (const s of ['Ext. Memory Size : 3456 KB', 'Sound Card       : AdLib 388h', 'MIDI Interface   : None', 'Serial Port(s)   : 3F8,2F8'])
    ok(`adlib box: ${s}`, box.includes(s), box);
  ok('adlib: HIMEM 3392K', pc.screen().includes('3392K of extended memory available'), pc.screen());
  let s = run(pc, 'SBTEST', 6000);
  ok('adlib: SBTEST finds the OPL3 at 388h but no SB', s.includes('No Sound Blaster found at 220h.') && s.includes('OPL3 (YMF262) FM synthesizer found at 388h'), s);
  run(pc, 'CD \\GAMES\\DOOM', 300); s = run(pc, 'DOOM', 6000);
  ok('adlib: DOOM on 4 MB says it has too little memory for its zone (it needs 4 MiB of XMS)', s.includes('Unable to allocate') && s.includes('RAM for zone'), s);
}
{ // SB16 without the MIDI daughterboard, Hercules, 8 MB: DOOM
  const { pc, box } = await bootWith({ ram: 8, mpu: { present: false }, video: 'hercules' }, 'hercules');
  const mono = () => { const m8 = pc.machine.cpu.m8, r = []; for (let y = 0; y < 25; y++) { let t = ''; for (let x = 0; x < 80; x++) t += String.fromCharCode(m8[0xB0000 + (y * 80 + x) * 2] || 32); r.push(t.trimEnd()); } return r.join('\n'); };
  ok('hercules: box via the mono card', box.includes('Monochrome (Hercules)') || mono().includes('Ext. Memory Size : 7552 KB'), box + mono());
}
{ // DOOM with no sound card, 8 MB: PC speaker
  const { pc, box } = await bootWith({ ram: 8, sound: 'none' }, 'doom-nosound');
  ok('doom-nosound: box 7552 KB', box.includes('Ext. Memory Size : 7552 KB'), box);
  run(pc, 'CD \\GAMES\\DOOM', 300);
  pc.type('DOOM\r');
  ok('doom-nosound: DOOM falls back to the PC speaker', pc.waitText('I_InitSound: PC speaker', { timeoutMs: 30000, stepMs: 5 }), pc.screen());
  ok('doom-nosound: ...and starts (mode 13h, 7 MiB zone from 7488K of XMS)', pc.until(() => pc.machine.cpu.m8[0x449] === 0x13, { timeoutMs: 30000 }), pc.screen());
  ok('doom-nosound: no faults', pc.faults.length === 0, JSON.stringify(pc.faults));
}
{ // DOOM on an AdLib alone, 8 MB: PC speaker effects, OPL music (as DMX did)
  const { pc } = await bootWith({ ram: 8, sound: 'adlib' }, 'doom-adlib');
  run(pc, 'CD \\GAMES\\DOOM', 300);
  let fm = 0; const o = pc.machine.out8.bind(pc.machine);
  pc.machine.out8 = (p, v) => { if (p === 0x389 || p === 0x38B) fm++; o(p, v); };
  pc.type('DOOM\r');
  ok('doom-adlib: PC speaker + AdLib music', pc.waitText('I_InitMusic: AdLib (FM) at 388h', { timeoutMs: 30000, stepMs: 5 }) && pc.hasText('I_InitSound: PC speaker'), pc.screen());
  ok('doom-adlib: starts', pc.until(() => pc.machine.cpu.m8[0x449] === 0x13, { timeoutMs: 30000 }));
  pc.run(4000);
  ok('doom-adlib: the title music plays on the OPL', fm > 200, `${fm} OPL data writes`);
}
{ // 2 MB
  const { box } = await bootWith({ ram: 2 }, '2mb');
  ok('2 MB: ext 1408 KB', box.includes('Ext. Memory Size : 1408 KB'), box);
}
console.log(`hardware: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
