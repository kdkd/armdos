#!/usr/bin/env node
// apps/turbo/tests/run.mjs - TURBO.COM and the unlocked clock ("make turbo-test"):
// ON/OFF/MAX/RESTORE through port F2h, the note TURBO MAX leaves in the BIOS data
// area's intra-application area, SECOND.BAT's bracket (its PAUSE screen, then the
// clock put back even when the program fails), a reset relocking the clock, and
// the real-time driver's clock controller.
import { session, Checker } from '../../dosutil/tests/harness.mjs';
import { RealtimeDriver } from '../../../emu/machine.mjs';

const t = new Checker('TURBO');
const s = await session({ name: 'turbo', command: true, dirs: ['ELBOW', 'ELBOW\\DEMOS', 'ELBOW\\DEMOS\\SECOND'],
  files: [{ src: 'build/TURBO.COM', dst: 'DOS\\' }, { src: 'apps/secondreality/dist/SECOND.BAT', dst: 'ELBOW\\DEMOS\\' }] });
const m = s.pc.machine;
const out = (cmd) => s.run(cmd).join('\n');

t.ok(/Turbo on: 100 MHz/.test(out('TURBO')), 'TURBO: the clock', s.pc.screen());
t.ok(/Turbo off: 12 MHz/.test(out('TURBO OFF')) && !m.turbo && m.mhz === 12, 'TURBO OFF: 12 MHz');
t.ok(/Turbo MAX/.test(out('TURBO MAX')) && m.unlocked && !m.turbo && m.mhz >= 100, 'TURBO MAX from 12 MHz: unlocked, turbo bit kept', `unlocked ${m.unlocked} mhz ${m.mhz}`);
t.ok(m.cpu.m8[0x4F0] === 0x54 && m.cpu.m8[0x4F1] === 0x42 && m.cpu.m8[0x4F2] === 0, 'the old setting noted at 0040:00F0h');
t.ok(/Turbo off: 12 MHz/.test(out('TURBO RESTORE')) && !m.unlocked && m.mhz === 12 && m.cpu.m8[0x4F0] === 0, 'TURBO RESTORE: back to 12 MHz, note cleared');
out('TURBO ON');
out('TURBO MAX'); out('TURBO MAX');
t.ok(/Turbo on: 100 MHz/.test(out('TURBO RESTORE')) && !m.unlocked && m.turbo, 'TURBO MAX twice: RESTORE still returns to the first setting');
t.ok(/TURBO \[ON/.test(out('TURBO /?')), 'TURBO /?: usage');

// SECOND.BAT: the instructions and PAUSE before the demo, the clock unlocked around it
s.pc.type('CLS\r'); s.waitPrompt();
s.pc.type('\\ELBOW\\DEMOS\\SECOND\r');
t.ok(s.pc.waitText('Press any key', { timeoutMs: 5000 }) && /Left arrow once/.test(s.pc.screen()) && /Sound Blaster Pro/.test(s.pc.screen()) && /work in progress/.test(s.pc.screen()), 'SECOND.BAT: how to pick the Sound Blaster, the work-in-progress note, then PAUSE', s.pc.screen());
t.ok(!m.unlocked, 'still locked at the PAUSE');
let seen = false; const o = m.setUnlocked.bind(m); m.setUnlocked = (on) => { if (on) seen = true; return o(on); };
s.pc.type(' '); s.waitPrompt();
t.ok(seen && !m.unlocked && m.turbo && m.mhz === 100, 'unlocked for the demo, put back after it (here ELBOW is missing and it fails)', s.pc.screen());

// the real-time driver: unlocking in the middle of a slice, then following the host
out('TURBO ON');
const d = new RealtimeDriver(m); d._schedule = () => {}; d.running = true; d.wall0 = performance.now(); d.emu0 = m.timeNs(); d._last = d.wall0;
let err = null;
const tick = () => { try { d._tick(); } catch (e) { err = e; } };
const spin = (ms) => { const end = performance.now() + ms; while (performance.now() < end) { tick(); const w = performance.now() + 4; while (performance.now() < w); } };
tick(); s.pc.type('TURBO MAX\r'); spin(1500);
t.ok(m.unlocked, 'TURBO MAX under the real-time driver');
s.pc.type('FOR %A IN (1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0) DO DIR C:\\ /S >NUL\r');
spin(1500);
t.ok(!err, 'no error when TURBO MAX arrives in the middle of a slice', err && err.stack);
t.ok(m.mhz !== 100, 'the driver moves the clock', m.mhz);
t.ok(d.stats.lagMs < 30, 'and keeps up with the wall clock', d.stats.lagMs);
d.running = false; s.waitPrompt(60000); out('TURBO RESTORE');

// a reset relocks
out('TURBO MAX'); m.reset();
t.ok(!m.unlocked && m.mhz === 100, 'a reset puts the limiter back');

// the controller (normally called by the real-time driver before every slice)
m.setUnlocked(true);
m.unlockedClock(700); t.ok(m.mhz === 700, 'the clock the driver asks for', m.mhz);
m.unlockedClock(5000); t.ok(m.mhz === 999, 'at most 999 MHz (three digits on the front panel)', m.mhz);
m.unlockedClock(10); t.ok(m.mhz === 100, 'never below the turbo clock', m.mhz);
m.unlockedClock(1000);
t.ok(m.in8(0xF2) === 3 && m.in8(0xF1) === 255, 'port F2h reads 3 while unlocked');
m.setTurbo(false); t.ok(!m.unlocked && m.mhz === 12, 'the TURBO button overrides');

t.done();
