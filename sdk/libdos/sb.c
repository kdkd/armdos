/* sb.c - Sound Blaster DSP, 8237 DMA and the double-buffered stream (sb.h). */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <armdos.h>
#include <sb.h>

#define inb(p)     armdos_inb(p)
#define outb(p, v) armdos_outb((p), (uint8_t)(v))

sb_card sb_info;
volatile unsigned long sb_irq_count;

/* ------------------------------------------------------------ timing */

/* Wait about n x 15 us: port 61h bit 4 (the AT's refresh request) toggles
 * every 15.085 us - the classic way to time short waits on an AT. */
static void wait15(unsigned n)
{
    unsigned b = inb(0x61) & 0x10;
    while (n--) {
        unsigned guard = 100000;
        while ((inb(0x61) & 0x10) == b && --guard) ;
        b ^= 0x10;
    }
}

/* ------------------------------------------------------------ BLASTER */

static unsigned field(const char *s, int hex)
{
    unsigned v = 0;
    for (;; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (hex && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (hex && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = v * (hex ? 16 : 10) + d;
    }
    return v;
}

static void parse_blaster(sb_card *c)
{
    const char *b = getenv("BLASTER");
    c->port = 0x220; c->irq = 7; c->dma8 = 1; c->dma16 = 5; c->type = 6;
    if (!b) return;
    c->dma16 = ~0u;
    for (; *b; b++) {
        if (*b == ' ' || *b == '\t') continue;
        switch (*b) {
        case 'A': case 'a': c->port = field(b + 1, 1); break;
        case 'I': case 'i': c->irq = field(b + 1, 0); break;
        case 'D': case 'd': c->dma8 = field(b + 1, 0); break;
        case 'H': case 'h': c->dma16 = field(b + 1, 0); break;
        case 'T': case 't': c->type = field(b + 1, 0); break;
        default: break;
        }
        while (b[1] && b[1] != ' ' && b[1] != '\t') b++;
    }
    if (c->dma16 == ~0u) c->dma16 = c->dma8;
}

/* ------------------------------------------------------------ DSP */

void sb_dsp_write(unsigned char v)
{
    unsigned guard = 100000;
    while ((inb(sb_info.port + 0xC) & 0x80) && --guard) ;
    outb(sb_info.port + 0xC, v);
}

int sb_dsp_read(void)
{
    unsigned guard = 100000;
    while (!(inb(sb_info.port + 0xE) & 0x80))
        if (!--guard) return -1;
    return inb(sb_info.port + 0xA);
}

int sb_reset_dsp(void)
{
    unsigned p = sb_info.port, k;
    outb(p + 6, 1);
    wait15(1);                               /* >= 3 us */
    outb(p + 6, 0);
    for (k = 0; k < 20; k++) {               /* up to ~300 us */
        if ((inb(p + 0xE) & 0x80) && inb(p + 0xA) == 0xAA) return 1;
        wait15(1);
    }
    return 0;
}

int sb_detect(sb_card *card)
{
    int hi, lo;
    memset(&sb_info, 0, sizeof sb_info);
    parse_blaster(&sb_info);
    if (sb_info.port && sb_reset_dsp()) {
        sb_dsp_write(0xE1);
        hi = sb_dsp_read(); lo = sb_dsp_read();
        if (hi > 0) {
            sb_info.dsp_major = (unsigned)hi;
            sb_info.dsp_minor = lo < 0 ? 0 : (unsigned)lo;
            sb_info.present = 1;
            if (sb_info.dsp_major < 4) sb_info.dma16 = sb_info.dma8;
        }
    }
    if (card) *card = sb_info;
    return sb_info.present;
}

const char *sb_name(void)
{
    if (!sb_info.present) return "no Sound Blaster";
    if (sb_info.dsp_major >= 4) return "Sound Blaster 16";
    if (sb_info.dsp_major == 3) return "Sound Blaster Pro";
    if (sb_info.dsp_major == 2) return "Sound Blaster 2.0";
    return "Sound Blaster";
}

void sb_speaker(int on)
{
    if (sb_info.present) sb_dsp_write(on ? 0xD1 : 0xD3);
}

static void mixer_set(unsigned reg, unsigned v)
{
    outb(sb_info.port + 4, reg);
    outb(sb_info.port + 5, v);
}

unsigned sb_mixer(unsigned reg, int value)
{
    if (!sb_info.present) return 0;
    outb(sb_info.port + 4, reg);
    if (value >= 0) outb(sb_info.port + 5, value);
    return inb(sb_info.port + 5);
}

void sb_set_volume(int master, int voice, int fm)
{
    if (!sb_info.present) return;
    if (sb_info.dsp_major >= 4) {
        if (master >= 0) { mixer_set(0x30, master << 3); mixer_set(0x31, master << 3); }
        if (voice >= 0)  { mixer_set(0x32, voice << 3);  mixer_set(0x33, voice << 3); }
        if (fm >= 0)     { mixer_set(0x34, fm << 3);     mixer_set(0x35, fm << 3); }
    } else if (sb_info.dsp_major == 3) {                 /* SB Pro: 4 bits per side */
        if (master >= 0) mixer_set(0x22, (master >> 1) * 0x11);
        if (voice >= 0)  mixer_set(0x04, (voice >> 1) * 0x11);
        if (fm >= 0)     mixer_set(0x26, (fm >> 1) * 0x11);
    }
}

/* ------------------------------------------------------------ 8237 */

static const uint8_t page_port[8] = { 0x87, 0x83, 0x81, 0x82, 0x8F, 0x8B, 0x89, 0x8A };

/* Program channel ch for auto-init "read from memory" of `bytes` bytes at phys. */
static void dma_start(unsigned ch, uint32_t phys, unsigned bytes)
{
    if (ch < 4) {
        unsigned count = bytes - 1;
        outb(0x0A, 4 | ch);                  /* mask */
        outb(0x0C, 0);                       /* clear the byte flip-flop */
        outb(0x0B, 0x58 | ch);               /* single, auto-init, read (memory -> device) */
        outb(ch * 2, phys & 0xFF);
        outb(ch * 2, (phys >> 8) & 0xFF);
        outb(page_port[ch], (phys >> 16) & 0xFF);
        outb(ch * 2 + 1, count & 0xFF);
        outb(ch * 2 + 1, (count >> 8) & 0xFF);
        if (phys >> 24) outb(0x400 + page_port[ch], phys >> 24);   /* ARM-PC/EISA high page */
        outb(0x0A, ch);                      /* unmask */
    } else {
        unsigned c = ch - 4, words = (bytes >> 1) - 1, w = (phys >> 1) & 0xFFFF;
        outb(0xD4, 4 | c);
        outb(0xD8, 0);
        outb(0xD6, 0x58 | c);
        outb(0xC0 + c * 4, w & 0xFF);
        outb(0xC0 + c * 4, (w >> 8) & 0xFF);
        outb(page_port[ch], (phys >> 16) & 0xFE);
        outb(0xC2 + c * 4, words & 0xFF);
        outb(0xC2 + c * 4, (words >> 8) & 0xFF);
        if (phys >> 24) outb(0x400 + page_port[ch], phys >> 24);
        outb(0xD4, c);
    }
}

static void dma_mask(unsigned ch)
{
    if (ch < 4) outb(0x0A, 4 | ch); else outb(0xD4, 4 | (ch - 4));
}

/* ------------------------------------------------------------ the stream */

static struct {
    volatile int   playing;          /* DMA running */
    int            installed;        /* our IRQ handler is in the IVT */
    unsigned       ch;               /* DMA channel in use */
    int            bits16, stereo;
    uint8_t       *raw;              /* malloc'd */
    uint8_t       *buf;              /* 2 x block, inside one DMA page */
    unsigned       block;
    volatile unsigned half;          /* the half the next IRQ ends */
    sb_fill_fn     fill;
    void          *user;
    armdos_vect_t  old;
    unsigned       vec;
    uint8_t        oldmask;
    /* sb_play_pcm */
    const uint8_t *src;
    unsigned long  left;
    int            tail;             /* silent blocks queued after the end */
    uint8_t        silence;
} S;

static void sb_eoi(void)
{
    if (sb_info.irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

static void sb_isr(struct armregs *f)
{
    unsigned p = sb_info.port, st = 1;
    if (sb_info.dsp_major >= 4) {                 /* SB16: which of its IRQs is it? */
        outb(p + 4, 0x82);
        st = inb(p + 5) & 3;
        if (!st) {                                /* not ours: chain (the old handler sends the EOI) */
            armdos_callold(S.old, f);
            return;
        }
        if (st & 2) inb(p + 0xF);
    }
    if (st & 1) inb(p + 0xE);
    sb_irq_count++;
    if (S.playing && S.fill) {
        S.fill(S.buf + S.half * S.block, S.block, S.user);
        S.half ^= 1;
    }
    sb_eoi();
}

static void sb_shutdown(void);

static void install_irq(void)
{
    static int atexit_done;
    unsigned irq = sb_info.irq, port = irq < 8 ? 0x21 : 0xA1, bit = 1u << (irq & 7);
    if (S.installed) return;
    S.vec = irq < 8 ? 0x08 + irq : 0x70 + irq - 8;
    S.old = armdos_getvect(S.vec);
    armdos_setvect(S.vec, sb_isr);            /* INT 21h AH=25h */
    armdos_disable();
    S.oldmask = inb(port) & bit;
    outb(port, inb(port) & ~bit);
    if (irq >= 8) outb(0x21, inb(0x21) & ~0x04);   /* the cascade */
    armdos_enable();
    S.installed = 1;
    if (!atexit_done) { atexit(sb_shutdown); atexit_done = 1; }
}

static void remove_irq(void)
{
    unsigned irq = sb_info.irq, port = irq < 8 ? 0x21 : 0xA1, bit = 1u << (irq & 7);
    if (!S.installed) return;
    armdos_disable();
    outb(port, (inb(port) & ~bit) | S.oldmask);
    armdos_enable();
    armdos_setvect(S.vec, S.old);
    S.installed = 0;
}

static void stop_stream(void)
{
    if (!sb_info.present) return;
    if (S.playing) {
        armdos_disable();
        S.playing = 0;
        armdos_enable();
        if (sb_info.dsp_major >= 4) sb_dsp_write(S.bits16 ? 0xD5 : 0xD0);   /* pause */
        else sb_dsp_write(0xD0);
        dma_mask(S.ch);
        sb_reset_dsp();                      /* leaves auto-init (and high-speed) mode */
    }
    remove_irq();
    free(S.raw);
    S.raw = S.buf = NULL;
}

void sb_stop(void)
{
    stop_stream();
    S.src = NULL; S.left = 0;
}

static void sb_shutdown(void) { sb_stop(); }

void sb_set_rate(unsigned rate)
{
    if (!sb_info.present) return;
    if (sb_info.dsp_major >= 4) {
        sb_dsp_write(0x41);
        sb_dsp_write(rate >> 8);
        sb_dsp_write(rate & 0xFF);
    } else {                                 /* time constant (per byte: stereo doubles it) */
        unsigned r = rate * (S.stereo ? 2 : 1);
        sb_dsp_write(0x40);
        sb_dsp_write(256 - 1000000 / (r ? r : 1));
    }
}

int sb_start(unsigned rate, unsigned format, unsigned block, sb_fill_fn fill, void *user)
{
    uint32_t a, boundary;
    unsigned total, samples;
    if (!sb_info.present || !fill || block < 16) return 0;
    stop_stream();
    S.bits16 = (format & SB_16BIT) && sb_info.dsp_major >= 4;
    S.stereo = (format & SB_STEREO) != 0;
    if (format & SB_16BIT && !S.bits16) return 0;        /* 16-bit needs an SB16 */
    S.ch = S.bits16 ? sb_info.dma16 : sb_info.dma8;
    block &= S.bits16 || S.stereo ? ~3u : ~1u;
    total = block * 2;
    if (total > (S.ch >= 4 ? 0x20000u : 0x10000u)) return 0;
    /* the buffer must not cross a 64 KB (8-bit) / 128 KB (16-bit) DMA page */
    S.raw = malloc(total * 2 + 4);
    if (!S.raw) return 0;
    boundary = S.ch >= 4 ? 0x20000 : 0x10000;
    a = ((uint32_t)S.raw + 3) & ~3u;
    if (a / boundary != (a + total - 1) / boundary) a = (a + total - 1) & ~(boundary - 1);
    S.buf = (uint8_t *)a;
    S.block = block; S.fill = fill; S.user = user; S.half = 0;
    fill(S.buf, block, user);
    fill(S.buf + block, block, user);
    install_irq();
    sb_speaker(1);
    dma_start(S.ch, a, total);
    S.playing = 1;
    if (sb_info.dsp_major >= 4) {
        unsigned mode = (S.stereo ? 0x20 : 0) | (format & SB_SIGNED ? 0x10 : 0);
        samples = S.bits16 ? block / 2 : block;
        sb_set_rate(rate);
        sb_dsp_write(S.bits16 ? 0xB6 : 0xC6);            /* auto-init output, FIFO on */
        sb_dsp_write(mode);
        sb_dsp_write((samples - 1) & 0xFF);
        sb_dsp_write((samples - 1) >> 8);
    } else {
        if (sb_info.dsp_major == 3) mixer_set(0x0E, S.stereo ? 0x02 : 0x00);
        sb_set_rate(rate);
        sb_dsp_write(0x48);
        sb_dsp_write((block - 1) & 0xFF);
        sb_dsp_write((block - 1) >> 8);
        sb_dsp_write(0x1C);                              /* 8-bit auto-init */
    }
    return 1;
}

int sb_busy(void) { return S.playing; }

/* sb_play_pcm: a stream whose fill copies the sound, then a little silence,
 * then asks the DSP to leave auto-init mode after the current block. */
static void pcm_fill(void *buf, unsigned bytes, void *user)
{
    unsigned n = S.left < bytes ? (unsigned)S.left : bytes;
    (void)user;
    if (n) { memcpy(buf, S.src, n); S.src += n; S.left -= n; }
    if (n < bytes) {
        memset((uint8_t *)buf + n, S.silence, bytes - n);
        if (S.tail++ == 2) {                              /* both halves now hold only silence */
            if (sb_info.dsp_major >= 4) sb_dsp_write(S.bits16 ? 0xD9 : 0xDA);
            else sb_dsp_write(0xDA);
            S.playing = 0;                                /* no more fills; the DSP stops after this block */
        }
    }
}

int sb_play_pcm(const void *data, unsigned long bytes, unsigned rate, unsigned format)
{
    unsigned block;
    if (!sb_info.present || !data || !bytes) return 0;
    sb_stop();
    S.src = data; S.left = bytes; S.tail = 0;
    S.silence = (format & (SB_16BIT | SB_SIGNED)) ? 0 : 0x80;
    block = rate / 10 * ((format & SB_16BIT ? 2 : 1) * (format & SB_STEREO ? 2 : 1));   /* 100 ms */
    if (block > 0x8000) block = 0x8000;
    if (block < 64) block = 64;
    return sb_start(rate, format, block, pcm_fill, NULL);
}
