// The ARM inspector panel: live registers, CPSR, disassembly around PC, MIPS,
// WFI sleep, the interrupt log, run control and a memory viewer. It polls the
// machine ~12 times a second while it is open and does nothing while closed.

import { disasm } from '../emu/disasm.js';
import { CP437 } from '../emu/render.js';
import { $, hex, el, prefs } from './util.js';
import { describe } from './debug.js';

const ALIAS = ['AX', 'BX', 'CX', 'DX', 'SI', 'DI', 'BP', 'DS', 'ES', '', '', '', '', 'SP', 'LR', 'PC'];
const MODES = { 0x10: 'USR', 0x11: 'FIQ', 0x12: 'IRQ', 0x13: 'SVC', 0x17: 'ABT', 0x1B: 'UND', 0x1F: 'SYS' };
const RAM_SIZE = 0x1000000, ROM_BASE = 0xFFF00000, FONT_BASE = 0x11000000;

export class Inspector {
  constructor(ctl) {
    this.ctl = ctl;                 // { machine(), dbg(), driver(), stats(), pause(), run(), step(), stepOver(), powered() }
    this.root = $('inspector');
    this.open = false;
    this.history = new Float32Array(120); this.histPos = 0;
    this.prevRegs = new Int32Array(16);
    this.log = []; this.logHold = false; this.logDirty = false;
    this.memAddr = 0xB8000; this.prevMem = null;
    this.lastIc = 0; this.lastHalted = 0; this.lastEmu = 0; this.wfi = 0;
    this.build();
    const wide = window.matchMedia('(min-width: 1181px)').matches;
    this.setOpen(prefs.get('inspector', wide));
    this.timer = setInterval(() => this.tick(), 80);
  }

  build() {
    const regs = $('regs');
    this.regEls = [];
    for (let k = 0; k < 16; k++) {
      const v = el('span', { class: 'rv', text: '--------' });
      const row = el('div', { class: 'reg' }, el('span', { class: 'rn', text: k === 13 ? 'sp' : k === 14 ? 'lr' : k === 15 ? 'pc' : 'r' + k }), el('span', { class: 'ra', text: ALIAS[k] }), v);
      // lay out r0..r7 in the left column and r8..r15 in the right
      row.style.gridRow = String((k % 8) + 1); row.style.gridColumn = k < 8 ? '1' : '2';
      regs.append(row); this.regEls.push({ row, v });
    }
    const cpsr = $('cpsr');
    this.flagEls = {};
    for (const f of ['N', 'Z', 'C', 'V', 'Q', 'I', 'F', 'T']) { const e = el('span', { class: 'flag', text: f, title: { N: 'negative', Z: 'zero', C: 'carry (CF)', V: 'overflow', Q: 'saturation', I: 'IRQs masked', F: 'FIQs masked', T: 'Thumb state' }[f] }); cpsr.append(e); this.flagEls[f] = e; }
    this.modeEl = el('span', { class: 'mode', text: 'MODE ---' }); cpsr.append(this.modeEl);

    this.spark = $('spark'); this.sctx = this.spark.getContext('2d');
    $('inspTab').onclick = () => this.setOpen(!this.open);
    $('dbgPause').onclick = () => this.ctl.pause();
    $('dbgRun').onclick = () => this.ctl.run();
    $('dbgStep').onclick = () => this.ctl.step();
    $('dbgOver').onclick = () => this.ctl.stepOver();
    $('bpSet').onclick = () => { const a = parseInt($('bpAddr').value, 16); if (!isNaN(a)) { this.ctl.dbg()?.addBp(a); $('bpAddr').value = ''; this.renderBps(); this.refresh(true); } };
    $('bpAddr').addEventListener('keydown', (e) => { if (e.key === 'Enter') $('bpSet').click(); });
    $('logPause').onclick = () => { this.logHold = !this.logHold; $('logPause').classList.toggle('lit', this.logHold); $('logPause').textContent = this.logHold ? 'HELD' : 'HOLD'; };
    $('hideTicks').onchange = () => { this.logDirty = true; };
    const setMem = (v) => { const a = v === 'pc' ? (this.ctl.machine()?.cpu.pc >>> 0) : parseInt(v, 16); if (!isNaN(a)) { this.memAddr = a >>> 0; this.prevMem = null; $('memAddr').value = hex(this.memAddr, 1); this.refresh(true); } };
    $('memAddr').addEventListener('change', (e) => setMem(e.target.value));
    $('memAddr').addEventListener('keydown', (e) => { if (e.key === 'Enter') setMem(e.target.value); });
    for (const b of document.querySelectorAll('[data-mem]')) b.onclick = () => setMem(b.dataset.mem);
    $('disasm').addEventListener('click', (e) => {
      const ln = e.target.closest('.ln'); if (!ln) return;
      this.ctl.dbg()?.toggleBp(parseInt(ln.dataset.a, 10)); this.renderBps(); this.refresh(true);
    });
    $('hex').addEventListener('wheel', (e) => {
      if (!this.open) return;
      e.preventDefault();
      this.memAddr = (this.memAddr + Math.sign(e.deltaY) * 8 * 2) >>> 0; this.prevMem = null; $('memAddr').value = hex(this.memAddr, 1); this.refresh(true);
    }, { passive: false });
  }

  setOpen(on) {
    this.open = on;
    this.root.classList.toggle('closed', !on);
    $('inspTab').setAttribute('aria-expanded', String(on));
    prefs.set('inspector', on);
    this.ctl.dbg()?.setLogging(on);
    if (on) this.refresh(true);
  }
  attach() {                 // a machine exists now (power on)
    const d = this.ctl.dbg();
    d.onLog = (e) => this.onLog(e);
    d.setLogging(this.open);
    this.lastIc = 0; this.lastHalted = 0; this.lastEmu = 0; this.prevMem = null;
    this.renderBps();
  }

  onLog(e) {
    if (this.logHold) return;
    const tick = (e.kind === 'irq' && e.irq === 0) || e.n === 0x1C || (e.kind === 'int' && e.n === 0x08);
    if (tick && $('hideTicks').checked) return;          // 18 times a second: they would drown everything else
    const log = this.log, last = log[log.length - 1];
    if (last && last.kind === e.kind && last.n === e.n && last.ah === e.ah && last.irq === e.irq) { last.count++; last.t = e.t; }
    else { e.count = 1; log.push(e); if (log.length > 400) log.splice(0, log.length - 300); }
    this.logDirty = true;
  }

  renderBps() {
    const d = this.ctl.dbg(), list = $('bpList');
    list.textContent = '';
    if (!d) return;
    for (const a of d.bps) list.append(el('button', { text: hex(a, 1), title: 'remove', onclick: () => { d.removeBp(a); this.renderBps(); this.refresh(true); } }));
  }

  tick() {
    if (!this.open || document.hidden) return;
    this.refresh(false);
  }

  refresh(force) {
    const m = this.ctl.machine();
    const powered = this.ctl.powered();
    const running = powered && this.ctl.running();
    $('runState').textContent = !powered ? 'NO POWER' : running ? (m.cpu.halted ? 'RUN · WFI' : 'RUNNING') : 'HALTED';
    $('runLamp').className = 'lamp ' + (!powered ? '' : running ? (m.cpu.halted ? 'idle' : 'run') : 'halt');
    for (const id of ['dbgPause', 'dbgStep', 'dbgOver', 'dbgRun']) $(id).disabled = !powered;
    $('dbgPause').classList.toggle('lit', powered && !running);
    if (!m) return;
    const cpu = m.cpu;

    // meters (only while running; a paused machine keeps its last values)
    if (running || force) {
      const s = this.ctl.stats();
      const ic = cpu.icount, emu = m.timeMs(), hal = m.haltedNs / 1e6;
      if (emu > this.lastEmu && this.lastEmu) this.wfi = Math.max(0, Math.min(1, (hal - this.lastHalted) / (emu - this.lastEmu)));
      this.lastIc = ic; this.lastEmu = emu; this.lastHalted = hal;
      if (running) {
        this.history[this.histPos++ % this.history.length] = s.effMips;
        $('mipsVal').textContent = s.effMips >= 10 ? s.effMips.toFixed(0) : s.effMips.toFixed(1);
        // host speed is only meaningful while the guest keeps the CPU busy for a while
        if (s.effMips > 5 && s.hostLoad > 0.02 && s.mips > (this.hostPeak || 0) * 0.5) this.hostPeak = Math.max(s.mips, (this.hostPeak || 0) * 0.98);
        $('hostVal').textContent = this.hostPeak ? this.hostPeak.toFixed(0) : '---';
        $('wfiVal').textContent = (this.wfi * 100).toFixed(0);
        this.drawSpark(m.mhz);
      }
      $('icountVal').textContent = Math.floor(ic + cpu._n).toLocaleString('en-US');
    }

    // registers
    const r = cpu.r;
    for (let k = 0; k < 16; k++) {
      const v = k === 15 ? cpu.pc : r[k];
      const e = this.regEls[k];
      e.v.textContent = hex(v);
      e.row.classList.toggle('chg', v !== this.prevRegs[k]);
      this.prevRegs[k] = v;
    }
    const ps = cpu.getCPSR();
    const bits = { N: 31, Z: 30, C: 29, V: 28, Q: 27, I: 7, F: 6, T: 5 };
    for (const [f, b] of Object.entries(bits)) this.flagEls[f].classList.toggle('on', ((ps >>> b) & 1) === 1);
    this.modeEl.innerHTML = `MODE <b>${MODES[cpu.mode] || '???'}</b> ${cpu.t ? 'THUMB' : 'ARM'}`;

    this.renderDisasm(cpu);
    const d = this.ctl.dbg();
    if (d && d.lastInt && this.log.length) { const [a, b] = describe(this.log[this.log.length - 1]); const t = `last: ${a}${b ? ' ' + b : ''}`; $('lastInt').textContent = t; $('lastInt').title = t; }
    if (this.logDirty) this.renderLog();
    this.renderHex(m);
  }

  drawSpark(mhz) {
    const c = this.sctx, W = this.spark.width, H = this.spark.height, h = this.history, n = h.length;
    c.clearRect(0, 0, W, H);
    let max = Math.max(mhz, 1); for (let k = 0; k < n; k++) if (h[k] > max) max = h[k];
    c.strokeStyle = 'rgba(255,179,64,.12)'; c.lineWidth = 1;
    for (let y = 0.25; y < 1; y += 0.25) { c.beginPath(); c.moveTo(0, Math.round(H * y) + 0.5); c.lineTo(W, Math.round(H * y) + 0.5); c.stroke(); }
    const x = (k) => (k / (n - 1)) * W, y = (v) => H - 2 - (v / max) * (H - 5);
    c.beginPath();
    for (let k = 0; k < n; k++) { const v = h[(this.histPos + k) % n]; if (k === 0) c.moveTo(x(k), y(v)); else c.lineTo(x(k), y(v)); }
    c.lineTo(W, H); c.lineTo(0, H); c.closePath();
    const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, 'rgba(255,179,64,.35)'); g.addColorStop(1, 'rgba(255,179,64,0)');
    c.fillStyle = g; c.fill();
    c.beginPath();
    for (let k = 0; k < n; k++) { const v = h[(this.histPos + k) % n]; if (k === 0) c.moveTo(x(k), y(v)); else c.lineTo(x(k), y(v)); }
    c.strokeStyle = '#ffb340'; c.lineWidth = 1.5; c.shadowColor = 'rgba(255,170,50,.8)'; c.shadowBlur = 4; c.stroke(); c.shadowBlur = 0;
    c.fillStyle = 'rgba(255,179,64,.55)'; c.font = '600 9px "IBM Plex Mono", monospace'; c.fillText(`${mhz} MHz clock`, 4, 10);
  }

  renderDisasm(cpu) {
    const pc = cpu.pc >>> 0, t = cpu.t, size = t ? 2 : 4;
    const r32 = (a) => cpu.fetchWord(a), r16 = (a) => cpu.fetchHalf(a);
    const bps = this.ctl.dbg()?.bps || new Set();
    let a = (pc - 5 * size) >>> 0;
    const out = [];
    for (let k = 0; k < 12; k++) {
      let d;
      try { d = disasm(r32, r16, a, t); } catch { d = { text: '??', size, word: 0 }; }
      const cur = a === pc;
      const text = d.text.replace(/\t/g, ' ').replace(/\s+/g, ' ');
      out.push(`<div class="ln${cur ? ' cur' : ''}" data-a="${a}"><span class="bp">${bps.has(a) ? '●' : cur ? '▸' : ''}</span><span class="ad">${hex(a)}</span><span class="op">${hex(d.word, d.size * 2)}</span><span class="tx">${escapeHtml(text)}</span></div>`);
      a = (a + d.size) >>> 0;
    }
    const html = out.join('');
    if (html !== this.lastDis) { $('disasm').innerHTML = html; this.lastDis = html; }
  }

  renderLog() {
    this.logDirty = false;
    const hide = $('hideTicks').checked;
    const box = $('intlog');
    const atBottom = box.scrollTop + box.clientHeight >= box.scrollHeight - 4;
    const rows = [];
    for (const e of this.log) {
      if (hide && ((e.kind === 'irq' && e.irq === 0) || e.n === 0x1C || (e.kind === 'int' && e.n === 0x08))) continue;
      const [a, b] = describe(e);
      rows.push(`<div class="e ${e.kind}"><span class="n">${(e.t / 1000).toFixed(3).padStart(8)}</span> ${a.padEnd(18)} <span class="x">${escapeHtml(b || '')}${e.count > 1 ? ` ×${e.count}` : ''}</span></div>`);
    }
    box.innerHTML = rows.slice(-160).join('');
    if (atBottom) box.scrollTop = box.scrollHeight;
  }

  renderHex(m) {
    const base = this.memAddr >>> 0, rows = 16, W = 8, bytes = new Int16Array(rows * W);
    const cpu = m.cpu;
    for (let k = 0; k < bytes.length; k++) {
      const a = (base + k) >>> 0;
      if (a < RAM_SIZE) bytes[k] = cpu.m8[a];
      else if (a >= ROM_BASE) bytes[k] = cpu.m8[a - ROM_BASE + RAM_SIZE];
      else if (a >= FONT_BASE && a < FONT_BASE + 0x2000) bytes[k] = m.font[a - FONT_BASE];
      else bytes[k] = -1;                   // I/O space: reading would have side effects
    }
    const prev = this.prevMem;
    let html = '';
    for (let r = 0; r < rows; r++) {
      let hx = '', asc = '';
      for (let c = 0; c < W; c++) {
        const k = r * W + c, v = bytes[k];
        const s = v < 0 ? '--' : hex(v, 2);
        const chg = prev && prev[k] !== v;
        hx += (chg ? `<span class="d">${s}</span>` : s) + (c === 3 ? '  ' : ' ');
        const ch = v < 0 ? ' ' : v < 32 ? '·' : CP437[v];
        asc += chg ? `<span class="d">${escapeHtml(ch)}</span>` : escapeHtml(ch === '\u0000' ? '·' : ch);
      }
      html += `<span class="a">${hex(base + r * W)}</span>  ${hx} <span class="c">${asc}</span>\n`;
    }
    this.prevMem = bytes;
    const box = $('hex');
    if (html !== this.lastHex) { box.innerHTML = html; this.lastHex = html; }
  }
}

function escapeHtml(s) { return s.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
