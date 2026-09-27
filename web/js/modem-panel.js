// The modem's front panel (docs/MODEM.md): a Hayes Smartmodem-style strip of lamps
// (HS AA CD OH RD SD TR MR) with a little line window, placed under the system unit, plus the
// phone list card, the Host Link prompt and the BBS sysop view. Pure DOM; ModemLine drives it.

import { el } from './util.js';

const LAMPS = [
  ['hs', 'HS', 'High speed (2400 bps or faster)'], ['aa', 'AA', 'Auto answer'], ['cd', 'CD', 'Carrier detect'],
  ['oh', 'OH', 'Off hook'], ['rd', 'RD', 'Receive data'], ['sd', 'SD', 'Send data'], ['tr', 'TR', 'Terminal ready (DTR)'],
  ['mr', 'MR', 'Modem ready'],
];

export class ModemPanel {
  constructor({ onSysop = () => {}, onChoose = () => {}, onCancelChoose = () => {}, onSpeed = () => {}, speed = 2400 } = {}) {
    this.lamps = {};
    this.prev = { rd: 0, sd: 0 };
    this.flash = { rd: 0, sd: 0 };
    const lampEls = LAMPS.map(([k, label, title]) => {
      const i = el('i', { class: 'mlamp', 'data-k': k });
      this.lamps[k] = i;
      return el('label', { title }, i, el('span', { text: label }));
    });
    this.lineEl = el('span', { class: 'mline-text', text: 'NO POWER' });
    this.sysopBtn = el('button', { class: 'mbtn', type: 'button', 'aria-pressed': 'false', title: 'Watch the ARM Pit BBS at 555-1989 (a second ARM-PC running in the background)', onclick: () => this.toggleSysop() }, 'SYSOP');
    this.chooseBtn = el('button', { class: 'mbtn go', type: 'button', onclick: () => onChoose() }, 'Choose file…');
    this.cancelBtn = el('button', { class: 'mbtn', type: 'button', onclick: () => onCancelChoose() }, 'Cancel');
    this.promptText = el('span', { class: 'mprompt-text' });
    this.prompt = el('div', { class: 'mprompt', hidden: true, role: 'status' }, this.promptText, this.chooseBtn, this.cancelBtn);
    this.canvas = el('canvas', { width: 720, height: 400, 'aria-label': 'The BBS screen' });
    this.sysopState = el('p', { class: 'sysop-state', text: 'The BBS is asleep. It boots the first time somebody dials 555-1989.' });
    this.sysop = el('div', { class: 'sysop', hidden: true, id: 'sysopView' },
      el('div', { class: 'sysop-head' }, el('b', { text: 'THE ARM PIT BBS' }), el('span', { text: 'sysop console · 555-1989' })),
      el('div', { class: 'sysop-screen' }, this.canvas), this.sysopState);
    this.onSysop = onSysop;
    // the speed switch: a four-position slide switch on the face (the card's maximum speed)
    const SPEEDS = [[2400, '2400', 'V.22bis, 2400 bps - 1988'], [14400, '14.4', 'V.32bis, 14,400 bps - 1991'], [33600, '33.6', 'V.34, 33,600 bps - 1996'], [56000, '56K', 'V.90, 56K - 1998']];
    this.speedInputs = SPEEDS.map(([v, label, title]) => {
      const inp = el('input', { type: 'radio', name: 'modemSpeed', value: String(v), 'aria-label': title });
      if (v === speed) inp.checked = true;
      inp.addEventListener('change', () => { if (inp.checked) { this.setSpeed(v); onSpeed(v); } });
      return el('label', { title }, inp, el('span', { text: label }));
    });
    this.speedEl = el('div', { class: 'mspeed', role: 'radiogroup', 'aria-label': 'Modem speed switch', 'data-speed': String(speed) },
      el('div', { class: 'mspeed-track' }, el('i', { class: 'mspeed-knob' })), el('div', { class: 'mspeed-labels' }, ...this.speedInputs));
    this.root = el('div', { class: 'modem', id: 'modemPanel', 'aria-label': 'Modem (COM2)' },
      el('div', { class: 'modem-box' },
        el('div', { class: 'modem-brand' }, el('b', { text: 'EUROPA' }), el('span', { class: 'mmodel', text: 'SMARTLINE ' + ({ 2400: '2400', 14400: '14.4', 33600: '33.6', 56000: '56K' })[speed] })),
        el('div', { class: 'modem-lamps', role: 'group', 'aria-label': 'Modem lamps' }, ...lampEls),
        el('div', { class: 'modem-line', 'aria-live': 'polite' }, this.lineEl),
        this.speedEl,
        this.sysopBtn),
      el('div', { class: 'phone-card', 'aria-label': 'Phone list' },
        el('b', { text: 'PHONE LIST' }),
        el('span', {}, 'ARM Pit BBS ', el('em', { text: '555-1989' })),
        el('span', {}, 'Host Link ', el('em', { text: '555-0100' })),
        el('span', {}, 'ARM-DOS Online ', el('em', { text: '555-0199' })),
        el('small', { text: 'modem on COM2 · ATDT' })),
      this.prompt, this.sysop);
    this.ctx2d = this.canvas.getContext('2d');
    this.imgData = null;
  }
  mount(after) { after.insertAdjacentElement('afterend', this.root); }

  setSpeed(v) {
    this.speedEl.dataset.speed = String(v);
    this.root.querySelector('.mmodel').textContent = 'SMARTLINE ' + ({ 2400: '2400', 14400: '14.4', 33600: '33.6', 56000: '56K' })[v];
    for (const i of this.speedInputs) i.checked = +i.value === v;
  }
  toggleSysop(on = this.sysop.hidden) {
    this.sysop.hidden = !on;
    this.sysopBtn.setAttribute('aria-pressed', String(on));
    this.onSysop(on);
  }
  setSysopState(text) { this.sysopState.textContent = text; this.sysopState.hidden = !text; }
  drawSysop({ width, height, data }) {
    if (this.canvas.width !== width || this.canvas.height !== height) { this.canvas.width = width; this.canvas.height = height; this.imgData = null; }
    if (!this.imgData) this.imgData = this.ctx2d.createImageData(width, height);
    this.imgData.data.set(data);
    this.ctx2d.putImageData(this.imgData, 0, 0);
    this.setSysopState('');
  }
  showPrompt(text, choose) {
    this.promptText.textContent = text;
    this.chooseBtn.hidden = !choose; this.cancelBtn.hidden = !choose;
    this.prompt.hidden = false;
  }
  hidePrompt() { this.prompt.hidden = true; }

  lamp(k, on) { const e = this.lamps[k]; if (e.classList.contains('on') !== !!on) e.classList.toggle('on', !!on); }
  /** p = modem.panel() or null when the machine is off. */
  update(p, now = performance.now()) {
    if (!p) { for (const k in this.lamps) this.lamp(k, false); this.text('NO POWER'); return; }
    for (const k of ['hs', 'cd', 'oh', 'tr', 'mr']) this.lamp(k, p[k]);
    this.lamp('aa', p.ringing ? Math.floor(now / 250) % 2 === 0 : p.aa);
    for (const k of ['rd', 'sd']) {
      if (p[k] !== this.prev[k]) { this.prev[k] = p[k]; this.flash[k] = now + 60; }
      this.lamp(k, now < this.flash[k]);
    }
    let t;
    switch (p.state) {
      case 'dialing': t = `DIALING ${p.number}`; break;
      case 'waitcarrier': t = `RINGING ${p.number}`; break;
      case 'training': t = `TRAINING ${p.rate}`; break;
      case 'answering': t = 'ANSWERING'; break;
      case 'ringing': t = 'RING'; break;
      case 'connected': t = p.online ? `CONNECT ${p.rate}` : `ON LINE ${p.rate} CMD`; break;
      case 'offhook': t = 'OFF HOOK'; break;
      default: t = p.oh ? 'OFF HOOK' : 'ON HOOK';
    }
    this.text(t);
  }
  text(t) { if (this.lineEl.textContent !== t) this.lineEl.textContent = t; }
}
