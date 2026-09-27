// A 3-digit red 7-segment display (the TURBO MHz readout), drawn as SVG.

const SEGS = {   // a b c d e f g
  ' ': 0, '-': 0b0000001,
  0: 0b1111110, 1: 0b0110000, 2: 0b1101101, 3: 0b1111001, 4: 0b0110011,
  5: 0b1011011, 6: 0b1011111, 7: 0b1110000, 8: 0b1111111, 9: 0b1111011,
  H: 0b0110111, i: 0b0010000, L: 0b0001110, o: 0b0011101, P: 0b1100111, E: 0b1001111,
};
// segment polygons in a 28x44 digit cell, slanted a little like the real thing
function digitPolys(x0) {
  const k = 0.1; // italic slant
  const P = (pts) => pts.map(([x, y]) => `${(x0 + x + (44 - y) * k).toFixed(1)},${y}`).join(' ');
  const t = 3.6, w = 20, h = 19, L = 3, T = 3;
  const hs = (y) => P([[L + 1.5, y], [L + 3, y - t / 2], [L + w - 3, y - t / 2], [L + w - 1.5, y], [L + w - 3, y + t / 2], [L + 3, y + t / 2]]);
  const vs = (x, y0) => P([[x, y0 + 1.5], [x + t / 2, y0 + 3], [x + t / 2, y0 + h - 3], [x, y0 + h - 1.5], [x - t / 2, y0 + h - 3], [x - t / 2, y0 + 3]]);
  return [hs(T), vs(L + w, T), vs(L + w, T + h), hs(T + 2 * h), vs(L, T + h), vs(L, T), hs(T + h)];
}

export class SevenSeg {
  constructor(svg, digits = 3) {
    this.svg = svg; this.segs = [];
    const ns = 'http://www.w3.org/2000/svg';
    for (let d = 0; d < digits; d++) {
      const polys = digitPolys(d * 31);
      const row = polys.map((pts) => {
        const p = document.createElementNS(ns, 'polygon');
        p.setAttribute('points', pts); p.setAttribute('class', 'seg');
        svg.appendChild(p); return p;
      });
      this.segs.push(row);
    }
    this.value = null;
  }
  show(text) {
    text = String(text ?? '').padStart(this.segs.length, ' ').slice(-this.segs.length);
    if (text === this.value) return;
    this.value = text;
    for (let d = 0; d < this.segs.length; d++) {
      const bits = SEGS[text[d]] ?? 0;
      for (let s = 0; s < 7; s++) this.segs[d][s].classList.toggle('on', !!(bits & (1 << (6 - s))));
    }
  }
}
