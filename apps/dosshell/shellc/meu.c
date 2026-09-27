/*
 * meu.c - group files (SHELL.MEU, DOSUTIL.MEU, ...) in the DOS 4.00 Shell's
 * own binary format, so the files the real Shell wrote can be read and ours
 * could be read by it. Layout (decoded from the shipped SHELL.MEU and
 * DOSUTIL.MEU; SHELL.MEU is also in the MIT source release, INC/SHELL.MEU):
 *
 *   0000  u16 1234h, 0014h, 00FEh, 0, 00A4h, 0010h, 00F7h, 0, 0318h, 0010h
 *   00A4  help index: u16 count, u16 1, then 16 x {u16 id, 0, u16 offset, u16 480}
 *   0128  8 zero bytes, then one space per item
 *   0318  item index: u16 count, u16 count, then 16 x {u16 id, 0, u16 offset, u16 556}
 *   then the records, a 480-byte help record and a 556-byte item record per
 *   item: help #1 at 0138h, the item index, item #1 at 039Ch, help #2 at 05C8h,
 *   item #2 at 07A8h, ... (1036 bytes apart)
 *
 *   help record:  C6h, text ('&' = new line), blank padded
 *   item record:  +0 flag byte, +1 title (40), +41 1 = program / 0 = group,
 *                 +42 password (8), +50 u16 help record id,
 *                 program: +52 u16 50, +54 u16 10, +56 startup command (500,
 *                 lines separated by BAh = the F4 marker; FCh/FDh/FEh = the
 *                 built-in Command Prompt / File System / Change Colors);
 *                 group:   +52 the group's file name (blank padded)
 */
#include "shell.h"

#define HLEN 480
#define ILEN 556
#define FILESIZE_MAX (0x138 + MEU_MAX * (HLEN + ILEN) + 0x84)

static uint8_t mbuf[FILESIZE_MAX];

static unsigned rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void wr16(uint8_t *p, unsigned v) { p[0] = v; p[1] = v >> 8; }

static void trimcopy(char *dst, const uint8_t *src, int n)
{
    memcpy(dst, src, n);
    dst[n] = 0;
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == 0 || (uint8_t)dst[n - 1] == 0xC6)) dst[--n] = 0;
}

int meu_load(const char *file, struct meugroup *g)
{
    memset(g, 0, sizeof *g);
    int len = file_read(file, mbuf, sizeof mbuf);
    if (len < 0x39C || rd16(mbuf) != 0x1234) return -1;
    unsigned hidx = rd16(mbuf + 8), iidx = rd16(mbuf + 16);
    if (hidx + 4 > (unsigned)len || iidx + 4 > (unsigned)len) return -1;
    int n = rd16(mbuf + iidx);
    if (n > MEU_MAX) n = MEU_MAX;
    int nh = rd16(mbuf + hidx);
    for (int i = 0; i < n; i++) {
        const uint8_t *e = mbuf + iidx + 4 + i * 8;
        unsigned off = rd16(e + 4);
        if (off + ILEN > (unsigned)len) return -1;
        const uint8_t *r = mbuf + off;
        struct meuitem *it = &g->it[g->n++];
        trimcopy(it->title, r + 1, MEU_TITLE);
        it->isprog = r[41];
        trimcopy(it->password, r + 42, 8);
        unsigned hid = rd16(r + 50);
        if (it->isprog) trimcopy(it->cmd, r + 56, MEU_CMD);
        else {
            trimcopy(it->cmd, r + 52, 12);
            char *sp = strchr(it->cmd, ' ');
            if (sp) *sp = 0;
        }
        for (int k = 0; k < nh && k < MEU_MAX; k++) {
            const uint8_t *h = mbuf + hidx + 4 + k * 8;
            if (rd16(h) == hid) {
                unsigned ho = rd16(h + 4);
                if (ho + HLEN <= (unsigned)len) trimcopy(it->help, mbuf + ho + 1, MEU_HELP);
                break;
            }
        }
    }
    return 0;
}

static unsigned help_off(int i) { return i == 0 ? 0x138 : 0x5C8 + (i - 1) * (HLEN + ILEN); }
static unsigned item_off(int i) { return 0x39C + i * (HLEN + ILEN); }

int meu_save(const char *file, const struct meugroup *g)
{
    int n = g->n;
    int size = n ? item_off(n - 1) + ILEN : 0x39C;
    memset(mbuf, 0, size);
    static const uint16_t hdr[10] = { 0x1234, 0x14, 0xFE, 0, 0xA4, 0x10, 0xF7, 0, 0x318, 0x10 };
    for (int i = 0; i < 10; i++) wr16(mbuf + i * 2, hdr[i]);
    wr16(mbuf + 0xA4, n);
    wr16(mbuf + 0xA6, 1);
    wr16(mbuf + 0x318, n);
    wr16(mbuf + 0x31A, n);
    memset(mbuf + 0x128, ' ', n);
    for (int i = 0; i < n; i++) {
        const struct meuitem *it = &g->it[i];
        uint8_t *h = mbuf + help_off(i), *r = mbuf + item_off(i);
        wr16(mbuf + 0xA8 + i * 8, i + 1);
        wr16(mbuf + 0xA8 + i * 8 + 4, help_off(i));
        wr16(mbuf + 0xA8 + i * 8 + 6, HLEN);
        wr16(mbuf + 0x31C + i * 8, i + 1);
        wr16(mbuf + 0x31C + i * 8 + 4, item_off(i));
        wr16(mbuf + 0x31C + i * 8 + 6, ILEN);
        memset(h, ' ', HLEN);
        h[0] = 0xC6;
        h[HLEN - 1] = 0xC6;
        memcpy(h + 1, it->help, strlen(it->help));
        memset(r, ' ', ILEN);
        r[0] = 0;
        memcpy(r + 1, it->title, strlen(it->title));
        r[41] = it->isprog;
        memcpy(r + 42, it->password, strlen(it->password));
        wr16(r + 50, i + 1);
        if (it->isprog) {
            wr16(r + 52, 50);
            wr16(r + 54, 10);
            memcpy(r + 56, it->cmd, strlen(it->cmd));
        } else
            memcpy(r + 52, it->cmd, strlen(it->cmd));
    }
    return file_write(file, mbuf, size);
}
