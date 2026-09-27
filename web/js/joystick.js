// The game port's joysticks (emu/dev/gameport.mjs, machine.joy): the browser's Gamepad API
// and the on-screen game pad (pckeys.js, JOY mode) move the potentiometers and press the
// buttons of the one or two analogue joysticks plugged into the ARM/AT's game port (201h).
//
//   first controller   left stick (or the D-pad, which wins while pressed) -> joystick A X/Y;
//                      face buttons south/east/west/north (Xbox A/B/X/Y) -> buttons 1/2/3/4
//                      (1-2 are stick A's, 3-4 are the port's stick-B buttons, which is how
//                      the Gravis/CH four-button sticks wired them); the right stick -> stick B
//                      X/Y (a second pair of axes: throttle/rudder style)
//   second controller  joystick B: its stick -> B X/Y, its south/east buttons -> buttons 3/4
//
// Stick positions (-1..1) become the potentiometer's 0-100 kOhm: -1 = 0 Ohm (left/up),
// centre 50 kOhm, +1 = 100 kOhm, with a radial dead zone around the centre (a cheap Gamepad
// stick rests a few percent off centre; a PC joystick had trim wheels for that), and the
// controller's round gate stretched to the PC joystick's square one.
// A joystick is "plugged in" while a controller is connected (the browser only reveals one
// after a button press: press a button before starting a game, which, like DOS games did,
// looks for the stick at start-up) or while the on-screen pad is in JOY mode.
//
//   const joy = new JoystickHost({ machine: () => m | null });
//   joy.frame()                       every animation frame (polls the Gamepad API)
//   joy.setPad({ on, x, y, buttons }) the on-screen pad's contribution
//   joy.onChange = (state) => ...     { connected, name, buttons, pads } for the lamps

const DEAD = 0.15;

// radial dead zone, then the round gate of a game controller's stick stretched to the square
// gate of a PC joystick (pushed into a corner, both potentiometers reach their ends)
function deadzone(x, y) {
  const r = Math.hypot(x, y);
  if (r < DEAD) return [0, 0];
  const mag = Math.min(1, (r - DEAD) / (1 - DEAD)), ux = x / r, uy = y / r;
  const k = mag / Math.max(Math.abs(ux), Math.abs(uy));
  return [Math.max(-1, Math.min(1, ux * k)), Math.max(-1, Math.min(1, uy * k))];
}
const pressed = (b) => !!b && (b.pressed || b.value > 0.5);

export class JoystickHost {
  constructor({ machine }) {
    this.machine = machine;
    this.onChange = null;
    this.pad = { on: false, x: 0, y: 0, buttons: 0 };
    this.applied = null;              // what the machine has: { m, plug: [a, b], axes: [4], buttons }
    this.shown = '';
    this.names = [];
    if (typeof window !== 'undefined') {
      window.addEventListener('gamepadconnected', () => this.frame());
      window.addEventListener('gamepaddisconnected', () => this.frame());
    }
  }
  setPad(p) { Object.assign(this.pad, p); this.frame(); }

  /** Read the controllers and the pad; tell the game port what changed. */
  frame() {
    let pads = [];
    try { pads = [...(navigator.getGamepads?.() || [])].filter((g) => g && g.connected); } catch {}
    const want = { plug: [false, false], axes: [0, 0, 0, 0], buttons: 0 };
    const g0 = pads[0], g1 = pads[1];
    if (g0) {
      want.plug[0] = true;
      const a = g0.axes || [], b = g0.buttons || [];
      let [x, y] = deadzone(a[0] || 0, a[1] || 0);
      // the D-pad (standard mapping 12-15) moves the stick all the way while pressed
      const dx = (pressed(b[15]) ? 1 : 0) - (pressed(b[14]) ? 1 : 0), dy = (pressed(b[13]) ? 1 : 0) - (pressed(b[12]) ? 1 : 0);
      if (dx || dy) { x = dx; y = dy; }
      want.axes[0] = x; want.axes[1] = y;
      for (let i = 0; i < 4; i++) if (pressed(b[i])) want.buttons |= 1 << i;
      if (!g1 && a.length >= 4) {         // the right stick: joystick B's axes
        want.plug[1] = true;
        [want.axes[2], want.axes[3]] = deadzone(a[2] || 0, a[3] || 0);
      }
    }
    if (g1) {
      want.plug[1] = true;
      const a = g1.axes || [], b = g1.buttons || [];
      let [x, y] = deadzone(a[0] || 0, a[1] || 0);
      const dx = (pressed(b[15]) ? 1 : 0) - (pressed(b[14]) ? 1 : 0), dy = (pressed(b[13]) ? 1 : 0) - (pressed(b[12]) ? 1 : 0);
      if (dx || dy) { x = dx; y = dy; }
      want.axes[2] = x; want.axes[3] = y;
      if (pressed(b[0])) want.buttons |= 4;
      if (pressed(b[1])) want.buttons |= 8;
    }
    const p = this.pad;
    if (p.on) {
      want.plug[0] = true;
      if (p.x || p.y || !g0) { want.axes[0] = p.x; want.axes[1] = p.y; }
      want.buttons |= p.buttons;
    }
    this.apply(want);
    const names = pads.map((g) => g.id);
    const state = `${names.join('|')}/${want.buttons}/${p.on}`;
    if (state !== this.shown) {
      this.shown = state;
      this.names = names;
      this.onChange?.({ connected: pads.length > 0, pad: p.on, names, buttons: want.buttons, plugged: want.plug.slice() });
    }
  }

  apply(want) {
    const m = this.machine();
    if (!m || !m.joy) { this.applied = null; return; }
    const j = m.joy;
    let a = this.applied;
    if (!a || a.m !== m || a.j !== j) a = this.applied = { m, j, plug: [null, null], axes: [NaN, NaN, NaN, NaN], buttons: -1 };
    for (let s = 0; s < 2; s++) {
      if (want.plug[s] !== a.plug[s]) {
        j.plug(s, want.plug[s]);
        a.plug[s] = want.plug[s]; a.axes[s * 2] = a.axes[s * 2 + 1] = want.plug[s] ? 0 : NaN;
        if (!want.plug[s]) a.buttons = -1;
      }
      if (!want.plug[s]) continue;
      for (const n of [s * 2, s * 2 + 1]) {
        const v = Math.round(want.axes[n] * 1000) / 1000;
        if (v !== a.axes[n]) { j.setAxis(n, v); a.axes[n] = v; }
      }
    }
    if (want.buttons !== a.buttons) {
      for (let i = 0; i < 4; i++) j.setButton(i, !!(want.buttons & (1 << i)));
      a.buttons = want.buttons;
    }
  }
}
