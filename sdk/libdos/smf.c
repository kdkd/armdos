/* smf.c - Standard MIDI File reader and player (midi.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <armdos.h>
#include <midi.h>

static unsigned be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | (p[2] << 8) | p[3]; }

/* variable-length quantity; returns 0 past the end */
static int varlen(const unsigned char **pp, const unsigned char *end, unsigned long *v)
{
    const unsigned char *p = *pp;
    unsigned long x = 0;
    int n;
    for (n = 0; n < 4; n++) {
        if (p >= end) return 0;
        x = (x << 7) | (*p & 0x7F);
        if (!(*p++ & 0x80)) { *pp = p; *v = x; return 1; }
    }
    *pp = p; *v = x;
    return 1;
}

static void track_delta(smf_track *t)
{
    unsigned long d;
    if (t->pos >= t->end || !varlen(&t->pos, t->end, &d)) { t->done = 1; return; }
    t->next_tick += d;
}

static int smpte(const smf_file *f) { return (f->division & 0x8000) != 0; }

/* microseconds of `ticks` ticks at the current tempo */
static unsigned long long ticks_us(const smf_file *f, unsigned long ticks)
{
    if (smpte(f)) {
        unsigned fps = (unsigned)(-(signed char)(f->division >> 8)), tpf = f->division & 0xFF;
        if (fps == 29) fps = 30;
        return (unsigned long long)ticks * 1000000ull / ((unsigned long long)fps * (tpf ? tpf : 1));
    }
    return (unsigned long long)ticks * f->tempo / (f->division ? f->division : 96);
}

void smf_rewind(smf_file *f)
{
    const unsigned char *p = f->mem + f->chunk_off, *end = f->mem + f->size;
    unsigned n = 0;
    while (p + 8 <= end && n < f->ntracks) {
        unsigned long len = be32(p + 4);
        const unsigned char *body = p + 8, *bend = body + len > end ? end : body + len;
        if (!memcmp(p, "MTrk", 4)) {
            smf_track *t = &f->tracks[n++];
            t->pos = body; t->end = bend; t->next_tick = 0; t->running = 0; t->done = 0;
            track_delta(t);
        }
        if (len > (unsigned long)(end - body)) break;
        p = body + len;
    }
    f->ntracks = n;
    f->tempo = 500000; f->cur_us = 0; f->ref_tick = 0; f->cur_tick = 0;
}

int smf_next(smf_file *f, smf_event *ev)
{
    unsigned i, best = ~0u;
    unsigned long bt = ~0ul;
    smf_track *t;
    for (i = 0; i < f->ntracks; i++)
        if (!f->tracks[i].done && f->tracks[i].next_tick < bt) { bt = f->tracks[i].next_tick; best = i; }
    if (best == ~0u) return 0;
    t = &f->tracks[best];
    memset(ev, 0, sizeof *ev);
    ev->track = (unsigned char)best;
    ev->tick = bt;
    f->cur_tick = bt;
    ev->time_us = (unsigned long)(f->cur_us + ticks_us(f, bt - f->ref_tick));
    {
        unsigned char st = *t->pos;
        if (st & 0x80) t->pos++;
        else st = t->running;
        if (!(st & 0x80)) { t->done = 1; return smf_next(f, ev); }     /* corrupt: drop the track */
        ev->status = st;
        if (st < 0xF0) {
            t->running = st;
            if (t->pos >= t->end) { t->done = 1; return smf_next(f, ev); }
            ev->d1 = *t->pos++ & 0x7F;
            if ((st & 0xE0) != 0xC0) {
                if (t->pos >= t->end) { t->done = 1; return smf_next(f, ev); }
                ev->d2 = *t->pos++ & 0x7F;
            }
        } else if (st == 0xF0 || st == 0xF7) {
            unsigned long len;
            if (!varlen(&t->pos, t->end, &len) || len > (unsigned long)(t->end - t->pos)) { t->done = 1; return smf_next(f, ev); }
            ev->data = t->pos; ev->len = len; t->pos += len;
        } else if (st == 0xFF) {
            unsigned long len;
            if (t->pos >= t->end) { t->done = 1; return smf_next(f, ev); }
            ev->d1 = *t->pos++;
            if (!varlen(&t->pos, t->end, &len) || len > (unsigned long)(t->end - t->pos)) { t->done = 1; return smf_next(f, ev); }
            ev->data = t->pos; ev->len = len; t->pos += len;
            if (ev->d1 == 0x51 && len >= 3) {                 /* tempo: re-anchor the time base */
                f->cur_us += ticks_us(f, bt - f->ref_tick);
                f->ref_tick = bt;
                f->tempo = ((unsigned long)ev->data[0] << 16) | (ev->data[1] << 8) | ev->data[2];
                if (!f->tempo) f->tempo = 500000;
            }
            if (ev->d1 == 0x2F) { t->done = 1; return 1; }    /* end of track */
        } else {                                              /* F1-FE in a file: skip */
            t->done = 1; return smf_next(f, ev);
        }
    }
    track_delta(t);
    return 1;
}

static void copy_text(char *dst, unsigned cap, const unsigned char *s, unsigned long len)
{
    unsigned i, n = len < cap - 1 ? (unsigned)len : cap - 1;
    for (i = 0; i < n; i++) dst[i] = (s[i] >= 32 && s[i] < 127) ? (char)s[i] : ' ';
    dst[n] = 0;
    while (n && dst[n - 1] == ' ') dst[--n] = 0;
}

int smf_open_mem(smf_file *f, const void *data, unsigned long len)
{
    const unsigned char *m = data;
    smf_event ev;
    int have_tempo = 0;
    memset(f, 0, sizeof *f);
    if (len < 14 || memcmp(m, "MThd", 4) || be32(m + 4) < 6) return -2;
    f->mem = (unsigned char *)m; f->size = len;
    f->format = be16(m + 8);
    f->ntracks = be16(m + 10);
    if (f->ntracks > SMF_MAX_TRACKS) f->ntracks = SMF_MAX_TRACKS;
    f->division = be16(m + 12);
    f->chunk_off = 8 + be32(m + 4);   /* the track chunks follow the header (usually at 14) */
    if (f->chunk_off > len) return -2;
    smf_rewind(f);
    if (!f->ntracks) return -2;
    /* scan: length, title, copyright, first tempo, time signature */
    f->timesig_num = 4; f->timesig_den = 4;
    f->first_tempo = 500000;
    while (smf_next(f, &ev)) {
        f->length_us = ev.time_us; f->length_ticks = ev.tick;
        if (ev.status != 0xFF) continue;
        if (ev.d1 == 0x51 && !have_tempo) { f->first_tempo = f->tempo; have_tempo = 1; }
        else if (ev.d1 == 0x58 && ev.len >= 2) { f->timesig_num = ev.data[0]; f->timesig_den = 1u << (ev.data[1] & 7); }
        else if (ev.d1 == 0x03 && !f->title[0] && ev.track == 0) copy_text(f->title, sizeof f->title, ev.data, ev.len);
        else if (ev.d1 == 0x01 && !f->title[0] && ev.track == 0) copy_text(f->title, sizeof f->title, ev.data, ev.len);
        else if (ev.d1 == 0x02 && !f->copyright[0]) copy_text(f->copyright, sizeof f->copyright, ev.data, ev.len);
    }
    smf_rewind(f);
    return 0;
}

int smf_load(smf_file *f, const char *path)
{
    FILE *fp = fopen(path, "rb");
    long n;
    unsigned char *m;
    int r;
    memset(f, 0, sizeof *f);
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END); n = ftell(fp); fseek(fp, 0, SEEK_SET);
    if (n <= 0) { fclose(fp); return -2; }
    m = malloc(n);
    if (!m) { fclose(fp); return -3; }
    if (fread(m, 1, n, fp) != (size_t)n) { fclose(fp); free(m); return -1; }
    fclose(fp);
    r = smf_open_mem(f, m, n);
    if (r) { free(m); memset(f, 0, sizeof *f); return r; }
    f->owned = 1;
    return 0;
}

void smf_free(smf_file *f)
{
    if (f->owned) free(f->mem);
    memset(f, 0, sizeof *f);
}

void mpu_send_event(const smf_event *ev)
{
    if (ev->status < 0xF0) mpu_msg(ev->status, ev->d1, ev->d2);
    else if (ev->status == 0xF0) { mpu_write(0xF0); mpu_send(ev->data, (unsigned)ev->len); }
    else if (ev->status == 0xF7) mpu_send(ev->data, (unsigned)ev->len);     /* escaped raw bytes */
}

static int esc_pressed(unsigned long now_us, void *user)
{
    struct armregs r;
    (void)now_us; (void)user;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0100;
    _armdos_int16(&r);
    if (r.cpsr & ARM_CPSR_Z) return 0;            /* no key */
    memset(&r, 0, sizeof r);
    _armdos_int16(&r);                            /* AH=00h: take it */
    return (r.r0 & 0xFF) == 27;
}

int mpu_play_smf(smf_file *f, smf_poll_fn poll, void *user)
{
    static const unsigned char gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    smf_event ev;
    int stopped = 0;
    unsigned long t0, now;
    if (!mpu_info.present && !mpu_detect(NULL)) return -1;
    if (!mpu_uart()) return -1;
    if (!poll) poll = esc_pressed;
    mpu_send(gm_on, sizeof gm_on);
    midi_timer_start();
    smf_rewind(f);
    t0 = midi_timer_us() + 100000;                /* the synth takes GM System On in 100 ms */
    while (!stopped && smf_next(f, &ev)) {
        for (;;) {
            now = midi_timer_us();
            if ((long)(now - t0 - ev.time_us) >= 0) break;
            now = (long)(now - t0) > 0 ? now - t0 : 0;
            if (poll(now, user)) { stopped = 1; break; }
            armdos_halt();
        }
        if (!stopped) mpu_send_event(&ev);
    }
    mpu_all_notes_off();
    midi_timer_stop();
    smf_rewind(f);
    return stopped;
}
