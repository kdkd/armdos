// The app layout: used when ARM-DOS runs from the home screen (display-mode
// standalone/fullscreen), with ?app, or on a phone held sideways. The page's
// prose and the drawn system unit give way to a slim bar of controls; the
// screen takes all the room that is left, with the keyboard or game pad below
// it (portrait) or floating over it (landscape).
//
// The bar's buttons press the real controls (power switch, turbo, reset...)
// so there is one code path for everything.

import { $, el, prefs } from './util.js';

const mm = (q) => window.matchMedia(q);
export const isStandalone = () => mm('(display-mode: standalone)').matches || mm('(display-mode: fullscreen)').matches || navigator.standalone === true;

export class AppLayout {
  constructor({ pckeys, input, disks, state }) {
    this.pckeys = pckeys; this.input = input; this.disks = disks; this.state = state;
    this.root = document.documentElement;
    this.forced = /[?&]app\b/.test(location.search);
    this.dismissed = sessionStorage.getItem('armdos.noapp') === '1';
    if (isStandalone()) this.root.classList.add('standalone');
    const click = (id) => () => $(id).click();
    $('abPower').onclick = click('powerBtn');
    $('abTurbo').onclick = click('turboBtn');
    $('abReset').onclick = click('resetBtn');
    $('abSound').onclick = click('soundBtn');
    $('abKbd').addEventListener('click', () => this.input.openSoftKeyboard());      // synchronous focus inside the tap
    $('abPc').onclick = () => this.setKeys(this.pckeys.mode === 'full' ? null : 'full');
    $('abPad').onclick = () => this.setKeys(this.pckeys.mode === 'pad' ? null : 'pad');
    $('abInsp').onclick = () => {
      const on = !this.root.classList.contains('insp-open');
      this.root.classList.toggle('insp-open', on);
      if (on && this.state.inspector) this.state.inspector.setOpen(true);
    };
    $('abExit').onclick = () => { this.dismissed = true; sessionStorage.setItem('armdos.noapp', '1'); this.forced = false; this.update(); };
    $('abDisk').onclick = () => this.toggleDiskMenu();
    document.addEventListener('pointerdown', (e) => { if (!e.target.closest('#diskMenu, #abDisk')) this.toggleDiskMenu(false); });
    $('pcKeysBtn').onclick = () => this.setKeys(this.pckeys.mode ? null : 'full');
    for (const q of ['(orientation: landscape)', '(display-mode: standalone)', '(pointer: coarse)']) mm(q).addEventListener?.('change', () => this.update());
    window.addEventListener('resize', () => this.update());
    // start with the keys the visitor last used (in the app layout, the pad or keyboard by default on touch)
    const saved = prefs.get('keys', null);
    this.update();
    if (saved) this.setKeys(saved);
    else if (this.app && mm('(pointer: coarse)').matches) this.setKeys('full');
    setInterval(() => this.sync(), 250);
    this.sync();
  }
  get app() { return this.root.classList.contains('app'); }
  update() {
    const phoneLandscape = mm('(pointer: coarse) and (orientation: landscape) and (max-height: 540px)').matches;
    const on = isStandalone() || this.forced || (phoneLandscape && !this.dismissed);
    this.root.classList.toggle('app', on);
    if (!on) this.root.classList.remove('insp-open');
  }
  setKeys(mode) {
    this.pckeys.show(mode);
    prefs.set('keys', mode);
    $('pcKeysBtn').setAttribute('aria-pressed', String(mode === 'full'));
    $('abPc').setAttribute('aria-pressed', String(mode === 'full'));
    $('abPad').setAttribute('aria-pressed', String(mode === 'pad'));
    if (mode && !this.app && mm('(max-width: 700px)').matches) requestAnimationFrame(() => $('monitor').scrollIntoView({ block: 'start', behavior: 'smooth' }));
  }
  toggleDiskMenu(force) {
    const menu = $('diskMenu');
    const open = force ?? menu.hidden;
    menu.hidden = !open;
    $('abDisk').setAttribute('aria-expanded', String(open));
    if (!open) return;
    menu.textContent = '';
    for (const d of this.disks.disks) {
      const b = el('button', { class: this.disks.inDrive === d ? 'cur' : '', disabled: d.missing ? true : false },
        el('span', { text: `${d.label}${d.sub ? ' · ' + d.sub : ''}` }), el('span', { class: 'sw' }));
      b.lastChild.style.background = d.colour || '#333';
      b.onclick = () => { this.toggleDiskMenu(false); if (this.disks.inDrive !== d) this.disks.insert(d); };
      menu.append(b);
    }
    const ej = el('button', { disabled: this.disks.inDrive ? false : true }, el('span', { text: 'Eject' }), el('span', { text: '⏏' }));
    ej.onclick = () => { this.toggleDiskMenu(false); this.disks.eject(); };
    menu.append(ej);
  }
  /** Mirror the machine's state on the bar. */
  sync() {
    if (!this.app) return;
    const s = this.state;
    $('abPower').setAttribute('aria-pressed', String(!!s.powered));
    $('abMhz').textContent = s.powered && s.machine ? String(s.machine.mhz) : '---';
    $('abTurbo').setAttribute('aria-pressed', String(!!s.turbo));
    $('abSound').setAttribute('aria-pressed', $('soundBtn').getAttribute('aria-pressed'));
    $('abDiskName').textContent = this.disks.inDrive ? this.disks.inDrive.label : 'empty';
  }
}
