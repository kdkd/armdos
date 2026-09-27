/*
 * mres.c - MOUSE.COM, the resident part: the INT 33h services of the
 * Microsoft mouse driver, fed by the BIOS PS/2 pointing-device services
 * (INT 15h AX=C207h: the BIOS calls mouse_event for every packet).
 *
 * Everything here - code, variables, tables - is placed before
 * mouse_res_end (see mouse.h); nothing may call into the C library.
 *
 * Coordinates are the Microsoft driver's "virtual screen": 640 x 200 in
 * every mode (640 x rows*8 in text modes), a text cell is 8 x 8 (16 x 8 in
 * 40-column modes), a pixel of the 320-wide modes is 2 units wide. In text
 * modes the cursor is the software cursor (screen mask AND, cursor mask
 * XOR on the character/attribute word; default FFFFh/7700h = inverse
 * colours); in modes 4, 5, 6 and 13h a 16x16 graphics cursor (the default
 * is the arrow). What the cursor covers is saved and put back only if the
 * program has not written over the cursor meanwhile.
 */
#include "mouse.h"

armdos_vect_t mouse_old10 RES, mouse_old33 RES;
const char mouse_sig[8] RESRO = { 'A', 'R', 'M', 'M', 'O', 'U', 'S', 'E' };

/* the default graphics cursor: the arrow (screen mask, cursor mask) */
static const uint16_t arrow[32] RESRO = {
    0x3FFF, 0x1FFF, 0x0FFF, 0x07FF, 0x03FF, 0x01FF, 0x00FF, 0x007F,
    0x003F, 0x001F, 0x01FF, 0x10FF, 0x30FF, 0xF87F, 0xF87F, 0xFC3F,
    0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7F80, 0x7C00, 0x6C00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000,
};

struct mstate {
    int16_t x, y;                       /* virtual screen position */
    int16_t xmin, xmax, ymin, ymax;
    int16_t mx, my;                     /* mickeys since AX=000Bh */
    int16_t lastmx, lastmy;             /* mickey counts passed to the handler */
    int32_t remx, remy;                 /* sub-pixel motion */
    uint16_t rx, ry;                    /* mickeys per 8 pixels */
    uint16_t sens_h, sens_v, sens_d, dbl;
    uint8_t buttons;
    int8_t shown;                       /* 0 = visible, < 0 hidden */
    uint8_t disabled, busy;
    uint16_t press[3], rel[3];
    int16_t pressx[3], pressy[3], relx[3], rely[3];
    uint16_t mask;                      /* event handler */
    uint32_t handler;
    uint16_t textsoft, smask, cmask;
    int16_t hotx, hoty;
    uint16_t gmask[32];
    uint16_t page;
    uint16_t psp;
};
static struct mstate M RES;

/* what is drawn: text cell or 16x16 pixels */
static uint8_t drawn RES, dmode RES;
static volatile uint16_t *daddr RES;
static uint16_t dsave RES, dval RES;
static int16_t gx RES, gy RES;
static uint8_t gsave[256] RES, gval[256] RES;

#define BDA8(o)  (*(volatile uint8_t  *)(0x400 + (o)))
#define BDA16(o) (*(volatile uint16_t *)(0x400 + (o)))

/* ------------------------------------------------------------ helpers */

RESFN static int32_t sdiv(int32_t a, int32_t b)    /* b > 0, truncating */
{
    uint32_t n = a < 0 ? -a : a, q = 0, r = 0;
    for (int i = 31; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= (uint32_t)b) { r -= b; q |= 1u << i; }
    }
    return a < 0 ? -(int32_t)q : (int32_t)q;
}

RESFN static int vmode(void) { return BDA8(0x49) & 0x7F; }
RESFN static int is_text(int m) { return m <= 3 || m == 7; }
RESFN static int text_rows(void) { return BDA8(0x84) + 1; }
RESFN static int maxy(void) { return is_text(vmode()) ? text_rows() * 8 - 1 : 199; }

/* the position as reported: snapped to the character cell / pixel */
RESFN static int rep_x(void)
{
    int m = vmode();
    if (is_text(m)) return BDA16(0x4A) == 40 ? M.x & ~15 : M.x & ~7;
    if (m == 4 || m == 5 || m == 0x13) return M.x & ~1;
    return M.x;
}
RESFN static int rep_y(void) { return is_text(vmode()) ? M.y & ~7 : M.y; }

RESFN static void clamp(void)
{
    if (M.x < M.xmin) M.x = M.xmin;
    if (M.x > M.xmax) M.x = M.xmax;
    if (M.y < M.ymin) M.y = M.ymin;
    if (M.y > M.ymax) M.y = M.ymax;
}

/* ------------------------------------------------------------ pixels */

RESFN static volatile uint8_t *cga_byte(int x, int y, int shift)
{
    return (volatile uint8_t *)0xB8000 + (y & 1) * 0x2000 + (y >> 1) * 80 + (x >> shift);
}

RESFN static int getpix(int m, int x, int y)
{
    if (m == 0x13) return ((volatile uint8_t *)0xA0000)[y * 320 + x];
    if (m == 6) return (*cga_byte(x, y, 3) >> (7 - (x & 7))) & 1;
    return (*cga_byte(x, y, 2) >> ((3 - (x & 3)) * 2)) & 3;
}

RESFN static void putpix(int m, int x, int y, int v)
{
    if (m == 0x13) { ((volatile uint8_t *)0xA0000)[y * 320 + x] = v; return; }
    if (m == 6) {
        volatile uint8_t *p = cga_byte(x, y, 3);
        int sh = 7 - (x & 7);
        *p = (*p & ~(1 << sh)) | ((v & 1) << sh);
        return;
    }
    volatile uint8_t *p = cga_byte(x, y, 2);
    int sh = (3 - (x & 3)) * 2;
    *p = (*p & ~(3 << sh)) | ((v & 3) << sh);
}

/* ------------------------------------------------------------ drawing */

RESFN static void erase(void)
{
    if (!drawn) return;
    drawn = 0;
    int m = vmode();
    if (m != dmode) return;                     /* the mode changed: nothing to restore */
    if (is_text(m)) {
        if (*daddr == dval) *daddr = dsave;
        return;
    }
    int w = m == 6 ? 640 : 320;
    for (int r = 0; r < 16; r++) {
        int y = gy + r;
        if (y < 0 || y >= 200) continue;
        for (int c = 0; c < 16; c++) {
            int x = gx + c;
            if (x < 0 || x >= w) continue;
            if (getpix(m, x, y) == gval[r * 16 + c]) putpix(m, x, y, gsave[r * 16 + c]);
        }
    }
}

RESFN static void draw(void)
{
    if (drawn || M.shown < 0 || M.disabled || M.busy) return;
    int m = vmode();
    if (is_text(m)) {
        int cols = BDA16(0x4A);
        int col = cols == 40 ? M.x >> 4 : M.x >> 3, row = M.y >> 3;
        if (row >= text_rows() || col >= cols) return;
        daddr = (volatile uint16_t *)((m == 7 ? 0xB0000 : 0xB8000) + BDA16(0x4E) + (row * cols + col) * 2);
        dsave = *daddr;
        dval = (dsave & M.smask) ^ M.cmask;
        *daddr = dval;
    } else if (m == 4 || m == 5 || m == 6 || m == 0x13) {
        int w = m == 6 ? 640 : 320, white = m == 0x13 ? 15 : m == 6 ? 1 : 3;
        int px = m == 6 ? M.x : M.x >> 1;
        gx = px - M.hotx;
        gy = M.y - M.hoty;
        for (int r = 0; r < 16; r++) {
            int y = gy + r;
            uint16_t sm = M.gmask[r], cm = M.gmask[16 + r];
            for (int c = 0; c < 16; c++) {
                int x = gx + c;
                if (y < 0 || y >= 200 || x < 0 || x >= w) continue;
                int p = getpix(m, x, y);
                int v = (((sm >> (15 - c)) & 1) ? p : 0) ^ (((cm >> (15 - c)) & 1) ? white : 0);
                gsave[r * 16 + c] = p;
                gval[r * 16 + c] = v;
                putpix(m, x, y, v);
            }
        }
    } else return;
    dmode = m;
    drawn = 1;
}

RESFN static void refresh(void)
{
    erase();
    draw();
}

/* ------------------------------------------------------- the services */

RESFN static void set_defaults(void)
{
    erase();
    M.shown = -1;
    int text = is_text(vmode());
    M.xmin = 0; M.xmax = 639;
    M.ymin = 0; M.ymax = maxy();
    M.x = 320;
    M.y = text ? ((M.ymax + 1) / 2) & ~7 : 100;
    M.mx = M.my = 0;
    M.remx = M.remy = 0;
    M.rx = 8; M.ry = 16;
    M.mask = 0; M.handler = 0;
    M.textsoft = 0; M.smask = 0xFFFF; M.cmask = 0x7700;
    M.hotx = 1; M.hoty = 1;
    for (int i = 0; i < 32; i++) M.gmask[i] = arrow[i];
    for (int i = 0; i < 3; i++) M.press[i] = M.rel[i] = 0;
    M.page = 0;
}

RESFN void mouse_reset(void)
{
    set_defaults();
    M.sens_h = M.sens_v = M.sens_d = 50;
    M.dbl = 64;
    M.disabled = 0;
}

#define set16(r, v) (*(r) = (uint16_t)(v))

RESFN void mouse_int33(struct armregs *f)
{
    uint32_t irq = m_irqoff();
    int ax = f->r0 & 0xFFFF;
    int bx = (int16_t)f->r1, cx = (int16_t)f->r2, dx = (int16_t)f->r3;
    switch (ax) {
    case 0x00:                          /* reset and status */
    case 0x21:                          /* software reset */
        set_defaults();
        set16(&f->r0, 0xFFFF);
        set16(&f->r1, 2);
        break;
    case 0x01:                          /* show cursor */
        if (M.shown < 0) M.shown++;
        draw();
        break;
    case 0x02:                          /* hide cursor */
    case 0x10:                          /* conditional off: treated as hide */
        M.shown--;
        erase();
        break;
    case 0x03:                          /* position and button status */
        set16(&f->r1, M.buttons);
        set16(&f->r2, rep_x());
        set16(&f->r3, rep_y());
        break;
    case 0x04:                          /* set position */
        M.x = cx; M.y = dx;
        clamp();
        refresh();
        break;
    case 0x05:                          /* button press data */
    case 0x06: {                        /* button release data */
        int b = bx & 0xFFFF;
        set16(&f->r0, M.buttons);
        if (b > 2) { set16(&f->r1, 0); break; }
        if (ax == 5) {
            set16(&f->r1, M.press[b]); set16(&f->r2, M.pressx[b]); set16(&f->r3, M.pressy[b]);
            M.press[b] = 0;
        } else {
            set16(&f->r1, M.rel[b]); set16(&f->r2, M.relx[b]); set16(&f->r3, M.rely[b]);
            M.rel[b] = 0;
        }
        break;
    }
    case 0x07:                          /* horizontal range */
        M.xmin = cx < dx ? cx : dx;
        M.xmax = cx < dx ? dx : cx;
        clamp();
        refresh();
        break;
    case 0x08:                          /* vertical range */
        M.ymin = cx < dx ? cx : dx;
        M.ymax = cx < dx ? dx : cx;
        clamp();
        refresh();
        break;
    case 0x09: {                        /* graphics cursor: BX, CX hot spot, ES:DX masks */
        const uint16_t *p = (const uint16_t *)f->r3;
        erase();
        M.hotx = bx; M.hoty = cx;
        for (int i = 0; i < 32; i++) M.gmask[i] = p[i];
        draw();
        break;
    }
    case 0x0A:                          /* text cursor: BX type, CX, DX masks */
        erase();
        M.textsoft = bx;
        if (bx == 0) { M.smask = f->r2; M.cmask = f->r3; }
        draw();
        break;
    case 0x0B:                          /* motion counters */
        set16(&f->r2, M.mx);
        set16(&f->r3, M.my);
        M.mx = M.my = 0;
        break;
    case 0x0C:                          /* event handler: CX mask, ES:DX */
        M.mask = f->r2;
        M.handler = f->r3;
        break;
    case 0x0F:                          /* mickey/pixel ratio */
        if (cx > 0) M.rx = cx;
        if (dx > 0) M.ry = dx;
        break;
    case 0x13:                          /* double-speed threshold */
        M.dbl = dx ? dx : 64;
        break;
    case 0x14: {                        /* swap event handlers */
        uint16_t om = M.mask;
        uint32_t oh = M.handler;
        M.mask = f->r2;
        M.handler = f->r3;
        set16(&f->r2, om);
        f->r3 = oh;
        f->r8 = 0;
        break;
    }
    case 0x15:                          /* state buffer size */
        set16(&f->r1, sizeof M);
        break;
    case 0x16: {                        /* save state to ES:DX */
        uint8_t *d = (uint8_t *)f->r3;
        const uint8_t *s = (const uint8_t *)&M;
        for (unsigned i = 0; i < sizeof M; i++) d[i] = s[i];
        break;
    }
    case 0x17: {                        /* restore state from ES:DX */
        const uint8_t *s = (const uint8_t *)f->r3;
        uint8_t *d = (uint8_t *)&M;
        erase();
        for (unsigned i = 0; i < sizeof M; i++) d[i] = s[i];
        draw();
        break;
    }
    case 0x1A:                          /* sensitivity */
        M.sens_h = bx; M.sens_v = cx; M.sens_d = dx;
        break;
    case 0x1B:
        set16(&f->r1, M.sens_h); set16(&f->r2, M.sens_v); set16(&f->r3, M.sens_d);
        break;
    case 0x1D:                          /* CRT page */
        M.page = bx;
        break;
    case 0x1E:
        set16(&f->r1, M.page);
        break;
    case 0x1F:                          /* disable driver */
        erase();
        M.disabled = 1;
        set16(&f->r0, 0x1F);
        f->r1 = (uint32_t)mouse_old33;
        f->r8 = 0;
        break;
    case 0x20:                          /* enable driver */
        M.disabled = 0;
        draw();
        break;
    case 0x24:                          /* version, type, IRQ */
        set16(&f->r1, MOUSE_VERSION);
        set16(&f->r2, 0x0400);          /* CH = 4: PS/2, CL = 0 */
        break;
    case 0x26:                          /* maximum virtual coordinates */
        set16(&f->r1, M.disabled);
        set16(&f->r2, 639);
        set16(&f->r3, maxy());
        break;
    case 0x6D6D:                        /* ARM-DOS MOUSE.COM: are you there? */
        set16(&f->r0, 0x4D4D);
        set16(&f->r1, M.psp);
        f->r4 = (uint32_t)mouse_sig;
        break;
    default:
        break;
    }
    m_irqrestore(irq);
}

/* the BIOS pointing-device handler (INT 15h AX=C207h): one call per packet,
   from the IRQ 12 handler, IRQs off */
RESFN void mouse_event(uint32_t status, int32_t dx, int32_t dy, int32_t dz)
{
    (void)dz;
    if (M.disabled) return;
    dy = -dy;                           /* PS/2: up is positive */
    M.mx += dx; M.my += dy;
    M.lastmx += dx; M.lastmy += dy;
    int oldx = rep_x(), oldy = rep_y();
    M.remx += dx * 8 * (int32_t)M.sens_h;
    M.remy += dy * 8 * (int32_t)M.sens_v;
    int32_t sx = sdiv(M.remx, M.rx * 50), sy = sdiv(M.remy, M.ry * 50);
    M.remx -= sx * M.rx * 50;
    M.remy -= sy * M.ry * 50;
    M.x += sx; M.y += sy;
    clamp();
    int nx = rep_x(), ny = rep_y();
    int events = 0;
    if (nx != oldx || ny != oldy) {
        events |= 1;
        if (drawn || M.shown == 0) refresh();
    }
    int nb = status & 7;
    for (int i = 0; i < 3; i++) {
        int bit = 1 << i;
        if ((nb & bit) && !(M.buttons & bit)) {
            M.press[i]++; M.pressx[i] = nx; M.pressy[i] = ny;
            events |= 2 << (2 * i);
        } else if (!(nb & bit) && (M.buttons & bit)) {
            M.rel[i]++; M.relx[i] = nx; M.rely[i] = ny;
            events |= 4 << (2 * i);
        }
    }
    M.buttons = nb;
    if (M.handler && (events & M.mask)) {
        struct armregs r;
        r.r0 = events & M.mask;
        r.r1 = nb;
        r.r2 = nx;
        r.r3 = ny;
        r.r4 = (uint16_t)M.lastmx;
        r.r5 = (uint16_t)M.lastmy;
        r.r6 = 0;
        m_callhandler(M.handler, &r);
    }
}

/* INT 10h: a mode set clears the screen under the cursor */
RESFN void mouse_int10(struct armregs *f)
{
    if (((f->r0 >> 8) & 0xFF) != 0) { mouse_old10(f); return; }
    uint32_t irq = m_irqoff();
    drawn = 0;
    M.busy = 1;
    m_irqrestore(irq);
    mouse_old10(f);
    irq = m_irqoff();
    M.busy = 0;
    if (M.ymax > maxy()) M.ymax = maxy();
    clamp();
    draw();
    m_irqrestore(irq);
}

/* remember the resident PSP (for MOUSE OFF) */
RESFN void mouse_setpsp(uint16_t seg) { M.psp = seg; }
