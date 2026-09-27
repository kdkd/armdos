// The phone line on the page (docs/MODEM.md): the PhoneExchange every machine shares, the
// visitor's modem speaker and front panel, the ARM Pit BBS (555-1989, a second ARM-PC in a
// Web Worker, booted the first time somebody dials it) and the Host Link (555-0100, ZMODEM to
// and from the visitor's real computer).
//
// main.js wiring (a few lines):
//   const line = new ModemLine({ sound, rom: () => state.rom, images: () => state.images });
//   new Machine({ ..., ...line.machineOptions() })
//   line.frame(state.powered ? state.machine : null)          // every animation frame
//   line.mount({ monitor: $('monitor'), unit: $('case') })  // the modem under the monitor, on the case

import { PhoneExchange, PortEndpoint } from '../emu/phone.js';
import { HostLink } from '../emu/hostlink.js';
import { registerLines } from '../emu/phonelines.js';
import { OnlineService, ONLINE_NUMBER } from '../emu/online/service.js';
import { fetchBinary, download, prefs } from './util.js';
import { ModemAudio } from './modem-audio.js';
import { ModemPanel } from './modem-panel.js';

export const MY_NUMBER = '555-2000', BBS_NUMBER = '555-1989', HOST_NUMBER = '555-0100';

export class ModemLine {
  constructor({ sound, rom = () => null, images = () => null } = {}) {
    this.sound = sound; this.getRom = rom; this.getImages = images;
    this.exchange = new PhoneExchange();
    this.audio = new ModemAudio(sound);
    const sp = +prefs.get('modemSpeed', 2400);
    this.speed = [2400, 14400, 33600, 56000].includes(sp) ? sp : 2400;
    this.machine = null;
    this.panel = new ModemPanel({
      speed: this.speed,
      onSpeed: (v) => { this.speed = v; prefs.set('modemSpeed', v); this.machine?.modem.setSwitch(v); this.sound.click?.('latch'); this.onSpeed?.(v); },
      onSysop: (on) => this.sysop(on),
      onChoose: () => this.pickNow(),
      onCancelChoose: () => this.finishPick([]),
    });
    this.events = [];                  // exchange events (tests / curious visitors)
    this.exchange.on((e) => { this.events.push(e); if (this.events.length > 50) this.events.shift(); });
    this.setupHostLink();
    this.setupBbs();
    // WOPR, the support line, the talking clock, the fax, the rival board, KREMVAX, ... (emu/phonelines.mjs)
    this.lines = registerLines(this.exchange);
    this.linesTimer = setInterval(() => this.lines.tick(performance.now()), 20);
    this.setupOnline();
  }
  mount(where) { this.panel.mount(where); }
  /** The card's speed jumper (web/js/openbox.js): the same as the panel's switch. */
  setSpeed(v) { this.speed = v; prefs.set('modemSpeed', v); this.panel.setSpeed(v); this.machine?.modem.setSwitch(v); }
  /** Is the modem card in the machine? Without it the panel stays dark. */
  setPresent(on) { this.present = on; this.panel.root.classList.toggle('absent', !on); }
  machineOptions() {
    return { phone: this.exchange, phoneNumber: MY_NUMBER, modemRate: this.speed, onModemSound: (e) => this.audio.event(e) };
  }
  /** Once per animation frame. */
  frame(machine) {
    if (machine && machine !== this.machine) { this.machine = machine; machine.modem.setSwitch(this.speed); }
    if (this.present === false) { this.panel.update(null); this.panel.text('NO MODEM CARD'); return; }
    this.panel.update(machine ? machine.modem.panel() : null);
  }
  /** The machine was switched off: the modem loses power (the far end gets NO CARRIER). */
  powerOff(machine) {
    machine?.modem.hangup();
    if (this.audio.ready) this.audio.hush(this.sound.ctx.currentTime);
  }

  // ------------------------------------------------------------------ Host Link (555-0100)
  setupHostLink() {
    this.host = new HostLink({
      pickFiles: () => this.pickFiles(),
      saveFile: (f) => {
        download(f.data, f.name);
        this.panel.showPrompt(`Host Link: received ${f.name} (${f.data.length.toLocaleString('en-US')} bytes) - saved as a download.`, false);
        clearTimeout(this.promptTimer); this.promptTimer = setTimeout(() => this.panel.hidePrompt(), 8000);
      },
      onStatus: (s) => { this.hostStatus = s; },
    });
    this.exchange.register(HOST_NUMBER, this.host);
    this.hostTimer = setInterval(() => this.host.tick(performance.now()), 20);
  }
  pickFiles() {
    return new Promise((resolve) => {
      this.pickResolve = resolve;
      this.panel.showPrompt('Host Link: choose the file(s) to send to ARM-DOS.', true);
      this.pickNow(true);
    });
  }
  pickNow(auto = false) {
    // browsers only open the file window from a user gesture; the keypress that asked for it
    // usually still counts (auto), otherwise the "Choose file…" button does
    const inp = document.createElement('input');
    inp.type = 'file'; inp.multiple = true; inp.style.display = 'none';
    inp.addEventListener('change', async () => {
      const files = await Promise.all([...inp.files].map(async (f) => ({ name: f.name, data: new Uint8Array(await f.arrayBuffer()), mtime: Math.floor(f.lastModified / 1000) })));
      inp.remove();
      this.finishPick(files);
    });
    inp.addEventListener('cancel', () => { inp.remove(); if (!auto) this.finishPick([]); });
    document.body.appendChild(inp);
    try { inp.click(); } catch { /* no activation: the button is there */ }
  }
  finishPick(files) {
    const r = this.pickResolve; this.pickResolve = null;
    this.panel.hidePrompt();
    if (r) r(files);
  }

  // ------------------------------------------------------------------ ARM-DOS Online (555-0199, apps/online)
  // fetches Wikipedia, Wiktionary, Open-Meteo and Hacker News from the browser; pictures are
  // converted to GIFs here (emu/online/picture.mjs)
  setupOnline() {
    this.online = new OnlineService({ onStatus: (s) => { this.onlineStatus = s; } });
    this.exchange.register(ONLINE_NUMBER, this.online);
    this.onlineTimer = setInterval(() => this.online.tick(performance.now()), 20);
  }

  // ------------------------------------------------------------------ The ARM Pit BBS (555-1989)
  setupBbs() {
    const ep = this.bbsEndpoint = new PortEndpoint((m) => this.toBbs(m));
    const lazy = {
      incoming: (leg) => {
        if (!this.worker && !this.bbsStarting && !this.bbsMissing) this.startBbs();
        return ep.incoming(leg);
      },
    };
    this.exchange.register(BBS_NUMBER, lazy);
    this.bbsQueue = [];
  }
  toBbs(m) {
    if (this.bbsReady) this.worker.postMessage(m);
    else this.bbsQueue.push(m);
  }
  async startBbs() {
    this.panel.setSysopState('The BBS is booting…');
    this.bbsStarting = true;
    const images = this.getImages();
    // (a busy or flaky server - a download can fail once: try again before giving up)
    const fetchRetry = async (entry) => {
      for (let i = 0; ; i++) {
        try { return await fetchBinary(entry.file, null, entry); } catch (e) { if (i >= 2) throw e; await new Promise((r) => setTimeout(r, 500 * (i + 1))); }
      }
    };
    try {
      if (!images || !images.bbs) { this.bbsMissing = true; throw new Error('no BBS disk image on this site yet'); }
      const [hd, rom] = await Promise.all([fetchRetry(images.bbs), this.getRom() || fetchRetry(images.rom)]);
      this.worker = new Worker(new URL('./modem-bbs-worker.js', import.meta.url), { type: 'module' });
      this.worker.onmessage = (e) => this.fromBbs(e.data);
      this.worker.onerror = (e) => { console.error('BBS worker', e); this.panel.setSysopState('The BBS crashed: ' + (e.message || 'worker error')); };
      this.worker.postMessage({ boot: { rom: rom.slice(), hd, number: BBS_NUMBER, rate: 56000 } });
      this.bbsReady = true;
      for (const m of this.bbsQueue.splice(0)) this.worker.postMessage(m);
      if (this.sysopOn) this.worker.postMessage({ sysop: true });
    } catch (err) {
      console.warn('BBS unavailable:', err);
      this.bbsQueue.length = 0;
      // no image at all: the board isn't here; a failed download: the next call tries again
      this.panel.setSysopState(this.bbsMissing ? 'The BBS is not on this site yet. 555-1989 just rings.'
        : 'The BBS could not be loaded (' + err.message + '). Hang up and dial again to try once more.');
    } finally {
      this.bbsStarting = false;
    }
  }
  fromBbs(msg) {
    if (msg.phone) { this.bbsEndpoint.handle(msg); return; }
    if (msg.screen) { this.panel.drawSysop(msg.screen); return; }
    if (msg.panel) { this.bbsPanel = msg.panel; return; }
    if (msg.status === 'error') { console.error(msg.error); this.panel.setSysopState('The BBS stopped: ' + msg.error.split('\n')[0]); }
  }
  sysop(on) {
    this.sysopOn = on;
    if (this.worker && this.bbsReady) this.worker.postMessage({ sysop: on });
  }
}
