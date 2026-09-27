// The on-screen ARM/AT keyboard and the game pad. Every key sends a real
// make on press and a real break on release (machine.keyDown/keyUp), so
// games see keys held; multi-touch works (hold an arrow, tap Ctrl to fire).
//
// Keyboard layout: Shift, Ctrl and Alt are sticky. Tap one and it stays down
// (lit) until the next key is released, so Ctrl then C is Ctrl-C and
// Ctrl, Alt, Del reboots. Hold one while pressing another and it acts like a
// normal modifier. Tap a lit one again to let it go.
// Game pad layout: every key is a plain hold-to-press key. Its JOY switch makes the D-pad
// and the four big buttons a joystick in the game port instead (js/joystick.js): the
// D-pad pushes stick A to its stops, FIRE/STRAFE/RUN/OPEN are joystick buttons 1-4 (DOOM's
// default joyb_fire 0 / strafe 1 / speed 2 / use 3); the small keys stay keys.

// Layouts: the key caps follow the layout KEYB.COM has active in the machine
// (system board port F6h, ARCH.md 4.6: bits 0-4 the layout, 5-7 its code
// page) - the legends in js/kbdlayouts.js are generated from KEYBOARD.SYS by
// apps/keyb/tools/mklegends.mjs. With a layout active the keyboard grows the
// 102nd key (<>, left of Z) where the layout uses it and an AltGr key, and each
// cap shows its unshifted and shifted characters, the AltGr one in small type
// (dead accent keys in red). No KEYB (or Ctrl+Alt+F1): the US caps, as before.

import { $, el, prefs } from './util.js';
import { LAYOUTS, CP_INDEX } from './kbdlayouts.js';

const MODS = new Set(['ShiftLeft', 'ShiftRight', 'ControlLeft', 'AltLeft', 'AltRight']);
const REPEAT_DELAY = 480, REPEAT_RATE = 70;

// [label, code, width (units), class]
const FULL = [
  [['Esc', 'Escape', 1, 'fn'], ['F1', 'F1'], ['F2', 'F2'], ['F3', 'F3'], ['F4', 'F4'], ['F5', 'F5'], ['F6', 'F6'], ['F7', 'F7'], ['F8', 'F8'], ['F9', 'F9'], ['F10', 'F10'], ['F11', 'F11'], ['F12', 'F12'], ['Ins', 'Insert', 1, 'fn'], ['Del', 'Delete', 1, 'fn']],
  [['`', 'Backquote'], ['1', 'Digit1'], ['2', 'Digit2'], ['3', 'Digit3'], ['4', 'Digit4'], ['5', 'Digit5'], ['6', 'Digit6'], ['7', 'Digit7'], ['8', 'Digit8'], ['9', 'Digit9'], ['0', 'Digit0'], ['-', 'Minus'], ['=', 'Equal'], ['⌫', 'Backspace', 2, 'fn']],
  [['Tab', 'Tab', 1.5, 'fn'], ['Q', 'KeyQ'], ['W', 'KeyW'], ['E', 'KeyE'], ['R', 'KeyR'], ['T', 'KeyT'], ['Y', 'KeyY'], ['U', 'KeyU'], ['I', 'KeyI'], ['O', 'KeyO'], ['P', 'KeyP'], ['[', 'BracketLeft'], [']', 'BracketRight'], ['\\', 'Backslash', 1.5]],
  [['Caps', 'CapsLock', 1.75, 'fn'], ['A', 'KeyA'], ['S', 'KeyS'], ['D', 'KeyD'], ['F', 'KeyF'], ['G', 'KeyG'], ['H', 'KeyH'], ['J', 'KeyJ'], ['K', 'KeyK'], ['L', 'KeyL'], [';', 'Semicolon'], ["'", 'Quote'], ['Enter', 'Enter', 2.25, 'fn enter']],
  [['Shift', 'ShiftLeft', 2.25, 'fn mod'], ['Z', 'KeyZ'], ['X', 'KeyX'], ['C', 'KeyC'], ['V', 'KeyV'], ['B', 'KeyB'], ['N', 'KeyN'], ['M', 'KeyM'], [',', 'Comma'], ['.', 'Period'], ['/', 'Slash'], [null, null, 0.75], ['↑', 'ArrowUp', 1, 'arrow'], [null, null, 1]],
  [['Ctrl', 'ControlLeft', 1.25, 'fn mod'], ['Alt', 'AltLeft', 1.25, 'fn mod'], ['', 'Space', 5, 'space'], ['Home', 'Home', 1, 'fn sm'], ['End', 'End', 1, 'fn sm'], ['PgUp', 'PageUp', 1, 'fn sm'], ['PgDn', 'PageDown', 1, 'fn sm'], [null, null, 0.5], ['←', 'ArrowLeft', 1, 'arrow'], ['↓', 'ArrowDown', 1, 'arrow'], ['→', 'ArrowRight', 1, 'arrow']],
];
// the game pad: a D-pad and big buttons (Wolf3D/DOOM: Ctrl fire, Alt strafe, Space open, Shift run)
const PAD_DPAD = [[null, ['↑', 'ArrowUp', '0,-1'], null], [['←', 'ArrowLeft', '-1,0'], null, ['→', 'ArrowRight', '1,0']], [null, ['↓', 'ArrowDown', '0,1'], null]];
// [label, key, key name, class, joystick button (JOY mode)]
const PAD_BUTTONS = [['FIRE', 'ControlLeft', 'Ctrl', 'big red', 0], ['OPEN', 'Space', 'Space', 'big', 3], ['STRAFE', 'AltLeft', 'Alt', 'big', 1], ['RUN', 'ShiftLeft', 'Shift', 'big', 2]];
const PAD_SMALL = [['Esc', 'Escape'], ['Enter', 'Enter'], ['Y', 'KeyY'], ['N', 'KeyN'], ['Tab', 'Tab'], ['1', 'Digit1'], ['2', 'Digit2'], ['3', 'Digit3'], ['4', 'Digit4'], ['5', 'Digit5'], ['6', 'Digit6'], ['7', 'Digit7']];

const LEGEND_CSS = `
.pckeys .k .lg-s, .pckeys .k .lg-b, .pckeys .k .lg-a { position: absolute; font-weight: 600; line-height: 1; pointer-events: none; }
.pckeys .k .lg-s { left: 22%; top: 5px; }
.pckeys .k .lg-b { left: 22%; bottom: 7px; }
.pckeys .k .lg-a { right: 14%; bottom: 6px; font-size: 10px; color: #5b5346; }
.pckeys .k .lg-1 { position: absolute; left: 0; right: 0; top: 50%; transform: translateY(-50%); pointer-events: none; }
.pckeys .k .dead { color: #b3261c; }
.pckeys .k.lg { font-size: 13px; }
@media (max-width: 560px) { .pckeys .k .lg-a { font-size: 8px; } .pckeys .k.lg { font-size: 11px; } .pckeys .k .lg-s { top: 3px; } .pckeys .k .lg-b { bottom: 5px; } }
`;

export class PcKeys {
  constructor({ machine, onChange, joystick }) {
    this.machine = machine;              // () => Machine | null
    this.joystick = joystick || null;    // JoystickHost (js/joystick.js): the pad's JOY mode
    this.joyMode = !!prefs.get('padJoy', false);
    this.onChange = onChange || (() => {});
    this.root = $('pcKeys');
    this.mode = null;                    // null (hidden) | 'full' | 'pad'
    this.down = new Map();               // pointerId -> { code, btn, timer }
    this.latched = new Set();            // sticky modifiers held down
    this.usedWhileHeld = new Set();      // modifiers that were combined while physically held
    this.root.addEventListener('pointerdown', (e) => this.press(e));
    for (const t of ['pointerup', 'pointercancel', 'lostpointercapture']) this.root.addEventListener(t, (e) => this.release(e));
    this.root.addEventListener('contextmenu', (e) => e.preventDefault());
    document.head.append(el('style', { text: LEGEND_CSS }));
    this.layout = 0;                     // port F6h: 0 = US (no KEYB)
    this.cpIndex = 0;                    // port F7h: the screen's code page (0 = 437)
    this.root.dataset.layout = 'US';
    setInterval(() => this.syncLayout(), 250);
  }

  /** The layout KEYB publishes (port F6h) and the screen code page (F7h). */
  syncLayout() {
    const m = this.machine();
    const nls = m && m.nls ? m.nls : { keyb: 0, cp: 0 };
    if (nls.keyb === this.layout && nls.cp === this.cpIndex) return;
    this.layout = nls.keyb; this.cpIndex = nls.cp;
    const L = this.layoutInfo();
    this.root.dataset.layout = L ? L.code : 'US';
    this.root.dataset.cp = String(L ? L.cp : CP_INDEX[this.cpIndex & 7] || 437);
    if (this.mode === 'full') this.show('full');
  }
  /** { code, name, cp, keys } of the active layout, or null for US. */
  layoutInfo() {
    const L = LAYOUTS[this.layout & 31];
    if (!L) return null;
    let cp = CP_INDEX[(this.layout >> 5) & 7];
    if (!L.keys[cp]) cp = L.cps[0];
    return { code: L.code, name: L.name, cp, keys: L.keys[cp] };
  }

  show(mode) {
    this.releaseAll();
    this.mode = mode;
    this.root.hidden = !mode;
    this.root.className = 'pckeys' + (mode ? ' ' + mode : '');
    this.root.textContent = '';
    if (mode === 'full') this.buildFull();
    else if (mode === 'pad') this.buildPad();
    this.joystick?.setPad({ on: mode === 'pad' && this.joyMode, x: 0, y: 0, buttons: 0 });
    document.documentElement.classList.toggle('keys-open', !!mode);
    document.documentElement.classList.toggle('pad-open', mode === 'pad');
    this.onChange(mode);
  }
  key(label, code, cls = '', flex = 1) {
    const b = el('button', { class: 'k ' + cls, 'data-code': code, tabindex: '-1', type: 'button' });
    b.textContent = label;
    b.style.flexGrow = String(flex);
    b.style.flexBasis = '0';
    return b;
  }
  buildFull() {
    const L = this.layoutInfo();
    for (let row of FULL) {
      if (L && row[0][1] === 'ShiftLeft' && L.keys.IntlBackslash)       // the 102nd key
        row = [['Shift', 'ShiftLeft', 1.25, 'fn mod'], ['<', 'IntlBackslash'], ...row.slice(1)];
      if (L && row[0][1] === 'ControlLeft')                            // AltGr
        row = row.flatMap((k) => k[1] === 'Space' ? [['', 'Space', 3.75, 'space'], ['AltGr', 'AltRight', 1.25, 'fn mod']] : [k]);
      const r = el('div', { class: 'krow' });
      for (const [label, code, w = 1, cls = ''] of row) {
        if (!code) { const g = el('span', { class: 'kgap' }); g.style.flexGrow = String(w); g.style.flexBasis = '0'; r.append(g); continue; }
        const lg = L && L.keys[code];
        const b = this.key(lg ? '' : label, code, cls + (lg ? ' lg' : ''), w);
        if (lg) this.legend(b, lg);
        r.append(b);
      }
      this.root.append(r);
    }
  }
  /** A layout key cap: unshifted / shifted characters, AltGr small (dead accents red). */
  legend(b, [base, shift, altgr = '', dead = 0]) {
    const span = (cls, t, d) => el('span', { class: cls + (d ? ' dead' : ''), text: t });
    const letter = base && base.toUpperCase() === shift && base !== shift;
    if (letter || shift === base || !shift) b.append(span('lg-1', letter ? shift : base, dead & 1));
    else { b.append(span('lg-s', shift, dead & 2)); b.append(span('lg-b', base, dead & 1)); }
    if (altgr) b.append(span('lg-a', altgr, dead & 4));
    b.title = [base, shift, altgr].filter(Boolean).join(' ');
  }
  buildPad() {
    const dpad = el('div', { class: 'dpad' });
    const jt = el('button', { class: 'k joytoggle', type: 'button', tabindex: '-1', 'aria-pressed': String(this.joyMode),
      title: 'JOY: the D-pad and the big buttons are a joystick in the game port (201h) instead of keys' }, 'JOY');
    PAD_DPAD.forEach((row, r) => row.forEach((k, c) => {
      if (!k) { dpad.append(r === 1 && c === 1 ? jt : el('span')); return; }   // the JOY switch sits in the D-pad's hub
      const b = this.key(k[0], k[1], 'arrow'); b.dataset.dir = k[2]; dpad.append(b);
    }));
    const btns = el('div', { class: 'pad-buttons' });
    for (const [label, code, sub, cls, jb] of PAD_BUTTONS) {
      const b = this.key(label, code, cls); b.dataset.jb = String(jb); b.dataset.sub = sub;
      b.append(el('small', { text: this.joyMode ? 'Joy ' + (jb + 1) : sub })); btns.append(b);
    }
    const small = el('div', { class: 'pad-small' });
    for (const [label, code] of PAD_SMALL) small.append(this.key(label, code, 'sm'));
    this.root.append(el('div', { class: 'pad-left' }, dpad), el('div', { class: 'pad-right' }, btns, small));
    this.root.classList.toggle('joy', this.joyMode);
  }
  /** The pad's JOY switch. */
  setJoyMode(on) {
    this.releaseAll();
    this.joyMode = !!on;
    prefs.set('padJoy', this.joyMode);
    if (this.mode === 'pad') this.show('pad');
  }
  /** The joystick the held D-pad arrows and big buttons make (JOY mode). */
  padJoystick() {
    let x = 0, y = 0, buttons = 0;
    for (const d of this.down.values()) {
      if (!d.joy) continue;
      if (d.dir) { x += d.dir[0]; y += d.dir[1]; }
      if (d.jb !== undefined) buttons |= 1 << d.jb;
    }
    this.joystick?.setPad({ on: true, x: Math.max(-1, Math.min(1, x)), y: Math.max(-1, Math.min(1, y)), buttons });
  }

  // ------------------------------------------------------------ press / release
  press(e) {
    const btn = e.target.closest('.k');
    if (!btn) return;
    e.preventDefault();
    if (btn.classList.contains('joytoggle')) { this.setJoyMode(!this.joyMode); return; }
    if (this.mode === 'pad' && this.joyMode && this.joystick && (btn.dataset.dir || btn.dataset.jb !== undefined)) {
      try { btn.setPointerCapture(e.pointerId); } catch {}
      btn.classList.add('down');
      this.down.set(e.pointerId, { joy: true, btn, dir: btn.dataset.dir ? btn.dataset.dir.split(',').map(Number) : null,
        jb: btn.dataset.dir ? undefined : +btn.dataset.jb });
      this.padJoystick();
      if (navigator.vibrate) try { navigator.vibrate(6); } catch {}
      return;
    }
    const m = this.machine(); if (!m) return;
    try { btn.setPointerCapture(e.pointerId); } catch {}
    const code = btn.dataset.code;
    const sticky = this.mode === 'full' && MODS.has(code);
    if (sticky && this.latched.has(code)) {             // tap a lit modifier: let it go
      this.latched.delete(code); m.keyUp(code); this.paintMods();
      this.down.set(e.pointerId, { code, btn, noop: true });
      return;
    }
    // any modifier physically held right now gets marked as "combined"
    for (const d of this.down.values()) if (MODS.has(d.code)) this.usedWhileHeld.add(d.code);
    m.keyDown(code);
    btn.classList.add('down');
    const d = { code, btn, timer: 0 };
    if (!MODS.has(code) && code !== 'CapsLock') {         // typematic repeat, like a real keyboard
      d.timer = setTimeout(function rep() { m.keyDown(code); d.timer = setTimeout(rep, REPEAT_RATE); }, REPEAT_DELAY);
    }
    this.down.set(e.pointerId, d);
    if (navigator.vibrate) try { navigator.vibrate(6); } catch {}
  }
  release(e) {
    const d = this.down.get(e.pointerId);
    if (!d) return;
    this.down.delete(e.pointerId);
    clearTimeout(d.timer);
    d.btn.classList.remove('down');
    if (d.joy) { this.padJoystick(); return; }
    if (d.noop) return;
    const m = this.machine(); if (!m) return;
    const sticky = this.mode === 'full' && MODS.has(d.code);
    if (sticky) {
      if (this.usedWhileHeld.has(d.code)) { this.usedWhileHeld.delete(d.code); m.keyUp(d.code); }
      else { this.latched.add(d.code); }                  // a tap: latch it (still down)
      this.paintMods();
      return;
    }
    m.keyUp(d.code);
    if (!MODS.has(d.code) && this.latched.size) {          // a key was released: drop the latched modifiers
      for (const c of [...this.latched].reverse()) m.keyUp(c);
      this.latched.clear(); this.paintMods();
    }
  }
  paintMods() {
    for (const b of this.root.querySelectorAll('.k.mod')) b.classList.toggle('latched', this.latched.has(b.dataset.code));
  }
  releaseAll() {
    const m = this.machine();
    let joy = false;
    for (const d of this.down.values()) { clearTimeout(d.timer); if (d.joy) joy = true; else if (m && !d.noop) m.keyUp(d.code); }
    this.down.clear();
    if (joy) this.padJoystick();
    if (m) for (const c of this.latched) m.keyUp(c);
    this.latched.clear(); this.usedWhileHeld.clear();
    this.paintMods();
  }
}
