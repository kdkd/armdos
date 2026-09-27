/*
 * scr.c - the screen buffer, keyboard and mouse, dialog boxes.
 *
 * Everything is composed in sb[] and copied to B800 by flush(), which writes
 * only the cells that changed (and hides the mouse cursor meanwhile).
 */
#include "acm.h"

uint16_t sb[SCR_W * SCR_H];
uint16_t usr[SCR_W * SCR_H];
static uint16_t shown[SCR_W * SCR_H];
static int shown_valid;
int ms_x, ms_y, ms_btn, ms_dbl;
int mouse_ok;
static int mouse_vis;
void (*idle_hook)(void);

const pal_t PAL_GREY = { 0x70, 0x7F, 0x70, 0x30, 0x70, 0x30 };
const pal_t PAL_CYAN = { 0x30, 0x3F, 0x30, 0x0F, 0x30, 0x0F };
const pal_t PAL_RED  = { 0x4F, 0x4F, 0x4F, 0x70, 0x4F, 0x70 };

static int int33(int ax, int bx, int *cx, int *dx)
{
    struct armregs r = {0};
    r.r0 = ax; r.r1 = bx;
    if (cx) r.r2 = *cx;
    if (dx) r.r3 = *dx;
    _armdos_int33(&r);
    if (cx) *cx = r.r2 & 0xFFFF;
    if (dx) *dx = r.r3 & 0xFFFF;
    return ax == 5 || ax == 6 ? (int)(r.r1 & 0xFFFF) : (int)(r.r0 & 0xFFFF);
}

void scr_init(void)
{
    struct armregs r = {0};
    /* is there an INT 33h driver? (vector non-zero, reset answers FFFFh) */
    if (ARMDOS_IVT[0x33]) {
        r.r0 = 0;
        _armdos_int33(&r);
        mouse_ok = (r.r0 & 0xFFFF) == 0xFFFF;
    }
    shown_valid = 0;
    mouse_vis = 0;
}

void mouse_show(int on)
{
    if (!mouse_ok || on == mouse_vis) return;
    int33(on ? 1 : 2, 0, 0, 0);
    mouse_vis = on;
}

/* mode 7 (the Hercules/MDA card option): colours as normal / bright / reverse
   video, the brighter colour lit (see README) */
static uint16_t mono_cell(uint16_t x)
{
    static const uint8_t lum[16] = { 0, 1, 3, 4, 2, 3, 4, 7, 5, 6, 8, 9, 7, 8, 10, 11 };
    int a = x >> 8, fg = a & 15, bg = (a >> 4) & 7, na;
    if (bg == 7 || lum[bg] > lum[fg]) na = 0x70;
    else if (fg == bg) na = 0x00;
    else na = (fg >= 8 || (fg == 7 && bg)) ? 0x0F : 0x07;
    return (uint16_t)((na << 8) | (x & 0xFF));
}

void flush(void)
{
    volatile uint16_t *v = ARMDOS_TEXT_VRAM;
    int mono = ARMDOS_BDA[0x49] == 7;
    int i, from = -1, to = -1;
    if (!shown_valid) { from = 0; to = SCR_W * SCR_H - 1; }
    else
        for (i = 0; i < SCR_W * SCR_H; i++)
            if (shown[i] != sb[i]) { if (from < 0) from = i; to = i; }
    if (from < 0) return;
    int vis = mouse_vis;
    if (vis) mouse_show(0);
    for (i = from; i <= to; i++) { shown[i] = sb[i]; v[i] = mono ? mono_cell(sb[i]) : sb[i]; }
    shown_valid = 1;
    if (vis) mouse_show(1);
}

void save_scr(uint16_t *to) { memcpy(to, sb, sizeof sb); }
void restore_scr(const uint16_t *from) { memcpy(sb, from, sizeof sb); }

void cursor_at(int x, int y)
{
    struct armregs r = {0};
    r.r0 = 0x0200;
    r.r3 = y < 0 ? (25 << 8) : ((y << 8) | x);
    _armdos_int10(&r);
}

void putc_(int x, int y, int ch, int attr)
{
    if (x < 0 || x >= SCR_W || y < 0 || y >= SCR_H) return;
    sb[y * SCR_W + x] = (uint8_t)ch | (attr << 8);
}

void put(int x, int y, const char *s, int attr)
{
    while (*s) putc_(x++, y, (uint8_t)*s++, attr);
}

void putn(int x, int y, const char *s, int n, int attr)
{
    for (int i = 0; i < n; i++) putc_(x + i, y, *s ? (uint8_t)*s++ : ' ', attr);
}

void fill(int x, int y, int w, int h, int ch, int attr)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) putc_(x + i, y + j, ch, attr);
}

void setattr(int x, int y, int w, int attr)
{
    for (int i = 0; i < w; i++)
        if (x + i >= 0 && x + i < SCR_W && y >= 0 && y < SCR_H)
            sb[y * SCR_W + x + i] = (sb[y * SCR_W + x + i] & 0xFF) | (attr << 8);
}

void box(int x, int y, int w, int h, int attr, int dbl)
{
    int tl = dbl ? 0xC9 : 0xDA, tr = dbl ? 0xBB : 0xBF, bl = dbl ? 0xC8 : 0xC0, br = dbl ? 0xBC : 0xD9;
    int hz = dbl ? 0xCD : 0xC4, vt = dbl ? 0xBA : 0xB3;
    putc_(x, y, tl, attr); putc_(x + w - 1, y, tr, attr);
    putc_(x, y + h - 1, bl, attr); putc_(x + w - 1, y + h - 1, br, attr);
    for (int i = 1; i < w - 1; i++) { putc_(x + i, y, hz, attr); putc_(x + i, y + h - 1, hz, attr); }
    for (int j = 1; j < h - 1; j++) { putc_(x, y + j, vt, attr); putc_(x + w - 1, y + j, vt, attr); }
}

/* the shadow: two columns to the right, one row below; characters stay */
void shadow(int x, int y, int w, int h)
{
    for (int j = y + 1; j <= y + h; j++) setattr(x + w, j, 2, A_SHADOW);
    setattr(x + 2, y + h, w, A_SHADOW);
}

int strwidth_hot(const char *s)
{
    int n = 0;
    for (; *s; s++) if (*s != '&') n++;
    return n;
}

void puthot(int x, int y, const char *s, int attr, int hot)
{
    for (; *s; s++) {
        if (*s == '&') { s++; if (!*s) break; putc_(x++, y, (uint8_t)*s, hot); continue; }
        putc_(x++, y, (uint8_t)*s, attr);
    }
}

/* ---- keyboard ---- */
int key_ready(void)
{
    struct armregs r = {0};
    r.r0 = 0x1100;
    _armdos_int16(&r);
    return !(r.cpsr & ARM_CPSR_Z);
}

int shift_state(void) { return ARMDOS_BDA[0x17]; }

static int readkey(void)
{
    struct armregs r = {0};
    r.r0 = 0x1000;
    _armdos_int16(&r);
    int k = r.r0 & 0xFFFF;
    if ((k >> 8) == 0xE0) k = (k & 0xFF) == 0x2F ? 0x352F : 0x1C00 | (k & 0xFF);
    else if ((k & 0xFF) == 0xE0 && (k >> 8)) k &= 0xFF00;
    return k;
}

static uint32_t last_click_tick;
static int last_click_x = -1, last_click_y = -1;

static int poll_mouse(void)
{
    int cx = 0, dx = 0;
    for (int b = 0; b < 2; b++) {
        int cnt = int33(5, b, &cx, &dx);
        if (cnt) {
            ms_x = cx / 8; ms_y = dx / 8; ms_btn = b + 1;
            uint32_t now = ARMDOS_BIOS_TICKS;
            ms_dbl = b == 0 && ms_x == last_click_x && ms_y == last_click_y && now - last_click_tick <= 9;
            if (b == 0) {
                last_click_tick = ms_dbl ? 0 : now;
                last_click_x = ms_x; last_click_y = ms_y;
            }
            return 1;
        }
    }
    return 0;
}

int getkey(void)
{
    for (;;) {
        if (key_ready()) return readkey();
        if (mouse_ok && poll_mouse()) return K_MOUSE;
        if (idle_hook) idle_hook();
        if (key_ready()) continue;
        armdos_halt();
    }
}

int wait_release(void)
{
    if (!mouse_ok) return 0;
    for (;;) {
        struct armregs r = {0};
        r.r0 = 3;
        _armdos_int33(&r);
        if (!(r.r1 & 3)) return 0;
        armdos_halt();
    }
}

/* ---- the function key bar ---- */
void keybar_draw(const char *const *labels)
{
    for (int i = 0; i < 10; i++) {
        int x = i * 8;
        char num[3];
        if (i < 9) { num[0] = '1' + i; num[1] = 0; }
        else { num[0] = '1'; num[1] = '0'; num[2] = 0; }
        put(x, KEYROW, num, A_KEYNUM);
        int lx = x + strlen(num);
        putn(lx, KEYROW, labels[i], i < 9 ? 6 : 6, A_KEYLBL);
        if (i < 9) putc_(x + 7, KEYROW, ' ', A_KEYNUM);
    }
}

/* ---- line editing (command line and input fields) ---- */
int edit_line(char *buf, int max, int *pos, int key)
{
    int l = strlen(buf);
    int c = key & 0xFF;
    if (*pos > l) *pos = l;
    if (key == K_BS) {
        if (*pos > 0) { memmove(buf + *pos - 1, buf + *pos, l - *pos + 1); (*pos)--; }
        return 1;
    }
    if (key == K_DEL) {
        if (*pos < l) memmove(buf + *pos, buf + *pos + 1, l - *pos);
        return 1;
    }
    if (key == K_LEFT) { if (*pos > 0) (*pos)--; return 1; }
    if (key == K_RIGHT) { if (*pos < l) (*pos)++; return 1; }
    if (key == K_HOME) { *pos = 0; return 1; }
    if (key == K_END) { *pos = l; return 1; }
    if (key == K_CLEFT) {
        while (*pos > 0 && buf[*pos - 1] == ' ') (*pos)--;
        while (*pos > 0 && buf[*pos - 1] != ' ') (*pos)--;
        return 1;
    }
    if (key == K_CRIGHT) {
        while (*pos < l && buf[*pos] != ' ') (*pos)++;
        while (*pos < l && buf[*pos] == ' ') (*pos)++;
        return 1;
    }
    if (key < 0x10000 && c >= 0x20 && c != 0x7F && (key >> 8) != 0) {
        if (l + 1 >= max) return 1;
        memmove(buf + *pos + 1, buf + *pos, l - *pos + 1);
        buf[(*pos)++] = c;
        return 1;
    }
    return 0;
}

/* ---- dialogs ---- */
static void place(int bx, int by, int w, ditem *it, int n, int *xs)
{
    /* auto-centre the buttons of each row that asks for it (x < 0) */
    for (int i = 0; i < n; i++) {
        xs[i] = bx + it[i].x;
        if (it[i].type == DI_CTEXT) xs[i] = bx + (w - (int)strlen(it[i].text)) / 2;
    }
    for (int i = 0; i < n; i++) {
        if (it[i].type != DI_BUTTON || it[i].x >= 0 || xs[i] != bx + it[i].x) continue;
        int tot = 0, cnt = 0;
        for (int j = i; j < n; j++)
            if (it[j].type == DI_BUTTON && it[j].y == it[i].y && it[j].x < 0) { tot += strlen(it[j].text) + 4; cnt++; }
        tot += (cnt - 1) * 2;
        int x = bx + (w - tot) / 2;
        for (int j = i; j < n; j++)
            if (it[j].type == DI_BUTTON && it[j].y == it[i].y && it[j].x < 0) {
                xs[j] = x;
                x += strlen(it[j].text) + 6;
                it[j].x = -2;       /* placed */
            }
    }
    for (int i = 0; i < n; i++) if (it[i].x == -2) it[i].x = -1;
}

static int focusable(const ditem *d) { return d->type == DI_INPUT || d->type == DI_CHECK || d->type == DI_BUTTON; }

void dlg_frame(int bx, int by, int w, int h, const char *title, const pal_t *pal)
{
    fill(bx, by, w, h, ' ', pal->text);
    box(bx + 1, by, w - 2, h, pal->border, 1);
    if (title && *title) {
        int tl = strlen(title) + 2;
        int tx = bx + (w - tl) / 2;
        putc_(tx, by, ' ', pal->title);
        put(tx + 1, by, title, pal->title);
        putc_(tx + tl - 1, by, ' ', pal->title);
    }
    shadow(bx, by, w, h);
}

int dialog(const char *title, int w, int h, const pal_t *pal, ditem *it, int n, int focus)
{
    uint16_t save[SCR_W * SCR_H];
    int xs[24], pos[24];
    int bx = (SCR_W - w) / 2, by = (22 - h) / 2 + 1;
    int rc = -1, fresh = 1;
    save_scr(save);
    place(bx, by, w, it, n, xs);
    for (int i = 0; i < n; i++) pos[i] = it[i].buf ? (int)strlen(it[i].buf) : 0;
    if (focus < 0 || focus >= n || !focusable(&it[focus]))
        for (focus = 0; focus < n && !focusable(&it[focus]); focus++) ;
    for (;;) {
        dlg_frame(bx, by, w, h, title, pal);
        int cx = -1, cy = -1;
        for (int i = 0; i < n; i++) {
            ditem *d = &it[i];
            int x = xs[i], y = by + d->y;
            switch (d->type) {
            case DI_TEXT: case DI_CTEXT: put(x, y, d->text, pal->text); break;
            case DI_HLINE:
                putc_(bx + 1, y, 0xC7, pal->border);
                fill(bx + 2, y, w - 4, 1, 0xC4, pal->border);
                putc_(bx + w - 2, y, 0xB6, pal->border);
                if (d->text) put(bx + (w - strlen(d->text)) / 2, y, d->text, pal->border);
                break;
            case DI_INPUT: {
                int l = strlen(d->buf), off = 0;
                if (pos[i] >= d->w) off = pos[i] - d->w + 1;
                putn(x, y, off < l ? d->buf + off : "", d->w, pal->input);
                if (i == focus) { cx = x + pos[i] - off; cy = y; }
                break;
            }
            case DI_CHECK:
                put(x, y, *d->val ? "[x] " : "[ ] ", pal->text);
                put(x + 4, y, d->text, pal->text);
                if (i == focus) { cx = x + 1; cy = y; }
                break;
            case DI_BUTTON: {
                int a = i == focus ? pal->fbutton : pal->button;
                putc_(x, y, '[', a); putc_(x + 1, y, ' ', a);
                put(x + 2, y, d->text, a);
                int l = strlen(d->text);
                putc_(x + 2 + l, y, ' ', a); putc_(x + 3 + l, y, ']', a);
                break;
            }
            }
        }
        flush();
        cursor_at(cx, cy);
        int k = getkey();
        ditem *f = focus < n ? &it[focus] : 0;
        if (k == K_ESC) break;
        if (k == K_MOUSE) {
            int hit = -1;
            for (int i = 0; i < n; i++) {
                int x = xs[i], y = by + it[i].y, len = 0;
                if (it[i].type == DI_BUTTON) len = strlen(it[i].text) + 4;
                else if (it[i].type == DI_INPUT) len = it[i].w;
                else if (it[i].type == DI_CHECK) len = strlen(it[i].text) + 4;
                if (len && ms_y == y && ms_x >= x && ms_x < x + len) hit = i;
            }
            if (ms_y < by || ms_y >= by + h || ms_x < bx || ms_x >= bx + w) {
                if (ms_btn == 2) break;
                continue;
            }
            if (hit < 0) continue;
            focus = hit;
            if (it[hit].type == DI_CHECK) *it[hit].val = !*it[hit].val;
            if (it[hit].type == DI_BUTTON) { k = K_ENTER; f = &it[hit]; }
            else continue;
        }
        if (k == K_TAB || k == K_DOWN || k == K_STAB || k == K_UP ||
            (f && f->type == DI_BUTTON && (k == K_LEFT || k == K_RIGHT))) {
            int dir = (k == K_STAB || k == K_UP || k == K_LEFT) ? -1 : 1;
            int j = focus;
            do { j = (j + dir + n) % n; } while (!focusable(&it[j]) && j != focus);
            focus = j;
            fresh = 0;
            continue;
        }
        if (k == 0x3920 && f && f->type == DI_CHECK) { *f->val = !*f->val; continue; }
        if (k == K_ENTER || (k == 0x3920 && f && f->type == DI_BUTTON)) {
            int b = 0;
            if (f && f->type == DI_BUTTON)
                for (int i = 0; i < focus; i++) b += it[i].type == DI_BUTTON;
            rc = b;
            break;
        }
        if (f && f->type == DI_INPUT) {
            /* the first character typed replaces the proposed text */
            if (fresh && k < 0x10000 && (k & 0xFF) >= 0x20 && (k >> 8) && k != K_BS) { f->buf[0] = 0; pos[focus] = 0; }
            fresh = 0;
            edit_line(f->buf, f->max, &pos[focus], k);
            continue;
        }
        /* a letter that starts a button's text presses it */
        if ((k & 0xFF) > ' ') {
            int b = 0, c = k & 0xFF;
            if (c >= 'a' && c <= 'z') c -= 32;
            for (int i = 0; i < n; i++) {
                if (it[i].type != DI_BUTTON) continue;
                int t = it[i].text[0];
                if (t == c) { rc = b; goto done; }
                b++;
            }
        }
    }
done:
    restore_scr(save);
    flush();
    cursor_at(0, -1);
    return rc;
}

/* a box with lines of text, nothing to wait for (progress, "please wait") */
void msgbox_draw(const char *title, const char **lines, int nl, const pal_t *pal, int *pbx, int *pby, int *pw)
{
    int w = 40;
    for (int i = 0; i < nl; i++) if ((int)strlen(lines[i]) + 8 > w) w = strlen(lines[i]) + 8;
    if (w > 76) w = 76;
    int h = nl + 4;
    int bx = (SCR_W - w) / 2, by = (22 - h) / 2 + 1;
    dlg_frame(bx, by, w, h, title, pal);
    for (int i = 0; i < nl; i++) {
        int l = strlen(lines[i]);
        if (l > w - 6) l = w - 6;
        putn(bx + (w - l) / 2, by + 2 + i, lines[i], l, pal->text);
    }
    if (pbx) *pbx = bx;
    if (pby) *pby = by;
    if (pw) *pw = w;
}

int message(const char *title, const char *l1, const char *l2, const pal_t *pal, const char *const *buttons, int nb)
{
    ditem it[8];
    int n = 0, w = 36, bw = 0;
    int l1l = l1 ? strlen(l1) : 0, l2l = l2 ? strlen(l2) : 0;
    if (l1l + 8 > w) w = l1l + 8;
    if (l2l + 8 > w) w = l2l + 8;
    for (int i = 0; i < nb; i++) bw += strlen(buttons[i]) + 6;
    if (bw + 6 > w) w = bw + 6;
    if (w > 78) w = 78;
    int y = 2;
    if (l1) { it[n++] = (ditem){ DI_CTEXT, 0, y++, 0, l1, 0, 0, 0 }; }
    if (l2) { it[n++] = (ditem){ DI_CTEXT, 0, y++, 0, l2, 0, 0, 0 }; }
    y++;
    for (int i = 0; i < nb && n < 8; i++) it[n++] = (ditem){ DI_BUTTON, -1, y, 0, buttons[i], 0, 0, 0 };
    return dialog(title, w, y + 2, pal, it, n, 0);
}

void error_box(const char *l1, const char *l2)
{
    static const char *const ok[] = { "OK" };
    message("Error", l1, l2, &PAL_RED, ok, 1);
}

/* a scrolling list in a box; returns the key that ended it (K_ENTER, K_ESC, ...) */
int listbox(const char *title, const char *const *lines, int n, int w, int h, int *sel, const pal_t *pal, const char *foot)
{
    uint16_t save[SCR_W * SCR_H];
    int bx = (SCR_W - w) / 2, by = (22 - h) / 2 + 1, rows = h - 2 - (foot ? 2 : 0);
    int top = 0, k;
    save_scr(save);
    if (*sel >= n) *sel = n - 1;
    if (*sel < 0) *sel = 0;
    for (;;) {
        if (*sel < top) top = *sel;
        if (*sel >= top + rows) top = *sel - rows + 1;
        dlg_frame(bx, by, w, h, title, pal);
        for (int i = 0; i < rows && top + i < n; i++) {
            int a = top + i == *sel ? pal->fbutton : pal->text;
            putc_(bx + 2, by + 1 + i, ' ', a);
            putn(bx + 3, by + 1 + i, lines[top + i], w - 6, a);
            putc_(bx + w - 3, by + 1 + i, ' ', a);
        }
        if (top > 0) putc_(bx + w - 2, by + 1, 0x18, pal->border);
        if (top + rows < n) putc_(bx + w - 2, by + rows, 0x19, pal->border);
        if (foot) {
            putc_(bx + 1, by + h - 3, 0xC7, pal->border);
            fill(bx + 2, by + h - 3, w - 4, 1, 0xC4, pal->border);
            putc_(bx + w - 2, by + h - 3, 0xB6, pal->border);
            put(bx + (w - strlen(foot)) / 2, by + h - 2, foot, pal->text);
        }
        flush();
        cursor_at(0, -1);
        k = getkey();
        if (k == K_UP) { if (*sel > 0) (*sel)--; }
        else if (k == K_DOWN) { if (*sel < n - 1) (*sel)++; }
        else if (k == K_PGUP) { *sel -= rows - 1; if (*sel < 0) *sel = 0; }
        else if (k == K_PGDN) { *sel += rows - 1; if (*sel >= n) *sel = n - 1; }
        else if (k == K_HOME || k == K_CPGUP) *sel = 0;
        else if (k == K_END || k == K_CPGDN) *sel = n - 1;
        else if (k == K_MOUSE) {
            if (ms_x > bx && ms_x < bx + w - 1 && ms_y > by && ms_y <= by + rows) {
                int i = top + ms_y - by - 1;
                if (i < n) { *sel = i; if (ms_dbl) { k = K_ENTER; break; } }
            } else if (ms_y == by + 1 && ms_x == bx + w - 2) { if (top > 0) { top--; if (*sel > 0) (*sel)--; } }
            else if (ms_y == by + rows && ms_x == bx + w - 2) { if (*sel < n - 1) (*sel)++; }
            else if (ms_y < by || ms_y >= by + h || ms_x < bx || ms_x >= bx + w) { k = K_ESC; break; }
        }
        else break;
    }
    restore_scr(save);
    flush();
    return k;
}
