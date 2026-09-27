/* ttssyn.c - DRARM's speech engine: utterance planning (phones, durations,
 * pitch contour) and a Klatt-style cascade/parallel formant synthesiser that
 * produces 8-bit unsigned PCM at 11025 Hz.
 *
 * Source: an impulsive glottal pulse (the derivative of a t^2(Te-t) flow
 * pulse: a sharp closure spike, which gives the buzzy "robot" voice) plus
 * aspiration noise. Cascade branch: nasal pole, nasal zero, F1..F5.
 * Parallel branch (frication noise): F2..F6 resonators with their own
 * amplitudes and a bypass path. Parameters are updated every 5 ms frame and
 * interpolated between phoneme targets with locus-style transitions.
 */
#include <math.h>
#include <string.h>
#include "tts.h"
#include "ttsint.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- phoneme table ------------------------------------------------------ */
#define C_VOW   0x01   /* vowel */
#define C_DIPH  0x02   /* diphthong (second target) */
#define C_SON   0x04   /* liquid/glide */
#define C_NAS   0x08   /* nasal */
#define C_STOP  0x10   /* stop (closure + burst) */
#define C_FRIC  0x20   /* fricative */
#define C_AFF   0x40   /* affricate (closure + frication) */
#define C_VCD   0x80   /* voiced */
#define C_ASP  0x100   /* aspiration only (h) */

typedef struct {
    unsigned short cls;
    unsigned char dur, mindur;           /* ms */
    short f1, f2, f3, b1, b2, b3;       /* targets (Hz) */
    short f1b, f2b, f3b;                /* diphthong end target */
    signed char av, af, ah;             /* dB (0 = off) */
    signed char a2, a3, a4, a5, a6, ab; /* parallel (frication/burst) amps, dB */
    unsigned char ext;                  /* ms of influence into neighbours */
    unsigned char rank;                 /* weight at a boundary */
} phdef;

#define V  (C_VOW | C_VCD)
static const phdef P[PH_COUNT] = {
    /*        cls        dur min   F1   F2   F3  B1  B2  B3   F1b  F2b  F3b  av af ah  a2 a3 a4 a5 a6 ab ext rank */
    [PH_PAUSE] = { 0,     60, 60,  400,1400,2500, 90,120,160,  0,   0,   0,   0, 0, 0,   0, 0, 0, 0, 0, 0,  0, 0 },
    [PH_IY] = { V,       145, 55,  290,2200,2950, 60, 90,150,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_IH] = { V,       120, 40,  400,1900,2550, 70,100,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_EY] = { V|C_DIPH,170, 90,  480,1750,2500, 70,100,140,330,2100,2700,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_EH] = { V,       140, 60,  560,1720,2450, 70,100,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AE] = { V,       200, 80,  680,1650,2400, 80,110,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AA] = { V,       200, 90,  720,1100,2450, 90,100,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AO] = { V,       200, 90,  580, 880,2450, 80, 90,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_OW] = { V|C_DIPH,190, 80,  540, 950,2400, 80, 90,140,420, 800,2350,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_UH] = { V,       140, 60,  450,1050,2250, 80,100,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_UW] = { V,       170, 70,  330, 950,2250, 70, 90,140,300, 850,2250,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AH] = { V,       130, 60,  640,1200,2400, 80, 90,140,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AX] = { V,        90, 45,  520,1400,2450, 80,100,140,  0,   0,   0,  58, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_ER] = { V,       160, 80,  480,1350,1650, 80, 90,110,  0,   0,   0,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AY] = { V|C_DIPH,210,120,  720,1200,2500, 90,100,140,380,1950,2600,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_AW] = { V|C_DIPH,220,110,  720,1250,2500, 90,100,140,460, 950,2400,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    [PH_OY] = { V|C_DIPH,230,130,  560, 850,2450, 80, 90,140,400,1900,2600,  60, 0, 0,   0, 0, 0, 0, 0, 0, 20, 2 },
    /* stops: the targets are loci; av of voiced = voice bar */
    [PH_P]  = { C_STOP,   85, 55,  400, 900,2150,150,150,200,  0,   0,   0,   0,60, 0,   0, 0, 0, 0, 0,57, 45, 6 },
    [PH_B]  = { C_STOP|C_VCD,75,55,200, 900,2150, 80,150,200,  0,   0,   0,  38,58, 0,   0, 0, 0, 0, 0,52, 45, 6 },
    [PH_T]  = { C_STOP,   75, 50,  400,1700,2650,150,150,200,  0,   0,   0,   0,60, 0,   0, 0,48,52,44, 0, 40, 6 },
    [PH_D]  = { C_STOP|C_VCD,70,50,200,1700,2650, 80,150,200,  0,   0,   0,  38,58, 0,   0, 0,46,48,40, 0, 40, 6 },
    [PH_K]  = { C_STOP,   80, 60,  350,1900,2400,150,150,200,  0,   0,   0,   0,60, 0,   0,53,43,40, 0, 0, 45, 6 },
    [PH_G]  = { C_STOP|C_VCD,75,60,200,1900,2400, 80,150,200,  0,   0,   0,  38,58, 0,   0,50,40,36, 0, 0, 45, 6 },
    [PH_F]  = { C_FRIC,  100, 80,  340,1100,2100,200,120,150,  0,   0,   0,   0,50, 0,   0, 0, 0, 0, 0,50, 25, 5 },
    [PH_V]  = { C_FRIC|C_VCD,60,40,220,1100,2100, 80,120,150,  0,   0,   0,  52,42, 0,   0, 0, 0, 0, 0,45, 25, 5 },
    [PH_TH] = { C_FRIC,   90, 60,  320,1300,2550,200, 90,200,  0,   0,   0,   0,50, 0,   0, 0,36,38,30,40, 25, 5 },
    [PH_DH] = { C_FRIC|C_VCD,50,30,270,1300,2550, 80, 90,200,  0,   0,   0,  52,40, 0,   0, 0,32,34, 0,34, 25, 5 },
    [PH_S]  = { C_FRIC,  105, 60,  320,1400,2550,200, 90,200,  0,   0,   0,   0,56, 0,   0, 0,46,56,42, 0, 25, 5 },
    [PH_Z]  = { C_FRIC|C_VCD,75,40,240,1400,2550, 70, 90,200,  0,   0,   0,  50,48, 0,   0, 0,42,52,38, 0, 25, 5 },
    [PH_SH] = { C_FRIC,  110, 80,  300,1800,2700,200, 90,300,  0,   0,   0,   0,56, 0,   0,57,52,46,40, 0, 25, 5 },
    [PH_ZH] = { C_FRIC|C_VCD,75,40,300,1800,2700, 70, 90,300,  0,   0,   0,  50,48, 0,   0,54,48,42,36, 0, 25, 5 },
    [PH_HH] = { C_ASP,    55, 25,    0,   0,   0,200,150,200,  0,   0,   0,   0, 0,47,   0, 0, 0, 0, 0, 0,  0, 0 },
    [PH_M]  = { C_NAS|C_VCD,75,55, 480,1100,2150, 50,200,200,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 30, 5 },
    [PH_N]  = { C_NAS|C_VCD,65,45, 480,1450,2550, 50,200,200,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 30, 5 },
    [PH_NG] = { C_NAS|C_VCD,90,60, 480,2000,2700, 50,200,200,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 30, 5 },
    [PH_L]  = { C_SON|C_VCD,75,40, 350,1000,2700, 60,120,160,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 40, 3 },
    [PH_R]  = { C_SON|C_VCD,75,50, 320,1100,1400, 70,100,120,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 45, 3 },
    [PH_W]  = { C_SON|C_VCD,70,50, 300, 650,2200, 60, 90,150,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 50, 3 },
    [PH_Y]  = { C_SON|C_VCD,70,40, 270,2100,3000, 60,100,200,  0,   0,   0,  56, 0, 0,   0, 0, 0, 0, 0, 0, 50, 3 },
    [PH_CH] = { C_AFF,    95, 70,  350,1800,2800,200, 90,300,  0,   0,   0,   0,60, 0,   0,57,52,46,40, 0, 30, 6 },
    [PH_JH] = { C_AFF|C_VCD,80,60, 260,1800,2800, 80, 90,300,  0,   0,   0,  45,50, 0,   0,54,48,42,36, 0, 30, 6 },
    [PH_WH] = { C_SON,    75, 50,  300, 650,2200, 60, 90,150,  0,   0,   0,   0, 0,50,   0, 0, 0, 0, 0, 0, 50, 3 },
};

/* ---- settings ------------------------------------------------------------ */
static int s_pitch = 5, s_speed = 5, s_tone = 5, s_volume = 5;
int tts_monotone;

void tts_settings(int pitch, int speed, int tone, int volume)
{
#define CL(x) ((x) < 0 ? 0 : (x) > 9 ? 9 : (x))
    s_pitch = CL(pitch); s_speed = CL(speed); s_tone = CL(tone); s_volume = CL(volume);
}
void tts_get_settings(int *pitch, int *speed, int *tone, int *volume)
{
    if (pitch) *pitch = s_pitch;
    if (speed) *speed = s_speed;
    if (tone) *tone = s_tone;
    if (volume) *volume = s_volume;
}

/* ---- the plan ------------------------------------------------------------ */
#define MAXPH   600
#ifdef TTS_MAXSEG             /* a program that only says short lines can use less (TERM: 360) */
#define MAXSEG  TTS_MAXSEG
#else
#define MAXSEG  900
#endif
#define MAXPP  1200
#define MAXWORDS 160

enum { SK_MAIN, SK_CLOSURE, SK_BURST, SK_ASP, SK_FRIC };

typedef struct {
    unsigned char ph, kind;
    int start, len;                      /* samples */
    float f[3], fb[3], bw[3];            /* target (and end target) */
    float av, af, ah, pa[6];             /* dB; pa = a2..a6, ab */
    unsigned char nasal, diph, rank, ext;   /* ext in ms */
} seg;

static tts_phone ph[MAXPH];
static int nph;
static seg sg[MAXSEG];
static int nsg;
static float ppt[MAXPP], ppv[MAXPP];     /* pitch points: sample, Hz */
static int npp;
static long wsample[MAXWORDS];
static int woffset[MAXWORDS], nwords;
static long total;

int  tts_word_count(void) { return nwords; }
long tts_word_sample(int i) { return i >= 0 && i < nwords ? wsample[i] : total; }
int  tts_word_offset(int i) { return i >= 0 && i < nwords ? woffset[i] : 0; }
long tts_total_samples(void) { return total; }

static int is_vowel(int p) { return P[p].cls & C_VOW; }
static int is_cons(int p) { return p != PH_PAUSE && !(P[p].cls & C_VOW); }

/* parse the rule notation into phones (letters of the spelled name etc.) */
int tts_parse_notation(const char *s, tts_phone *out, int max);

static void push_pause(int ms, int flags)
{
    if (nph >= MAXPH) return;
    ph[nph].ph = PH_PAUSE; ph[nph].stress = (unsigned char)(ms / 10); ph[nph].flags = (unsigned char)flags;
    ph[nph].word = nwords ? nwords - 1 : 0;
    nph++;
}

/* text -> phones */
static void build_phones(const char *text)
{
    static tts_token tk[160];
    int n = tts_tokenise(text, tk, 160), i, j;
    nph = 0; nwords = 0;
    push_pause(40, 0);
    for (i = 0; i < n; i++) {
        if (tk[i].kind == TK_PUNCT) {
            int q = tk[i].text[0] == '?', stop = tk[i].text[0] != ',';
            /* mark the phrase end on the last real phone */
            for (j = nph - 1; j >= 0 && ph[j].ph == PH_PAUSE; j--) ;
            if (j >= 0 && ph[nph - 1].ph != PH_PAUSE) {
                int k;
                for (k = j; k >= 0 && ph[k].ph != PH_PAUSE; k--) {
                    if (q) ph[k].flags |= TF_QUESTION;
                    if (k == j) ph[k].flags |= TF_PHRASEEND | (stop ? TF_FINAL : 0);
                }
                push_pause(stop ? 380 : 180, 0);
            }
            continue;
        }
        if (nwords >= MAXWORDS) continue;
        {
            int start = nph, m;
            if (tk[i].kind == TK_PHON) m = tts_parse_notation(tk[i].text, ph + nph, MAXPH - nph - 4);
            else m = tts_word_phones(tk[i].text, ph + nph, MAXPH - nph - 4, !tk[i].nodict);
            if (m <= 0) continue;
            woffset[nwords] = tk[i].src;
            for (j = 0; j < m; j++) ph[start + j].word = (unsigned char)nwords;
            ph[start].flags |= TF_WORDSTART;
            ph[start + m - 1].flags |= TF_WORDEND;
            nph += m;
            nwords++;
            /* spelled letters are separate little utterances */
            if (tk[i].kind == TK_PHON && tts_spell_mode) {
                for (j = start; j < nph; j++) ph[j].flags |= TF_FINAL * 0;
                ph[nph - 1].flags |= TF_PHRASEEND | TF_FINAL;
                push_pause(250, 0);
            }
        }
    }
    /* a sentence without final punctuation ends like a statement */
    for (j = nph - 1; j >= 0 && ph[j].ph == PH_PAUSE; j--) ;
    if (j >= 0 && !(ph[j].flags & TF_PHRASEEND)) ph[j].flags |= TF_PHRASEEND | TF_FINAL;
    push_pause(120, 0);
}

/* is phone i in the final syllable of its phrase? (the vowel of the last word
 * holding the phrase end, and what follows it) */
static int phrase_final_syll(int i)
{
    int j;
    for (j = i; j < nph && ph[j].ph != PH_PAUSE; j++) {
        if (j > i && is_vowel(ph[j].ph)) return 0;
        if (ph[j].flags & TF_PHRASEEND) return 1;
    }
    return 0;
}

static short tailpos[MAXPH], tailn[MAXPH];

/* the TF_QUESTION/TF_FINAL flags of the phrase end after phone i; -1 if the
 * phrase has no end mark */
static int phrase_flags(int i)
{
    for (; i < nph && ph[i].ph != PH_PAUSE; i++)
        if (ph[i].flags & TF_PHRASEEND) return ph[i].flags & (TF_QUESTION | TF_FINAL);
    return -1;
}

/* how many words after phone i's word until the end of its phrase */
static int words_to_end(int i)
{
    int j;
    for (j = i; j < nph && ph[j].ph != PH_PAUSE; j++)
        if (ph[j].flags & TF_PHRASEEND) return ph[j].word - ph[i].word;
    return 99;
}

static float speed_factor(void)
{
    /* 0 = slowest (x1.6) .. 5 = 1.0 .. 9 = fastest (x0.62) */
    static const float f[10] = { 1.75f, 1.58f, 1.42f, 1.28f, 1.16f, 1.06f, 0.96f, 0.86f, 0.77f, 0.68f };
    return f[s_speed];
}

static int ms2s(float ms) { return (int)(ms * (TTS_RATE / 1000.0f) + 0.5f); }

static void set_targets(seg *s, int p)
{
    const phdef *d = &P[p];
    s->ph = (unsigned char)p;
    s->f[0] = d->f1; s->f[1] = d->f2; s->f[2] = d->f3;
    s->bw[0] = d->b1; s->bw[1] = d->b2; s->bw[2] = d->b3;
    s->diph = (d->cls & C_DIPH) || d->f1b;
    s->fb[0] = d->f1b ? d->f1b : d->f1; s->fb[1] = d->f2b ? d->f2b : d->f2; s->fb[2] = d->f3b ? d->f3b : d->f3;
    s->av = d->av; s->af = 0; s->ah = d->ah;
    s->pa[0] = d->a2; s->pa[1] = d->a3; s->pa[2] = d->a4; s->pa[3] = d->a5; s->pa[4] = d->a6; s->pa[5] = d->ab;
    s->nasal = (d->cls & C_NAS) != 0;
    s->rank = d->rank; s->ext = d->ext;
}

static seg *add_seg(int p, int kind, int len)
{
    seg *s;
    if (nsg >= MAXSEG) return 0;
    s = &sg[nsg++];
    memset(s, 0, sizeof *s);
    set_targets(s, p);
    s->kind = (unsigned char)kind;
    s->len = len < 1 ? 1 : len;
    return s;
}

static void add_pp(long t, float hz)
{
    if (npp >= MAXPP) return;
    if (npp && t <= ppt[npp - 1]) t = (long)ppt[npp - 1] + 1;
    ppt[npp] = (float)t; ppv[npp] = hz; npp++;
}

static float base_f0(void)
{
    static const float b[10] = { 60, 65, 70, 77, 81, 86, 93, 103, 114, 129 };
    return b[s_pitch];
}

static void plan(void)
{
    int i, t = 0;
    float sf = speed_factor(), base = base_f0();
    int nvow_total = 0, vowel_no = 0;
    int w = -1;
    nsg = 0; npp = 0;
    for (i = 0; i < nph; i++) if (is_vowel(ph[i].ph)) nvow_total++;
    /* per phrase: the nucleus (last stressed vowel of a content word, else the
     * last vowel) gets tailpos 0, the vowels after it 1, 2, ...; tailn = how
     * many follow the nucleus. Everything else: -1. */
    {
        int a = 0;
        for (i = 0; i < nph; i++) tailpos[i] = -1;
        while (a < nph) {
            int e = a, j, nucl = -1, lastv = -1, cnt = 0;
            while (e < nph && ph[e].ph != PH_PAUSE) e++;
            for (j = a; j < e; j++) if (is_vowel(ph[j].ph)) {
                lastv = j;
                if (ph[j].stress == 1 && !(ph[j].flags & TF_FUNCWORD)) nucl = j;
            }
            if (nucl < 0) nucl = lastv;
            if (nucl >= 0) {
                for (j = nucl; j < e; j++) if (is_vowel(ph[j].ph)) tailpos[j] = (short)cnt++;
                for (j = nucl; j < e; j++) if (is_vowel(ph[j].ph)) tailn[j] = (short)(cnt - 1);
            }
            a = e + 1;
        }
    }
    for (i = 0; i < nph; i++) {
        int p = ph[i].ph, cls = P[p].cls;
        int prev = i ? ph[i - 1].ph : PH_PAUSE, next = i + 1 < nph ? ph[i + 1].ph : PH_PAUSE;
        float pct = 1.0f, ms;
        seg *s;
        if (ph[i].word != w && p != PH_PAUSE) { w = ph[i].word; if (w < MAXWORDS) wsample[w] = t; }
        if (p == PH_PAUSE) {
            s = add_seg(PH_PAUSE, SK_MAIN, ms2s(ph[i].stress * 10.0f * (0.5f + 0.5f * sf)));
            if (s) { s->start = t; t += s->len; }
            continue;
        }
        /* durations, after Klatt's rules (simplified) */
        if (cls & C_VOW) {
            if (ph[i].flags & TF_FUNCWORD) pct *= 0.55f;
            else if (!ph[i].stress) pct *= 0.6f;
            if (is_cons(next) && !(P[next].cls & C_VCD) && !(ph[i].flags & TF_WORDEND)) pct *= 0.75f;
        } else {
            if (is_cons(prev) || is_cons(next)) pct *= 0.75f;
            if (ph[i].flags & TF_FUNCWORD) pct *= 0.8f;
        }
        if (phrase_final_syll(i)) pct *= (ph[i].flags & TF_FUNCWORD) ? 1.2f : 1.35f;
        ms = P[p].mindur + (P[p].dur - P[p].mindur) * pct;
        ms *= sf;
        if (tts_spell_mode) ms *= 1.3f;
        if (cls & (C_STOP | C_AFF)) {
            int voiceless = !(cls & C_VCD);
            float clo = ms * (cls & C_AFF ? 0.45f : 0.7f);
            int after_pause = prev == PH_PAUSE;
            if (after_pause) clo *= 0.5f;
            s = add_seg(p, SK_CLOSURE, ms2s(clo));
            if (s) {
                s->start = t; t += s->len;
                if (voiceless) s->av = 0; else if (after_pause) s->av = 0;
                s->af = 0; memset(s->pa, 0, sizeof s->pa);
            }
            if (cls & C_AFF) {
                s = add_seg(p, SK_FRIC, ms2s(ms * 0.55f));
                if (s) { s->start = t; t += s->len; s->af = P[p].af; }
            } else {
                float bl = p == PH_K || p == PH_G ? 14 : p == PH_T || p == PH_D ? 10 : 7;
                s = add_seg(p, SK_BURST, ms2s(bl));
                if (s) { s->start = t; t += s->len; s->af = P[p].af; s->av = voiceless ? 0 : s->av; }
                /* aspiration of a voiceless stop before a voiced sound */
                if (voiceless && (is_vowel(next) || (P[next].cls & C_SON))) {
                    float al = (prev == PH_S ? 10 : 30) * (0.6f + 0.4f * sf);
                    s = add_seg(p, SK_ASP, ms2s(al));
                    if (s) { s->start = t; t += s->len; s->ah = 50; s->af = 0; s->rank = 0; s->ext = 0; }
                } else if (!voiceless && is_vowel(next)) {
                    /* short voiced release */
                }
            }
            continue;
        }
        s = add_seg(p, SK_MAIN, ms2s(ms));
        if (!s) break;
        s->start = t;
        /* an h between voiced sounds ("one hundred", "megahertz") is breathy
         * voice, not a block of noise: keep some voicing, soften the breath */
        if (p == PH_HH && prev != PH_PAUSE && (P[prev].cls & C_VCD) && (is_vowel(next) || (P[next].cls & C_VCD))) {
            s->av = 50; s->ah = 42;
        }
        if (cls & C_FRIC) s->af = P[p].af;
        /* pitch points for vowels */
        if (cls & C_VOW) {
            float decl = 3.0f - 6.0f * (nvow_total > 1 ? (float)vowel_no / (nvow_total - 1) : 0.5f);
            float b = base + decl * base / 88.0f;
            int stressed = ph[i].stress == 1 && !(ph[i].flags & TF_FUNCWORD);
            int L = s->len, onset = ms2s(40);
            float k = base / 88.0f;          /* excursions scale with the pitch */
            int pf = phrase_flags(i), nuc = tailpos[i] == 0, tail = tailpos[i] > 0;
            int wend = words_to_end(i);
            if (onset > L / 2) onset = L / 2;
            if (tts_monotone) {
                add_pp(t, base); add_pp(t + L, base);
            } else if (tts_spell_mode) {
                /* a spelled letter: said on its own, a short fall from just
                 * above the baseline (the recording: ~97 -> 88 Hz) */
                add_pp(t, b + 8 * k); add_pp(t + L, b - 2 * k);
            } else if (pf >= 0 && !(pf & TF_QUESTION) && wend <= 2) {
                float dk = (pf & TF_FINAL) ? k : 0.4f * k;   /* a comma steps down less */
                /* the end of a statement "runs out of breath": the last three
                 * words step down, each lower than the one before, and the
                 * last one sinks to the bottom of the voice */
                if (wend == 2) {
                    add_pp(t, b); add_pp(t + L * 35 / 100, b + (stressed ? 9 : 0) * k); add_pp(t + L, b - 1 * dk);
                } else if (wend == 1) {
                    add_pp(t, b - 4 * dk); add_pp(t + L, b - 6 * dk);
                } else {
                    int nv = 0, iv = 0, j;
                    float x0, x1;
                    for (j = i; j >= 0 && ph[j].word == ph[i].word && ph[j].ph != PH_PAUSE; j--) if (is_vowel(ph[j].ph)) iv++;
                    for (j = i; j < nph && ph[j].word == ph[i].word && ph[j].ph != PH_PAUSE; j++) if (is_vowel(ph[j].ph)) nv++;
                    nv += iv - 1;             /* vowels in the word; iv = 1-based index */
                    x0 = (float)(iv - 1) / nv; x1 = (float)iv / nv;
                    add_pp(t, b - (12 + 7 * x0) * dk);
                    add_pp(t + L, b - (12 + 7 * x1) * dk);
                }
            } else if (pf < 0 || !(pf & TF_QUESTION) || (!nuc && !tail)) {
                /* inside the phrase: the stressed-syllable bump - a quick high
                 * onset that settles */
                if (stressed) {
                    /* the recording's accents rise into the vowel and fall back */
                    add_pp(t, b + 1 * k);
                    add_pp(t + L * 35 / 100, b + 13 * k);
                    add_pp(t + L, b + 3 * k);
                } else if (ph[i].stress == 2) {
                    add_pp(t, b + 6 * k); add_pp(t + L, b + 2 * k);
                } else {
                    add_pp(t, b); add_pp(t + L, b - 1 * k);
                }
            } else {
                /* questions and commas: the last accent of the phrase and the
                 * syllables after it carry the tune - question rise, comma level */
                float v0, v1, end, x0, x1;
                int q = pf & TF_QUESTION;
                if (q) { v0 = b + 4 * k; end = b + 60 * k; }
                else { v0 = b + 6 * k; end = b + 4 * k; }
                x0 = (float)(tailpos[i]) / (tailn[i] + 1);
                x1 = (float)(tailpos[i] + 1) / (tailn[i] + 1);
                v1 = v0 + (end - v0) * x1;
                if (nuc) {
                    float peak = q ? b + 8 * k : b + 14 * k;
                    add_pp(t, peak);
                    add_pp(t + onset, q ? v0 : peak - 6 * k);
                    add_pp(t + L, v1);
                } else {
                    add_pp(t, v0 + (end - v0) * x0);
                    add_pp(t + L, v1);
                }
            }
            vowel_no++;
        }
        t += s->len;
    }
    /* h and the aspiration take the formants of what follows */
    for (i = 0; i < nsg; i++)
        if ((sg[i].ph == PH_HH || sg[i].kind == SK_ASP) && i + 1 < nsg && sg[i + 1].ph != PH_PAUSE) {
            int k;
            for (k = 0; k < 3; k++) { sg[i].f[k] = sg[i].fb[k] = sg[i + 1].f[k]; sg[i].bw[k] = sg[i + 1].bw[k] * 1.25f; }
        }
    if (!npp) { add_pp(0, base); }
    add_pp(t + 1, ppv[npp - 1]);
    total = t;
    for (i = nwords; i < MAXWORDS && i < nwords + 1; i++) wsample[i] = total;
}

/* ---- per-frame parameters -------------------------------------------------- */
typedef struct {
    float f0, f[5], bw[5], av, ah, af, pa[6], fnz;
} frame;

static float target_at(const seg *s, int k, int off)
{
    if (!s->diph) return s->f[k];
    {
        float x = (float)off / s->len;
        if (x < 0.25f) return s->f[k];
        if (x > 0.85f) return s->fb[k];
        return s->f[k] + (s->fb[k] - s->f[k]) * (x - 0.25f) / 0.6f;
    }
}

static float boundary(const seg *a, const seg *b, int k, int end_of_a)
{
    float va = end_of_a ? target_at(a, k, a->len) : target_at(a, k, 0);
    float vb = end_of_a ? target_at(b, k, 0) : target_at(b, k, b->len);
    float wa = a->rank, wb = b->rank;
    if (a->ph == PH_PAUSE || a->kind == SK_ASP || a->ph == PH_HH) return vb;
    if (b->ph == PH_PAUSE || b->kind == SK_ASP || b->ph == PH_HH) return va;
    if (wa + wb <= 0) return 0.5f * (va + vb);
    return (va * wa + vb * wb) / (wa + wb);
}

static float lerp(float a, float b, float x) { return a + (b - a) * x; }
static float db2lin(float db) { return db <= 0 ? 0.0f : powf(10.0f, (db - 60.0f) / 20.0f); }

static int cur_seg;
/* broad formants: each glottal pulse rings briefly and dies away, which
 * makes the clicky, buzzy voice of the old synthesisers */
static const float bw_scale[3] = { 1.6f, 1.1f, 1.7f };

static float nasal_zero(const seg *s)
{
    return s->nasal ? (s->ph == PH_M ? 450.0f : s->ph == PH_N ? 520.0f : 600.0f) : 270.0f;
}

/* a value that belongs to a segment, crossfaded over 10 ms at its edges
 * (halfway at the boundary) */
static float xfade(float prev, float cur, float next, int off, int L, int hasp, int hasn)
{
    int x = ms2s(10);
    if (x > L / 2) x = L / 2;
    if (x < 1) return cur;
    if (hasp && off < x) return lerp(prev, cur, 0.5f + 0.5f * off / x);
    if (hasn && off > L - x) return lerp(cur, next, 0.5f * (off - (L - x)) / x);
    return cur;
}

static void params_at(long t, frame *fr)
{
    int i, k;
    const seg *s, *pv, *nx;
    int off, L, tin, tout;
    while (cur_seg + 1 < nsg && t >= sg[cur_seg + 1].start) cur_seg++;
    while (cur_seg > 0 && t < sg[cur_seg].start) cur_seg--;
    s = &sg[cur_seg];
    pv = cur_seg > 0 ? &sg[cur_seg - 1] : s;
    nx = cur_seg + 1 < nsg ? &sg[cur_seg + 1] : s;
    off = (int)(t - s->start); L = s->len;
    tin = ms2s(pv->ext); tout = ms2s(nx->ext);
    if (s->kind == SK_BURST || s->kind == SK_ASP || s->kind == SK_FRIC) { tin = tout = 0; }
    if (tin > L / 2) tin = L / 2;
    if (tout > L / 2) tout = L / 2;
    for (k = 0; k < 3; k++) {
        float v = target_at(s, k, off);
        if (tin > 0 && off < tin && pv != s) v = lerp(boundary(pv, s, k, 1), v, (float)off / tin);
        else if (tout > 0 && off > L - tout && nx != s) v = lerp(v, boundary(s, nx, k, 1), (float)(off - (L - tout)) / tout);
        fr->f[k] = v;
        fr->bw[k] = xfade(pv->bw[k], s->bw[k], nx->bw[k], off, L, pv != s, nx != s) * bw_scale[k];
    }
    fr->f[3] = 3300; fr->bw[3] = 350;
    fr->f[4] = 3850; fr->bw[4] = 400;
    /* the nasal zero glides too: a sudden jump of an anti-resonator is a click */
    fr->fnz = xfade(nasal_zero(pv), nasal_zero(s), nasal_zero(nx), off, L, pv != s, nx != s);
    /* source amplitudes: crossfade over 6 ms around boundaries */
    {
        int x = ms2s(6);
        float av = s->av, ah = s->ah, af = s->af, pa[6];
        for (k = 0; k < 6; k++) pa[k] = s->pa[k];
        if (off < x && pv != s) {
            float m = 0.5f + 0.5f * off / x;
            av = lerp(pv->av, av, m); ah = lerp(pv->ah, ah, m);
            if (s->kind != SK_BURST) af = lerp(pv->af, af, m);
        } else if (off > L - x && nx != s) {
            float m = 0.5f * (off - (L - x)) / x;
            av = lerp(av, nx->av, m); ah = lerp(ah, nx->ah, m);
            if (s->kind != SK_BURST) af = lerp(af, nx->af, m);
        }
        /* decay into a pause (the phrase fades out) */
        if (nx->ph == PH_PAUSE && s->ph != PH_PAUSE && (P[s->ph].cls & C_VCD)) {
            int fade = ms2s(70);
            if (fade > L) fade = L;
            if (off > L - fade) av -= 14.0f * (off - (L - fade)) / fade;
        }
        fr->av = db2lin(av); fr->ah = db2lin(ah); fr->af = db2lin(af);
        for (k = 0; k < 6; k++) fr->pa[k] = db2lin(pa[k]);
    }
    /* pitch */
    {
        static int pi;
        if (pi >= npp || (pi > 0 && ppt[pi - 1] > t)) pi = 0;
        while (pi < npp && ppt[pi] <= t) pi++;
        if (pi == 0) fr->f0 = ppv[0];
        else if (pi >= npp) fr->f0 = ppv[npp - 1];
        else fr->f0 = lerp(ppv[pi - 1], ppv[pi], (t - ppt[pi - 1]) / (ppt[pi] - ppt[pi - 1]));
    }
    (void)i;
}

/* ---- the synthesiser ------------------------------------------------------ */
typedef struct { float a, b, c, y1, y2; } reson;

static void set_res(reson *r, float f, float bw)
{
    float T = 1.0f / TTS_RATE;
    float rr = expf(-(float)M_PI * bw * T);
    r->c = -rr * rr;
    r->b = 2.0f * rr * cosf(2.0f * (float)M_PI * f * T);
    r->a = 1.0f - r->b - r->c;
}
/* unity gain at the centre frequency (for the parallel branch) */
static void set_res_peak(reson *r, float f, float bw)
{
    float th = 2.0f * (float)M_PI * f / TTS_RATE, re, im;
    set_res(r, f, bw);
    re = 1.0f - r->b * cosf(th) - r->c * cosf(2 * th);
    im = r->b * sinf(th) + r->c * sinf(2 * th);
    r->a = sqrtf(re * re + im * im);
}
static float res(reson *r, float x)
{
    float y = r->a * x + r->b * r->y1 + r->c * r->y2;
    r->y2 = r->y1; r->y1 = y;
    return y;
}
/* anti-resonator: y = a x + b x1 + c x2 */
typedef struct { float a, b, c, x1, x2; } antires;
static void set_anti(antires *r, float f, float bw)
{
    reson t;
    set_res(&t, f, bw);
    r->a = 1.0f / t.a; r->b = -t.b / t.a; r->c = -t.c / t.a;
}
static float anti(antires *r, float x)
{
    float y = r->a * x + r->b * r->x1 + r->c * r->x2;
    r->x2 = r->x1; r->x1 = x;
    return y;
}

static struct {
    long t;                 /* next sample */
    frame fr;
    int frame_left;
    reson rc[5], rnp, rp[5], tilt_dummy;
    antires rnz;
    float phase, T0, Te;    /* glottal period (samples), open phase */
    int in_period;
    float tilt, lp, hp;
    unsigned noise;
    float agc, nprev, o1, o2, i1, i2, pamp;
    unsigned pulses;
} S;

#ifndef CREAK_AMP
#define CREAK_AMP      0.16f   /* alternate pulses this much weaker (~1.5 dB) */
#define CREAK_AMP_LOW  0.16f   /* extra at 60 Hz and below */
#define CREAK_PER      0.008f  /* alternate periods +-0.8 % */
#define CREAK_PER_LOW  0.020f
#define CREAK_RAND     0.05f   /* random shimmer */
#endif

static float noise(void)
{
    S.noise = S.noise * 1103515245u + 12345u;
    return (float)((int)(S.noise >> 9) - (1 << 22)) / (float)(1 << 22);
}

static void load_frame(void)
{
    int k;
    params_at(S.t, &S.fr);
    for (k = 0; k < 5; k++) set_res(&S.rc[k], S.fr.f[k], S.fr.bw[k]);
    set_res(&S.rnp, 270, 100);
    set_anti(&S.rnz, S.fr.fnz, 100);
    /* parallel: F2 F3 F4 (current), 3850, 4900 */
    set_res_peak(&S.rp[0], S.fr.f[1], 200);
    set_res_peak(&S.rp[1], S.fr.f[2], 250);
    set_res_peak(&S.rp[2], 3300, 300);
    set_res_peak(&S.rp[3], 3850, 400);
    set_res_peak(&S.rp[4], 4400, 700);
    S.frame_left = ms2s(5);
}

int tts_begin(const char *text)
{
    build_phones(text);
    plan();
    memset(&S, 0, sizeof S);
    S.noise = 0x1234567u;
    cur_seg = 0;
    load_frame();
    S.phase = 0; S.in_period = 0;
    return (int)total;
}

static float volume_gain(void)
{
    static const float v[10] = { 0.12f, 0.2f, 0.3f, 0.42f, 0.56f, 0.72f, 0.85f, 1.0f, 1.2f, 1.45f };
    return v[s_volume];
}

int tts_generate(unsigned char *buf, int max)
{
    int n = 0;
    float gain = 20000.0f * volume_gain() / 128.0f;
    /* tone: 0 = dull (strong low-pass) .. 9 = bright */
    float tilt = 0.92f - 0.055f * s_tone;
    if (tilt < 0) tilt = 0;
    while (n < max && S.t < total) {
        float glot = 0, asp, out, casc, par, nz;
        if (S.frame_left <= 0) load_frame();
        S.frame_left--;
        /* glottal source: a pulse per period */
        if (S.fr.av > 0 || S.in_period) {
            if (!S.in_period || S.phase >= S.T0) {
                S.phase = 0;
                S.T0 = TTS_RATE / (S.fr.f0 > 40 ? S.fr.f0 : 40);
                /* a little creak, as in the recording: successive pulses
                 * alternate in strength (and slightly in length), more so as
                 * the voice sinks below 80 Hz, plus a touch of random shimmer */
                {
                    float low = (80.0f - S.fr.f0) / 20.0f, d, j;
                    if (low < 0) low = 0; else if (low > 1) low = 1;
                    d = CREAK_AMP + CREAK_AMP_LOW * low;
                    j = CREAK_PER + CREAK_PER_LOW * low;
                    S.pulses++;
                    S.pamp = (S.pulses & 1 ? 1.0f - d : 1.0f) * (1.0f + CREAK_RAND * noise());
                    S.T0 *= S.pulses & 1 ? 1.0f + j : 1.0f - j;
                }
                S.Te = 0.3f * S.T0;
                S.in_period = S.fr.av > 0;
            }
            if (S.in_period && S.phase < S.Te) {
                float x = S.phase, Te = S.Te;
                /* derivative of the flow x^2 (Te - x): 2 x Te - 3 x^2, peak -Te^2 at closure */
                glot = (2 * x * Te - 3 * x * x) / (Te * Te) * S.fr.av * S.pamp;
            }
            S.phase += 1;
        }
        /* spectral tilt */
        S.lp = glot + tilt * S.lp;
        glot = S.lp * (1 - tilt);
        /* noise, low-passed (x + x1) / 2: -5 dB at 3.5 kHz, -10 dB at 4.5 kHz, nothing at
         * Nyquist - the old 11 kHz voices had no top-octave hiss */
        {
            float w = noise();
            nz = 0.5f * (w + S.nprev); S.nprev = w;
        }
        asp = nz * S.fr.ah;
        casc = glot * 1.0f + asp * 0.3f;
        casc = res(&S.rnp, anti(&S.rnz, casc));
        casc = res(&S.rc[0], casc);
        casc = res(&S.rc[1], casc);
        casc = res(&S.rc[2], casc);
        casc = res(&S.rc[3], casc);
        casc = res(&S.rc[4], casc);
        /* frication */
        par = 0;
        if (S.fr.af > 0) {
            float fn = nz * S.fr.af;
            par = res(&S.rp[0], fn) * S.fr.pa[0] - res(&S.rp[1], fn) * S.fr.pa[1]
                + res(&S.rp[2], fn) * S.fr.pa[2] - res(&S.rp[3], fn) * S.fr.pa[3]
                + res(&S.rp[4], fn) * S.fr.pa[4] + fn * S.fr.pa[5];
            par *= 0.55f;
        }
        out = (casc * 0.5f + par) * gain;
        /* the output low-pass (2nd-order Butterworth at 3.8 kHz: -4 dB at 4 kHz,
         * -22 dB at 5 kHz): the band the 11 kHz original carried, and no energy
         * near Nyquist for the DAC's images to turn into fizz and crackle */
        {
            float y = 0.492028f * (out + S.i2) + 0.984057f * S.i1 - 0.706804f * S.o1 - 0.261309f * S.o2;
            S.i2 = S.i1; S.i1 = out; S.o2 = S.o1; S.o1 = y; out = y;
        }
        {
            int v = (int)(out + (out >= 0 ? 0.5f : -0.5f));
            if (v > 127) v = 127; else if (v < -128) v = -128;
            buf[n++] = (unsigned char)(v + 128);
        }
        S.t++;
    }
    return n;
}

int tts_text_phonemes(const char *text, char *out, int outsz)
{
    int i, o = 0;
    build_phones(text);
    out[0] = 0;
    for (i = 0; i < nph; i++) {
        const char *nm = tts_phone_name(ph[i].ph);
        int l = (int)strlen(nm);
        if (ph[i].ph == PH_PAUSE) { if (i == 0 || i == nph - 1) continue; nm = "/"; l = 1; }
        if (o + l + 4 >= outsz) break;
        if (o) out[o++] = ' ';
        memcpy(out + o, nm, l); o += l;
        if (ph[i].stress && ph[i].ph != PH_PAUSE) out[o++] = (char)('0' + ph[i].stress);
    }
    out[o] = 0;
    return o;
}

#ifdef TTS_DEBUG
#include <stdio.h>
void tts_dump_segments(void)
{
    static const char *const kinds[] = { "main", "closure", "burst", "asp", "fric" };
    int i;
    for (i = 0; i < nsg; i++)
        printf("%6.3f %-3s %-7s %4.0f ms  av %2.0f af %2.0f ah %2.0f\n", sg[i].start / (float)TTS_RATE,
               tts_phone_name(sg[i].ph), kinds[sg[i].kind], sg[i].len * 1000.0f / TTS_RATE, sg[i].av, sg[i].af, sg[i].ah);
}
#endif
