// KeyboardEvent.code -> PC scan code set 1 (XT) make codes.
// Values > 0xFF are E0-prefixed (0xE000 | code). Pause and PrintScreen are
// special-cased in makeSeq/breakSeq.

export const SET1 = {
  Escape: 0x01, Digit1: 0x02, Digit2: 0x03, Digit3: 0x04, Digit4: 0x05, Digit5: 0x06, Digit6: 0x07,
  Digit7: 0x08, Digit8: 0x09, Digit9: 0x0A, Digit0: 0x0B, Minus: 0x0C, Equal: 0x0D, Backspace: 0x0E,
  Tab: 0x0F, KeyQ: 0x10, KeyW: 0x11, KeyE: 0x12, KeyR: 0x13, KeyT: 0x14, KeyY: 0x15, KeyU: 0x16,
  KeyI: 0x17, KeyO: 0x18, KeyP: 0x19, BracketLeft: 0x1A, BracketRight: 0x1B, Enter: 0x1C,
  ControlLeft: 0x1D, KeyA: 0x1E, KeyS: 0x1F, KeyD: 0x20, KeyF: 0x21, KeyG: 0x22, KeyH: 0x23,
  KeyJ: 0x24, KeyK: 0x25, KeyL: 0x26, Semicolon: 0x27, Quote: 0x28, Backquote: 0x29,
  ShiftLeft: 0x2A, Backslash: 0x2B, KeyZ: 0x2C, KeyX: 0x2D, KeyC: 0x2E, KeyV: 0x2F, KeyB: 0x30,
  KeyN: 0x31, KeyM: 0x32, Comma: 0x33, Period: 0x34, Slash: 0x35, ShiftRight: 0x36,
  NumpadMultiply: 0x37, AltLeft: 0x38, Space: 0x39, CapsLock: 0x3A,
  F1: 0x3B, F2: 0x3C, F3: 0x3D, F4: 0x3E, F5: 0x3F, F6: 0x40, F7: 0x41, F8: 0x42, F9: 0x43, F10: 0x44,
  NumLock: 0x45, ScrollLock: 0x46, Numpad7: 0x47, Numpad8: 0x48, Numpad9: 0x49, NumpadSubtract: 0x4A,
  Numpad4: 0x4B, Numpad5: 0x4C, Numpad6: 0x4D, NumpadAdd: 0x4E, Numpad1: 0x4F, Numpad2: 0x50,
  Numpad3: 0x51, Numpad0: 0x52, NumpadDecimal: 0x53, IntlBackslash: 0x56, F11: 0x57, F12: 0x58,
  NumpadEqual: 0x59, IntlRo: 0x73, IntlYen: 0x7D,
  NumpadEnter: 0xE01C, ControlRight: 0xE01D, NumpadDivide: 0xE035, AltRight: 0xE038,
  Home: 0xE047, ArrowUp: 0xE048, PageUp: 0xE049, ArrowLeft: 0xE04B, ArrowRight: 0xE04D,
  End: 0xE04F, ArrowDown: 0xE050, PageDown: 0xE051, Insert: 0xE052, Delete: 0xE053,
  MetaLeft: 0xE05B, MetaRight: 0xE05C, ContextMenu: 0xE05D,
  PrintScreen: -1, Pause: -2,
};

export function makeSeq(code) {
  const s = SET1[code];
  if (s === undefined) return null;
  if (s === -1) return [0xE0, 0x2A, 0xE0, 0x37];
  if (s === -2) return [0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5];
  return s > 0xFF ? [0xE0, s & 0xFF] : [s];
}
export function breakSeq(code) {
  const s = SET1[code];
  if (s === undefined) return null;
  if (s === -1) return [0xE0, 0xB7, 0xE0, 0xAA];
  if (s === -2) return [];                       // Pause has no break code
  return s > 0xFF ? [0xE0, (s & 0xFF) | 0x80] : [s | 0x80];
}

// US layout: character -> [code, shift]
export const CHARMAP = {};
(function () {
  const plain = '`1234567890-=qwertyuiop[]\\asdfghjkl;\'zxcvbnm,./ ';
  const shifted = '~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:"ZXCVBNM<>? ';
  const codes = ['Backquote', 'Digit1', 'Digit2', 'Digit3', 'Digit4', 'Digit5', 'Digit6', 'Digit7', 'Digit8', 'Digit9', 'Digit0', 'Minus', 'Equal',
    'KeyQ', 'KeyW', 'KeyE', 'KeyR', 'KeyT', 'KeyY', 'KeyU', 'KeyI', 'KeyO', 'KeyP', 'BracketLeft', 'BracketRight', 'Backslash',
    'KeyA', 'KeyS', 'KeyD', 'KeyF', 'KeyG', 'KeyH', 'KeyJ', 'KeyK', 'KeyL', 'Semicolon', 'Quote',
    'KeyZ', 'KeyX', 'KeyC', 'KeyV', 'KeyB', 'KeyN', 'KeyM', 'Comma', 'Period', 'Slash', 'Space'];
  for (let i = 0; i < codes.length; i++) {
    if (!(plain[i] in CHARMAP)) CHARMAP[plain[i]] = [codes[i], false];
    if (!(shifted[i] in CHARMAP)) CHARMAP[shifted[i]] = [codes[i], true];
  }
  CHARMAP['\r'] = ['Enter', false]; CHARMAP['\n'] = ['Enter', false];
  CHARMAP['\t'] = ['Tab', false]; CHARMAP['\b'] = ['Backspace', false]; CHARMAP['\x1b'] = ['Escape', false];
})();

// names usable in "{...}" escapes of typed text
export const KEYNAMES = {
  ESC: 'Escape', ENTER: 'Enter', TAB: 'Tab', BS: 'Backspace', BACKSPACE: 'Backspace', SPACE: 'Space',
  UP: 'ArrowUp', DOWN: 'ArrowDown', LEFT: 'ArrowLeft', RIGHT: 'ArrowRight',
  HOME: 'Home', END: 'End', PGUP: 'PageUp', PGDN: 'PageDown', INS: 'Insert', DEL: 'Delete',
  F1: 'F1', F2: 'F2', F3: 'F3', F4: 'F4', F5: 'F5', F6: 'F6', F7: 'F7', F8: 'F8', F9: 'F9', F10: 'F10', F11: 'F11', F12: 'F12',
  CTRL: 'ControlLeft', ALT: 'AltLeft', SHIFT: 'ShiftLeft', CAPS: 'CapsLock', NUMLOCK: 'NumLock', SCROLL: 'ScrollLock',
  PAUSE: 'Pause', PRTSC: 'PrintScreen', BREAK: 'Pause',
};
