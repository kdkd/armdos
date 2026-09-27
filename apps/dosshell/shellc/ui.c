/*
 * ui.c - input (keyboard, INT 33h mouse), the layer stack the screens are
 * redrawn from, the title bar with the clock, the function-key line, the
 * action bar with its pulldowns, and the dialog boxes.
 */
#include "shell.h"

const char ENTERGLYPH[] = "<\xC4\xD9";
const char BTN_ENTER[] = "  <\xC4\xD9=Enter   Esc=Cancel   F1=Help";

/* ------------------------------------------------------------- mouse ---- */
int mouse_ok;
static int mouse_shown;
static int mbuttons, last_click_row = -1, last_click_col, last_click_tick;
void (*idle_hook)(void);

static uint32_t ticks(void) { return *(volatile uint32_t *)0x46C; }

void mouse_init(void)
{
    struct armregs r = {0};
    if (!ARMDOS_IVT[0x33]) return;
    r.r0 = 0;
    _armdos_int33(&r);
    mouse_ok = (r.r0 & 0xFFFF) == 0xFFFF;
    if (!mouse_ok) return;
    memset(&r, 0, sizeof r);            /* text pointer: invert the cell (the Shell's block) */
    r.r0 = 0x000A;
    r.r1 = 0;
    r.r2 = 0xFFFF;
    r.r3 = 0x7700;
    _armdos_int33(&r);
    mouse_shown = 0;
    mouse_show(1);
}

void mouse_show(int on)
{
    struct armregs r = {0};
    if (!mouse_ok || on == mouse_shown) return;
    r.r0 = on ? 1 : 2;
    _armdos_int33(&r);
    mouse_shown = on;
}

void mouse_done(void)
{
    mouse_show(0);
}

static int mouse_poll(struct ev *e)
{
    struct armregs r = {0};
    if (!mouse_ok) return 0;
    r.r0 = 3;
    _armdos_int33(&r);
    int b = r.r1 & 1;
    int row = (r.r3 & 0xFFFF) / 8, col = (r.r2 & 0xFFFF) / 8;
    if (b == mbuttons) return 0;
    mbuttons = b;
    e->row = row;
    e->col = col;
    e->key = 0;
    if (b) {
        uint32_t t = ticks();
        if (row == last_click_row && col == last_click_col && t - last_click_tick < 9) {
            e->type = EV_DBL;
            last_click_row = -1;
        } else {
            e->type = EV_DOWN;
            last_click_row = row;
            last_click_col = col;
            last_click_tick = t;
        }
    } else
        e->type = EV_UP;
    return 1;
}

/* ---------------------------------------------------------- keyboard ---- */
int key_pending(void)
{
    struct armregs r = {0};
    r.r0 = 0x1100;
    _armdos_int16(&r);
    return !(r.cpsr & ARM_CPSR_Z);
}

static int key_read(void)
{
    struct armregs r = {0};
    r.r0 = 0x1000;
    _armdos_int16(&r);
    int k = r.r0 & 0xFFFF;
    int sc = k >> 8, a = k & 0xFF;
    if (a == 0xE0 && sc) a = 0;         /* grey keys */
    if (sc == 0xE0) sc = (a == 0x0D || a == 0x0A) ? 0x1C : 0x35;   /* keypad Enter, / */
    return (sc << 8) | a;
}

static int last_min = -1;

int ev_get(struct ev *e)
{
    for (;;) {
        if (key_pending()) {
            e->type = EV_KEY;
            e->key = key_read();
            e->row = e->col = -1;
            return e->type;
        }
        if (mouse_poll(e)) return e->type;
        if (opt.date) {
            struct armregs r = {0};
            r.r0 = 0x2C00;
            _armdos_int21(&r);
            int m = (r.r2 & 0xFF) + ((r.r2 >> 8) & 0xFF) * 60;
            if (m != last_min) {
                last_min = m;
                draw_clock();
            }
        }
        if (idle_hook) idle_hook();
        armdos_halt();
    }
}

/* -------------------------------------------------------------- layers -- */
static struct { drawfn fn; void *ctx; } layers[12];
static int nlayers;

void push_layer(drawfn fn, void *ctx)
{
    if (nlayers < 12) {
        layers[nlayers].fn = fn;
        layers[nlayers].ctx = ctx;
        nlayers++;
    }
}

int layer_count(void) { return nlayers; }

void pop_layer(void)
{
    if (nlayers) nlayers--;
}

void redraw(void)
{
    for (int i = 0; i < nlayers; i++) layers[i].fn(layers[i].ctx);
    s_flush();
}

/* ------------------------------------------------------- title, clock --- */
int title_mode;
static const char *cur_title = "";
static int in_title;

static void put_clock(void)
{
    struct armregs r = {0};
    char b[16];
    if (!opt.date) return;
    r.r0 = 0x2A00;                      /* date: CX year, DH month, DL day */
    _armdos_int21(&r);
    int y = r.r2 & 0xFFFF, mo = (r.r3 >> 8) & 0xFF, d = r.r3 & 0xFF;
    b[0] = '0' + mo / 10; b[1] = '0' + mo % 10; b[2] = '-';
    b[3] = '0' + d / 10;  b[4] = '0' + d % 10;  b[5] = '-';
    b[6] = '0' + (y / 10) % 10; b[7] = '0' + y % 10; b[8] = 0;
    s_put(0, 3, b, CLR(C_TITLE));
    memset(&r, 0, sizeof r);
    r.r0 = 0x2C00;
    _armdos_int21(&r);
    int h = (r.r2 >> 8) & 0xFF, m = r.r2 & 0xFF;
    int pm = h >= 12;
    h %= 12;
    if (!h) h = 12;
    int i = 0;
    b[i++] = h >= 10 ? '1' : ' ';
    b[i++] = '0' + h % 10;
    b[i++] = ':';
    b[i++] = '0' + m / 10;
    b[i++] = '0' + m % 10;
    b[i++] = ' ';
    b[i++] = pm ? 'p' : 'a';
    b[i++] = 'm';
    b[i] = 0;
    s_put(0, 69, b, CLR(C_TITLE));
}

void draw_title(const char *title)
{
    cur_title = title;
    in_title = 1;
    s_fill(0, 0, 1, COLS, ' ', CLR(C_TITLE));
    int n = strlen(title), c = 39 - n / 2;
    s_fill(0, c - 1, 1, n + 2, ' ', CLR(C_TITLEBOX));
    s_put(0, c, title, CLR(C_TITLEBOX));
    put_clock();
}

void draw_clock(void)
{
    if (!in_title) return;
    put_clock();
    s_flush();
}

void title_off(void) { in_title = 0; }

void draw_fkeys(const char *text)
{
    s_fill(24, 0, 1, COLS, ' ', CLR(C_FKEY));
    s_put(24, 0, text, CLR(C_FKEY));
}

/* which key does a click at col on a label line such as
   "  F10=Actions  Esc=Cancel  Shift+F9=Command Prompt" stand for */
int fkey_hit(const char *text, int col)
{
    int n = strlen(text);
    if (col < 0 || col >= n || text[col] == ' ') return 0;
    int s = col, e = col;
    while (s > 0 && !(text[s - 1] == ' ' && (s < 2 || text[s - 2] == ' ' || strchr("=", text[s]) == 0))) {
        if (text[s - 1] == ' ' && s >= 2 && text[s - 2] == ' ') break;
        s--;
    }
    while (e < n && !(text[e] == ' ' && (e + 1 >= n || text[e + 1] == ' '))) e++;
    /* the token is text[s..e): "KEY=Label" */
    char k[12];
    int i = 0;
    while (s + i < e && text[s + i] != '=' && i < 11) { k[i] = text[s + i]; i++; }
    k[i] = 0;
    static const struct { const char *n; int k; } map[] = {
        { "Esc", K_ESC }, { "F1", K_F1 }, { "F2", K_F2 }, { "F3", K_F3 }, { "F9", K_F9 },
        { "F10", K_F10 }, { "F11", K_F11 }, { "Shift+F9", K_SF9 }, { "<\xC4\xD9", K_ENTER },
        { 0, 0 }
    };
    for (int j = 0; map[j].n; j++)
        if (!strcmp(k, map[j].n)) return map[j].k;
    return 0;
}

/* ---------------------------------------------------- action bar -------- */
static void put_mn(int r, int c, const char *s, int mn, int attr, int mnattr, int avail)
{
    for (int i = 0; s[i]; i++) {
        int ch = (uint8_t)s[i];
        if (i == mn && !avail) ch = '*';
        s_ch(r, c + i, ch, i == mn ? mnattr : attr);
    }
}

void draw_actionbar(const struct menu *m, int n, int active, int openmenu)
{
    s_fill(1, 0, 1, COLS, ' ', CLR(C_BAR));
    for (int i = 0; i < n; i++) {
        int sel = i == active;
        int len = strlen(m[i].name);
        if (sel) s_fill(1, m[i].col - 1, 1, len + 2, ' ', CLR(C_BARSEL));
        put_mn(1, m[i].col, m[i].name, m[i].mn, sel ? CLR(C_BARSEL) : CLR(C_BAR),
               sel ? CLR(C_BARSELMN) : CLR(C_BARMN), 1);
    }
    s_ch(1, 66, B_V, CLR(C_BAR));
    s_put(1, 69, "F1=Help", CLR(C_BAR));
    (void)openmenu;
}

struct pullctx {
    const struct menu *m;
    int sel;
    availfn avail;
    int menu;
};

static int pull_rows(const struct menu *m) { return m->n + 2; }

static void pull_draw(void *p)
{
    struct pullctx *c = p;
    const struct menu *m = c->m;
    int left = m->col - 3, h = pull_rows(m);
    s_fill(2, left, h, m->width, ' ', CLR(C_PULL));
    s_box(2, left, h, m->width, CLR(C_PULL));
    for (int i = 0; i < m->n; i++) {
        const struct mitem *it = &m->items[i];
        if (!it->text) continue;
        int av = c->avail ? c->avail(c->menu, i) : 1;
        int sel = i == c->sel;
        int a = sel ? CLR(C_PULLSEL) : CLR(C_PULL);
        if (sel) s_fill(3 + i, left + 2, 1, m->width - 4, ' ', a);
        put_mn(3 + i, left + 3, it->text, it->mn, a, sel ? CLR(C_PULLSELMN) : CLR(C_PULLMN), av);
        if (it->right) s_put(3 + i, left + m->width - 3 - strlen(it->right), it->right, a);
    }
}

struct barctx {
    const struct menu *m;
    int n, active;
};

static void bar_draw(void *p)
{
    struct barctx *b = p;
    draw_actionbar(b->m, b->n, b->active, -1);
}

static int first_item(const struct menu *m, availfn av, int mi)
{
    (void)av; (void)mi;
    for (int i = 0; i < m->n; i++)
        if (m->items[i].text) return i;
    return 0;
}

static int step_item(const struct menu *m, int i, int d)
{
    for (int k = 0; k < m->n; k++) {
        i = (i + d + m->n) % m->n;
        if (m->items[i].text) return i;
    }
    return i;
}

static int menu_at(const struct menu *m, int n, int col)
{
    for (int i = 0; i < n; i++) {
        int len = strlen(m[i].name);
        if (col >= m[i].col - 1 && col <= m[i].col + len) return i;
    }
    return -1;
}

/* the action bar is active; opened: a pulldown is showing. Returns
   menu<<8 | item for a chosen available item, -1 when left. *helpid gets
   the (menu, item) under the cursor when F1 was pressed (caller shows help) */
int run_actionbar(const struct menu *m, int n, availfn avail, int start, int opened, int *helpid)
{
    struct barctx bc = { m, n, start };
    struct pullctx pc = { &m[start], 0, avail, start };
    int result = -1;
    push_layer(bar_draw, &bc);
    if (opened) {
        pc.sel = first_item(&m[bc.active], avail, bc.active);
        push_layer(pull_draw, &pc);
    }
    for (;;) {
        pc.m = &m[bc.active];
        pc.menu = bc.active;
        redraw();
        struct ev e;
        ev_get(&e);
        if (e.type == EV_KEY) {
            int k = e.key, a = KASCII(k);
            if (k == K_F1) {
                if (helpid) *helpid = opened ? (bc.active << 8 | pc.sel) : (bc.active << 8 | 0xFF);
                result = -2;
                break;
            }
            if (k == K_F10) break;
            if (k == K_ESC) {
                if (opened) { pop_layer(); opened = 0; continue; }
                break;
            }
            if (k == K_LEFT || k == K_RIGHT) {
                bc.active = (bc.active + (k == K_LEFT ? n - 1 : 1)) % n;
                if (opened) pc.sel = first_item(&m[bc.active], avail, bc.active);
                continue;
            }
            if (!opened) {
                if (k == K_ENTER || k == K_DOWN) {
                    pc.sel = first_item(&m[bc.active], avail, bc.active);
                    push_layer(pull_draw, &pc);
                    opened = 1;
                    continue;
                }
                if (a > ' ') {
                    int u = upc(a);
                    for (int i = 0; i < n; i++)
                        if (upc(m[i].name[m[i].mn]) == u) {
                            bc.active = i;
                            pc.sel = first_item(&m[i], avail, i);
                            push_layer(pull_draw, &pc);
                            opened = 1;
                            break;
                        }
                }
                continue;
            }
            const struct menu *cm = &m[bc.active];
            if (k == K_UP || k == K_DOWN) {
                pc.sel = step_item(cm, pc.sel, k == K_UP ? -1 : 1);
                continue;
            }
            if (k == K_ENTER) {
                if (!avail || avail(bc.active, pc.sel)) {
                    result = bc.active << 8 | pc.sel;
                    break;
                }
                beep();
                continue;
            }
            if (a > ' ') {
                int u = upc(a), hit = 0;
                for (int i = 0; i < cm->n; i++)
                    if (cm->items[i].text && upc(cm->items[i].text[cm->items[i].mn]) == u &&
                        (!avail || avail(bc.active, i))) {
                        result = bc.active << 8 | i;
                        hit = 1;
                        break;
                    }
                if (hit) break;
                beep();
            }
            continue;
        }
        if (e.type == EV_DOWN || e.type == EV_DBL) {
            if (e.row == 1) {
                int mi = menu_at(m, n, e.col);
                if (mi >= 0) {
                    bc.active = mi;
                    pc.sel = first_item(&m[mi], avail, mi);
                    if (!opened) { push_layer(pull_draw, &pc); opened = 1; }
                    continue;
                }
                if (e.col >= 69 && e.col <= 75) {
                    if (helpid) *helpid = bc.active << 8 | 0xFF;
                    result = -2;
                    break;
                }
            }
            if (opened) {
                const struct menu *cm = &m[bc.active];
                int left = cm->col - 3;
                if (e.row >= 3 && e.row < 3 + cm->n && e.col > left && e.col < left + cm->width - 1) {
                    int i = e.row - 3;
                    if (cm->items[i].text) pc.sel = i;
                    continue;
                }
            }
            break;                      /* a click outside closes */
        }
        if (e.type == EV_UP && opened) {
            const struct menu *cm = &m[bc.active];
            int left = cm->col - 3;
            if (e.row >= 3 && e.row < 3 + cm->n && e.col > left && e.col < left + cm->width - 1) {
                int i = e.row - 3;
                if (cm->items[i].text && (!avail || avail(bc.active, i))) {
                    result = bc.active << 8 | i;
                    break;
                }
            }
        }
    }
    if (opened) pop_layer();
    pop_layer();
    return result;
}

/* ------------------------------------------------------------- dialogs --- */
static int list_count(const char *s)
{
    int n = 1;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

static const char *list_item(const char *s, int i, int *len)
{
    while (i-- > 0) {
        s = strchr(s, '\n');
        if (!s) { *len = 0; return ""; }
        s++;
    }
    const char *e = strchr(s, '\n');
    *len = e ? e - s : (int)strlen(s);
    return s;
}

struct fstate {             /* editing state of the focused input field */
    int pos, scroll, fresh;
    char orig[MEU_CMD + 1];
};
static struct fstate fs_;

static int field_x(const struct dfield *f)
{
    return f->col + (f->label ? strlen(f->label) : 0);
}

void dialog_draw(void *p)
{
    struct dialog *d = p;
    int at = CLR(d->color);
    s_fill(d->row, d->col, d->h, d->w, ' ', at);
    s_box(d->row, d->col, d->h, d->w, at);
    if (d->title) {
        int n = strlen(d->title), inner = d->w - 2;
        s_put(d->row + 1, d->col + (d->tcol ? d->tcol : 1 + (inner - n + 1) / 2), d->title, at);
    }
    if (d->buttons) {
        s_hdiv(d->row + d->h - 3, d->col, d->w, at);
        s_put(d->row + d->h - 2, d->col + 1, d->buttons, at);
    }
    for (int i = 0; i < d->nf; i++) {
        struct dfield *f = &d->f[i];
        int r = d->row + f->row, c = d->col + f->col;
        int foc = i == d->focus;
        switch (f->type) {
        case DF_TEXT:
            s_put(r, c, f->label, at);
            break;
        case DF_INPUT:
        case DF_FIXED:
        case DF_PASSWORD: {
            int x = d->col + field_x(f);
            if (f->label) s_put(r, c, f->label, at);
            s_ch(r, x, '[', at);
            int sc = foc ? fs_.scroll : 0;
            int len = strlen(f->buf);
            for (int k = 0; k < f->width; k++) {
                int ch = sc + k < len ? (uint8_t)f->buf[sc + k] : ' ';
                s_ch(r, x + 1 + k, ch, at);
            }
            s_ch(r, x + 1 + f->width, f->type == DF_INPUT ? '>' : ']', at);
            break;
        }
        case DF_RADIO:
        case DF_CHECK:
        case DF_CHOICE: {
            int n = list_count(f->label);
            for (int k = 0; k < n; k++) {
                int len;
                const char *s = list_item(f->label, k, &len);
                int on = f->type == DF_RADIO ? f->value == k : f->type == DF_CHECK ? (f->value >> k) & 1 : 0;
                int tx = f->type == DF_CHOICE ? c : c + (f->width ? f->width : 3);
                if (f->type != DF_CHOICE && on) s_ch(r + k, c, G_TRI, at);
                int cur = foc && ((f->type == DF_CHOICE && f->value == k) ||
                                  (f->type != DF_CHOICE && (f->max == k)));
                int bw = f->barw ? f->barw : len + 2;
                if (cur) s_fill(r + k, tx - 1, 1, bw, ' ', CLR(C_DLGSEL));
                for (int j = 0; j < len; j++)
                    s_ch(r + k, tx + j, (uint8_t)s[j], cur ? CLR(C_DLGSEL) : at);
            }
            break;
        }
        }
    }
}

static int is_input(const struct dfield *f)
{
    return f->type == DF_INPUT || f->type == DF_FIXED || f->type == DF_PASSWORD;
}

static int focusable(const struct dfield *f) { return f->type != DF_TEXT; }

static void focus_field(struct dialog *d, int i)
{
    d->focus = i;
    struct dfield *f = &d->f[i];
    if (is_input(f)) {
        strncpy(fs_.orig, f->buf, sizeof fs_.orig - 1);
        fs_.pos = strlen(f->buf);
        if (fs_.pos > f->max) fs_.pos = f->max;
        fs_.scroll = 0;
        if (fs_.pos >= f->width) fs_.scroll = fs_.pos - f->width + 1;
        fs_.pos = 0;
        fs_.scroll = 0;
        fs_.fresh = 1;
    }
}

static int next_focus(struct dialog *d, int dir)
{
    int i = d->focus;
    for (int k = 0; k < d->nf; k++) {
        i = (i + dir + d->nf) % d->nf;
        if (focusable(&d->f[i])) return i;
    }
    return d->focus;
}

/* -> 1 if the key was used by the field */
static int field_key(struct dialog *d, struct dfield *f, int k)
{
    int a = KASCII(k);
    if (is_input(f)) {
        int len = strlen(f->buf);
        if (k == K_LEFT) { if (fs_.pos) fs_.pos--; }
        else if (k == K_RIGHT) { if (fs_.pos < len) fs_.pos++; }
        else if (k == K_HOME) fs_.pos = 0;
        else if (k == K_END) fs_.pos = len;
        else if (k == K_DEL) { if (fs_.pos < len) memmove(f->buf + fs_.pos, f->buf + fs_.pos + 1, len - fs_.pos); }
        else if (k == K_BS) {
            if (fs_.pos) { memmove(f->buf + fs_.pos - 1, f->buf + fs_.pos, len - fs_.pos + 1); fs_.pos--; }
        } else if (k == K_F9) { strcpy(f->buf, fs_.orig); fs_.pos = 0; }
        else if (k == K_F4 && f->type == DF_INPUT && f->help == H_COMMANDS) {
            if (len < f->max) { memmove(f->buf + fs_.pos + 1, f->buf + fs_.pos, len - fs_.pos + 1); f->buf[fs_.pos++] = (char)0xBA; }
        } else if (a >= ' ' && a != 0x7F) {
            if (fs_.fresh && f->value) {        /* /r: the first key replaces the default */
                f->buf[0] = 0;
                len = 0;
                fs_.pos = 0;
            }
            if (len < f->max) {
                memmove(f->buf + fs_.pos + 1, f->buf + fs_.pos, len - fs_.pos + 1);
                f->buf[fs_.pos++] = (char)(f->type == DF_PASSWORD ? upc(a) : a);
            } else beep();
        } else return 0;
        fs_.fresh = 0;
        if (fs_.pos < fs_.scroll) fs_.scroll = fs_.pos;
        if (fs_.pos >= fs_.scroll + f->width) fs_.scroll = fs_.pos - f->width + 1;
        return 1;
    }
    if (f->type == DF_RADIO || f->type == DF_CHECK || f->type == DF_CHOICE) {
        int n = list_count(f->label);
        int *cur = f->type == DF_CHOICE ? &f->value : &f->max;
        if (k == K_UP) { *cur = (*cur + n - 1) % n; if (f->type == DF_RADIO) f->value = *cur; return 1; }
        if (k == K_DOWN) { *cur = (*cur + 1) % n; if (f->type == DF_RADIO) f->value = *cur; return 1; }
        if (k == K_SPACE) {
            if (f->type == DF_CHECK) f->value ^= 1 << *cur;
            else if (f->type == DF_RADIO) f->value = *cur;
            return 1;
        }
        if (f->type == DF_CHOICE && a >= '1' && a < '1' + n) { f->value = a - '1'; return 2; }
    }
    (void)d;
    return 0;
}

static void dialog_cursor(struct dialog *d)
{
    struct dfield *f = &d->f[d->focus];
    if (d->nf && is_input(f))
        s_cursor(d->row + f->row, d->col + field_x(f) + 1 + fs_.pos - fs_.scroll);
    else
        s_cursor(-1, 0);
}

static int field_at(struct dialog *d, int row, int col, int *sub)
{
    for (int i = 0; i < d->nf; i++) {
        struct dfield *f = &d->f[i];
        int r = d->row + f->row;
        if (is_input(f)) {
            int x = d->col + field_x(f);
            if (row == r && col >= x && col <= x + f->width + 1) { *sub = col - x - 1; return i; }
        } else if (f->type != DF_TEXT) {
            int n = list_count(f->label);
            if (row >= r && row < r + n && col > d->col && col < d->col + d->w - 1) { *sub = row - r; return i; }
        }
    }
    return -1;
}

int dialog_run(struct dialog *d)
{
    int ret;
    if (d->row < 0) d->row = (25 - d->h) / 2;
    if (d->col < 0) d->col = (80 - d->w) / 2;
    if (d->nf && !focusable(&d->f[d->focus])) d->focus = next_focus(d, 1);
    if (d->nf) focus_field(d, d->focus);
    push_layer(dialog_draw, d);
    for (;;) {
        redraw();
        dialog_cursor(d);
        struct ev e;
        ev_get(&e);
        struct dfield *f = d->nf ? &d->f[d->focus] : 0;
        if (e.type == EV_KEY) {
            int k = e.key;
            if (k == K_ESC) { ret = K_ESC; break; }
            if (k == K_F2 && d->f2) { ret = K_F2; break; }
            if (k == K_F1) {
                s_cursor(-1, 0);
                help_show(f && f->help ? f->help : d->help, 0, 0);
                continue;
            }
            if (k == K_TAB || k == K_STAB) {
                if (d->nf) focus_field(d, next_focus(d, k == K_TAB ? 1 : -1));
                continue;
            }
            if (k == K_ENTER) {
                if (d->noenter && f) {
                    int nx = next_focus(d, 1);
                    if (nx > d->focus) { focus_field(d, nx); continue; }
                }
                ret = K_ENTER;
                break;
            }
            if (f) {
                int u = field_key(d, f, k);
                if (u == 2) { ret = K_ENTER; break; }
                if (u) continue;
            }
            if ((k == K_UP || k == K_DOWN) && d->nf) {
                focus_field(d, next_focus(d, k == K_UP ? -1 : 1));
                continue;
            }
            continue;
        }
        if (e.type == EV_DOWN || e.type == EV_DBL) {
            if (d->buttons && e.row == d->row + d->h - 2) {
                int k = fkey_hit(d->buttons, e.col - d->col - 1);
                if (k == K_ENTER || k == K_ESC || (k == K_F2 && d->f2)) { ret = k; break; }
                if (k == K_F1) { help_show(f && f->help ? f->help : d->help, 0, 0); continue; }
                continue;
            }
            int sub, i = field_at(d, e.row, e.col, &sub);
            if (i >= 0) {
                if (i != d->focus) focus_field(d, i);
                struct dfield *g = &d->f[i];
                if (is_input(g)) {
                    int len = strlen(g->buf);
                    fs_.pos = fs_.scroll + (sub < 0 ? 0 : sub);
                    if (fs_.pos > len) fs_.pos = len;
                    fs_.fresh = 0;
                } else if (g->type == DF_CHOICE) {
                    g->value = sub;
                    if (e.type == EV_DBL) { ret = K_ENTER; break; }
                } else {
                    g->max = sub;
                    if (g->type == DF_CHECK) g->value ^= 1 << sub; else g->value = sub;
                }
            }
        }
    }
    s_cursor(-1, 0);
    pop_layer();
    return ret;
}

/* a pop-up message (the Shell's error and warning boxes): no title, the
   text on the first line, the keys under a divider (as the real Shell) */
int message(const char *text, int help)
{
    static char lines[3][60];
    struct dfield f[3];
    int n = 0;
    const char *p = text;
    memset(f, 0, sizeof f);
    while (*p && n < 3) {               /* wrap at 52 columns */
        int len = strlen(p);
        if (len > 52) {
            len = 52;
            while (len > 0 && p[len] != ' ') len--;
            if (!len) len = 52;
        }
        memcpy(lines[n], p, len);
        lines[n][len] = 0;
        f[n].type = DF_TEXT;
        f[n].row = 1 + n;
        f[n].col = 3;
        f[n].label = lines[n];
        n++;
        p += len;
        while (*p == ' ') p++;
    }
    struct dialog d = { 0, 3, 10, 8, 58, C_WARN, f, n, BTN_ENTER, help ? help : H_MESSAGE, 0, 0, 0, 0 };
    beep();
    return dialog_run(&d);
}

int choose2(const char *title, const char *text, const char *c1, const char *c2, int help)
{
    char list[128];
    int y = text ? 5 : 3;
    int w = 58;
    snprintf(list, sizeof list, "1. %s\n2. %s", c1, c2);
    struct dfield f[2] = {
        { DF_TEXT, 3, 3, text ? text : "", 0, 0, 0, 0, 0, 0 },
        { DF_CHOICE, y, 4, list, 0, 0, 0, 0, help, w - 5 },
    };
    struct dialog d = { title, -1, -1, y + 5, w, C_DLG, f, 2, BTN_ENTER, help, 1, 0, 0, 0 };
    if (!text) { f[0].type = DF_TEXT; f[0].label = ""; }
    int k = dialog_run(&d);
    if (k != K_ENTER) return 0;
    return f[1].value + 1;
}

/* a warning with two numbered choices (critical errors, group file missing):
   the message box's colours and layout, the choices under the text */
int warn_choice(const char *text, const char *c1, const char *c2, int help)
{
    static char list[128];
    snprintf(list, sizeof list, "1. %s\n2. %s", c1, c2);
    struct dfield f[2] = {
        { DF_TEXT, 1, 3, text, 0, 0, 0, 0, 0, 0 },
        { DF_CHOICE, 3, 4, list, 0, 0, 0, 0, help, 53 },
    };
    struct dialog d = { 0, 3, 10, 9, 58, C_WARN, f, 2, BTN_ENTER, help, 1, 0, 0, 0 };
    beep();
    if (dialog_run(&d) != K_ENTER) return 0;
    return f[1].value + 1;
}

struct statusctx { const char *title, *l1, *l2; int row, col, w, h; };
static struct statusctx stc;

static void status_draw(void *p)
{
    struct statusctx *c = p;
    int at = CLR(C_DLG);
    s_fill(c->row, c->col, c->h, c->w, ' ', at);
    s_box(c->row, c->col, c->h, c->w, at);
    s_center(c->row + 1, c->col + 1, c->w - 2, c->title, at);
    if (c->l1) s_put(c->row + 3, c->col + 3, c->l1, at);
    if (c->l2) s_put(c->row + 4, c->col + 3, c->l2, at);
}

/* a box showing what is being done; status_box(0,..) removes it */
void status_box(const char *title, const char *l1, const char *l2)
{
    static int shown;
    if (!title) {
        if (shown) pop_layer();
        shown = 0;
        return;
    }
    stc.title = title; stc.l1 = l1; stc.l2 = l2;
    stc.w = 50; stc.h = 7; stc.row = 9; stc.col = 15;
    if (!shown) push_layer(status_draw, &stc);
    shown = 1;
    redraw();
}

void beep(void)
{
    /* PC speaker, ~1 kHz for a moment (only with /SND, as the Shell) */
    if (!opt.snd) return;
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, 0xA9);
    armdos_outb(0x42, 0x04);
    armdos_outb(0x61, armdos_inb(0x61) | 3);
    uint32_t t = ticks();
    while (ticks() - t < 2) armdos_halt();
    armdos_outb(0x61, armdos_inb(0x61) & ~3);
}
