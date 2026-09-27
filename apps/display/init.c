/*
 * init.c - DISPLAY.SYS INIT (DOS 4.00 DEV/DISPLAY/INIT.ASM, PARSER.ASM).
 * Discarded after INIT: it is linked after display_res_end.
 *
 *   DEVICE=[d:][path]DISPLAY.SYS CON[:]=(type[,[hwcp][,n]])
 *   DEVICE=[d:][path]DISPLAY.SYS CON[:]=(type[,[hwcp][,(n,m)]])
 *
 *   type  EGA (the ARM-PC's VGA; DOS 4.00 also knew LCD, MONO and CGA)
 *   hwcp  the code page of the display's ROM font (437)
 *   n     how many code pages MODE CON CP PREPARE can prepare, 0-12
 *   m     fonts per code page (accepted, not used)
 *
 * Messages as DISPLAY 4.00 (verified under DOSBox-X): no parameters or bad
 * syntax -> "Invalid syntax on DISPLAY.SYS code page driver", a type this
 * display cannot switch -> "CON code page driver cannot be initialized",
 * n too big (or no memory for it) -> "Insufficient memory"; each followed by
 * a beep; the driver then does not install. MONO and CGA install nothing,
 * silently, as DISPLAY 4.00's "not a code page switching display" path.
 */
#include "display.h"

static void dos_write(const char *s)
{
    unsigned n = 0;
    while (s[n]) n++;
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = 1; r.r2 = n; r.r3 = (uint32_t)s;
    int21(&r);
}

static const char *skipb(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static const char *num(const char *p, unsigned *v)
{
    p = skipb(p);
    if (!is_digit(*p)) return 0;
    unsigned n = 0;
    while (is_digit(*p)) { n = n * 10 + (*p++ - '0'); if (n > 65535) n = 65535; }
    *v = n;
    return skipb(p);
}

static void fail(struct req_init *q, const char *msg)
{
    if (msg) { dos_write(msg); dos_write("\r\n\a"); }
    q->units = 0;
    q->brk = (uint32_t)&display_header;
    q->cfgerr = 0;
    q->h.status = RS_DONE | RS_ERROR | DE_GENFAIL;
    display_header.next = (struct devhdr *)0xFFFFFFFFu;
}

void disp_init(struct req_init *q)
{
    static const char SYNTAX[] = "Invalid syntax on DISPLAY.SYS code page driver";
    const char *p = (const char *)q->arg;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;   /* the path */
    p = skipb(p);
    if (p[0] != 'C' || p[1] != 'O' || p[2] != 'N') { fail(q, SYNTAX); return; }
    p = skipb(p + 3);
    if (*p == ':') p = skipb(p + 1);
    if (*p != '=') { fail(q, SYNTAX); return; }
    p = skipb(p + 1);
    if (*p != '(') { fail(q, SYNTAX); return; }
    p = skipb(p + 1);
    char type[9];
    int tl = 0;
    while (*p && *p != ',' && *p != ')' && *p != '\r' && tl < 8) type[tl++] = *p++;
    while (tl && type[tl - 1] == ' ') tl--;
    type[tl] = 0;
    unsigned hw = NOCP, n = 0, m = 0;
    int have_n = 0;
    p = skipb(p);
    if (*p == ',') {
        p = skipb(p + 1);
        if (is_digit(*p)) { if (!(p = num(p, &hw))) { fail(q, SYNTAX); return; } }
        if (*p == ',') {
            p = skipb(p + 1);
            if (*p == '(') {
                if (!(p = num(p + 1, &n))) { fail(q, SYNTAX); return; }
                if (*p == ',') { if (!(p = num(p + 1, &m))) { fail(q, SYNTAX); return; } }
                if (*p != ')') { fail(q, SYNTAX); return; }
                p = skipb(p + 1);
            } else if (!(p = num(p, &n))) { fail(q, SYNTAX); return; }
            have_n = 1;
        }
    }
    if (*p != ')') { fail(q, SYNTAX); return; }
    p = skipb(p + 1);
    if (*p && *p != '\r' && *p != '\n') { fail(q, SYNTAX); return; }
    if (!tl) { fail(q, SYNTAX); return; }

    int ega = type[0] == 'E' && type[1] == 'G' && type[2] == 'A' && !type[3];
    if (!ega) {
        int quiet = (type[0] == 'M' && type[1] == 'O' && type[2] == 'N' && type[3] == 'O' && !type[4]) ||
                    (type[0] == 'C' && type[1] == 'G' && type[2] == 'A' && !type[3]);
        fail(q, quiet ? 0 : "CON code page driver cannot be initialized");
        return;
    }
    if (BDA8(0x49) == 7 || (BDA16(0x10) & 0x30) == 0x30) {      /* an MDA/Hercules: its font is a ROM */
        fail(q, "CON code page driver cannot be initialized");
        return;
    }
    if (!have_n) n = 0;
    if (n > MAX_SLOTS) { fail(q, "Insufficient memory"); return; }
    uint32_t start = ((uint32_t)display_res_end + 15) & ~15u;
    uint32_t end = start + n * sizeof(struct slot);
    if (end > q->brk) { fail(q, "Insufficient memory"); return; }

    disp_hwcp = hw;
    disp_nslots = n;
    disp_nfonts = have_n ? (m ? m : 1) : 0;
    disp_slots = (struct slot *)start;
    for (unsigned i = 0; i < n; i++) { disp_slots[i].cp = NOCP; disp_slots[i].ok = 0; disp_slots[i].got = 0; }
    disp_active = NOCP;

    /* the CON driver we are loaded over: the one in the List of Lists (+0Ch) */
    struct armregs r = { 0 };
    r.r0 = 0x5200;
    int21(&r);
    const uint8_t *lol = (const uint8_t *)r.r1;
    disp_oldcon = (struct devhdr *)(lol[0x0C] | (lol[0x0D] << 8) | (lol[0x0E] << 16) | ((uint32_t)lol[0x0F] << 24));

    disp_old10 = (armdos_vect_t)IVT_SLOT(0x10);
    disp_old2f = (armdos_vect_t)IVT_SLOT(0x2F);
    IVT_SLOT(0x10) = (uint32_t)disp_int10;
    IVT_SLOT(0x2F) = (uint32_t)disp_int2f;
    disp_publish();

    q->units = 0;
    q->brk = end;
    q->h.status = RS_DONE;
}
