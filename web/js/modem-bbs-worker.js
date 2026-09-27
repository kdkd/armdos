// The ARM Pit BBS (555-1989, docs/MODEM.md): a second ARM-PC running in a Web Worker, with
// its own copy of the ROM and its own hard disk (build/bbs.img). It has no screen on the page;
// the sysop view asks for snapshots of its display. Its modem sits on the page's
// PhoneExchange through a message-port bridge (emu/phone.js PortExchange / PortEndpoint).
//
// page -> worker: { boot: { rom, hd, number } }  { phone: ... }  { sysop: true|false }
//                 { keys: 'text' }  (the sysop typing at the BBS's own keyboard)
// worker -> page: { phone: ... }  { status: 'running' | 'error', error }  { panel }
//                 { screen: { width, height, data } }  (while the sysop view is open)

import { Machine, RealtimeDriver } from '../emu/machine.js';
import { renderScreen } from '../emu/render.js';
import { PortExchange } from '../emu/phone.js';

const px = new PortExchange((m) => postMessage(m));
let machine = null, driver = null, sysop = false, img = null, lastShot = 0, lastPanel = '';

function boot({ rom, hd, number = '555-1989', rate = 56000 }) {
  machine = new Machine({
    rom, hd, jit: true, turbo: true,
    phone: px, phoneNumber: number, modemRate: rate,     // a fast line: the caller's switch decides
    onModemChange: (p) => {
      const s = JSON.stringify([p.oh, p.cd, p.state, p.rate]);
      if (s !== lastPanel) { lastPanel = s; postMessage({ panel: { oh: p.oh, cd: p.cd, state: p.state, rate: p.rate } }); }
    },
  });
  driver = new RealtimeDriver(machine, {
    maxSliceMs: 8, maxLagMs: 250,
    onFrame: () => {
      if (!sysop) return;
      const now = performance.now();
      if (now - lastShot < 250) return;
      lastShot = now;
      img = renderScreen(machine, img, machine.timeMs());
      const data = new Uint8ClampedArray(img.data);          // a copy we can transfer
      postMessage({ screen: { width: img.width, height: img.height, data } }, [data.buffer]);
    },
  });
  driver.start();
  postMessage({ status: 'running' });
}

onmessage = (e) => {
  const msg = e.data;
  try {
    if (msg.phone) { px.handle(msg); return; }
    if (msg.boot) { boot(msg.boot); return; }
    if ('sysop' in msg) { sysop = !!msg.sysop; lastShot = 0; return; }
    if (msg.keys && machine) machine.typeText(msg.keys);
  } catch (err) {
    postMessage({ status: 'error', error: String(err && err.stack || err) });
  }
};
