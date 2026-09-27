// ARM-DOS 4.00 - the page. Wires the emulator (../emu) to the drawn machine:
// power, reset, turbo, LEDs, the CRT, sound, drives, printer, serial and the
// inspector. The emulator itself is created on the first power-on.

import { Machine } from '../emu/machine.js';
import { renderScreen } from '../emu/render.js';
import { $, fetchBinary, download, prefs } from './util.js';
import { Display, textImage, PHOSPHOR } from './display.js';
import { Sound } from './audio.js';
import { SbAudio } from './audio-sb.js';
import { GmAudio } from './audio-gm.js';
import { SevenSeg } from './sevenseg.js';
import './altled.js';
import { DiskStore, cmosStore } from './storage.js';
import { StreamedDisk } from './hdstream.js';
import { Driver, Debugger } from './debug.js';
import { Inspector } from './inspector.js';
import { MemMapPanel } from './memmap-panel.js';
import { ElbowPanel } from './elbow-panel.js';
import { Printer } from './printer.js';
import { DiskBox } from './diskbox.js';
import { Input } from './input.js';
import { PcKeys } from './pckeys.js';
import { AppLayout } from './applayout.js';
import { Files, looksLikeImage } from './files.js';
import { ModemLine } from './modem-line.js';
import { CdRom } from './cdrom.js';
import { OpenBox } from './openbox.js';
import { JoystickHost } from './joystick.js';

const q = new URLSearchParams(location.search);
const state = {
  powered: false, machine: null, driver: null, dbg: null,
  images: null, rom: null, hd: null, store: null,
  turbo: prefs.get('turbo', true),
  stats: { mips: 0, effMips: 0, hostLoad: 0, emuMs: 0, lagMs: 0 },
  frameImg: null, vgaFont: null,
  monitor: PHOSPHOR[prefs.get('monitor', 'vga')] ? prefs.get('monitor', 'vga') : 'vga',   // 'vga' | Hercules mono phosphor
  monitorOn: null,                // what the running machine has plugged in
};
window.armdos = state;           // for the curious (and for tests)

// ------------------------------------------------------------------ parts
const sound = new Sound();
const sbAudio = new SbAudio(sound);     // Sound Blaster 16 + OPL3 output (emulated-time audio)
state.sbAudio = sbAudio;
const gm = new GmAudio(sbAudio);        // MPU-401 + General MIDI synth (audio-gm.js), loaded on first use
state.gm = gm;
sound.muted = !prefs.get('sound', true);
sound.ambient = prefs.get('ambient', false);
const display = new Display($('screen'), { webgl: q.get('gl') !== '0' });
display.crt = prefs.get('crt', true);
const videoOpts = (mon) => (mon === 'vga' ? { video: 'vga' } : { video: 'hercules', monitor: mon });
const mhz = new SevenSeg($('mhzDisplay'));
const printer = new Printer(sound);
// COM2 modem: phone line, front panel, BBS worker, Host Link (web/js/modem-line.js)
const line = new ModemLine({ sound, rom: () => state.rom, images: () => state.images });
line.mount($('case'));
state.line = line;
// the CD-ROM drive in the case and the disc box (web/js/cdrom.js)
const cd = new CdRom({ sound, machine: () => state.machine });
cd.mount($('case'));
state.cd = cd;
const disks = new DiskBox({
  sound,
  onInsert: (data, wp) => { state.machine?.insertFloppy(data, wp); files?.changed(); },
  onEject: () => { state.machine?.ejectFloppy(); files?.changed(); },
  isImage: looksLikeImage,
  onDropFiles: (fs) => files.copyIn(fs, 'A'),
});
const input = new Input({ machine: () => (state.powered ? state.machine : null), sound });
state.sound = sound; state.input = input;     // (tests)
// the game port's joysticks: Gamepad API controllers and the pad's JOY mode (js/joystick.js)
const joystick = new JoystickHost({ machine: () => state.machine });
state.joystick = joystick;
joystick.onChange = (s) => {
  const lit = s.connected || s.pad;
  const what = s.connected ? s.names.map((n) => n.replace(/\s*\(.*$/, '')).join(' + ') : s.pad ? 'the on-screen pad (JOY)' : '';
  for (const id of ['joyLed', 'abJoyLed']) { const l = $(id); if (l) { l.classList.toggle('on', lit); l.classList.toggle('fire', lit && !!s.buttons); } }
  for (const id of ['joyLamp', 'abJoy']) {
    const l = $(id); if (!l) continue;
    l.title = lit ? `Joystick: ${what} in the game port (201h)` : 'Joystick: none. Connect a game controller and press one of its buttons (or switch the on-screen pad to JOY) before starting a game';
  }
};
joystick.onChange({ connected: false, pad: false, names: [], buttons: 0 });
const pckeys = new PcKeys({ machine: () => (state.powered ? state.machine : null), joystick });
const inspector = new Inspector({
  machine: () => state.machine,
  dbg: () => state.dbg,
  stats: () => state.stats,
  powered: () => state.powered,
  running: () => !!state.driver?.running,
  pause, run: resume, step, stepOver,
});
state.inspector = inspector;
// the inspector's MEMORY MAP (web/js/memmap-panel.js): live execute/read/write heat map
state.memmap = new MemMapPanel({ machine: () => (state.powered ? state.machine : null), running: () => !!state.driver?.running });
// the inspector's ELBOW view (web/js/elbow-panel.js): the x86 block running and the ARM code ELBOW made of it
state.elbow = new ElbowPanel({ machine: () => (state.powered ? state.machine : null), running: () => !!state.driver?.running });
state.appLayout = new AppLayout({ pckeys, input, disks, state });
// the inside of the case: cards, SIMMs, clock jumper, drives (web/js/openbox.js)
const box = new OpenBox({
  state, sound, line, gm, cd,
  setMonitor: (v) => { state.monitor = v; if (v !== 'vga') prefs.set('phosphor', v); prefs.set('monitor', v); monitorSel.value = v; monitorNote(); },
  lastPhosphor: () => prefs.get('phosphor', 'green'),
});
box.mount($('case'));
state.box = box;
line.onSpeed = () => box.external();

// the offline cache (web/sw.js): the app shell plus the hash-versioned disk images. A new release
// downloads in the background, all or nothing, then waits; it takes over only here, as the page
// starts and before the machine has loaded anything, so a reload is enough to update (the page
// reloads itself once into the new release) and a running machine never sees two releases mixed.
const swStart = (async () => {
  if (!('serviceWorker' in navigator) || q.has('nosw') || location.protocol === 'file:') return;
  const sw = navigator.serviceWorker;
  const reg = sw.register('sw.js', { updateViaCache: 'none' });
  const before = sw.controller;
  if (!before) { reg.catch((e) => console.warn('service worker', e)); return; }    // a first visit: this page came from the network
  const sleep = (ms) => new Promise((res) => setTimeout(res, ms));
  try {
    const r = await reg;
    if (!r.installing && !r.waiting) await Promise.race([r.update().catch(() => {}), sleep(4000)]);   // (offline: fails at once)
    const w = r.installing || r.waiting;
    if (!w) return;
    sw.startMessages();
    sw.addEventListener('message', (e) => { if (e.data?.armdosUpdate) state.updating = e.data.armdosUpdate; });
    state.updating = state.updating || { done: 0, total: 0 };
    const got = await Promise.race([new Promise((res) => {
      const settled = () => { if (w.state !== 'installing') { w.removeEventListener('statechange', settled); res(w.state === 'redundant' ? 'failed' : 'ready'); } };
      w.addEventListener('statechange', settled); settled();
    }), sleep(20000).then(() => 'slow')]);
    if (got === 'failed') console.warn('ARM-DOS: the new release did not arrive complete; running the one already here');
    if (got !== 'ready') return;            // (a slow one goes on downloading and takes over next time)
    if (w.state === 'installed') {
      const answer = await Promise.race([new Promise((res) => {
        const ch = new MessageChannel(); ch.port1.onmessage = (e) => res(e.data); w.postMessage('takeover', [ch.port2]);
      }), sleep(3000)]);
      if (answer !== 'ok') return;          // another ARM-DOS window is open: the new release waits for it to close
    }
    await Promise.race([new Promise((res) => { if (sw.controller !== before) res(); else sw.addEventListener('controllerchange', res, { once: true }); }), sleep(5000)]);
    try {                                   // (once a minute at most, whatever goes wrong)
      if (Date.now() - (+sessionStorage.getItem('armdos-updated') || 0) < 60000) return;
      sessionStorage.setItem('armdos-updated', String(Date.now()));
    } catch { return; }
    location.reload();
    await new Promise(() => {});            // (the page is going away)
  } catch (e) { console.warn('service worker', e); } finally { state.updating = null; }
})();

// ------------------------------------------------------------------ LEDs
const leds = {};
function led(id, on) { const e = leds[id] || (leds[id] = $(id)); e.classList.toggle('on', on); }
const blinkTimers = {};
function blink(id, ms = 90) { led(id, true); clearTimeout(blinkTimers[id]); blinkTimers[id] = setTimeout(() => led(id, false), ms); }

// ------------------------------------------------------------------ loading (in the background after first paint)
const vgaFont = fetch('emu/fonts/vga8x16.bin').then((r) => r.arrayBuffer()).then((b) => (state.vgaFont = new Uint8Array(b)));
const loading = (async () => {
  await swStart;
  const images = await (await fetch('images.json', { cache: 'no-cache' })).json();
  state.images = images;
  cd.load(images);
  gm.setImages(images);
  // C: is streamed in chunks (web/js/hdstream.js) when images.json describes it that way
  const stream = images.hd.chunks ? new StreamedDisk(images.hd, { onProgress: updateHdStatus, onWait: (on) => $('hdLed').classList.toggle('net', on) }) : null;
  state.hdStream = stream;
  const [rom, hd, font] = await Promise.all([
    fetchBinary(images.rom.file, null, images.rom),
    stream ? stream.img : fetchBinary(images.hd.file, null, images.hd),
    vgaFont,
  ]);
  state.rom = rom; state.vgaFont = font;
  state.store = new DiskStore(images.hd.sha);
  state.store.onChange = updateHdStatus;
  await state.store.restore(hd, stream ? (lba) => stream.wrote(lba) : null);
  state.hd = hd;
  return true;
})();
loading.catch((e) => { console.error(e); state.loadError = e; });
disks.load();
const files = new Files({
  floppy: () => disks.inDrive,
  ensureFloppy: async () => { const d = disks.disks.find((x) => x.id === 'blank' && !x.missing) || disks.disks.find((x) => !x.missing && !x.writeProtected); if (d) await disks.insert(d); },
  hd: () => state.hd,
  hdStream: () => state.hdStream,
  powered: () => state.powered,
  machine: () => (state.powered ? state.machine : null),
  sound,
  onFloppyWritten: () => disks.markWritten(),
  onHdWritten: (lbas) => { for (const l of lbas) { state.hdStream?.wrote(l); state.store.markDirty(l, 1); } state.store.flush(); },
});
loading.then(() => files.changed(), () => {});

function updateHdStatus() {
  const s = state.store; if (!s) return;
  // a failed save stays on show (here and on the disk bay) until one works again
  $('hdStatus').classList.toggle('warn', !!s.error);
  $('hdBay').classList.toggle('not-saving', !!s.error);
  if (s.error) {
    $('hdStatus').textContent = `Drive C: your changes are NOT being saved: this browser refused to store them (${s.error}). ` +
      'They are kept while this page stays open, and the page keeps trying. Download C: keeps a copy.';
    return;
  }
  $('hdStatus').textContent = s.saved
    ? `Drive C: remembers your changes in this browser (${(s.saved / 2).toLocaleString('en-US')} KB changed since it left the factory).`
    : 'Drive C: is exactly as it left the factory. Anything you save to it will still be here next time.';
  const d = state.hdStream;
  if (d && !d.complete) $('hdStatus').textContent += ` C: ${Math.floor(d.progress * 100)}% cached; the rest arrives as it is needed.`;
}

// ------------------------------------------------------------------ the machine
function createMachine() {
  const m = new Machine({
    rom: state.rom, hd: state.hd,
    fd: disks.inDrive?.data || null, fdWriteProtected: !!disks.inDrive?.writeProtected,
    turbo: state.turbo, jit: q.get('jit') !== '0',
    cmos: cmosStore.load(),
    onSerial: serialOut, onDebug: (b) => { if (q.has('debug')) serialOut(b); },
    onSpeaker: (on, f) => sound.speaker(on, f, m.timeMs()),
    onDiskActivity: diskActivity,
    onDiskWrite: (drive, lba, count) => { if (drive === 0x80) state.store.markDirty(lba, count); else disks.markWritten(); files.changed(); },
    onCmosWrite: (ram) => cmosStore.save(ram),
    onLeds: (bits) => input.setLeds(bits),
    onPrint: (b) => printer.enqueue(b),
    onExit: () => {},
    ...line.machineOptions(),
    ...cd.machineOptions(),
    ...gm.machineOptions(),
    ...videoOpts(state.monitor),
    ...box.machineOptions(),
  });
  // the floppy controller's recalibrate has no activity callback: listen in
  const fdc = m.fdc, cmd = fdc.command.bind(fdc);
  fdc.command = (c) => {
    if (c === 3) {                       // recalibrate: at POST the classic out-and-back seek, later a short step out
      if (!state.fdRecalled) sound.fdPostSeek(); else sound.fdRecal(fdc.lastCyl > 0 ? fdc.lastCyl + 2 : 3);
      state.fdRecalled = true; blink('fdLed', 500);
    }
    return cmd(c);
  };
  state.machine = m;
  if (state.hdStream) m.ata.source = state.hdStream;      // C: streams: reads wait (BSY) for missing chunks
  sbAudio.attach(m);
  state.dbg = new Debugger(m);
  state.driver = new Driver(m, {
    maxSliceMs: 14,
    onFrame: (s) => { state.stats = s; sound.frameSync(m.timeMs()); if (!m.unlocked || performance.now() - (state.mhzShown || 0) > 250) { mhz.show(m.mhz); state.mhzShown = performance.now(); }   // the clock display (TURBO MAX moves it every frame)
      if (m.turbo !== state.turbo) { state.turbo = m.turbo; led('turboLed', m.turbo); $('turboBtn').setAttribute('aria-pressed', String(m.turbo)); }   // TURBO ON/OFF from DOS
    },
    onBreak: () => { sound.speakerOff(); inspector.refresh(true); },
    onCrash: (e) => crashed(e),
  });
  inspector.attach();
  return m;
}

let hdPrevCyl = 0;
function diskActivity(drive, lba, count, isWrite, cyl, prevCyl) {
  if (drive === 0x80) {
    blink('hdLed', 60);
    const c = Math.floor(lba / 1008);
    sound.hdAccess(c, hdPrevCyl, isWrite);
    hdPrevCyl = c;
  } else {
    blink('fdLed', 350);
    sound.fdSeek(prevCyl ?? 0, cyl ?? 0);
    sound.fdRead(count);
  }
}

// ------------------------------------------------------------------ power
let powerBusy = false;
async function powerOn() {
  if (powerBusy) return; powerBusy = true;
  sound.init();
  sound.powerSwitch(true);
  $('powerBtn').setAttribute('aria-pressed', 'true');
  $('powerBtn').classList.remove('beckon');
  $('postit').classList.add('gone'); setTimeout(() => { $('postit').hidden = true; }, 900);   // (once faded: out of the layout, it stuck out at phone width)
  prefs.set('seen', true);
  state.powered = true;
  led('pwrLed', true); led('monLed', true); led('turboLed', state.turbo);
  sound.fanStart();
  if (state.monitor === 'vga') sound.degauss(1, true, 0.72);   // (a mono monitor has no shadow mask and no degauss coil)
  display.powerOn(state.monitor === 'vga');
  printer.setOnline(true);
  state.fdRecalled = false;
  // the machine's lights come on at once; the tube warms up while the ROM loads
  const t0 = performance.now();
  display.setMonitor(state.monitor === 'vga' ? null : state.monitor);      // the monitor on the desk now
  $('monBrand').textContent = state.monitor === 'vga' ? 'VGA COLOR DISPLAY' : 'MONOCHROME DISPLAY';
  $('degaussBtn').hidden = state.monitor !== 'vga';
  const ph = PHOSPHOR[state.monitor];
  const splash = setInterval(() => {
    if (!state.vgaFont) return;
    const dots = '.'.repeat(1 + (Math.floor((performance.now() - t0) / 300) % 3));
    const u = state.updating, pct = u?.total ? ` ${Math.floor((100 * u.done) / u.total)}%` : '';
    display.setImage(textImage(state.vgaFont, ['', '', u ? '  Updating ARM-DOS to the new release' + pct + dots : '  Reading the ROM' + dots], ph ? { fg: ph.text[0], hi: ph.text[1] } : {}));
  }, 150);
  try { await loading; } catch { /* handled below */ }
  clearInterval(splash);
  if (state.loadError || !state.rom) {
    const f = state.vgaFont;
    if (f) display.setImage(textImage(f, ['', '', '!  ROM missing', '', '  The firmware did not arrive. Check your connection and', '  switch the machine off and on again.']));
    console.error('load failed', state.loadError);
    powerBusy = false; return;
  }
  if (!state.powered) { powerBusy = false; return; }       // switched off while loading
  const again = !!state.machine;
  const m = state.machine || createMachine();
  if (again) { m.stopped = false; box.apply(m); m.powerCycle(); state.dbg.prepareResume(); }
  else box.apply(m);
  state.monitorOn = state.monitor;
  monitorNote();
  if (disks.inDrive) m.insertFloppy(disks.inDrive.data, !!disks.inDrive.writeProtected); else m.ejectFloppy();
  m.setTurbo(state.turbo);
  mhz.show(state.turbo ? m.turboMhz : m.slowMhz);
  box.setPowered(true);
  state.driver.start();
  state.hdStream?.schedulePrefetch(6000);                // the rest of C: follows once the boot has settled
  powerBusy = false;
  if (!matchMedia('(pointer: coarse)').matches) $('screen').focus({ preventScroll: true });
  inspector.refresh(true);
}
function powerOff() {
  sound.powerSwitch(false);
  $('powerBtn').setAttribute('aria-pressed', 'false');
  state.powered = false;
  if (state.driver) state.driver.stop();
  state.dbg?.clearTemp();
  sound.speakerOff(); sound.fanStop(); sound.crtOff(); sbAudio.flush(); line.powerOff(state.machine); cd.powerOff();
  display.powerOff();
  setTimeout(() => { if (!state.powered) display.setImage(null); }, 1800);
  monitorNote();
  box.setPowered(false);
  for (const id of ['pwrLed', 'monLed', 'turboLed', 'hdLed', 'fdLed']) led(id, false);
  mhz.show('');
  printer.setOnline(false);
  input.releaseAll();
  if (document.pointerLockElement) document.exitPointerLock();
  state.store?.flush();
  inspector.refresh(true);
}
$('powerBtn').onclick = () => { if (state.powered) powerOff(); else powerOn(); files.changed(); };
if (!prefs.get('seen', false)) $('powerBtn').classList.add('beckon');

$('resetBtn').onclick = () => {
  sound.init(); sound.click('button');
  if (!state.powered || !state.machine) return;
  const m = state.machine;
  state.dbg.clearTemp();
  m.stopped = false; m.reset();
  state.fdRecalled = true;
  if (!state.driver.running) { state.dbg.prepareResume(); state.driver.start(); }
  $('screen').focus({ preventScroll: true });
};
// the monitor's DEGAUSS button (colour monitor only; press again soon and the coil, still warm, does little)
$('degaussBtn').onclick = () => {
  sound.init(); sound.click('latch');
  if (!state.powered || display.phosphor) return;
  sound.degauss(display.degauss(220), false);
};
$('degaussBtn').hidden = state.monitor !== 'vga';
$('turboBtn').onclick = () => {
  sound.init(); sound.click('latch');
  state.turbo = !state.turbo; prefs.set('turbo', state.turbo);
  $('turboBtn').setAttribute('aria-pressed', String(state.turbo));
  if (!state.powered) return;
  led('turboLed', state.turbo);
  const m = state.machine;
  if (m) { m.setTurbo(state.turbo); mhz.show(state.turbo ? m.turboMhz : m.slowMhz); }
};
$('turboBtn').setAttribute('aria-pressed', String(state.turbo));

// ------------------------------------------------------------------ run control (inspector)
function pause() {
  if (!state.driver?.running) return;
  state.driver.stop(); sound.speakerOff();
  inspector.refresh(true);
}
function resume() {
  if (!state.powered || !state.driver || state.driver.running) return;
  state.dbg.prepareResume();
  state.driver.start();
  inspector.refresh(true);
}
function step() {
  if (!state.powered || !state.machine) return;
  if (state.driver.running) state.driver.stop();
  state.dbg.step();
  inspector.refresh(true);
}
function stepOver() {
  if (!state.powered || !state.machine) return;
  if (state.driver.running) state.driver.stop();
  if (state.dbg.armStepOver()) resume(); else step();
}
function crashed(e) {
  sound.speakerOff();
  const f = state.vgaFont;
  if (f) display.setImage(textImage(f, ['', '!  The emulator stopped with an internal error:', '', '  ' + String(e && e.message || e).slice(0, 76), '', '  Switch the machine off and on again to restart it.'], { hi: [255, 85, 85] }));
}

// ------------------------------------------------------------------ the screen
function frame(now) {
  requestAnimationFrame(frame);
  if (state.powered && state.machine && !powerBusy) {
    state.frameImg = renderScreen(state.machine, state.frameImg);
    display.setImage(state.frameImg);
  }
  line.frame(state.powered ? state.machine : null);
  joystick.frame();
  display.draw(now);
}
requestAnimationFrame(frame);

// ------------------------------------------------------------------ toolbar
$('cadBtn').onclick = () => { if (state.powered) { state.machine?.typeText('{CTRL+ALT+DEL}'); $('screen').focus({ preventScroll: true }); } };
// the monitor swap: VGA colour, or a Hercules card with a mono monitor. The card is
// read at power-on (a power cycle, like swapping cards); until then a note says so.
const monitorSel = $('monitorSel');
monitorSel.value = state.monitor;
function monitorNote() {
  const n = $('monitorNote');
  const pending = state.powered && state.monitorOn && state.monitorOn !== state.monitor;
  n.hidden = !pending;
  if (pending) n.textContent = state.monitor === 'vga'
    ? 'The VGA card and colour monitor go in at the next power-on: switch the machine off and on again.'
    : `The Hercules card and ${PHOSPHOR[state.monitor].label} monitor go in at the next power-on: switch the machine off and on again. (Programs that need a VGA, like DOOM, will say so.)`;
}
monitorSel.onchange = () => {
  state.monitor = monitorSel.value; prefs.set('monitor', state.monitor); if (state.monitor !== 'vga') prefs.set('phosphor', state.monitor); monitorNote(); box.external(); if (!state.powered) $('degaussBtn').hidden = state.monitor !== 'vga';   // (the monitor is swapped at power-on)
};
const followKbdBtn = $('followKbdBtn');          // "Keyboard: follow my computer's layout" (js/input.js)
followKbdBtn.setAttribute('aria-pressed', String(input.follow));
followKbdBtn.onclick = () => { input.setFollow(!input.follow); followKbdBtn.setAttribute('aria-pressed', String(input.follow)); };
const crtBtn = $('crtBtn');
crtBtn.setAttribute('aria-pressed', String(display.crt));
crtBtn.onclick = () => { display.crt = !display.crt; display.needsDraw = true; prefs.set('crt', display.crt); crtBtn.setAttribute('aria-pressed', String(display.crt)); };
if (!display.usingGL) { crtBtn.disabled = true; crtBtn.title = 'This browser has no WebGL, so the picture is drawn flat'; crtBtn.setAttribute('aria-pressed', 'false'); }
const soundBtn = $('soundBtn');
soundBtn.setAttribute('aria-pressed', String(!sound.muted));
soundBtn.onclick = () => { sound.init(); sound.setMuted(!sound.muted); prefs.set('sound', !sound.muted); soundBtn.setAttribute('aria-pressed', String(!sound.muted)); };
const ambientBtn = $('ambientBtn');
gm.mountToggle(ambientBtn);
$('gmBtn')?.addEventListener('click', () => box.external());
ambientBtn.setAttribute('aria-pressed', String(sound.ambient));
ambientBtn.onclick = () => { sound.init(); sound.setAmbient(!sound.ambient); prefs.set('ambient', sound.ambient); ambientBtn.setAttribute('aria-pressed', String(sound.ambient)); };
// Full screen: the mouse is captured on the way in (still inside the click's user activation)
// and let go on the way out; inside, Ctrl+Alt+M or the auto-hiding bar toggles it.
$('fullBtn').onclick = () => {
  const mon = $('monitor');
  if (document.fullscreenElement) { document.exitFullscreen(); return; }
  if (!mon.requestFullscreen) return;
  mon.requestFullscreen().then(() => {
    $('screen').focus({ preventScroll: true });
    if (state.powered) input.lockMouse();
  }).catch(() => {});
};
{
  const bar = $('fsBar'); let hideTimer = 0;
  const wake = () => { bar.classList.add('show'); clearTimeout(hideTimer); hideTimer = setTimeout(() => bar.classList.remove('show'), 2600); };
  document.addEventListener('fullscreenchange', () => {
    const on = document.fullscreenElement === $('monitor');
    state.fullscreen = on;
    if (on) wake();
    else if (document.pointerLockElement) document.exitPointerLock();
  });
  $('monitor').addEventListener('mousemove', () => { if (state.fullscreen && !document.pointerLockElement) wake(); });
  document.addEventListener('pointerlockchange', () => { if (state.fullscreen) wake(); });
  $('fsMouse').onclick = () => { input.toggleMouse(); };
  $('fsExit').onclick = () => { if (document.fullscreenElement) document.exitFullscreen(); };
}

// ------------------------------------------------------------------ serial
const serialEl = $('serialOut');
let serialBuf = '', serialTimer = 0;
function serialOut(b) {
  if (b === 13) return;
  serialBuf += b === 10 || b === 9 || b >= 32 ? String.fromCharCode(b) : '';
  if (!serialTimer) serialTimer = setTimeout(() => {
    serialTimer = 0;
    serialEl.textContent = (serialEl.textContent + serialBuf).slice(-20000);
    serialBuf = '';
    serialEl.scrollTop = serialEl.scrollHeight;
  }, 50);
}
$('serialForm').onsubmit = (e) => {
  e.preventDefault();
  const v = $('serialInput').value;
  if (state.powered && state.machine) state.machine.serialInput(v + '\r');
  $('serialInput').value = '';
};

// ------------------------------------------------------------------ disks: downloads and factory reset
$('hdDownload').onclick = async () => {
  try { await loading; } catch { return; }
  const d = state.hdStream, btn = $('hdDownload');
  if (d && !d.complete) {
    btn.disabled = true;
    try { await d.fetchAll((p) => { btn.textContent = `Fetching C: ${Math.floor(p * 100)}%`; }); }
    catch (e) { btn.textContent = 'C: incomplete (offline?)'; setTimeout(() => { btn.textContent = 'Download C:'; btn.disabled = false; }, 4000); return; }
    btn.textContent = 'Download C:'; btn.disabled = false;
  }
  download(state.hd, 'armdos-c.img');
};
const hdReset = $('hdReset');
let resetArmed = 0;
hdReset.onclick = async () => {
  if (!resetArmed) {
    hdReset.classList.add('armed'); hdReset.textContent = 'Erase your changes to C:?';
    resetArmed = setTimeout(() => { resetArmed = 0; hdReset.classList.remove('armed'); hdReset.textContent = 'Reset C: to factory'; }, 4000);
    return;
  }
  clearTimeout(resetArmed); resetArmed = 0;
  hdReset.classList.remove('armed'); hdReset.textContent = 'Reset C: to factory';
  try { await loading; } catch { return; }
  await state.store.clear();
  if (state.hdStream) state.hdStream.resetOverlay();      // chunks holding your changes are fetched afresh
  else state.hd.set(await fetchBinary(state.images.hd.file, null, state.images.hd));   // same buffer: the ATA device keeps pointing at it
  state.store.image = state.hd;
  cmosStore.clear();
  if (state.powered && state.machine) { state.machine.stopped = false; state.machine.powerCycle(); if (!state.driver.running) { state.dbg.prepareResume(); state.driver.start(); } }
  updateHdStatus();
  files.changed();
};

// flush the disk when the page goes away
addEventListener('pagehide', () => state.store?.flush());
document.addEventListener('visibilitychange', () => { if (document.hidden) state.store?.flush(); });
