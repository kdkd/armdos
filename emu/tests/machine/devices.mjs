#!/usr/bin/env node
// Device-level tests: drive the I/O ports directly (no guest code) and check
// the behaviour documented in ARCH.md §4/§6 and emu/README.md.
import { Machine } from '../../machine.mjs';

let fails = 0, passes = 0;
const eq = (name, got, want) => {
  const ok = JSON.stringify(got) === JSON.stringify(want);
  if (ok) passes++; else { fails++; console.log(`FAIL ${name}: got ${JSON.stringify(got)} want ${JSON.stringify(want)}`); }
};
// a machine whose CPU just sleeps (no ROM code runs)
function mk(o = {}) {
  const m = new Machine({ jit: false, rtcBaseMs: new Date(2026, 5, 15, 13, 45, 30).getTime(), ...o });
  m.cpu.halted = 1; m.cpu.i = 1;
  return m;
}
const drain = (m) => { const out = []; for (let k = 0; k < 64; k++) { m.runFor(1); if (!(m.in8(0x64) & 1)) { if (m.kbc.q.length === 0) break; continue; } out.push(m.in8(0x60)); } return out; };

// ---------------- PIC
{
  const m = mk();
  m.out8(0x21, 0x00); m.out8(0xA1, 0x00);
  m.pic.raise(3); m.pic.raise(8); m.pic.raise(0);
  eq('pic line', m.cpu.irqLine, 1);
  eq('pic ack order 1', m.in8(0x20), 0);
  m.out8(0x20, 0x20);
  eq('pic ack order 2 (IRQ8 before IRQ3)', m.in8(0x20), 8);
  eq('pic line blocked while 8 in service', m.cpu.irqLine, 0);
  m.out8(0xA0, 0x20); m.out8(0x20, 0x20);
  eq('pic ack order 3', m.in8(0x20), 3);
  m.out8(0x20, 0x20);
  eq('pic empty -> FF', m.in8(0x20), 0xFF);
  m.out8(0x21, 0x02); m.pic.raise(1);
  eq('pic masked', [m.cpu.irqLine, m.in8(0x20)], [0, 0xFF]);
  eq('pic mask readback', [m.in8(0x21), m.in8(0xA1)], [0x02, 0x00]);
  // ICW sequence does not clobber the mask
  m.out8(0x20, 0x11); m.out8(0x21, 0x08); m.out8(0x21, 0x04); m.out8(0x21, 0x01);
  eq('pic ICW ignored', m.in8(0x21), 0x02);
  m.out8(0x21, 0x00);
  eq('pic unmask delivers', m.in8(0x20), 1);
  m.out8(0x20, 0x61);   // specific EOI IRQ1
  eq('pic specific EOI', m.pic.isr, 0);
}
// ---------------- PIT
{
  const m = mk();
  const raised = []; const orig = m.pic.raise.bind(m.pic);
  m.pic.raise = (n) => { if (n === 0) raised.push(m.timeNs()); orig(n); };
  m.out8(0x43, 0x34); m.out8(0x40, 0x00); m.out8(0x40, 0x00);  // mode 2, 65536
  m.runFor(1000);
  eq('pit 18.2 Hz ticks in 1 s', raised.length, 18);
  const period = (raised[5] - raised[4]) / 1e6;
  eq('pit period ms', Math.round(period * 100) / 100, 54.93);
  raised.length = 0;
  m.out8(0x43, 0x36); m.out8(0x40, 1193 & 0xFF); m.out8(0x40, 1193 >> 8);   // mode 3, 1 kHz
  m.runFor(100);
  eq('pit 1 kHz ticks in 100 ms', raised.length, 100);
  // counter reads decrease; latch freezes the value
  m.out8(0x43, 0x34); m.out8(0x40, 0x10); m.out8(0x40, 0x27);   // 10000
  m.runFor(1);
  m.out8(0x43, 0x00);
  m.runFor(2);
  const lo = m.in8(0x40), hi = m.in8(0x40); const latched = lo | (hi << 8);
  m.out8(0x43, 0x00); const lo2 = m.in8(0x40), hi2 = m.in8(0x40);
  eq('pit latched value is from latch time', latched > (lo2 | (hi2 << 8)), true);
  eq('pit latch range', latched <= 10000 && latched > 0, true);
  // read-back status: mode 2, rw 3, output high
  m.out8(0x43, 0xE2);   // read-back status only (bit5=1 count not latched), ch0
  eq('pit read-back status', m.in8(0x40) & 0x3F, 0x34);
  // mode 0 fires once
  raised.length = 0;
  m.out8(0x43, 0x30); m.out8(0x40, 0xA9); m.out8(0x40, 0x04);    // 1193 ticks = 1 ms
  m.runFor(10);
  eq('pit mode 0 one-shot', raised.length, 1);
  // lobyte-only access
  m.out8(0x43, 0x14); m.out8(0x40, 100);
  eq('pit lobyte mode', m.pit.ch[0].reload, 100);
  // channel 2 + speaker
  const spk = [];
  const m2 = mk({ onSpeaker: (on, f) => spk.push([on, Math.round(f)]) });
  m2.out8(0x43, 0xB6); m2.out8(0x42, 0xA9); m2.out8(0x42, 0x04);
  m2.out8(0x61, 0x03);
  m2.out8(0x61, 0x00);
  eq('speaker on 1000 Hz then off', spk, [[true, 1000], [false, 0]]);
  // port 61 bit 5 follows OUT2 (square wave toggles)
  m2.out8(0x61, 0x01);
  let seen = new Set(); for (let k = 0; k < 40; k++) { m2.runFor(0.05); seen.add(m2.in8(0x61) & 0x20); }
  eq('port 61 bit 5 toggles', seen.size, 2);
}
// ---------------- keyboard / mouse
{
  const m = mk();
  m.keyDown('KeyA'); m.keyUp('KeyA');
  eq('kbd A make/break', drain(m), [0x1E, 0x9E]);
  m.keyDown('ArrowUp'); m.keyUp('ArrowUp');
  eq('kbd E0 arrow', drain(m), [0xE0, 0x48, 0xE0, 0xC8]);
  m.keyDown('Pause'); m.keyUp('Pause');
  eq('kbd Pause', drain(m), [0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5]);
  m.keyDown('PrintScreen'); m.keyUp('PrintScreen');
  eq('kbd PrintScreen', drain(m), [0xE0, 0x2A, 0xE0, 0x37, 0xE0, 0xB7, 0xE0, 0xAA]);
  m.keyDown('NumpadEnter');
  eq('kbd keypad enter', drain(m), [0xE0, 0x1C]);
  eq('kbd unknown code ignored', m.keyDown('Fn'), false);
  const m3 = mk(); const got = [];
  m3.typeText('aB{F1}{CTRL+C}');
  for (let k = 0; k < 400; k++) { m3.runFor(1); if (m3.in8(0x64) & 1) got.push(m3.in8(0x60)); }
  eq('typeText sequence', got, [0x1E, 0x9E, 0x2A, 0x30, 0xB0, 0xAA, 0x3B, 0xBB, 0x1D, 0x2E, 0xAE, 0x9D]);
  // IRQ1 raised for keyboard bytes
  const m4 = mk(); m4.out8(0x21, 0xFD);
  m4.keyDown('KeyZ'); m4.runFor(1);
  eq('kbd IRQ1', [m4.cpu.irqLine, m4.in8(0x20)], [1, 1]);
  eq('status: OBF, not aux', m4.in8(0x64) & 0x21, 0x01);
  // controller commands
  m.out8(0x64, 0xAA); eq('8042 self test', drain(m), [0x55]);
  m.out8(0x64, 0x20); const cb = drain(m); eq('8042 command byte has IRQ1+IRQ12 enabled', cb[0] & 3, 3);
  m.out8(0x60, 0xF2); eq('kbd identify', drain(m), [0xFA, 0xAB, 0x41]);
  m.out8(0x60, 0xED); m.out8(0x60, 0x07); eq('kbd LEDs acked', drain(m), [0xFA, 0xFA]);
  // mouse
  m.out8(0x64, 0xD4); m.out8(0x60, 0xF2); eq('mouse id', drain(m), [0xFA, 0x00]);
  m.out8(0x64, 0xD4); m.out8(0x60, 0xF4); eq('mouse enable ack', drain(m), [0xFA]);
  m.mouseMove(10, 5); m.mouseButtons(1);
  m.runFor(20);
  const st = m.in8(0x64);
  eq('mouse byte flagged aux', st & 0x21, 0x21);
  eq('mouse packet', drain(m), [0x09 | 0x20, 10, 0xFB]);    // dy=+5 down -> -5
  m.mouseMove(-300, 0);
  const pk = []; for (let k = 0; k < 60; k++) { m.runFor(2); if (m.in8(0x64) & 1) pk.push(m.in8(0x60)); }
  eq('mouse large move split in packets', pk.length >= 6 && pk[1] === 0x01 && (pk[0] & 0x10) !== 0, true);
}
// ---------------- CMOS / RTC
{
  const writes = [];
  const m = mk({ onCmosWrite: (ram, i) => writes.push(i) });
  const rd = (i) => { m.out8(0x70, i); return m.in8(0x71); };
  eq('rtc time BCD', [rd(4), rd(2), rd(0)], [0x13, 0x45, 0x30]);
  eq('rtc date BCD', [rd(0x32), rd(9), rd(8), rd(7), rd(6)], [0x20, 0x26, 0x06, 0x15, 0x02]);
  eq('rtc regs A/B/D', [rd(0x0A) & 0x80, rd(0x0B), rd(0x0D)], [0, 0x02, 0x80]);
  m.runFor(2000);
  eq('rtc advances with emulated time', rd(0), 0x32);
  m.out8(0x70, 4); m.out8(0x71, 0x09);        // set hour 09
  eq('rtc set hour', [rd(4), rd(2)], [0x09, 0x45]);
  m.out8(0x70, 0x0B); m.out8(0x71, 0x06);     // binary mode
  eq('rtc binary mode', rd(4), 9);
  m.out8(0x70, 0x0B); m.out8(0x71, 0x00);     // 12-hour BCD
  m.out8(0x70, 4); m.out8(0x71, 0x82);        // 2 PM
  m.out8(0x70, 0x0B); m.out8(0x71, 0x02);
  eq('rtc 12h write -> 24h read', rd(4), 0x14);
  m.out8(0x70, 0x50); m.out8(0x71, 0xAB);
  eq('cmos RAM + hook', [rd(0x50), writes.at(-1)], [0xAB, 0x50]);
  eq('cmos default base memory', rd(0x15) | (rd(0x16) << 8), 640);
  // periodic interrupt at 1024 Hz on IRQ8
  const m2 = mk(); let n8 = 0; const o = m2.pic.raise.bind(m2.pic); m2.pic.raise = (n) => { if (n === 8) n8++; o(n); };
  m2.out8(0x70, 0x0B); m2.out8(0x71, 0x42);
  m2.runFor(100);
  eq('rtc periodic IRQ8 ~1024 Hz', Math.abs(n8 - 102) <= 1, true);
  m2.out8(0x70, 0x0C); eq('rtc reg C flags', m2.in8(0x71) & 0xC0, 0xC0);
}
// ---------------- ATA
{
  const hd = new Uint8Array(1024 * 1024); hd[512] = 0x77;
  const m = mk({ hd });
  eq('ata status ready', m.in8(0x1F7) & 0xC9, 0x40);
  m.out8(0x1F6, 0xE0); m.out8(0x1F2, 1); m.out8(0x1F3, 1); m.out8(0x1F4, 0); m.out8(0x1F5, 0); m.out8(0x1F7, 0x20);
  eq('ata DRQ after read', m.in8(0x1F7) & 0x08, 0x08);
  const w = m.read(0x100001F0, 4);
  eq('ata 32-bit data read', w & 0xFF, 0x77);
  for (let k = 0; k < 127; k++) m.read(0x100001F0, 4);
  eq('ata done after 512 bytes', m.in8(0x1F7) & 0x08, 0);
  m.out8(0x1F3, 0xFF); m.out8(0x1F4, 0xFF); m.out8(0x1F5, 0xFF); m.out8(0x1F6, 0xEF); m.out8(0x1F7, 0x20);
  eq('ata out of range -> ERR/IDNF', [m.in8(0x1F7) & 0x01, m.in8(0x1F1)], [1, 0x10]);
  m.out8(0x1F7, 0x99);
  eq('ata unknown command -> ABRT', [m.in8(0x1F7) & 0x01, m.in8(0x1F1)], [1, 0x04]);
  m.out8(0x1F6, 0xB0);
  eq('ata slave absent', m.in8(0x1F7), 0);
  const m2 = mk();
  eq('ata no disk', m2.in8(0x1F7), 0);
  // IRQ14 when nIEN clear
  m.out8(0x1F6, 0xE0); m.out8(0xA1, 0x00); m.out8(0x21, 0x00); m.out8(0x3F6, 0x00);
  m.out8(0x1F2, 1); m.out8(0x1F3, 0); m.out8(0x1F4, 0); m.out8(0x1F5, 0); m.out8(0x1F7, 0x20);
  eq('ata IRQ14', m.in8(0x20), 14);
}
// ---------------- FDC
{
  const fd = new Uint8Array(1474560); fd[512 * 3] = 0x42;
  const act = [];
  const m = mk({ fd, fdWriteProtected: true, onDiskActivity: (...a) => act.push(a) });
  eq('fdc media / status', [m.in8(0x302), m.in8(0x307)], [4, 0x70]);
  eq('fdc changed bit cleared by read', m.in8(0x307), 0x50);
  const dma = (a) => { for (let k = 0; k < 4; k++) m.out8(0x300 + k, (a >>> (8 * k)) & 0xFF); };
  dma(0x5000); m.out8(0x304, 1); m.out8(0x305, 3); m.out8(0x306, 0); m.out8(0x307, 1);
  eq('fdc DMA read', [m.cpu.m8[0x5000], m.in8(0x307) & 1], [0x42, 0]);
  m.out8(0x307, 2);
  eq('fdc write-protected -> error', m.in8(0x307) & 1, 1);
  eq('fdc activity callback', act[0].slice(0, 4), [0, 3, 1, false]);
  m.ejectFloppy();
  eq('fdc eject (error bit from the last command stays)', [m.in8(0x302), m.in8(0x307)], [0, 0x21]);
  m.insertFloppy(new Uint8Array(737280));
  eq('fdc insert 720K', [m.in8(0x302), m.in8(0x307) & 0x60], [3, 0x60]);
}
// ---------------- UART, E9, system board
{
  const ser = [], dbg = [];
  const m = mk({ onSerial: (b) => ser.push(b), onDebug: (b) => dbg.push(b) });
  m.out8(0x3F8, 0x41); m.out8(0xE9, 0x42);
  eq('uart/e9 out', [ser, dbg], [[0x41], [0x42]]);
  eq('uart LSR THRE, no data', m.in8(0x3FD) & 0x61, 0x60);
  m.serialInput('x');
  eq('uart rx', [m.in8(0x3FD) & 1, m.in8(0x3F8), m.in8(0x3FD) & 1], [1, 0x78, 0]);
  eq('e9 read', m.in8(0xE9), 0xE9);
  eq('board id / MHz / turbo', [m.in8(0xF0), m.in8(0xF1), m.in8(0xF2)], [0x41, 100, 1]);
  m.setTurbo(false);
  eq('slow mode MHz', m.in8(0xF1), 12);
  eq('unassigned port reads FF', m.in8(0x3E8), 0xFF);
  let code = null; const m2 = mk({ onExit: (c) => { code = c; } });
  m2.out8(0xF4, 7);
  eq('exit port', [code, m2.stopped, m2.exitCode], [7, true, 7]);
}
// ---------------- LPT1 (printer)
{
  const out = [];
  const m = mk({ onPrint: (b) => out.push(b) });
  eq('lpt status ready/selected/not busy', m.in8(0x379), 0xDF);
  m.out8(0x378, 0x41);
  eq('lpt data latch read back', m.in8(0x378), 0x41);
  m.out8(0x37A, 0x0D); m.out8(0x37A, 0x0C);          // strobe as INT 17h does
  m.out8(0x378, 0x42); m.out8(0x37A, 0x0D);
  m.out8(0x37A, 0x0D);                                // bit 0 stays high: no second byte
  m.out8(0x37A, 0x0C); m.out8(0x378, 0x43);           // falling edge + new data: nothing
  eq('lpt strobe rising edge delivers the latch', out, [0x41, 0x42]);
  eq('lpt control read back', m.in8(0x37A), 0xEC);
  m.write(0x10000378, 2, 0x010A);                     // 16-bit store: data 0x0A, then 0x379 (ignored)
  m.out8(0x37A, 0x0C); m.out8(0x37A, 0x01);
  eq('lpt second strobe after re-arm', out, [0x41, 0x42, 0x0A]);
}
// ---------------- VGA registers
{
  const m = mk();
  m.out8(0x3C8, 5); m.out8(0x3C9, 1); m.out8(0x3C9, 2); m.out8(0x3C9, 3); m.out8(0x3C9, 4);
  m.out8(0x3C7, 5);
  eq('dac read back + autoincrement', [m.in8(0x3C9), m.in8(0x3C9), m.in8(0x3C9), m.in8(0x3C9)], [1, 2, 3, 4]);
  m.out8(0x3C7, 20);
  eq('default dac entry 20 (grey)', [m.in8(0x3C9), m.in8(0x3C9), m.in8(0x3C9)], [14, 14, 14]);
  m.in8(0x3DA); m.out8(0x3C0, 0x10); m.out8(0x3C0, 0x04);
  eq('attr mode control write (blink off)', m.vga.blinkEnabled, false);
  m.in8(0x3DA); m.out8(0x3C0, 0x30);   // index with PAS
  eq('attr flip-flop reset by 3DA', m.vga.attrIndex, 0x10);
  let vr = 0, tot = 0;
  for (let k = 0; k < 1000; k++) { m.runFor(0.1); tot++; if (m.in8(0x3DA) & 8) vr++; }
  eq('vretrace ~10% of the time', vr > 60 && vr < 140, true);
  m.out8(0x3E0, 0x13);
  eq('mode register', m.in8(0x3E0), 0x13);
  m.out8(0x3D4, 0x0E); m.out8(0x3D5, 0x01);
  eq('crtc read back', m.in8(0x3D5), 0x01);
}
// ---------------- memory map through the bus
{
  const m = mk();
  eq('font RAM word access', (m.write(0x11000010, 4, 0x11223344), m.read(0x11000010, 4)), 0x11223344);
  eq('unmapped read', m.read(0x12000000, 4), -1);
  eq('unmapped write', m.write(0x10010000, 1, 0), false);
  eq('hole reads FF', m.cpu.m8[0xC8000], 0xFF);
}
console.log(`devices: ${passes} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
