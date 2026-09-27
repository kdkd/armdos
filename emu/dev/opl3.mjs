// YMF262 (OPL3) FM synthesis: a hand port of Nuked OPL3 to JavaScript.
//
//   Nuked OPL3, Copyright (C) 2013-2020 Nuke.YKT
//   (Nuked-OPL3-fast modifications Copyright (C) 2026 Tony Gies)
//   Licensed under the GNU Lesser General Public License, version 2.1 or later.
//   Ported to JavaScript for ARM-DOS (2026) from the copy vendored in Chocolate
//   Doom (opl/opl3.c: upstream 1.8, commit cfedb09, fork 1.8-fast.3). This file
//   is a separately loaded module and stays under the LGPL-2.1+ (the licence
//   text: emu/dev/OPL3-LICENSE.txt, shipped next to it on the web site). Thanks (from the original): the MAME team (Jarek
//   Burczynski, Tatsuyuki Satoh), forums.submarine.org.uk (carbon14, opl3),
//   OPLx decapsulated (Matthew Gambrell, Olli Niemitalo), siliconpr0n.org (John
//   McMaster, digshadow).
//
// The port keeps upstream's per-sample structure and the fork's bit-exact
// shortcuts that are cheap in JS (runtime-built waveform table, cached
// envelope/phase terms, the silent-slot fast paths, the 36-step noise LFSR
// advance). It generates native samples at 49716 Hz (OPL3_Generate: the two
// DAC outputs A/B = left/right); resampling is the caller's business.
// Pointers become fields: a slot's modulation input is `modKind` (0 zero,
// 1 its own feedback, 2 another slot's output `modSlot`); a channel's outputs
// are the list `outs`. Timers and the status register are not part of Nuked
// OPL3 and live in the device (sb16.mjs).

export const OPL_RATE = 49716;

const logsinrom = new Uint16Array([
  0x859, 0x6c3, 0x607, 0x58b, 0x52e, 0x4e4, 0x4a6, 0x471, 0x443, 0x41a, 0x3f5, 0x3d3, 0x3b5, 0x398, 0x37e, 0x365,
  0x34e, 0x339, 0x324, 0x311, 0x2ff, 0x2ed, 0x2dc, 0x2cd, 0x2bd, 0x2af, 0x2a0, 0x293, 0x286, 0x279, 0x26d, 0x261,
  0x256, 0x24b, 0x240, 0x236, 0x22c, 0x222, 0x218, 0x20f, 0x206, 0x1fd, 0x1f5, 0x1ec, 0x1e4, 0x1dc, 0x1d4, 0x1cd,
  0x1c5, 0x1be, 0x1b7, 0x1b0, 0x1a9, 0x1a2, 0x19b, 0x195, 0x18f, 0x188, 0x182, 0x17c, 0x177, 0x171, 0x16b, 0x166,
  0x160, 0x15b, 0x155, 0x150, 0x14b, 0x146, 0x141, 0x13c, 0x137, 0x133, 0x12e, 0x129, 0x125, 0x121, 0x11c, 0x118,
  0x114, 0x10f, 0x10b, 0x107, 0x103, 0x0ff, 0x0fb, 0x0f8, 0x0f4, 0x0f0, 0x0ec, 0x0e9, 0x0e5, 0x0e2, 0x0de, 0x0db,
  0x0d7, 0x0d4, 0x0d1, 0x0cd, 0x0ca, 0x0c7, 0x0c4, 0x0c1, 0x0be, 0x0bb, 0x0b8, 0x0b5, 0x0b2, 0x0af, 0x0ac, 0x0a9,
  0x0a7, 0x0a4, 0x0a1, 0x09f, 0x09c, 0x099, 0x097, 0x094, 0x092, 0x08f, 0x08d, 0x08a, 0x088, 0x086, 0x083, 0x081,
  0x07f, 0x07d, 0x07a, 0x078, 0x076, 0x074, 0x072, 0x070, 0x06e, 0x06c, 0x06a, 0x068, 0x066, 0x064, 0x062, 0x060,
  0x05e, 0x05c, 0x05b, 0x059, 0x057, 0x055, 0x053, 0x052, 0x050, 0x04e, 0x04d, 0x04b, 0x04a, 0x048, 0x046, 0x045,
  0x043, 0x042, 0x040, 0x03f, 0x03e, 0x03c, 0x03b, 0x039, 0x038, 0x037, 0x035, 0x034, 0x033, 0x031, 0x030, 0x02f,
  0x02e, 0x02d, 0x02b, 0x02a, 0x029, 0x028, 0x027, 0x026, 0x025, 0x024, 0x023, 0x022, 0x021, 0x020, 0x01f, 0x01e,
  0x01d, 0x01c, 0x01b, 0x01a, 0x019, 0x018, 0x017, 0x017, 0x016, 0x015, 0x014, 0x014, 0x013, 0x012, 0x011, 0x011,
  0x010, 0x00f, 0x00f, 0x00e, 0x00d, 0x00d, 0x00c, 0x00c, 0x00b, 0x00a, 0x00a, 0x009, 0x009, 0x008, 0x008, 0x007,
  0x007, 0x007, 0x006, 0x006, 0x005, 0x005, 0x005, 0x004, 0x004, 0x004, 0x003, 0x003, 0x003, 0x002, 0x002, 0x002,
  0x002, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
]);

// exp table, pre-shifted left by one (as in the fork)
const exprom = new Uint16Array([
  0xff4, 0xfea, 0xfde, 0xfd4, 0xfc8, 0xfbe, 0xfb4, 0xfa8, 0xf9e, 0xf92, 0xf88, 0xf7e, 0xf72, 0xf68, 0xf5c, 0xf52,
  0xf48, 0xf3e, 0xf32, 0xf28, 0xf1e, 0xf14, 0xf08, 0xefe, 0xef4, 0xeea, 0xee0, 0xed4, 0xeca, 0xec0, 0xeb6, 0xeac,
  0xea2, 0xe98, 0xe8e, 0xe84, 0xe7a, 0xe70, 0xe66, 0xe5c, 0xe52, 0xe48, 0xe3e, 0xe34, 0xe2a, 0xe20, 0xe16, 0xe0c,
  0xe04, 0xdfa, 0xdf0, 0xde6, 0xddc, 0xdd2, 0xdca, 0xdc0, 0xdb6, 0xdac, 0xda4, 0xd9a, 0xd90, 0xd88, 0xd7e, 0xd74,
  0xd6a, 0xd62, 0xd58, 0xd50, 0xd46, 0xd3c, 0xd34, 0xd2a, 0xd22, 0xd18, 0xd10, 0xd06, 0xcfe, 0xcf4, 0xcec, 0xce2,
  0xcda, 0xcd0, 0xcc8, 0xcbe, 0xcb6, 0xcae, 0xca4, 0xc9c, 0xc92, 0xc8a, 0xc82, 0xc78, 0xc70, 0xc68, 0xc60, 0xc56,
  0xc4e, 0xc46, 0xc3c, 0xc34, 0xc2c, 0xc24, 0xc1c, 0xc12, 0xc0a, 0xc02, 0xbfa, 0xbf2, 0xbea, 0xbe0, 0xbd8, 0xbd0,
  0xbc8, 0xbc0, 0xbb8, 0xbb0, 0xba8, 0xba0, 0xb98, 0xb90, 0xb88, 0xb80, 0xb78, 0xb70, 0xb68, 0xb60, 0xb58, 0xb50,
  0xb48, 0xb40, 0xb38, 0xb32, 0xb2a, 0xb22, 0xb1a, 0xb12, 0xb0a, 0xb02, 0xafc, 0xaf4, 0xaec, 0xae4, 0xade, 0xad6,
  0xace, 0xac6, 0xac0, 0xab8, 0xab0, 0xaa8, 0xaa2, 0xa9a, 0xa92, 0xa8c, 0xa84, 0xa7c, 0xa76, 0xa6e, 0xa68, 0xa60,
  0xa58, 0xa52, 0xa4a, 0xa44, 0xa3c, 0xa36, 0xa2e, 0xa28, 0xa20, 0xa18, 0xa12, 0xa0c, 0xa04, 0x9fe, 0x9f6, 0x9f0,
  0x9e8, 0x9e2, 0x9da, 0x9d4, 0x9ce, 0x9c6, 0x9c0, 0x9b8, 0x9b2, 0x9ac, 0x9a4, 0x99e, 0x998, 0x990, 0x98a, 0x984,
  0x97c, 0x976, 0x970, 0x96a, 0x962, 0x95c, 0x956, 0x950, 0x948, 0x942, 0x93c, 0x936, 0x930, 0x928, 0x922, 0x91c,
  0x916, 0x910, 0x90a, 0x904, 0x8fc, 0x8f6, 0x8f0, 0x8ea, 0x8e4, 0x8de, 0x8d8, 0x8d2, 0x8cc, 0x8c6, 0x8c0, 0x8ba,
  0x8b4, 0x8ae, 0x8a8, 0x8a2, 0x89c, 0x896, 0x890, 0x88a, 0x884, 0x87e, 0x878, 0x872, 0x86c, 0x866, 0x860, 0x85a,
  0x854, 0x850, 0x84a, 0x844, 0x83e, 0x838, 0x832, 0x82c, 0x828, 0x822, 0x81c, 0x816, 0x810, 0x80c, 0x806, 0x800,
]);

// the eight waveforms as log-sin values (bit 15 = negative), 8 x 1024
const logsin_wf = (() => {
  const t = new Uint16Array(8 * 1024);
  for (let p = 0; p < 1024; p++) {
    const c2 = (p & 0x100) ? logsinrom[(p & 0xff) ^ 0xff] : logsinrom[p & 0xff];
    const c6 = (p & 0x200) ? 0x8000 : 0;
    const c5 = c6 ? 0x1000 : ((p & 0x80) ? logsinrom[((p ^ 0xff) << 1) & 0xff] : logsinrom[(p << 1) & 0xff]);
    t[0 * 1024 + p] = c6 | c2;
    t[1 * 1024 + p] = c6 ? 0x1000 : c2;
    t[2 * 1024 + p] = c2;
    t[3 * 1024 + p] = (p & 0x100) ? 0x1000 : logsinrom[p & 0xff];
    t[4 * 1024 + p] = ((((p & 0x300) === 0x100) ? 0x8000 : 0) | c5) & 0xffff;
    t[5 * 1024 + p] = c5;
    t[6 * 1024 + p] = c6;
    t[7 * 1024 + p] = (c6 | ((c6 ? ((p & 0x1ff) ^ 0x1ff) : p) << 3)) & 0xffff;
  }
  return t;
})();

const mt = [1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30];
const kslrom = [0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64];
const kslshift = [8, 1, 2, 0];
const eg_incstep = [[0, 0, 0, 0], [1, 0, 0, 0], [1, 0, 1, 0], [1, 1, 1, 0]];
const ad_slot = [0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1,
  12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1];
const ch_slot = [0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32];

const CH_2OP = 0, CH_4OP = 1, CH_4OP2 = 2, CH_DRUM = 3;
const EGK_NORM = 1, EGK_DRUM = 2;
const EG_ATTACK = 0, EG_DECAY = 1, EG_SUSTAIN = 2, EG_RELEASE = 3;
const MOD_ZERO = 0, MOD_FB = 1, MOD_SLOT = 2;

class Slot {
  constructor(chip, num) {
    this.chip = chip; this.num = num; this.channel = null;
    this.modKind = MOD_ZERO; this.modSlot = null;
    this.tremOn = 0;
    this.pg_reset = 0; this.pg_phase = 0; this.pg_inc = 0; this.pg_phase_out = 0;
    this.out = 0; this.fbmod = 0; this.prout = 0;
    this.eg_rout = 0x1ff; this.eg_out = 0x1ff; this.eg_tl_ksl = 0;
    this.key = 0; this.eg_gen = EG_RELEASE;
    this.reg_vib = 0; this.reg_mult = 0; this.reg_wf = 0; this.eg_ksl = 0; this.eg_ks = 0;
    this.reg_type = 0; this.reg_ksr = 0; this.reg_ksl = 0; this.reg_tl = 0;
    this.reg_ar = 0; this.reg_dr = 0; this.reg_sl = 0; this.reg_rr = 0;
    this.eg_rates = [0, 0, 0, 0]; this.eg_rate_hi = [0, 0, 0, 0]; this.eg_rate_lo = [0, 0, 0, 0];
    this.pg_inc_vib = new Uint32Array(8);
  }
}

class Channel {
  constructor(chip, num) {
    this.chip = chip; this.ch_num = num;
    this.slotz = [null, null]; this.pair = null;
    this.outs = [];
    this.chtype = CH_2OP; this.f_num = 0; this.block = 0; this.fb = 0; this.con = 0; this.alg = 0; this.ksv = 0;
    this.cha = 1; this.chb = 1;       // DAC A (left) / B (right) enables (C and D are not wired on a Sound Blaster)
  }
}

export class OPL3 {
  constructor() { this.reset(); }

  reset() {
    this.slot = []; this.channel = [];
    for (let i = 0; i < 36; i++) this.slot.push(new Slot(this, i));
    for (let c = 0; c < 18; c++) {
      const ch = new Channel(this, c), ls = ch_slot[c];
      ch.slotz[0] = this.slot[ls]; ch.slotz[1] = this.slot[ls + 3];
      this.slot[ls].channel = ch; this.slot[ls + 3].channel = ch;
      this.channel.push(ch);
    }
    for (let c = 0; c < 18; c++) {
      const ch = this.channel[c];
      if ((c % 9) < 3) ch.pair = this.channel[c + 3];
      else if ((c % 9) < 6) ch.pair = this.channel[c - 3];
    }
    this.timer = 0; this.eg_timer = 0; this.eg_timerrem = 0; this.eg_state = 0; this.eg_add = 0; this.eg_timer_lo = 0;
    this.newm = 0; this.nts = 0; this.rhy = 0; this.vibpos = 0; this.vibshift = 1;
    this.tremolo = 0; this.tremolopos = 0; this.tremoloshift = 4;
    this.noise = 1; this.noise_hh = 0; this.noise_sd = 0;
    this.rm_hh_bit2 = 0; this.rm_hh_bit3 = 0; this.rm_hh_bit7 = 0; this.rm_hh_bit8 = 0; this.rm_tc_bit3 = 0; this.rm_tc_bit5 = 0;
    this.mixL = 0; this.mixR = 0;       // mixbuff[0] / mixbuff[1]
    for (const ch of this.channel) this.setupAlg(ch);
  }

  // ------------------------------------------------------------------ envelope
  updateKSL(s) {
    let ksl = (kslrom[s.channel.f_num >> 6] << 2) - ((0x08 - s.channel.block) << 5);
    if (ksl < 0) ksl = 0;
    s.eg_ksl = ksl & 0xff;
    s.eg_tl_ksl = (s.reg_tl << 2) + (s.eg_ksl >> kslshift[s.reg_ksl]);
  }
  updateRate(s) {
    s.eg_ks = s.channel.ksv >> ((s.reg_ksr ^ 1) << 1);
    for (let i = 0; i < 4; i++) {
      const rate = (s.eg_ks + (s.eg_rates[i] << 2)) & 0xff;
      let hi = rate >> 2;
      if (hi & 0x10) hi = 0x0f;
      s.eg_rate_hi[i] = hi; s.eg_rate_lo[i] = rate & 3;
    }
  }
  envelopeCalc(s) {
    s.eg_out = s.eg_rout + s.eg_tl_ksl + (s.tremOn ? this.tremolo : 0);
    let reset = 0, reg_rate, rate_hi, rate_lo;
    if (s.key && s.eg_gen === EG_RELEASE) { reset = 1; reg_rate = s.eg_rates[0]; rate_hi = s.eg_rate_hi[0]; rate_lo = s.eg_rate_lo[0]; }
    else { reg_rate = s.eg_rates[s.eg_gen]; rate_hi = s.eg_rate_hi[s.eg_gen]; rate_lo = s.eg_rate_lo[s.eg_gen]; }
    s.pg_reset = reset;
    const eg_shift = rate_hi + this.eg_add;
    let shift = 0;
    if (reg_rate !== 0) {
      if (rate_hi < 12) {
        if (this.eg_state) {
          if (eg_shift === 12) shift = 1;
          else if (eg_shift === 13) shift = (rate_lo >> 1) & 1;
          else if (eg_shift === 14) shift = rate_lo & 1;
        }
      } else {
        shift = (rate_hi & 3) + eg_incstep[rate_lo][this.eg_timer_lo];
        if (shift & 4) shift = 3;
        if (!shift) shift = this.eg_state;
      }
    }
    let eg_rout = s.eg_rout, eg_inc = 0, eg_off = 0;
    if (reset && rate_hi === 0x0f) eg_rout = 0;                    // instant attack
    if ((s.eg_rout & 0x1f8) === 0x1f8) eg_off = 1;                 // envelope off
    if (s.eg_gen !== EG_ATTACK && !reset && eg_off) eg_rout = 0x1ff;
    switch (s.eg_gen) {
      case EG_ATTACK:
        if (!s.eg_rout) s.eg_gen = EG_DECAY;
        else if (s.key && shift > 0 && rate_hi !== 0x0f) eg_inc = (~s.eg_rout) >> (4 - shift);
        break;
      case EG_DECAY:
        if ((s.eg_rout >> 4) === s.reg_sl) s.eg_gen = EG_SUSTAIN;
        else if (!eg_off && !reset && shift > 0) eg_inc = 1 << (shift - 1);
        break;
      default:
        if (!eg_off && !reset && shift > 0) eg_inc = 1 << (shift - 1);
    }
    s.eg_rout = (eg_rout + eg_inc) & 0x1ff;
    if (reset) s.eg_gen = EG_ATTACK;
    if (!s.key) s.eg_gen = EG_RELEASE;
  }

  // ------------------------------------------------------------------ phase
  updatePhaseInc(s) {
    const ch = s.channel, m = mt[s.reg_mult];
    s.pg_inc = ((((ch.f_num << ch.block) >>> 1) * m) >>> 1);
    for (let vp = 0; vp < 8; vp++) {
      let f_num = ch.f_num;
      let range = (f_num >> 7) & 7;
      if (!(vp & 3)) range = 0; else if (vp & 1) range >>= 1;
      range >>= this.vibshift;
      if (vp & 4) range = -range;
      f_num = (f_num + range) & 0xffff;
      s.pg_inc_vib[vp] = ((((f_num << ch.block) >>> 1) * m) >>> 1);
    }
  }
  phaseGenerate(s) {
    const inc = s.reg_vib ? s.pg_inc_vib[this.vibpos] : s.pg_inc;
    const phase = (s.pg_phase >>> 9) & 0xffff;
    if (s.pg_reset) s.pg_phase = 0;
    s.pg_phase = (s.pg_phase + inc) >>> 0;
    s.pg_phase_out = phase;
    const n = s.num;
    if (n === 13) {                                     // hi-hat
      this.rm_hh_bit2 = (phase >> 2) & 1; this.rm_hh_bit3 = (phase >> 3) & 1;
      this.rm_hh_bit7 = (phase >> 7) & 1; this.rm_hh_bit8 = (phase >> 8) & 1;
      if (this.rhy & 0x20) {
        const x = (this.rm_hh_bit2 ^ this.rm_hh_bit7) | (this.rm_hh_bit3 ^ this.rm_tc_bit5) | (this.rm_tc_bit3 ^ this.rm_tc_bit5);
        s.pg_phase_out = (x << 9) | ((x ^ (this.noise_hh & 1)) ? 0xd0 : 0x34);
      }
    } else if (n === 16) {                              // snare drum
      if (this.rhy & 0x20) s.pg_phase_out = (this.rm_hh_bit8 << 9) | ((this.rm_hh_bit8 ^ (this.noise_sd & 1)) << 8);
    } else if (n === 17) {                              // top cymbal
      if (this.rhy & 0x20) {
        this.rm_tc_bit3 = (phase >> 3) & 1; this.rm_tc_bit5 = (phase >> 5) & 1;
        const x = (this.rm_hh_bit2 ^ this.rm_hh_bit7) | (this.rm_hh_bit3 ^ this.rm_tc_bit5) | (this.rm_tc_bit3 ^ this.rm_tc_bit5);
        s.pg_phase_out = (x << 9) | 0x80;
      }
    }
  }

  // ------------------------------------------------------------------ slot
  slotWrite20(s, d) {
    s.tremOn = (d >> 7) & 1; s.reg_vib = (d >> 6) & 1; s.reg_type = (d >> 5) & 1;
    s.eg_rates[2] = s.reg_type ? 0 : s.reg_rr;
    s.reg_ksr = (d >> 4) & 1; s.reg_mult = d & 0x0f;
    this.updateRate(s); this.updatePhaseInc(s);
  }
  slotWrite40(s, d) { s.reg_ksl = (d >> 6) & 3; s.reg_tl = d & 0x3f; this.updateKSL(s); }
  slotWrite60(s, d) { s.reg_ar = (d >> 4) & 0x0f; s.reg_dr = d & 0x0f; s.eg_rates[0] = s.reg_ar; s.eg_rates[1] = s.reg_dr; this.updateRate(s); }
  slotWrite80(s, d) {
    s.reg_sl = (d >> 4) & 0x0f; if (s.reg_sl === 0x0f) s.reg_sl = 0x1f;
    s.reg_rr = d & 0x0f; s.eg_rates[2] = s.reg_type ? 0 : s.reg_rr; s.eg_rates[3] = s.reg_rr;
    this.updateRate(s);
  }
  slotWriteE0(s, d) { s.reg_wf = d & 7; if (!this.newm) s.reg_wf &= 3; }

  modValue(s) { return s.modKind === MOD_SLOT ? s.modSlot.out : s.modKind === MOD_FB ? s.fbmod : 0; }
  slotGenerate(s) {
    const wf = logsin_wf[(s.reg_wf << 10) | ((s.pg_phase_out + this.modValue(s)) & 0x3ff)];
    let level = (wf & 0x7fff) + (s.eg_out << 3);
    if (level > 0x1fff) level = 0x1fff;
    const v = exprom[level & 0xff] >> (level >> 8);
    s.out = (wf & 0x8000) ? ~v : v;
  }
  slotCalcFB(s, fb) {
    s.fbmod = fb !== 0 ? (s.prout + s.out) >> (9 - fb) : 0;
    s.prout = s.out;
  }
  processSlot(s, fb) {
    const rhythmSlot = s.num === 13 || s.num === 16 || s.num === 17;
    if (!s.key && s.eg_rout === 0x1ff && !rhythmSlot) {        // key off, fully attenuated
      if (fb === 0 && s.pg_inc === 0 && s.out === 0 && this.modValue(s) === 0 && s.eg_tl_ksl === 0 &&
          !(s.tremOn && this.tremolo) && s.pg_phase === 0 && s.reg_vib === 0 && s.reg_wf === 0) {
        s.fbmod = 0; s.prout = 0; s.eg_out = 0x1ff; s.pg_reset = 0; s.eg_gen = EG_RELEASE; s.pg_phase_out = 0;
        return;
      }
      this.slotCalcFB(s, fb);
      s.eg_out = s.eg_rout + s.eg_tl_ksl + (s.tremOn ? this.tremolo : 0);
      s.pg_reset = 0; s.eg_gen = EG_RELEASE;
      const inc = s.reg_vib ? s.pg_inc_vib[this.vibpos] : s.pg_inc;
      s.pg_phase_out = (s.pg_phase >>> 9) & 0xffff;
      s.pg_phase = (s.pg_phase + inc) >>> 0;
      // eg_out >= 0x1ff: the exp lookup is zero, only the sign bit survives
      s.out = (logsin_wf[(s.reg_wf << 10) | ((s.pg_phase_out + this.modValue(s)) & 0x3ff)] & 0x8000) ? -1 : 0;
      return;
    }
    if (s.eg_gen === EG_SUSTAIN && s.key && s.eg_rates[EG_SUSTAIN] === 0) {   // held sustain
      this.slotCalcFB(s, fb);
      s.eg_out = s.eg_rout + s.eg_tl_ksl + (s.tremOn ? this.tremolo : 0);
      s.pg_reset = 0;
      if ((s.eg_rout & 0x1f8) === 0x1f8) s.eg_rout = 0x1ff;
      if (!s.reg_vib && !rhythmSlot) {
        s.pg_phase_out = (s.pg_phase >>> 9) & 0xffff;
        s.pg_phase = (s.pg_phase + s.pg_inc) >>> 0;
      } else this.phaseGenerate(s);
      this.slotGenerate(s);
      return;
    }
    this.slotCalcFB(s, fb);
    this.envelopeCalc(s);
    this.phaseGenerate(s);
    this.slotGenerate(s);
  }

  // ------------------------------------------------------------------ channel
  updateRhythm(data) {
    this.rhy = data & 0x3f;
    const c6 = this.channel[6], c7 = this.channel[7], c8 = this.channel[8];
    if (this.rhy & 0x20) {
      c6.outs = [c6.slotz[1], c6.slotz[1]];
      c7.outs = [c7.slotz[0], c7.slotz[0], c7.slotz[1], c7.slotz[1]];
      c8.outs = [c8.slotz[0], c8.slotz[0], c8.slotz[1], c8.slotz[1]];
      for (let c = 6; c < 9; c++) this.channel[c].chtype = CH_DRUM;
      this.setupAlg(c6); this.setupAlg(c7); this.setupAlg(c8);
      const k = (s, on) => { if (on) s.key |= EGK_DRUM; else s.key &= ~EGK_DRUM; };
      k(c7.slotz[0], this.rhy & 0x01);   // hh
      k(c8.slotz[1], this.rhy & 0x02);   // tc
      k(c8.slotz[0], this.rhy & 0x04);   // tom
      k(c7.slotz[1], this.rhy & 0x08);   // sd
      k(c6.slotz[0], this.rhy & 0x10); k(c6.slotz[1], this.rhy & 0x10);   // bd
    } else {
      for (let c = 6; c < 9; c++) {
        const ch = this.channel[c];
        ch.chtype = CH_2OP; this.setupAlg(ch);
        ch.slotz[0].key &= ~EGK_DRUM; ch.slotz[1].key &= ~EGK_DRUM;
      }
    }
  }
  freqChanged(ch) {
    for (const s of ch.slotz) { this.updateKSL(s); this.updateRate(s); this.updatePhaseInc(s); }
  }
  writeA0(ch, d) {
    if (this.newm && ch.chtype === CH_4OP2) return;
    ch.f_num = (ch.f_num & 0x300) | d;
    ch.ksv = (ch.block << 1) | ((ch.f_num >> (9 - this.nts)) & 1);
    this.freqChanged(ch);
    if (this.newm && ch.chtype === CH_4OP) { ch.pair.f_num = ch.f_num; ch.pair.ksv = ch.ksv; this.freqChanged(ch.pair); }
  }
  writeB0(ch, d) {
    if (this.newm && ch.chtype === CH_4OP2) return;
    ch.f_num = (ch.f_num & 0xff) | ((d & 3) << 8);
    ch.block = (d >> 2) & 7;
    ch.ksv = (ch.block << 1) | ((ch.f_num >> (9 - this.nts)) & 1);
    this.freqChanged(ch);
    if (this.newm && ch.chtype === CH_4OP) {
      ch.pair.f_num = ch.f_num; ch.pair.block = ch.block; ch.pair.ksv = ch.ksv; this.freqChanged(ch.pair);
    }
  }
  setupAlg(ch) {
    const m = (s, kind, src = null) => { s.modKind = kind; s.modSlot = src; };
    const [s0, s1] = ch.slotz;
    if (ch.chtype === CH_DRUM) {
      if (ch.ch_num === 7 || ch.ch_num === 8) { m(s0, MOD_ZERO); m(s1, MOD_ZERO); return; }
      if (ch.alg & 1) { m(s0, MOD_FB); m(s1, MOD_ZERO); } else { m(s0, MOD_FB); m(s1, MOD_SLOT, s0); }
      return;
    }
    if (ch.alg & 0x08) return;
    if (ch.alg & 0x04) {
      const p = ch.pair, [p0, p1] = p.slotz;
      p.outs = [];
      switch (ch.alg & 3) {
        case 0: m(p0, MOD_FB); m(p1, MOD_SLOT, p0); m(s0, MOD_SLOT, p1); m(s1, MOD_SLOT, s0); ch.outs = [s1]; break;
        case 1: m(p0, MOD_FB); m(p1, MOD_SLOT, p0); m(s0, MOD_ZERO); m(s1, MOD_SLOT, s0); ch.outs = [p1, s1]; break;
        case 2: m(p0, MOD_FB); m(p1, MOD_ZERO); m(s0, MOD_SLOT, p1); m(s1, MOD_SLOT, s0); ch.outs = [p0, s1]; break;
        case 3: m(p0, MOD_FB); m(p1, MOD_ZERO); m(s0, MOD_SLOT, p1); m(s1, MOD_ZERO); ch.outs = [p0, s0, s1]; break;
      }
    } else if (ch.alg & 1) { m(s0, MOD_FB); m(s1, MOD_ZERO); ch.outs = [s0, s1]; }
    else { m(s0, MOD_FB); m(s1, MOD_SLOT, s0); ch.outs = [s1]; }
  }
  updateAlg(ch) {
    ch.alg = ch.con;
    if (this.newm) {
      if (ch.chtype === CH_4OP) { ch.pair.alg = 0x04 | (ch.con << 1) | ch.pair.con; ch.alg = 0x08; this.setupAlg(ch.pair); }
      else if (ch.chtype === CH_4OP2) { ch.alg = 0x04 | (ch.pair.con << 1) | ch.con; ch.pair.alg = 0x08; this.setupAlg(ch); }
      else this.setupAlg(ch);
    } else this.setupAlg(ch);
  }
  writeC0(ch, d) {
    ch.fb = (d & 0x0e) >> 1; ch.con = d & 1;
    this.updateAlg(ch);
    if (this.newm) { ch.cha = (d >> 4) & 1; ch.chb = (d >> 5) & 1; }
    else { ch.cha = ch.chb = 1; }
  }
  keyOn(ch) {
    if (this.newm) {
      if (ch.chtype === CH_4OP) { ch.slotz[0].key |= EGK_NORM; ch.slotz[1].key |= EGK_NORM; ch.pair.slotz[0].key |= EGK_NORM; ch.pair.slotz[1].key |= EGK_NORM; }
      else if (ch.chtype === CH_2OP || ch.chtype === CH_DRUM) { ch.slotz[0].key |= EGK_NORM; ch.slotz[1].key |= EGK_NORM; }
    } else { ch.slotz[0].key |= EGK_NORM; ch.slotz[1].key |= EGK_NORM; }
  }
  keyOff(ch) {
    if (this.newm) {
      if (ch.chtype === CH_4OP) { ch.slotz[0].key &= ~EGK_NORM; ch.slotz[1].key &= ~EGK_NORM; ch.pair.slotz[0].key &= ~EGK_NORM; ch.pair.slotz[1].key &= ~EGK_NORM; }
      else if (ch.chtype === CH_2OP || ch.chtype === CH_DRUM) { ch.slotz[0].key &= ~EGK_NORM; ch.slotz[1].key &= ~EGK_NORM; }
    } else { ch.slotz[0].key &= ~EGK_NORM; ch.slotz[1].key &= ~EGK_NORM; }
  }
  set4Op(d) {
    for (let bit = 0; bit < 6; bit++) {
      const c = bit < 3 ? bit : bit + 6;
      if ((d >> bit) & 1) { this.channel[c].chtype = CH_4OP; this.channel[c + 3].chtype = CH_4OP2; this.updateAlg(this.channel[c]); }
      else { this.channel[c].chtype = CH_2OP; this.channel[c + 3].chtype = CH_2OP; this.updateAlg(this.channel[c]); this.updateAlg(this.channel[c + 3]); }
    }
  }

  /** Register write; reg 0x000-0x1FF (bit 8 = second register array). */
  writeReg(reg, v) {
    const high = (reg >> 8) & 1, regm = reg & 0xff;
    v &= 0xff;
    switch (regm & 0xf0) {
      case 0x00:
        if (high) { if ((regm & 0x0f) === 4) this.set4Op(v); else if ((regm & 0x0f) === 5) this.newm = v & 1; }
        else if ((regm & 0x0f) === 8) this.nts = (v >> 6) & 1;
        break;
      case 0x20: case 0x30: { const a = ad_slot[regm & 0x1f]; if (a >= 0) this.slotWrite20(this.slot[18 * high + a], v); break; }
      case 0x40: case 0x50: { const a = ad_slot[regm & 0x1f]; if (a >= 0) this.slotWrite40(this.slot[18 * high + a], v); break; }
      case 0x60: case 0x70: { const a = ad_slot[regm & 0x1f]; if (a >= 0) this.slotWrite60(this.slot[18 * high + a], v); break; }
      case 0x80: case 0x90: { const a = ad_slot[regm & 0x1f]; if (a >= 0) this.slotWrite80(this.slot[18 * high + a], v); break; }
      case 0xe0: case 0xf0: { const a = ad_slot[regm & 0x1f]; if (a >= 0) this.slotWriteE0(this.slot[18 * high + a], v); break; }
      case 0xa0: if ((regm & 0x0f) < 9) this.writeA0(this.channel[9 * high + (regm & 0x0f)], v); break;
      case 0xb0:
        if (regm === 0xbd && !high) {
          this.tremoloshift = (((v >> 7) ^ 1) << 1) + 2;
          const vibshift = ((v >> 6) & 1) ^ 1;
          if (vibshift !== this.vibshift) { this.vibshift = vibshift; for (const s of this.slot) this.updatePhaseInc(s); }
          this.updateRhythm(v);
        } else if ((regm & 0x0f) < 9) {
          const ch = this.channel[9 * high + (regm & 0x0f)];
          this.writeB0(ch, v);
          if (v & 0x20) this.keyOn(ch); else this.keyOff(ch);
        }
        break;
      case 0xc0: if ((regm & 0x0f) < 9) this.writeC0(this.channel[9 * high + (regm & 0x0f)], v); break;
    }
  }

  /** True while no operator can make a sound (all keys off, all envelopes at maximum attenuation). */
  isSilent() {
    for (const s of this.slot) if (s.key || s.eg_rout !== 0x1ff) return false;
    return true;
  }

  /**
   * One native sample (49716 Hz). Returns nothing; the outputs are this.outL /
   * this.outR (DAC A and B, clipped to 16 bits), as OPL3_Generate.
   */
  generate() {
    // DAC B (right) outputs the previous sample's mix: Nuked's mixbuff[1]
    this.outR = this.mixR > 32767 ? 32767 : this.mixR < -32768 ? -32768 : this.mixR;
    // advance the noise LFSR 36 steps (one per slot), capturing the hh/sd taps
    {
      const s = this.noise;
      const f0_8 = (s ^ (s >> 14)) & 0x1ff;
      const f9_17 = ((s >> 9) ^ f0_8) & 0x1ff;
      const f18_22 = ((s >> 18) ^ f9_17) & 0x1f;
      const f23_31 = f0_8 ^ ((f9_17 >> 5) | (f18_22 << 4));
      const f32_35 = (f9_17 ^ f23_31) & 0x0f;
      this.noise_hh = (s >> 13) & 1; this.noise_sd = (s >> 16) & 1;
      this.noise = ((f9_17 >> 4) & 0x1f) | (f18_22 << 5) | (f23_31 << 10) | (f32_35 << 19);
    }
    // all 36 slots, channel by channel (modulator then carrier)
    const chs = this.channel;
    for (let c = 0; c < 18; c++) {
      const ch = chs[c], fb = ch.fb;
      this.processSlot(ch.slotz[0], fb);
      this.processSlot(ch.slotz[1], fb);
    }
    // left mix: slots 15-35 contribute their previous sample (CHANNELSAMPLEDELAY)
    let l = 0, r = 0;
    for (let c = 0; c < 18; c++) {
      const ch = chs[c], outs = ch.outs;
      if (!outs.length || !(ch.cha | ch.chb)) continue;
      let al = 0, ar = 0;
      for (let k = 0; k < outs.length; k++) {
        const s = outs[k];
        al += s.num >= 15 ? s.prout : s.out;
        ar += s.num >= 33 ? s.prout : s.out;
      }
      if (ch.cha) l += (al << 16) >> 16;
      if (ch.chb) r += (ar << 16) >> 16;
    }
    this.mixL = l;
    this.outL = l > 32767 ? 32767 : l < -32768 ? -32768 : l;
    // tremolo, vibrato and the envelope clock
    if ((this.timer & 0x3f) === 0x3f) { this.tremolopos++; if (this.tremolopos === 210) this.tremolopos = 0; }
    this.tremolo = (this.tremolopos < 105 ? this.tremolopos : 210 - this.tremolopos) >> this.tremoloshift;
    if ((this.timer & 0x3ff) === 0x3ff) this.vibpos = (this.vibpos + 1) & 7;
    this.timer = (this.timer + 1) & 0xffff;
    if (this.eg_state) {
      const low = this.eg_timer % 0x2000;
      if (!low) this.eg_add = 0;
      else { let sh = 0; while (((low >> sh) & 1) === 0) sh++; this.eg_add = sh + 1; }
      this.eg_timer_lo = this.eg_timer % 4;
    }
    if (this.eg_timerrem || this.eg_state) {
      if (this.eg_timer === 0xfffffffff) { this.eg_timer = 0; this.eg_timerrem = 1; }
      else { this.eg_timer++; this.eg_timerrem = 0; }
    }
    this.eg_state ^= 1;
    this.mixR = r;
  }
}
