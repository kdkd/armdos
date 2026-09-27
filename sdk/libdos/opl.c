/* opl.c - OPL2/OPL3 detection and register access, and a tiny General MIDI
 * player on top of it using a GENMIDI (DMX format) instrument bank (sb.h).
 *
 * The MIDI helper is not reentrant: call it from one place (the main loop or
 * one timer interrupt), not both. */
#include <string.h>
#include <stdint.h>
#include <armdos.h>
#include <sb.h>

#define inb(p)     armdos_inb(p)
#define outb(p, v) armdos_outb((p), (uint8_t)(v))
#define OPL_PORT   0x388

extern const unsigned char _sb_genmidi_bank[175 * 36];

/* ------------------------------------------------------------ the chip */

void opl_write(unsigned reg, unsigned val)
{
    unsigned p = OPL_PORT + ((reg & 0x100) ? 2 : 0);
    outb(p, reg & 0xFF);
    (void)inb(OPL_PORT); (void)inb(OPL_PORT);          /* address settle (3.3 us on an OPL2) */
    outb(p + 1, val);
    (void)inb(OPL_PORT); (void)inb(OPL_PORT);
}

static void wait15(unsigned n)                         /* ~15 us units (port 61h refresh toggle) */
{
    unsigned b = inb(0x61) & 0x10;
    while (n--) {
        unsigned guard = 100000;
        while ((inb(0x61) & 0x10) == b && --guard) ;
        b ^= 0x10;
    }
}

int opl_detect(void)
{
    unsigned s1, s2;
    opl_write(0x04, 0x60);                             /* reset both timers */
    opl_write(0x04, 0x80);                             /* reset the IRQ */
    s1 = inb(OPL_PORT);
    opl_write(0x02, 0xFF);                             /* timer 1: 80 us */
    opl_write(0x04, 0x21);                             /* start timer 1 */
    wait15(8);                                         /* ~120 us */
    s2 = inb(OPL_PORT);
    opl_write(0x04, 0x60);
    opl_write(0x04, 0x80);
    if ((s1 & 0xE0) != 0x00 || (s2 & 0xE0) != 0xC0) return 0;
    return (s1 & 0x06) == 0 ? 3 : 2;                   /* an OPL2 reads 06h in bits 1-2 */
}

void opl_reset(void)
{
    unsigned r, a;
    /* Silence first. A key-off only starts an operator's release, at the rate
     * in its 80h register, and release rate 0 means "never": clearing the
     * registers right after the key-offs froze every note still fading out
     * at about -47 dB (total level 3Fh), a faint chord that went on after the
     * program had exited. So: the fastest release (rate 15) on every
     * operator, all keys off (rhythm ones too), ~4 ms for the envelopes to
     * run out (a full-scale one takes 128 samples, 2.6 ms, at rate 15), and
     * only then clear. */
    for (a = 0; a <= 0x100; a += 0x100) {
        for (r = 0x80; r <= 0x95; r++) opl_write(a | r, 0x0F);    /* fastest release */
        for (r = 0xB0; r <= 0xB8; r++) opl_write(a | r, 0);       /* keys off */
    }
    opl_write(0xBD, 0);
    wait15(256);
    for (a = 0; a <= 0x100; a += 0x100)
        for (r = 0x20; r <= 0xF5; r++) opl_write(a | r, (r >= 0x40 && r <= 0x55) ? 0x3F : 0);
    opl_write(0x105, 0);
    opl_write(0x104, 0);
    opl_write(0x01, 0x20);                             /* waveform select enable (OPL2) */
    opl_write(0x08, 0);
    opl_write(0xBD, 0);
}

/* ------------------------------------------------------------ MIDI */

/* F-number for pitch (octave, 1/32-semitone step): block = octave - 1 */
static const uint16_t fnum_table[384] = {
    345, 345, 346, 347, 347, 348, 349, 349, 350, 351, 351, 352,
    352, 353, 354, 354, 355, 356, 356, 357, 358, 358, 359, 359,
    360, 361, 361, 362, 363, 363, 364, 365, 365, 366, 367, 367,
    368, 369, 369, 370, 371, 371, 372, 373, 373, 374, 375, 375,
    376, 377, 377, 378, 379, 380, 380, 381, 382, 382, 383, 384,
    384, 385, 386, 386, 387, 388, 389, 389, 390, 391, 391, 392,
    393, 393, 394, 395, 396, 396, 397, 398, 398, 399, 400, 401,
    401, 402, 403, 404, 404, 405, 406, 406, 407, 408, 409, 409,
    410, 411, 412, 412, 413, 414, 415, 415, 416, 417, 418, 418,
    419, 420, 421, 421, 422, 423, 424, 424, 425, 426, 427, 428,
    428, 429, 430, 431, 431, 432, 433, 434, 435, 435, 436, 437,
    438, 438, 439, 440, 441, 442, 442, 443, 444, 445, 446, 446,
    447, 448, 449, 450, 450, 451, 452, 453, 454, 455, 455, 456,
    457, 458, 459, 460, 460, 461, 462, 463, 464, 465, 465, 466,
    467, 468, 469, 470, 470, 471, 472, 473, 474, 475, 476, 476,
    477, 478, 479, 480, 481, 482, 482, 483, 484, 485, 486, 487,
    488, 489, 489, 490, 491, 492, 493, 494, 495, 496, 497, 498,
    498, 499, 500, 501, 502, 503, 504, 505, 506, 507, 507, 508,
    509, 510, 511, 512, 513, 514, 515, 516, 517, 518, 519, 520,
    520, 521, 522, 523, 524, 525, 526, 527, 528, 529, 530, 531,
    532, 533, 534, 535, 536, 537, 538, 539, 540, 541, 542, 543,
    544, 545, 545, 546, 547, 548, 549, 550, 551, 552, 553, 554,
    555, 556, 557, 558, 559, 560, 561, 562, 563, 565, 566, 567,
    568, 569, 570, 571, 572, 573, 574, 575, 576, 577, 578, 579,
    580, 581, 582, 583, 584, 585, 586, 587, 588, 590, 591, 592,
    593, 594, 595, 596, 597, 598, 599, 600, 601, 602, 604, 605,
    606, 607, 608, 609, 610, 611, 612, 613, 615, 616, 617, 618,
    619, 620, 621, 622, 623, 625, 626, 627, 628, 629, 630, 631,
    633, 634, 635, 636, 637, 638, 639, 641, 642, 643, 644, 645,
    646, 648, 649, 650, 651, 652, 653, 655, 656, 657, 658, 659,
    661, 662, 663, 664, 665, 666, 668, 669, 670, 671, 673, 674,
    675, 676, 677, 679, 680, 681, 682, 684, 685, 686, 687, 689,
};

/* velocity / volume -> loudness (square-root curve) */
static const uint8_t vol_curve[128] = {
    0, 11, 16, 20, 23, 25, 28, 30, 32, 34, 36, 37, 39, 41, 42, 44,
    45, 46, 48, 49, 50, 52, 53, 54, 55, 56, 57, 59, 60, 61, 62, 63,
    64, 65, 66, 67, 68, 69, 69, 70, 71, 72, 73, 74, 75, 76, 76, 77,
    78, 79, 80, 80, 81, 82, 83, 84, 84, 85, 86, 87, 87, 88, 89, 89,
    90, 91, 92, 92, 93, 94, 94, 95, 96, 96, 97, 98, 98, 99, 100, 100,
    101, 101, 102, 103, 103, 104, 105, 105, 106, 106, 107, 108, 108, 109, 109, 110,
    110, 111, 112, 112, 113, 113, 114, 114, 115, 115, 116, 117, 117, 118, 118, 119,
    119, 120, 120, 121, 121, 122, 122, 123, 123, 124, 124, 125, 125, 126, 126, 127,
};

#define MAXV 18
static const uint8_t op_mod[9] = { 0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12 };

/* GENMIDI instrument, 36 bytes */
struct gm_op { uint8_t tremolo, attack, sustain, waveform, scale, level; };
struct gm_voice { struct gm_op mod; uint8_t feedback; struct gm_op car; uint8_t unused; uint8_t off_lo, off_hi; };
struct gm_instr { uint8_t flags_lo, flags_hi, fine, fixed; struct gm_voice v[2]; };
#define GM_FIXED   0x01
#define GM_2VOICE  0x04

static struct {
    int ch, key, on;
    unsigned age;
    const struct gm_instr *ins;
    int iv;                       /* which of the instrument's voices */
    int note;                     /* note played (fixed note for fixed instruments) */
    int vel;
} V[MAXV];

static struct { int prog, vol, expr, pan, bend; } C[16];
static const struct gm_instr *bank;
static int nvoices, opl3, master = 127;
static unsigned clock;

static unsigned vreg(int v, unsigned r) { return (v >= 9 ? 0x100 : 0) | r; }

static void write_op(int v, unsigned op, const struct gm_op *o, unsigned level)
{
    opl_write(vreg(v, 0x20 + op), o->tremolo);
    opl_write(vreg(v, 0x60 + op), o->attack);
    opl_write(vreg(v, 0x80 + op), o->sustain);
    opl_write(vreg(v, 0xE0 + op), o->waveform);
    opl_write(vreg(v, 0x40 + op), (o->scale & 0xC0) | level);
}

static unsigned pan_bits(int ch)
{
    if (!opl3) return 0x30;
    return C[ch].pan < 43 ? 0x10 : C[ch].pan > 85 ? 0x20 : 0x30;   /* C0h bit 4 = left, bit 5 = right */
}

static void voice_volume(int v)
{
    const struct gm_voice *gv = &V[v].ins->v[V[v].iv];
    int ch = V[v].ch;
    unsigned chv = (unsigned)C[ch].vol * C[ch].expr / 127 * master / 127;
    unsigned full = (vol_curve[V[v].vel] * 2 * (vol_curve[chv] + 1)) >> 9;
    unsigned car = 0x3F - (full > 0x3F ? 0x3F : full);
    unsigned mod = 0x3F - (full > 0x3F ? 0x3F : full), vi = v % 9;
    opl_write(vreg(v, 0x40 + op_mod[vi] + 3), (gv->car.scale & 0xC0) | car);
    if (gv->feedback & 1) {                            /* additive: the modulator is heard too */
        mod = gv->mod.level > car ? gv->mod.level : car;
        opl_write(vreg(v, 0x40 + op_mod[vi]), (gv->mod.scale & 0xC0) | mod);
    }
}

static void voice_freq(int v, int keyon)
{
    const struct gm_instr *in = V[v].ins;
    const struct gm_voice *gv = &in->v[V[v].iv];
    int note = V[v].note, p, oct, block, fnum;
    if (!(in->flags_lo & GM_FIXED)) note += (int16_t)(gv->off_lo | (gv->off_hi << 8));
    while (note < 0) note += 12;
    while (note > 127) note -= 12;
    p = note * 32 + C[V[v].ch].bend;
    if (V[v].iv) p += in->fine / 2 - 64;
    if (p < 0) p = 0;
    oct = p / 384;
    fnum = fnum_table[p % 384];
    block = oct - 1;
    if (block < 0) { fnum >>= -block; block = 0; }
    while (block > 7) { fnum <<= 1; block--; }
    if (fnum > 1023) fnum = 1023;
    opl_write(vreg(v, 0xA0 + v % 9), fnum & 0xFF);
    opl_write(vreg(v, 0xB0 + v % 9), (keyon ? 0x20 : 0) | (block << 2) | (fnum >> 8));
}

static void voice_off(int v)
{
    if (!V[v].on) return;
    V[v].on = 0;
    V[v].age = ++clock;
    voice_freq(v, 0);
}

static int voice_alloc(void)
{
    int v, best = -1;
    unsigned oldest = ~0u;
    for (v = 0; v < nvoices; v++)                      /* the free voice released longest ago */
        if (!V[v].on && V[v].age < oldest) { oldest = V[v].age; best = v; }
    if (best >= 0) return best;
    for (v = 0; v < nvoices; v++)                      /* steal: a second voice first ... */
        if (V[v].iv) { voice_off(v); return v; }
    for (v = 0; v < nvoices; v++)                      /* ... else the oldest note of the highest channel */
        if (best < 0 || V[v].ch > V[best].ch || (V[v].ch == V[best].ch && V[v].age < V[best].age)) best = v;
    voice_off(best);
    return best;
}

int midi_init(const void *genmidi)
{
    int type = opl_detect(), i;
    if (!type) { nvoices = 0; return 0; }
    bank = (const struct gm_instr *)(genmidi ? (const uint8_t *)genmidi : _sb_genmidi_bank);
    if (genmidi && !memcmp(genmidi, "#OPL_II#", 8)) bank = (const struct gm_instr *)((const uint8_t *)genmidi + 8);
    opl_reset();
    opl3 = type == 3;
    if (opl3) opl_write(0x105, 1);
    nvoices = opl3 ? 18 : 9;
    memset(V, 0, sizeof V);
    for (i = 0; i < 16; i++) { C[i].prog = 0; C[i].vol = 100; C[i].expr = 127; C[i].pan = 64; C[i].bend = 0; }
    master = 127;
    return nvoices;
}

void midi_shutdown(void)
{
    if (!nvoices) return;
    midi_all_off();
    opl_reset();
    nvoices = 0;
}

void midi_program(int ch, int program) { if (ch >= 0 && ch < 16) C[ch].prog = program & 127; }

void midi_note_off(int ch, int note)
{
    int v;
    for (v = 0; v < nvoices; v++)
        if (V[v].on && V[v].ch == ch && V[v].key == note) voice_off(v);
}

void midi_note_on(int ch, int note, int velocity)
{
    const struct gm_instr *in;
    int v, iv, n = note;
    if (!nvoices || ch < 0 || ch > 15) return;
    if (velocity <= 0) { midi_note_off(ch, note); return; }
    if (velocity > 127) velocity = 127;
    if (ch == 9) {
        if (note < 35 || note > 81) return;
        in = bank + 128 + note - 35;
    } else in = bank + C[ch].prog;
    if (in->flags_lo & GM_FIXED) n = in->fixed;
    for (iv = 0; iv < ((in->flags_lo & GM_2VOICE) ? 2 : 1); iv++) {
        const struct gm_voice *gv = &in->v[iv];
        v = voice_alloc();
        V[v].ch = ch; V[v].key = note; V[v].note = n; V[v].vel = velocity;
        V[v].on = 1; V[v].age = ++clock; V[v].iv = iv; V[v].ins = in;
        opl_write(vreg(v, 0xB0 + v % 9), 0);           /* make sure the envelope restarts */
        write_op(v, op_mod[v % 9] + 3, &gv->car, 0x3F);
        write_op(v, op_mod[v % 9], &gv->mod, (gv->feedback & 1) ? 0x3F : gv->mod.level);
        opl_write(vreg(v, 0xC0 + v % 9), gv->feedback | pan_bits(ch));
        voice_volume(v);
        voice_freq(v, 1);
    }
}

void midi_control(int ch, int controller, int value)
{
    int v;
    if (ch < 0 || ch > 15) return;
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    switch (controller) {
    case 7: C[ch].vol = value; break;
    case 11: C[ch].expr = value; break;
    case 10:
        C[ch].pan = value;
        for (v = 0; v < nvoices; v++)
            if (V[v].on && V[v].ch == ch)
                opl_write(vreg(v, 0xC0 + v % 9), V[v].ins->v[V[v].iv].feedback | pan_bits(ch));
        return;
    case 120: case 123:
        for (v = 0; v < nvoices; v++) if (V[v].on && V[v].ch == ch) voice_off(v);
        return;
    case 121: C[ch].vol = 100; C[ch].expr = 127; C[ch].pan = 64; C[ch].bend = 0; break;
    default: return;
    }
    for (v = 0; v < nvoices; v++) if (V[v].on && V[v].ch == ch) voice_volume(v);
}

void midi_pitch_bend(int ch, int value)
{
    int v;
    if (ch < 0 || ch > 15) return;
    C[ch].bend = (value - 8192) / 128;                 /* 1/32 semitones, +-2 semitones */
    for (v = 0; v < nvoices; v++) if (V[v].on && V[v].ch == ch) voice_freq(v, 1);
}

void midi_all_off(void)
{
    int v;
    for (v = 0; v < nvoices; v++) voice_off(v);
}

void midi_set_volume(int volume)
{
    int v;
    master = volume < 0 ? 0 : volume > 127 ? 127 : volume;
    for (v = 0; v < nvoices; v++) if (V[v].on) voice_volume(v);
}
