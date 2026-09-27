/*
 * init.c - ANSI.SYS INIT (ANSIINIT.ASM CON$INIT + PARSER.ASM PARSE_PARM).
 * Discarded after INIT: it is linked after ansi_res_end.
 *
 *   DEVICE=[d:][path]ANSI.SYS [/X] [/L] [/K]
 *
 *   /X  extended keys (the grey keys of the enhanced keyboard, F11/F12) are
 *       distinct keys for reassignment; also ESC[1q / ESC[0q at run time
 *   /L  keep the number of lines set by MODE CON LINES= across mode sets
 *   /K  treat the enhanced keyboard as a conventional one (INT 16h 00h/01h)
 */
#include "ansi.h"

static void dos_write(int h, const char *s, unsigned n)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)s;
    int21(&r);
}

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }

static int delim(int c) { return c == ' ' || c == '\t' || c == ',' || c == ';' || c == '='; }

/* parse the switches after the file name; returns the offending text or 0 */
static const char *parse(const char *p)
{
    while (delim(*p)) p++;
    while (*p && *p != '\r' && *p != '\n' && !delim(*p) && *p != '/') p++;   /* the path */
    for (;;) {
        const char *old = p;            /* the error text starts here (blanks included) */
        while (delim(*p)) p++;
        if (!*p || *p == '\r' || *p == '\n') return 0;
        if (p[0] == '/' && (p[1] == 'X' || p[1] == 'L' || p[1] == 'K' ||
                            p[1] == 'x' || p[1] == 'l' || p[1] == 'k') &&
            (!p[2] || p[2] == '\r' || p[2] == '\n' || p[2] == '/' || delim(p[2]))) {
            int c = p[1] & ~0x20;
            if (c == 'X') ansi_switch_x = 1;
            else if (c == 'L') ansi_switch_l = 1;
            else ansi_switch_k = 1;
            p += 2;
            continue;
        }
        ansi_switch_x = ansi_switch_l = ansi_switch_k = 0;
        return old;
    }
}

void ansi_init(struct req_init *q)
{
    const char *bad = parse((const char *)q->arg);
    if (bad) {
        /* "Invalid parameter - %1": %1 = the rest of the line up to and
           including its CR, as PARSER.ASM substitutes it */
        static const char msg[] = "Invalid parameter - ";
        unsigned n = 0;
        while (bad[n] && bad[n] != '\r') n++;
        if (bad[n] == '\r') n++;
        dos_write(2, msg, slen(msg));
        dos_write(2, bad, n);
        dos_write(2, "\r\n", 2);
        q->units = 0;
        q->brk = (uint32_t)&ansi_header;
        q->cfgerr = 0xFFFF;
        q->h.status = RS_DONE | RS_ERROR | DE_BADCMD;
        return;
    }
    if ((BDA8(0x96) & 0x10) && !ansi_switch_k) ansi_ext16 = 1;

    /* the CON driver we replace: the one in the List of Lists (+0Ch) */
    struct armregs r = { 0 };
    r.r0 = 0x5200;
    int21(&r);
    const uint8_t *lol = (const uint8_t *)r.r1;
    ansi_oldcon = (struct devhdr *)(lol[0x0C] | (lol[0x0D] << 8) | (lol[0x0E] << 16) |
                                    ((uint32_t)lol[0x0F] << 24));

    ansi_old10 = (armdos_vect_t)IVT_SLOT(0x10);
    ansi_old2f = (armdos_vect_t)IVT_SLOT(0x2F);
    IVT_SLOT(0x10) = (uint32_t)ansi_int10;
    IVT_SLOT(0x2F) = (uint32_t)ansi_int2f;
    IVT_SLOT(0x1B) = (uint32_t)ansi_int1b;
    IVT_SLOT(0x29) = (uint32_t)ansi_int29;

    q->units = 0;
    q->brk = (uint32_t)ansi_res_end;
    q->h.status = RS_DONE;
}

/* for the compiler's structure initialisation (INIT code only) */
void *memset(void *d, int c, unsigned n)
{
    uint8_t *p = d;
    while (n--) *p++ = c;
    return d;
}
