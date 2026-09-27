/*
 * edit.c - F4, a simple full-screen text editor.
 *
 * The text is kept as an array of lines (without CR LF); saving writes the
 * lines with CR LF. Tabs are kept and shown to the next multiple of 8.
 */
#include <stdlib.h>
#include "acm.h"

#define EROWS 23

typedef struct { char *s; int n, cap; } eline;

static eline *L;
static int nl, capl;
static int cx, cy, top, left, modified, overwrite, finalnl;

static int ensure(eline *l, int need)
{
    if (need <= l->cap) return 0;
    int nc = l->cap ? l->cap : 16;
    while (nc < need) nc *= 2;
    char *ns = realloc(l->s, nc);
    if (!ns) return -1;
    l->s = ns; l->cap = nc;
    return 0;
}

static int insert_line(int at, const char *s, int n)
{
    if (nl == capl) {
        int nc = capl ? capl * 2 : 256;
        eline *nlp = realloc(L, nc * sizeof(eline));
        if (!nlp) return -1;
        L = nlp; capl = nc;
    }
    memmove(L + at + 1, L + at, (nl - at) * sizeof(eline));
    L[at].s = 0; L[at].n = 0; L[at].cap = 0;
    nl++;
    if (ensure(&L[at], n + 1)) return -1;
    memcpy(L[at].s, s, n);
    L[at].n = n;
    return 0;
}

static void delete_line(int at)
{
    free(L[at].s);
    memmove(L + at, L + at + 1, (nl - at - 1) * sizeof(eline));
    nl--;
}

static void free_all(void)
{
    for (int i = 0; i < nl; i++) free(L[i].s);
    free(L);
    L = 0; nl = capl = 0;
}

static int load(const char *path)
{
    int h = d_open(path, 0);
    finalnl = 1;
    if (h < 0) { insert_line(0, "", 0); return doserr == 2 ? 0 : -1; }
    uint32_t size = d_filesize(h);
    char *b = malloc(size + 1);
    if (!b) { d_close(h); return -2; }
    uint32_t got = 0;
    while (got < size) {
        int n = d_read(h, b + got, size - got > 32768 ? 32768 : size - got);
        if (n <= 0) break;
        got += n;
    }
    d_close(h);
    if (got && b[got - 1] == 0x1A) got--;
    uint32_t s = 0;
    for (uint32_t i = 0; i <= got; i++) {
        if (i == got || b[i] == '\n') {
            if (i == got && s == got && nl) break;      /* text ended with a newline */
            int e = i;
            if (e > (int)s && b[e - 1] == '\r') e--;
            if (insert_line(nl, b + s, e - s)) { free(b); return -2; }
            s = i + 1;
        }
    }
    finalnl = got == 0 || b[got - 1] == '\n';
    free(b);
    if (!nl) insert_line(0, "", 0);
    return 0;
}

static int save(const char *path)
{
    int h = d_creat(path, 0x20);
    if (h < 0) { error_box("Cannot write the file", path); return -1; }
    for (int i = 0; i < nl; i++) {
        if (L[i].n && d_write(h, L[i].s, L[i].n) != L[i].n) goto full;
        if ((i < nl - 1 || finalnl) && d_write(h, "\r\n", 2) != 2) goto full;
    }
    d_close(h);
    modified = 0;
    return 0;
full:
    d_close(h);
    error_box("Disk full writing", path);
    return -1;
}

static int dcol(eline *l, int x)
{
    int c = 0;
    for (int i = 0; i < x && i < l->n; i++) c = l->s[i] == '\t' ? (c + 8) & ~7 : c + 1;
    return c;
}

static int xfromcol(eline *l, int col)
{
    int c = 0, i;
    for (i = 0; i < l->n; i++) {
        int nc = l->s[i] == '\t' ? (c + 8) & ~7 : c + 1;
        if (nc > col) break;
        c = nc;
    }
    return i;
}

static void draw(const char *title)
{
    fill(0, 0, SCR_W, 1, ' ', 0x30);
    putn(0, 0, title, 44, 0x30);
    char t[32], n[12];
    strcpy(t, "Line "); u2s(n, cy + 1); strcat(t, n);
    put(47, 0, t, 0x30);
    strcpy(t, "Col "); u2s(n, dcol(&L[cy], cx) + 1); strcat(t, n);
    put(59, 0, t, 0x30);
    if (modified) put(68, 0, "*", 0x30);
    put(72, 0, overwrite ? "Overwr" : "Insert", 0x30);
    for (int r = 0; r < EROWS; r++) {
        int y = 1 + r, i = top + r;
        fill(0, y, SCR_W, 1, ' ', A_PANEL);
        if (i >= nl) continue;
        eline *l = &L[i];
        int c = 0;
        for (int k = 0; k < l->n; k++) {
            unsigned char ch = l->s[k];
            if (ch == '\t') { c = (c + 8) & ~7; continue; }
            if (c >= left && c - left < SCR_W) putc_(c - left, y, ch, A_PANEL);
            c++;
        }
    }
    static const char *const kb[10] = { "Help", "Save", "", "", "", "", "Search", "", "", "Quit" };
    keybar_draw(kb);
    flush();
    cursor_at(dcol(&L[cy], cx) - left, 1 + cy - top);
}

static void fixview(void)
{
    if (cy >= nl) cy = nl - 1;
    if (cy < 0) cy = 0;
    if (cx > L[cy].n) cx = L[cy].n;
    if (cy < top) top = cy;
    if (cy >= top + EROWS) top = cy - EROWS + 1;
    int dc = dcol(&L[cy], cx);
    if (dc < left) left = dc - dc % 10;
    if (dc >= left + SCR_W) left = dc - SCR_W + 10;
}

static void help(void)
{
    static const char *const h[] = {
        "Arrows, Home, End, PgUp, PgDn    move",
        "Ctrl-PgUp / Ctrl-PgDn            start / end of the file",
        "Ctrl-Left / Ctrl-Right           word left / right",
        "Ins                              insert / overwrite",
        "Ctrl-Y                           delete the line",
        "F2                               save",
        "F7                               search",
        "Esc, F10                         quit",
    };
    int sel = 0;
    listbox("Editor keys", h, 8, 62, 12, &sel, &PAL_CYAN, 0);
}

static int lcase(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static void find(void)
{
    static char s[40];
    ditem it[] = {
        { DI_TEXT, 3, 2, 0, "Search for", 0, 0, 0 },
        { DI_INPUT, 3, 3, 34, 0, s, 40, 0 },
    };
    if (dialog("Search", 40, 6, &PAL_GREY, it, 2, 1) != 0 || !s[0]) return;
    int l = strlen(s);
    for (int pass = 0; pass < 2; pass++)
        for (int i = pass ? 0 : cy; i < nl; i++) {
            int from = (!pass && i == cy) ? cx + 1 : 0;
            for (int k = from; k + l <= L[i].n; k++) {
                int m = 0;
                while (m < l && lcase((uint8_t)L[i].s[k + m]) == lcase((uint8_t)s[m])) m++;
                if (m == l) { cy = i; cx = k; return; }
            }
        }
    static const char *const ok[] = { "OK" };
    message("Search", "String not found", s, &PAL_GREY, ok, 1);
}

void edit_file(const char *path)
{
    char title[90];
    strcpy(title, "Edit: ");
    strcat(title, path);
    str_lower(title + 6);
    int rc = load(path);
    if (rc < 0) {
        error_box(rc == -2 ? "Not enough memory to edit" : "Cannot open the file", path);
        free_all();
        return;
    }
    cx = cy = top = left = modified = overwrite = 0;
    uint16_t save_s[SCR_W * SCR_H];
    save_scr(save_s);
    void (*oldidle)(void) = idle_hook;
    idle_hook = 0;
    for (;;) {
        fixview();
        draw(title);
        int k = getkey();
        eline *l = &L[cy];
        int lo = k & 0xFF;
        if (k == K_MOUSE) {
            if (ms_y == KEYROW) k = K_F(ms_x / 8 + 1 > 10 ? 10 : ms_x / 8 + 1);
            else if (ms_y >= 1 && ms_y <= EROWS && top + ms_y - 1 < nl) {
                cy = top + ms_y - 1;
                cx = xfromcol(&L[cy], ms_x + left);
                continue;
            } else continue;
        }
        if (k == K_ESC || k == K_F(10)) {
            if (modified) {
                static const char *const b[] = { "Save", "Don't save", "Continue editing" };
                int r = message("Edit", "The file has been modified.", "Do you want to save it?", &PAL_GREY, b, 3);
                if (r == 0) { if (save(path)) continue; }
                else if (r != 1) continue;
            }
            break;
        }
        switch (k) {
        case K_F(1): help(); continue;
        case K_F(2): save(path); continue;
        case K_F(7): find(); continue;
        case K_UP: if (cy > 0) { int c = dcol(l, cx); cy--; cx = xfromcol(&L[cy], c); } continue;
        case K_DOWN: if (cy < nl - 1) { int c = dcol(l, cx); cy++; cx = xfromcol(&L[cy], c); } continue;
        case K_LEFT: if (cx > 0) cx--; else if (cy > 0) { cy--; cx = L[cy].n; } continue;
        case K_RIGHT: if (cx < l->n) cx++; else if (cy < nl - 1) { cy++; cx = 0; } continue;
        case K_HOME: cx = 0; continue;
        case K_END: cx = l->n; continue;
        case K_PGUP: { int c = dcol(l, cx); cy -= EROWS - 1; top -= EROWS - 1; if (top < 0) top = 0; if (cy < 0) cy = 0; cx = xfromcol(&L[cy], c); continue; }
        case K_PGDN: { int c = dcol(l, cx); cy += EROWS - 1; top += EROWS - 1; if (cy >= nl) cy = nl - 1; if (top > nl - 1) top = nl - 1; cx = xfromcol(&L[cy], c); continue; }
        case K_CPGUP: case K_CHOME: cy = 0; cx = 0; continue;
        case K_CPGDN: case K_CEND: cy = nl - 1; cx = L[cy].n; continue;
        case K_CLEFT:
            while (cx > 0 && l->s[cx - 1] == ' ') cx--;
            while (cx > 0 && l->s[cx - 1] != ' ') cx--;
            continue;
        case K_CRIGHT:
            while (cx < l->n && l->s[cx] != ' ') cx++;
            while (cx < l->n && l->s[cx] == ' ') cx++;
            continue;
        case K_INS: overwrite = !overwrite; continue;
        case K_ENTER:
            if (insert_line(cy + 1, l->s + cx, l->n - cx)) continue;
            L[cy].n = cx;
            cy++; cx = 0; modified = 1;
            continue;
        case K_BS:
            if (cx > 0) { memmove(l->s + cx - 1, l->s + cx, l->n - cx); l->n--; cx--; modified = 1; }
            else if (cy > 0) {
                eline *p = &L[cy - 1];
                if (ensure(p, p->n + l->n + 1)) continue;
                cx = p->n;
                memcpy(p->s + p->n, l->s, l->n);
                p->n += l->n;
                delete_line(cy);
                cy--; modified = 1;
            }
            continue;
        case K_DEL:
            if (cx < l->n) { memmove(l->s + cx, l->s + cx + 1, l->n - cx - 1); l->n--; modified = 1; }
            else if (cy < nl - 1) {
                eline *nx = &L[cy + 1];
                if (ensure(l, l->n + nx->n + 1)) continue;
                memcpy(l->s + l->n, nx->s, nx->n);
                l->n += nx->n;
                delete_line(cy + 1);
                modified = 1;
            }
            continue;
        }
        if (lo == 0x19 && (k >> 8) == 0x15) {           /* Ctrl-Y */
            if (nl > 1) delete_line(cy); else L[0].n = 0;
            cx = 0; modified = 1;
            continue;
        }
        if ((lo >= 0x20 || lo == '\t') && (k >> 8) && k < 0x10000 && lo != 0x7F) {
            if (overwrite && cx < l->n) { l->s[cx++] = lo; modified = 1; continue; }
            if (ensure(l, l->n + 2)) continue;
            memmove(l->s + cx + 1, l->s + cx, l->n - cx);
            l->s[cx++] = lo;
            l->n++;
            modified = 1;
        }
    }
    idle_hook = oldidle;
    free_all();
    restore_scr(save_s);
}
