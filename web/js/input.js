// Keyboard, mouse, paste and the mobile keyboard.
// The screen (canvas) takes the keyboard when focused; every key, Escape
// included, goes to the machine. Clicking anywhere else lets go.

import { CHARMAP } from '../emu/dev/keymap.js';
import { $, prefs } from './util.js';
import { LAYOUTS, CP_INDEX, cpByte } from './kbdlayouts.js';

// "Keyboard: follow my computer's layout" (off by default): a printable key
// (KeyboardEvent.key is one character) is typed as the character the visitor's
// own layout made - the key itself when the machine's layout (KEYB's, or US)
// gives that character for the same key, else by Alt+keypad digits with the
// character's byte in the screen's code page (port F7h, ARCH.md 4.6), as a
// user of the real machine would type a character that is not on the keys.
// That bypasses KEYB's remapping for printable keys; everything else (Ctrl
// and Alt combinations, cursor and function keys) stays physical. Host dead
// keys are left to the browser; AltGr is not passed on (it only picks
// characters). Off: every key goes by its position, as always.
const NUMPAD = ['Numpad0', 'Numpad1', 'Numpad2', 'Numpad3', 'Numpad4', 'Numpad5', 'Numpad6', 'Numpad7', 'Numpad8', 'Numpad9'];

export class Input {
  constructor({ machine, sound, onFocusChange }) {
    this.machine = machine;             // () => Machine | null
    this.sound = sound;
    this.canvas = $('screen');
    this.held = new Set();
    this.mods = new Set();              // modifiers to apply to the next tap()
    this.hintTimer = 0;
    this.onFocusChange = onFocusChange || (() => {});
    this.follow = !!prefs.get('followHost', false);
    this.followed = new Map();          // e.code -> the code sent for it (fast path), or null (typed by Alt+keypad)
    this.bindKeyboard(); this.bindMouse(); this.bindPaste(); this.bindMobile();
  }

  setFollow(on) { this.releaseAll(); this.follow = !!on; prefs.set('followHost', this.follow); }

  /** The code page of the screen (port F7h) and the machine's layout legends. */
  nlsState(m) {
    const nls = m.nls || { keyb: 0, cp: 0 };
    const cp = CP_INDEX[nls.cp & 7] || 437;
    const L = LAYOUTS[nls.keyb & 31];
    let keys = null;
    if (L) { let kcp = CP_INDEX[(nls.keyb >> 5) & 7]; if (!L.keys[kcp]) kcp = L.cps[0]; keys = L.keys[kcp]; }
    return { cp, keys };
  }
  /** Type character ch for the key e.code (follow mode). */
  followType(m, e, ch) {
    const { cp, keys } = this.nlsState(m);
    const shift = e.shiftKey, altgr = e.getModifierState && e.getModifierState('AltGraph');
    // the key itself, when the machine's layout gives the same character for it
    let mine = '';
    if (!altgr) {
      if (keys) { const k = keys[e.code]; mine = k ? (shift ? k[1] : k[0]) : ''; if (k && k[3] & (shift ? 2 : 1)) mine = ''; }
      else { for (const [c, [code, sh]] of Object.entries(CHARMAP)) if (code === e.code && sh === shift && c.length === 1 && c >= ' ') { mine = c; break; } }
    }
    if (mine && mine === ch) {
      if (m.keyDown(e.code)) { this.followed.set(e.code, e.code); this.held.add(e.code); }
      return;
    }
    const b = cpByte(cp, ch);
    this.followed.set(e.code, null);
    if (b <= 0) return;
    const digits = String(b).padStart(3, '0');
    m.keyDown('AltLeft');
    for (const d of digits) { m.keyDown(NUMPAD[+d]); m.keyUp(NUMPAD[+d]); }
    m.keyUp('AltLeft');
  }

  bindKeyboard() {
    const c = this.canvas, hint = $('kbdHint'), nudge = $('kbdNudge');
    // Until the first key typed into the machine (remembered), a switched-on machine that
    // does not have the keyboard says so on the screen.
    this.typed = !!prefs.get('typedOnce', false);
    const updateNudge = () => { nudge.hidden = this.typed || !this.machine() || document.activeElement === c; };
    setInterval(updateNudge, 400);
    // Typing while nothing on the page takes text: the screen takes the keyboard (and the
    // key), scrolled into view. Not for text fields and dialogs, Tab/Escape/shortcuts, a
    // control reached by keyboard navigation (Tab), or touch screens.
    const PASS = new Set(['Enter', 'Backspace', 'Delete', 'Insert', 'Home', 'End', 'PageUp', 'PageDown',
      'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10']);
    // (how the focus got where it is: Tab = keyboard navigation, to be left alone; a
    // pointer = a click, after which typing is meant for the PC. :focus-visible can't
    // tell: the keypress itself turns it on.)
    let tabbed = false;
    document.addEventListener('pointerdown', () => { tabbed = false; }, true);
    document.addEventListener('keydown', (e) => {
      if (e.key === 'Tab') { tabbed = true; return; }
      if (document.activeElement === c || !this.machine() || e.defaultPrevented) return;
      if (e.ctrlKey || e.metaKey || e.altKey || matchMedia('(pointer: coarse)').matches) return;
      if (!(e.key.length === 1 || PASS.has(e.key))) return;
      const t = e.target instanceof Element ? e.target : null;
      if (t && t.closest('input, textarea, select, [contenteditable="true"], [contenteditable=""], .modal')) return;
      if (t && t !== document.body && tabbed) return;
      e.preventDefault(); e.stopPropagation();
      c.focus({ preventScroll: true });
      const r = $('tube').getBoundingClientRect();
      if (r.top < 0 || r.bottom > innerHeight) $('tube').scrollIntoView({ block: 'center', behavior: 'smooth' });
      c.dispatchEvent(new KeyboardEvent('keydown', { key: e.key, code: e.code, shiftKey: e.shiftKey, repeat: e.repeat, bubbles: true, cancelable: true }));
    }, true);
    c.addEventListener('keydown', (e) => {
      const m = this.machine(); if (!m) return;
      if (!this.typed) { this.typed = true; prefs.set('typedOnce', true); nudge.hidden = true; }
      // Ctrl+Alt+M: capture / release the mouse (the M never reaches DOS; Ctrl and Alt are released as usual)
      if (e.code === 'KeyM' && e.ctrlKey && e.altKey && !e.shiftKey) { e.preventDefault(); if (!e.repeat) this.toggleMouse(); return; }
      // Windows sends a Ctrl press with every AltGr press: take it back, so AltGr is AltGr (KEYB's third level)
      if (e.code === 'AltRight' && this.lastCtrl && this.held.has('ControlLeft') && e.timeStamp - this.lastCtrl < 50) {
        m.keyUp('ControlLeft'); this.held.delete('ControlLeft'); this.phantomCtrl = true;
      }
      if (e.code === 'ControlLeft' && !e.repeat) this.lastCtrl = e.timeStamp;
      if (this.follow) {
        if (e.key === 'Dead' || e.key === 'AltGraph' || (this.phantomCtrl && (e.code === 'ControlLeft' || e.code === 'AltRight'))) { e.preventDefault(); return; }
        const altgr = e.getModifierState && e.getModifierState('AltGraph');
        if ([...e.key].length === 1 && !e.metaKey && !e.code.startsWith('Numpad') && (altgr || (!e.ctrlKey && !e.altKey))) {
          e.preventDefault();
          if (e.repeat && this.followed.get(e.code)) { m.keyDown(e.code); return; }
          this.followType(m, e, e.key);
          return;
        }
      }
      if (e.repeat && this.held.has(e.code)) { m.keyDown(e.code); e.preventDefault(); return; }   // typematic
      if (m.keyDown(e.code)) { this.held.add(e.code); e.preventDefault(); }
    });
    c.addEventListener('keyup', (e) => {
      const m = this.machine(); if (!m) return;
      if (e.code === 'ControlLeft' && this.phantomCtrl) { this.phantomCtrl = false; e.preventDefault(); return; }
      if (this.followed.has(e.code)) {
        const sent = this.followed.get(e.code);
        this.followed.delete(e.code);
        if (sent) { m.keyUp(sent); this.held.delete(sent); }
        e.preventDefault();
        return;
      }
      if (this.follow && (e.key === 'Dead' || e.key === 'AltGraph')) { e.preventDefault(); return; }
      if (m.keyUp(e.code)) e.preventDefault();
      this.held.delete(e.code);
    });
    c.addEventListener('focus', () => {
      nudge.hidden = true;
      $('tube').classList.add('focused');
      hint.hidden = false; hint.classList.remove('quiet');
      clearTimeout(this.hintTimer); this.hintTimer = setTimeout(() => hint.classList.add('quiet'), 3200);
      this.onFocusChange(true);
    });
    c.addEventListener('blur', () => {
      $('tube').classList.remove('focused');
      hint.hidden = true;
      this.releaseAll();
      this.onFocusChange(false);
    });
    // a click on the screen focuses it (Shift+click captures the mouse)
    c.addEventListener('mousedown', (e) => {
      if (e.shiftKey && this.machine()) { e.preventDefault(); this.lockMouse(); }
    });
    c.addEventListener('click', () => c.focus({ preventScroll: true }));
    window.addEventListener('blur', () => this.releaseAll());
  }
  releaseAll() {
    const m = this.machine();
    if (m) for (const code of this.held) m.keyUp(code);
    this.held.clear();
    this.followed.clear();
    this.phantomCtrl = false;
  }
  setLeds(bits) {
    $('ledScroll').classList.toggle('on', !!(bits & 1));
    $('ledNum').classList.toggle('on', !!(bits & 2));
    $('ledCaps').classList.toggle('on', !!(bits & 4));
  }

  // ------------------------------------------------------------ mouse (PS/2 via Pointer Lock)
  bindMouse() {
    const c = this.canvas, btn = $('mouseBtn');
    btn.onclick = () => { if (document.pointerLockElement === c) document.exitPointerLock(); else this.lockMouse(); };
    document.addEventListener('pointerlockchange', () => {
      const on = document.pointerLockElement === c;
      btn.classList.toggle('on', on);
      btn.textContent = on ? 'Mouse: captured (Esc)' : 'Mouse';
      const fs = $('fsMouse');
      if (fs) { fs.textContent = on ? 'Mouse: captured' : 'Mouse: free (click to capture)'; fs.classList.toggle('on', on); }
      if (!on) { const m = this.machine(); if (m) m.mouseButtons(0); }
    });
    const bits = (b) => (b & 1) | ((b & 2) ? 2 : 0) | ((b & 4) ? 4 : 0);
    document.addEventListener('mousemove', (e) => { if (document.pointerLockElement === c) this.machine()?.mouseMove(e.movementX, e.movementY); });
    document.addEventListener('mousedown', (e) => { if (document.pointerLockElement === c) { e.preventDefault(); this.machine()?.mouseButtons(bits(e.buttons)); } });
    document.addEventListener('mouseup', (e) => { if (document.pointerLockElement === c) this.machine()?.mouseButtons(bits(e.buttons)); });
    c.addEventListener('contextmenu', (e) => { if (document.pointerLockElement === c) e.preventDefault(); });
  }
  toggleMouse() {
    if (document.pointerLockElement === this.canvas) document.exitPointerLock(); else this.lockMouse();
  }
  lockMouse() {
    const c = this.canvas;
    c.focus({ preventScroll: true });
    try { const p = c.requestPointerLock(); if (p && p.catch) p.catch(() => {}); } catch {}
  }

  // ------------------------------------------------------------ typing text
  /** Queue text to be typed (paced like a typist by the machine). */
  typeText(text) {
    const m = this.machine(); if (!m) return;
    text = text.replace(/\r\n?/g, '\n');
    const q = m.typeQ;
    const press = (codes) => { for (const c of codes) q.push({ code: c, down: true }); for (const c of codes.slice().reverse()) q.push({ code: c, down: false }); };
    const cp = this.nlsState(m).cp;
    for (const ch of text) {
      const map = CHARMAP[ch];
      if (!map) {                       // not on the US keys: Alt + the code-page byte on the keypad
        const b = cpByte(cp, ch);
        if (b > 0) {
          q.push({ code: 'AltLeft', down: true });
          for (const d of String(b).padStart(3, '0')) { q.push({ code: NUMPAD[+d], down: true }); q.push({ code: NUMPAD[+d], down: false }); }
          q.push({ code: 'AltLeft', down: false });
        }
        continue;
      }
      press(map[1] ? ['ShiftLeft', map[0]] : [map[0]]);
    }
    if (m.typeNextNs < m.timeNs()) m.typeNextNs = m.timeNs();
    m.reschedule();
  }
  tap(code) {
    const m = this.machine(); if (!m) return;
    const codes = [...this.mods, code];
    const q = m.typeQ;
    for (const c of codes) q.push({ code: c, down: true });
    for (const c of codes.slice().reverse()) q.push({ code: c, down: false });
    if (m.typeNextNs < m.timeNs()) m.typeNextNs = m.timeNs();
    m.reschedule();
    this.clearMods();
  }
  clearMods() { this.mods.clear(); }

  bindPaste() {
    const modal = $('pasteModal'), ta = $('pasteText');
    $('pasteBtn').onclick = async () => {
      modal.hidden = false; ta.value = '';
      try { if (navigator.clipboard && navigator.clipboard.readText) ta.value = await navigator.clipboard.readText(); } catch {}
      ta.focus(); ta.select();
    };
    $('pasteCancel').onclick = () => { modal.hidden = true; };
    modal.addEventListener('keydown', (e) => { if (e.key === 'Escape') modal.hidden = true; });
    $('pasteForm').onsubmit = (e) => {
      e.preventDefault();
      modal.hidden = true;
      this.typeText(ta.value);
      this.canvas.focus({ preventScroll: true });
    };
  }

  // ------------------------------------------------------------ the device's own keyboard (phones, tablets)
  // iOS opens its keyboard only when a real, visible, enabled text field is
  // focused synchronously inside the tap handler. The field (#softKbd) sits on
  // screen at 1px, nearly transparent, 16px text (no zoom), and always holds a
  // sentinel so Backspace has something to delete. Input arrives as
  // beforeinput/input events (virtual keys have no useful key codes).
  bindMobile() {
    const input = this.soft = $('softKbd');
    const SENT = '    ';
    const reset = () => { input.value = SENT; try { input.setSelectionRange(SENT.length, SENT.length); } catch {} };
    reset();
    this.openSoftKeyboard = () => {             // must run inside the user's tap
      reset();
      input.focus({ preventScroll: true });
      document.documentElement.classList.add('soft-kbd-open');
      this.keepScreenVisible();
    };
    $('softKbdBtn').addEventListener('click', this.openSoftKeyboard);
    input.addEventListener('blur', () => document.documentElement.classList.remove('soft-kbd-open'));
    let composing = false;
    const handle = (type, data) => {
      if (type === 'insertText' || type === 'insertReplacementText' || type === 'insertFromPaste' || type === 'insertCompositionText') {
        const s = data || '';
        if (s.length === 1 && this.mods.size && CHARMAP[s.toLowerCase()]) this.tap(CHARMAP[s.toLowerCase()][0]);
        else this.typeText(s);
        return true;
      }
      if (type === 'insertLineBreak' || type === 'insertParagraph') { this.tap('Enter'); return true; }
      if (type === 'deleteContentBackward' || type === 'deleteWordBackward' || type === 'deleteSoftLineBackward') { this.tap('Backspace'); return true; }
      if (type === 'deleteContentForward') { this.tap('Delete'); return true; }
      return false;
    };
    input.addEventListener('beforeinput', (e) => {
      if (composing || !e.cancelable) return;       // composition (Android): handled from the value in 'input'
      if (handle(e.inputType, e.data)) e.preventDefault();
    });
    input.addEventListener('compositionstart', () => { composing = true; });
    input.addEventListener('compositionend', () => { composing = false; this.diffSoft(SENT, reset); });
    input.addEventListener('input', () => { if (!composing) this.diffSoft(SENT, reset); });
    // hardware keys and some Android keyboards send real keydowns for Enter/Backspace
    input.addEventListener('keydown', (e) => {
      if (e.isComposing || e.keyCode === 229) return;
      if (e.key === 'Enter') { e.preventDefault(); this.tap('Enter'); }
      else if (e.key === 'Backspace') { e.preventDefault(); this.tap('Backspace'); }
      else if (e.key === 'Escape' || e.key === 'Tab' || e.key.startsWith('Arrow') || /^F\d+$/.test(e.key)) {
        e.preventDefault(); this.tap(e.key === 'Escape' ? 'Escape' : e.key === 'Tab' ? 'Tab' : e.key);
      } else if (e.ctrlKey && e.key.length === 1 && CHARMAP[e.key.toLowerCase()]) {
        e.preventDefault(); this.mods.add('ControlLeft'); this.tap(CHARMAP[e.key.toLowerCase()][0]);
      }
    });
    // on touch screens, tapping the monitor also brings up the keyboard
    this.canvas.addEventListener('click', (e) => {
      if (matchMedia('(pointer: coarse)').matches && document.activeElement !== input && !document.pointerLockElement) this.openSoftKeyboard();
    });
    if (window.visualViewport) visualViewport.addEventListener('resize', () => { if (document.activeElement === input) this.keepScreenVisible(); });
  }
  /** Whatever the field holds beyond the sentinel was typed; anything missing was deleted. */
  diffSoft(SENT, reset) {
    const v = this.soft.value;
    if (v.startsWith(SENT)) { const add = v.slice(SENT.length); if (add) { if (add === '\n') this.tap('Enter'); else this.typeText(add); } }
    else if (v.length < SENT.length && SENT.startsWith(v)) for (let k = v.length; k < SENT.length; k++) this.tap('Backspace');
    else if (v.length) this.typeText(v.replace(/ /g, ''));
    reset();
  }
  /** With the keyboard up, scroll so the whole screen sits above it. */
  keepScreenVisible() {
    const tube = $('tube'), vv = window.visualViewport;
    const go = () => {
      const r = tube.getBoundingClientRect();
      const top = vv ? vv.offsetTop : 0, h = vv ? vv.height : innerHeight;
      if (document.documentElement.classList.contains('app')) return;      // the app layout is fixed
      let dy = 0;
      if (r.height > h - 8) dy = r.top - top - 4;                         // taller than the space: show the top
      else if (r.bottom > top + h - 4) dy = r.bottom - (top + h) + 8;
      else if (r.top < top) dy = r.top - top - 8;
      if (Math.abs(dy) > 2) window.scrollBy(0, dy);
    };
    requestAnimationFrame(go); setTimeout(go, 350);
  }
}
