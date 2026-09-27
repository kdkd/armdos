// The alt-CPU LED: like the "8088" lamp on dual-processor machines of the era,
// it glows while ELBOW is running x86 code (ARCH.md, system-board port F5h).
// Brightness is the share of emulated time spent in x86 code over the last
// ~100 ms, read from the machine's running total (machine.altCpuNs()).
import { $ } from './util.js';

const leds = () => [$('x86Led'), $('abX86Led')].filter(Boolean);
let prevOn = 0, prevT = 0, shown = -1;

function tick() {
  const m = window.armdos?.powered ? window.armdos.machine : null;
  let duty = 0;
  if (m && m.altCpuNs) {
    const on = m.altCpuNs(), t = m.timeNs();
    if (t > prevT && prevT) duty = Math.max(0, Math.min(1, (on - prevOn) / (t - prevT)));
    if (m.altCpu?.on && t === prevT) duty = 1;          // paused while lit
    prevOn = on; prevT = t;
  } else { prevOn = 0; prevT = 0; }
  const v = $('x86Val');
  if (v) v.textContent = m ? String(Math.round(duty * 100)) : '--';
  const level = Math.round(duty * 20) / 20;
  if (level === shown) return;
  shown = level;
  for (const el of leds()) {
    el.classList.toggle('on', level > 0);
    el.style.opacity = level > 0 ? String(0.35 + 0.65 * level) : '';
  }
  document.documentElement.dataset.x86 = level > 0 ? 'on' : 'off';
}
setInterval(tick, 100);
