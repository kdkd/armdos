/* link.c - the modem and the line protocol (emu/online/protocol.mjs): dialling with AT
 * commands, frames in and out, dispatching what the service sends. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "online.h"

struct net net;

/* ------------------------------------------------------------ the modem */
int modem_open(void)
{
    return com_open(cfg.port, cfg.baud);
}

static char mline[80];
static int mlen;

/* collect a line of modem output (result codes); returns 1 when a line is complete */
static int modem_line(void)
{
    int c;
    while ((c = com_getc()) >= 0) {
        if (c == '\r' || c == '\n') {
            if (mlen) { mline[mlen] = 0; mlen = 0; return 1; }
            continue;
        }
        if (mlen < (int)sizeof mline - 1) mline[mlen++] = (char)c;
    }
    return 0;
}

static int is_result(const char *s)
{
    static const char *const r[] = { "OK", "ERROR", "CONNECT", "BUSY", "NO CARRIER", "NO ANSWER", "NO DIALTONE", 0 };
    for (int i = 0; r[i]; i++) if (!strncmp(s, r[i], strlen(r[i]))) return 1;
    return 0;
}

/* send a command, wait for a result code; returns 1 on OK/CONNECT */
static int modem_cmd(const char *cmd, uint32_t ms, int (*abort_check)(void))
{
    com_rxpurge();
    mlen = 0;
    com_puts(cmd); com_puts("\r");
    uint32_t t = TICKS(), lim = ms2ticks(ms);
    net.result[0] = 0;
    while (TICKS() - t < lim) {
        if (modem_line()) {
            if (is_result(mline)) {
                strncpy(net.result, mline, sizeof net.result - 1);
                net.result[sizeof net.result - 1] = 0;
                return !strcmp(mline, "OK") || !strncmp(mline, "CONNECT", 7);
            }
            continue;
        }
        if (abort_check && abort_check()) {
            com_puts("\r");                    /* any character stops the dialling */
            delay_ms(300);
            strcpy(net.result, "CANCELLED");
            return 0;
        }
        idle();
    }
    if (!net.result[0]) strcpy(net.result, "NO RESPONSE");
    return 0;
}

static void (*dial_progress)(int, const char *);
static int dial_abort(void)
{
    /* the progress callback animates and returns nothing; Esc cancels */
    if (dial_progress) dial_progress(0, NULL);
    if (key_ready()) {
        int k = key_get();
        if (k == K_ESC) return 1;
    }
    return 0;
}

int modem_dial(void (*progress)(int step, const char *text))
{
    char buf[64];
    dial_progress = progress;
    net_reset();
    com_dtr(1);
    if (!modem_cmd("ATZ", 3000, NULL)) {
        if (!modem_cmd("ATZ", 3000, NULL)) return -1;
    }
    delay_ms(200);
    if (cfg.init[0] && !modem_cmd(cfg.init, 3000, NULL)) return -1;
    snprintf(buf, sizeof buf, "ATDT%s", cfg.number);
    progress(1, buf);
    /* the 56K handshake alone is ~18 s; ringing and answering on top */
    if (!modem_cmd(buf, 75000, dial_abort)) {
        if (!strcmp(net.result, "CANCELLED")) modem_cmd("ATH0", 2000, NULL);
        return -1;
    }
    net.rate = atol(net.result + 7);
    if (!net.rate) net.rate = 2400;
    net.carrier = 1;
    net.t0 = TICKS();
    progress(2, net.result);
    return 0;
}

void modem_hangup(void)
{
    com_dtr(0);                                /* &D2: the modem hangs up */
    delay_ms(600);
    com_dtr(1);
    if (com_carrier()) {
        delay_ms(1100); com_puts("+++"); delay_ms(1100);
        modem_cmd("ATH0", 2000, NULL);
    }
    net.carrier = 0; net.online = 0;
}

uint32_t online_seconds(void)
{
    if (!net.carrier) return 0;
    return (uint32_t)((TICKS() - net.t0) * 10u / 182u);
}

/* ------------------------------------------------------------ frames */
static int fst, ftype, flen, fpos;
static unsigned fsum;
static uint8_t fbuf[1024];
static char junk[17];

void net_reset(void)
{
    if (net.incoming && !net.incoming->complete) net.incoming->complete = 1;
    memset(&net, 0, sizeof net);
    fst = 0;
}

void net_send(char type, const void *payload, int n)
{
    const uint8_t *p = payload;
    uint8_t h[4] = { 0x02, (uint8_t)type, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8) };
    unsigned sum = h[1] + h[2] + h[3];
    for (int i = 0; i < n; i++) sum += p[i];
    com_write(h, 4);
    if (n) com_write(p, n);
    uint8_t s = (uint8_t)sum;
    com_write(&s, 1);
}

static void remember(char type, const void *p, int n)
{
    if (n > (int)sizeof net.lastreq) n = sizeof net.lastreq;
    net.lastreq_type = type; net.lastreq_n = n;
    if (n) memcpy(net.lastreq, p, n);
}

void net_request(char type, const char *s)
{
    int n = s ? (int)strlen(s) : 0;
    net.waiting = 1; net.status[0] = 0;
    remember(type, s, n);
    net_send(type, s, n);
}

void net_resend(struct doc *d)
{
    net.waiting = 1; net.status[0] = 0;
    remember(d->rtype, d->req, d->reqn);
    net_send(d->rtype, d->req, d->reqn);
}

void net_follow(uint16_t docid, uint16_t link)
{
    uint8_t p[4] = { (uint8_t)docid, (uint8_t)(docid >> 8), (uint8_t)link, (uint8_t)(link >> 8) };
    net.waiting = 1; net.status[0] = 0;
    remember('F', p, 4);
    net_send('F', p, 4);
}

void net_cancel(void)
{
    net_send('X', NULL, 0);
    net.discard = 1;
    net.waiting = 0;
    if (net.incoming) { net.incoming->complete = 1; net.incoming->partial = 1; net.incoming = NULL; }
    if (viewer_pending) viewer_end();
}

static void copystr(char *dst, int max, const uint8_t *p, int n)
{
    if (n > max - 1) n = max - 1;
    memcpy(dst, p, n); dst[n] = 0;
}

static void dispatch(int type, const uint8_t *p, int n)
{
    if (net.discard) { if (type == 'Z') net.discard = 0; return; }
    switch (type) {
    case 'W': {
        int k = 0; while (k < n && p[k]) k++;
        copystr(net.member, sizeof net.member, p, k);
        if (k < n) copystr(net.welcome_text, sizeof net.welcome_text, p + k + 1, n - k - 1);
        net.welcome = 1;
        break;
    }
    case 'D': {
        if (n < 3) break;
        const char *title = (const char *)p + 3;
        int tl = (int)strnlen(title, n - 3);
        const char *chn = title + tl + 1;
        char t[72], c[24];
        copystr(t, sizeof t, (const uint8_t *)title, tl);
        copystr(c, sizeof c, (const uint8_t *)chn, tl + 4 < n ? (int)strnlen(chn, n - tl - 4) : 0);
        if (net.incoming) net.incoming->complete = 1;
        net.incoming = doc_new((uint16_t)(p[0] | (p[1] << 8)), (char)p[2], t, c);
        net.newdoc = net.incoming;
        if (net.incoming) {
            net.incoming->rtype = net.lastreq_type;
            memcpy(net.incoming->req, net.lastreq, net.lastreq_n);
            net.incoming->reqn = net.lastreq_n;
        }
        net.waiting = 0;
        break;
    }
    case 'T':
        if (net.incoming) doc_append(net.incoming, p, n);
        break;
    case 'E':
        if (net.incoming) { net.incoming->complete = 1; net.incoming = NULL; }
        break;
    case 'G':
        net.waiting = 0;
        viewer_begin(p, n);
        break;
    case 'F':
        viewer_end();
        break;
    case 'R':
        net.waiting = 0;
        net.msg_class = n ? (char)p[0] : 'E';
        copystr(net.msg, sizeof net.msg, p + 1, n - 1);
        net.msg_pending = 1;
        break;
    case 'S':
        copystr(net.status, sizeof net.status, p, n);
        break;
    case 'Q':
        copystr(net.goodbye_text, sizeof net.goodbye_text, p, n);
        net.goodbye = 1;
        break;
    }
}

void net_poll(void)
{
    int c;
    int guard = 4096;
    while (guard-- > 0 && (c = com_getc()) >= 0) {
        net.rxbytes++;
        switch (fst) {
        case 0:
            if (c == 0x02) { fst = 1; break; }
            /* outside frames: modem result codes and the host's banner */
            memmove(junk, junk + 1, sizeof junk - 2); junk[sizeof junk - 2] = (char)c;
            if (!memcmp(junk + sizeof junk - 11, "NO CARRIER", 10)) net.nocarrier = 1;
            break;
        case 1: ftype = c; fsum = c; fst = 2; break;
        case 2: flen = c; fsum += c; fst = 3; break;
        case 3:
            flen |= c << 8; fsum += c; fpos = 0;
            fst = flen > (int)sizeof fbuf ? 0 : flen ? 4 : 5;
            break;
        case 4:
            fsum += c;
            /* picture data goes to the viewer as it arrives, so the picture grows smoothly */
            if (ftype == 'B') { if (!net.discard) viewer_byte((uint8_t)c); fpos++; }
            else fbuf[fpos++] = (uint8_t)c;
            if (fpos == flen) fst = 5;
            break;
        case 5:
            fst = 0;
            if ((fsum & 0xFF) == (unsigned)c && ftype != 'B') dispatch(ftype, fbuf, flen);
            break;
        }
    }
    int dcd = com_carrier();
    if (net.carrier && !dcd) {
        net.carrier = 0;
        if (!net.goodbye) net.lost = 1;
        net.online = 0;
        strcpy(net.result, "NO CARRIER");
    }
}

int net_signon(const char *name, const char *pass)
{
    char p[80];
    int n = snprintf(p, sizeof p, "%s", name);
    p[n++] = 0;
    n += snprintf(p + n, sizeof p - n, "%s", pass) + 1;
    n += snprintf(p + n, sizeof p - n, "ONLINE %s", ONLINE_VERSION);
    uint32_t t = TICKS(), sent = 0;
    int tries = 0;
    while (TICKS() - t < ms2ticks(20000)) {
        if (tries == 0 || (TICKS() - sent > ms2ticks(4000) && tries < 4)) { net_send('H', p, n); sent = TICKS(); tries++; }
        net_poll();
        if (net.welcome) { net.online = 1; return 1; }
        if (!net.carrier) return 0;
        if (key_ready() && key_get() == K_ESC) return 0;
        idle();
    }
    return 0;
}
