/*
 * panel.c - the two panels: reading and sorting directories, drawing the
 * brief / full file lists, the info panel and the directory tree.
 */
#include <stdlib.h>
#include "acm.h"

panel P[2];

int list_rows(void) { return S->ministatus ? 18 : 20; }

int panel_per_page(panel *p)
{
    return list_rows() * (p->st->mode == M_BRIEF ? 3 : 1);
}

/* ---- reading ---- */
static int sortmode;

static int isdotdot(const fent *f) { return f->name[0] == '.' && f->name[1] == '.'; }

static int cmpname(const char *a, const char *b) { return strcmp(a, b); }

static int cmpext(const char *a, const char *b)
{
    const char *ea = strchr(a, '.'), *eb = strchr(b, '.');
    int r = strcmp(ea ? ea : "", eb ? eb : "");
    return r ? r : strcmp(a, b);
}

static int fcmp(const void *va, const void *vb)
{
    const fent *a = va, *b = vb;
    if (isdotdot(a)) return -1;
    if (isdotdot(b)) return 1;
    int da = (a->attr & 0x10) != 0, db = (b->attr & 0x10) != 0;
    if (da != db) return db - da;
    if (da) return sortmode == S_EXT ? cmpext(a->name, b->name) : cmpname(a->name, b->name);
    switch (sortmode) {
    case S_EXT: return cmpext(a->name, b->name);
    case S_TIME: {
        uint32_t ta = ((uint32_t)a->date << 16) | a->time, tb = ((uint32_t)b->date << 16) | b->time;
        if (ta != tb) return ta > tb ? -1 : 1;
        return cmpname(a->name, b->name);
    }
    case S_SIZE:
        if (a->size != b->size) return a->size > b->size ? -1 : 1;
        return cmpname(a->name, b->name);
    }
    return cmpname(a->name, b->name);
}

void panel_sort(panel *p)
{
    char keep[13];
    strcpy(keep, p->n ? p->f[p->st->cur < p->n ? p->st->cur : 0].name : "");
    sortmode = p->st->sort;
    if (sortmode == S_UNSORTED) {
        /* directory order, ".." first */
        for (int i = 1; i < p->n; i++)
            if (isdotdot(&p->f[i])) { fent t = p->f[i]; memmove(p->f + 1, p->f, i * sizeof(fent)); p->f[0] = t; }
    } else
        qsort(p->f, p->n, sizeof(fent), fcmp);
    panel_goto_name(p, keep);
}

void panel_selcount(panel *p)
{
    p->nsel = 0; p->selbytes = 0;
    for (int i = 0; i < p->n; i++)
        if (p->f[i].sel) { p->nsel++; p->selbytes += p->f[i].size; }
}

void panel_goto_name(panel *p, const char *name)
{
    for (int i = 0; i < p->n; i++)
        if (!strcmp(p->f[i].name, name)) { p->st->cur = i; panel_fix(p); return; }
    panel_fix(p);
}

void panel_fix(panel *p)
{
    int pp = panel_per_page(p);
    struct acpanel *s = p->st;
    if (s->cur >= p->n) s->cur = p->n - 1;
    if (s->cur < 0) s->cur = 0;
    if (s->top > s->cur) s->top = s->cur;
    if (s->cur >= s->top + pp) s->top = s->cur - pp + 1;
    if (s->top + pp > p->n) s->top = p->n - pp;
    if (s->top < 0) s->top = 0;
}

void panel_read(panel *p, const char *keep)
{
    struct dta d;
    char spec[96], keepname[13];
    int attrs = 0x10 | (S->hidden ? 0x06 : 0);
    strcpy(keepname, keep ? keep : "");
    if (p->st->mode == M_TREE) { tree_read(p); }
    p->n = 0;
    p->nfiles = 0; p->totbytes = 0;
    path_join(spec, p->st->path, "*.*");
    if (!d_findfirst(spec, attrs, &d)) do {
        if (d.name[0] == '.' && d.name[1] == 0) continue;
        if (d.attr & 0x08) continue;
        if (!(d.attr & 0x10)) {
            if (p->st->filter[0] && !wildmatch(p->st->filter, d.name)) continue;
            p->nfiles++;
            p->totbytes += d.size;
        }
        if (p->n >= p->cap) {
            int nc = p->cap ? p->cap * 2 : 128;
            fent *nf = realloc(p->f, nc * sizeof(fent));
            if (!nf) break;
            p->f = nf; p->cap = nc;
        }
        fent *f = &p->f[p->n++];
        memset(f, 0, sizeof *f);
        strcpy(f->name, d.name);
        f->attr = d.attr; f->size = d.attr & 0x10 ? 0 : d.size;
        f->date = d.date; f->time = d.time;
    } while (!d_findnext(&d));
    p->nsel = 0; p->selbytes = 0;
    if (!keepname[0]) strcpy(keepname, p->st->curname);
    p->st->cur = 0;
    sortmode = p->st->sort;
    panel_sort(p);
    p->st->cur = 0;
    panel_goto_name(p, keepname);
}

/* read again keeping the cursor and the selection */
void panel_reread(panel *p)
{
    char keep[13];
    int n = p->n;
    char (*sel)[13] = 0;
    int ns = 0;
    fent *c = panel_curfile(p);
    strcpy(keep, c ? c->name : "");
    if (p->nsel && (sel = malloc(p->nsel * 13))) {
        for (int i = 0; i < n; i++) if (p->f[i].sel) strcpy(sel[ns++], p->f[i].name);
    }
    int top = p->st->top;
    panel_read(p, keep);
    for (int j = 0; j < ns; j++)
        for (int i = 0; i < p->n; i++) if (!strcmp(p->f[i].name, sel[j])) p->f[i].sel = 1;
    free(sel);
    panel_selcount(p);
    p->st->top = top;
    panel_fix(p);
}

fent *panel_curfile(panel *p)
{
    if (p->st->mode == M_TREE || p->st->mode == M_INFO || !p->n) return 0;
    return &p->f[p->st->cur];
}

void panel_setpath(panel *p, const char *path, const char *keepname)
{
    strcpy(p->st->path, path);
    p->st->top = 0;
    p->st->curname[0] = 0;
    panel_read(p, keepname);
}

/* ---- movement ---- */
void panel_move(panel *p, int key)
{
    struct acpanel *s = p->st;
    int rows = list_rows(), pp = panel_per_page(p);
    if (s->mode == M_TREE) {
        int *c = &p->tcur;
        if (key == K_UP) (*c)--;
        else if (key == K_DOWN) (*c)++;
        else if (key == K_PGUP) *c -= rows - 1;
        else if (key == K_PGDN) *c += rows - 1;
        else if (key == K_HOME) *c = 0;
        else if (key == K_END) *c = p->tn - 1;
        if (*c >= p->tn) *c = p->tn - 1;
        if (*c < 0) *c = 0;
        return;
    }
    switch (key) {
    case K_UP: s->cur--; break;
    case K_DOWN: s->cur++; break;
    case K_LEFT: s->cur -= s->mode == M_BRIEF ? rows : pp - 1; break;
    case K_RIGHT: s->cur += s->mode == M_BRIEF ? rows : pp - 1; break;
    case K_PGUP: s->cur -= pp - 1; s->top -= pp - 1; break;
    case K_PGDN: s->cur += pp - 1; s->top += pp - 1; break;
    case K_HOME: s->cur = 0; break;
    case K_END: s->cur = p->n - 1; break;
    }
    if (s->top < 0) s->top = 0;
    panel_fix(p);
}

int panel_hit(panel *p, int x, int y)
{
    int rows = list_rows(), r = y - 2, rx = x - p->x;
    if (r < 0 || r >= rows || rx < 1 || rx > 38) return -1;
    if (p->st->mode == M_TREE) {
        int i = p->ttop + r;
        return i < p->tn ? i : -1;
    }
    if (p->st->mode == M_INFO) return -1;
    int i = p->st->top + r;
    if (p->st->mode == M_BRIEF) {
        int c = (rx - 1) / 13;
        if (c > 2) c = 2;
        i += c * rows;
    }
    return i < p->n ? i : -1;
}

/* ---- drawing ---- */
static void fmt_name(char *o, const fent *f)
{
    memset(o, ' ', 12);
    o[12] = 0;
    if (isdotdot(f)) { o[0] = o[1] = '.'; return; }
    const char *d = strchr(f->name, '.');
    int bl = d ? d - f->name : (int)strlen(f->name);
    memcpy(o, f->name, bl);
    if (d) memcpy(o + 9, d + 1, strlen(d + 1));
    if (!(f->attr & 0x10)) str_lower(o);
}

static void fmt_status(char *o, const fent *f)
{
    char t[16];
    memset(o, ' ', 38);
    o[38] = 0;
    fmt_name(t, f);
    memcpy(o, t, 12);
    if (f->attr & 0x10) memcpy(o + 13, isdotdot(f) ? "\x10UP--DIR\x11" : "\x10SUB-DIR\x11", 9);
    else { u2s_pad(t, f->size, 9); memcpy(o + 13, t, 9); }
    fmt_date(t, f->date); memcpy(o + 23, t, 8);
    fmt_time(t, f->time); memcpy(o + 32, t, 6);
}

static void draw_title(panel *p, const char *t, int active)
{
    char buf[40];
    int l = strlen(t);
    if (l > 34) { strcpy(buf, "..."); strcat(buf, t + l - 31); t = buf; l = 34; }
    int x = p->x + (40 - l - 2) / 2;
    int a = active ? A_CURSOR : A_PANEL;
    putc_(x, 0, ' ', a);
    put(x + 1, 0, t, a);
    putc_(x + l + 1, 0, ' ', a);
}

static void hsep(panel *p, int y, const int *cols, int nc)
{
    putc_(p->x, y, 0xC7, A_PANEL);
    fill(p->x + 1, y, 38, 1, 0xC4, A_PANEL);
    putc_(p->x + 39, y, 0xB6, A_PANEL);
    for (int i = 0; i < nc; i++) putc_(p->x + cols[i], y, 0xC1, A_PANEL);
}

static void draw_files(panel *p, int active)
{
    static const int bcols[] = { 13, 26 }, fcols[] = { 13, 23, 32 };
    int x = p->x, rows = list_rows(), full = p->st->mode == M_FULL;
    const int *cols = full ? fcols : bcols;
    int nc = full ? 3 : 2;
    char t[48];
    for (int i = 0; i < nc; i++)
        for (int y = 1; y < 2 + rows; y++) putc_(x + cols[i], y, 0xB3, A_PANEL);
    if (full) {
        put(x + 5, 1, "Name", A_HEAD); put(x + 16, 1, "Size", A_HEAD);
        put(x + 26, 1, "Date", A_HEAD); put(x + 34, 1, "Time", A_HEAD);
    } else {
        put(x + 5, 1, "Name", A_HEAD); put(x + 18, 1, "Name", A_HEAD); put(x + 31, 1, "Name", A_HEAD);
    }
    int pp = panel_per_page(p);
    for (int k = 0; k < pp; k++) {
        int i = p->st->top + k;
        if (i >= p->n) break;
        fent *f = &p->f[i];
        int cur = active && i == p->st->cur;
        int a = cur ? (f->sel ? A_CURSEL : A_CURSOR) : (f->sel ? A_SEL : A_PANEL);
        fmt_name(t, f);
        if (full) {
            int y = 2 + k;
            putn(x + 1, y, t, 12, a);
            if (f->attr & 0x10) putn(x + 14, y, isdotdot(f) ? "\x10UP--DIR\x11" : "\x10SUB-DIR\x11", 9, a);
            else { u2s_pad(t, f->size, 9); putn(x + 14, y, t, 9, a); }
            fmt_date(t, f->date); putn(x + 24, y, t, 8, a);
            fmt_time(t, f->time); putn(x + 33, y, t, 6, a);
            if (cur) for (int c = 0; c < 3; c++) setattr(x + fcols[c], y, 1, A_CURSOR);
        } else {
            int col = k / rows, y = 2 + k % rows;
            putn(x + 1 + col * 13, y, t, 12, a);
        }
    }
    if (S->ministatus) {
        hsep(p, 2 + rows, cols, nc);
        fent *f = panel_curfile(p);
        if (f) { fmt_status(t, f); putn(x + 1, 3 + rows, t, 38, A_PANEL); }
    }
    if (p->nsel) {
        char n1[16], n2[16];
        commas(n1, p->selbytes);
        u2s(n2, p->nsel);
        strcpy(t, " "); strcat(t, n1); strcat(t, " bytes in "); strcat(t, n2);
        strcat(t, p->nsel == 1 ? " selected file " : " selected files ");
        int l = strlen(t);
        if (l > 38) l = 38;
        int y = S->ministatus ? 2 + rows : 22;
        putn(x + (40 - l) / 2, y, t, l, A_SEL);
    }
}

static void info_line(int x, int y, const char *num, const char *text)
{
    put(x, y, num, A_HEAD);
    put(x + strlen(num), y, text, A_PANEL);
}

static void numfield(char *t, uint32_t v)
{
    char n[16];
    commas(n, v);
    int l = strlen(n);
    memset(t, ' ', 11 - l);
    strcpy(t + 11 - l, n);
}

static void draw_info(panel *p)
{
    int x = p->x;
    panel *o = &P[p == &P[0]];
    const char *path = o->st->mode == M_INFO || !o->st->visible ? p->st->path : o->st->path;
    int drive = path[0] - 'A';
    char t[64], n[16];
    uint32_t tot, fr;
    put(x + 4, 2, "The ARM Commander, Version 1.0", A_PANEL);
    put(x + 5, 3, "(C) 1989 Europa Micro Systems", A_PANEL);
    putc_(x, 4, 0xC7, A_PANEL); fill(x + 1, 4, 38, 1, 0xC4, A_PANEL); putc_(x + 39, 4, 0xB6, A_PANEL);
    struct armregs r = {0};
    _armdos_intr(0x12, &r);
    numfield(t, (r.r0 & 0xFFFF) * 1024u);
    info_line(x + 2, 6, t, " Bytes Memory");
    numfield(t, mem_free_for_child());
    info_line(x + 2, 7, t, " Bytes Free");
    if (!d_diskfree(drive, &tot, &fr)) {
        numfield(t, tot);
        info_line(x + 2, 9, t, " total bytes on drive ");
        putc_(x + 35, 9, path[0], A_PANEL); putc_(x + 36, 9, ':', A_PANEL);
        numfield(t, fr);
        info_line(x + 2, 10, t, " bytes free on drive ");
        putc_(x + 34, 10, path[0], A_PANEL); putc_(x + 35, 10, ':', A_PANEL);
    } else
        put(x + 4, 9, "Drive not ready", A_PANEL);
    putc_(x, 12, 0xC7, A_PANEL); fill(x + 1, 12, 38, 1, 0xC4, A_PANEL); putc_(x + 39, 12, 0xB6, A_PANEL);
    if (o->st->mode == M_BRIEF || o->st->mode == M_FULL) {
        u2s(n, o->nfiles);
        strcpy(t, n); strcat(t, o->nfiles == 1 ? " file uses " : " files use ");
        commas(n, o->totbytes); strcat(t, n); strcat(t, " bytes");
        put(x + 3, 14, t, A_PANEL);
        strcpy(t, "in "); strcat(t, path);
        putn(x + 3, 15, t, 35, A_PANEL);
    }
}

/* ---- tree ---- */
static int tree_add(panel *p, const char *name, int depth, int parent)
{
    if (p->tn >= p->tcap) {
        int nc = p->tcap ? p->tcap * 2 : 64;
        tent *nt = realloc(p->t, nc * sizeof(tent));
        if (!nt) return -1;
        p->t = nt; p->tcap = nc;
    }
    tent *t = &p->t[p->tn];
    memset(t, 0, sizeof *t);
    strcpy(t->name, name);
    t->depth = depth; t->parent = parent;
    return p->tn++;
}

static int ncmp(const void *a, const void *b) { return strcmp(a, b); }

static void tree_scan(panel *p, char *path, int depth, int parent)
{
    struct dta d;
    char (*names)[13] = malloc(128 * 13);
    int n = 0, l = strlen(path);
    if (!names || depth > 20) { free(names); return; }
    path_join(path, path, "*.*");
    if (!d_findfirst(path, 0x16, &d)) do {
        if ((d.attr & 0x10) && d.name[0] != '.' && n < 128) strcpy(names[n++], d.name);
    } while (!d_findnext(&d));
    path[l] = 0;
    qsort(names, n, 13, ncmp);
    int lastidx = -1;
    for (int i = 0; i < n; i++) {
        int idx = tree_add(p, names[i], depth + 1, parent);
        if (idx < 0) break;
        lastidx = idx;
        path_join(path, path, names[i]);
        tree_scan(p, path, depth + 1, idx);
        path[l] = 0;
        if (l == 3) path[2] = '\\';
    }
    if (lastidx >= 0) p->t[lastidx].last = 1;
    free(names);
}

void tree_read(panel *p)
{
    char path[128];
    p->tn = 0;
    path[0] = p->st->path[0]; path[1] = ':'; path[2] = '\\'; path[3] = 0;
    tree_add(p, "\\", 0, -1);
    p->t[0].last = 1;
    tree_scan(p, path, 0, 0);
    p->treedrive = p->st->path[0];
    tree_goto_path(p, p->st->path);
}

void tree_path(panel *p, int i, char *out)
{
    int chain[24], n = 0;
    for (int j = i; j > 0 && n < 24; j = p->t[j].parent) chain[n++] = j;
    out[0] = p->treedrive; out[1] = ':'; out[2] = '\\'; out[3] = 0;
    while (n--) {
        if (out[3]) strcat(out, "\\");
        strcat(out, p->t[chain[n]].name);
    }
}

void tree_goto_path(panel *p, const char *path)
{
    char t[128];
    for (int i = 0; i < p->tn; i++) {
        tree_path(p, i, t);
        if (!strcmp(t, path)) { p->tcur = i; return; }
    }
}

static void draw_tree(panel *p, int active)
{
    int x = p->x, rows = list_rows() + (S->ministatus ? 0 : 0);
    char t[128];
    if (p->tcur < p->ttop) p->ttop = p->tcur;
    if (p->tcur >= p->ttop + rows) p->ttop = p->tcur - rows + 1;
    for (int k = 0; k < rows && p->ttop + k < p->tn; k++) {
        int i = p->ttop + k, y = 2 + k;
        tent *e = &p->t[i];
        int nx;
        if (e->depth == 0) { nx = x + 1; }
        else {
            /* continuation lines of the ancestors */
            int j = e->parent;
            while (j > 0) {
                tent *a = &p->t[j];
                if (!a->last) putc_(x + 2 + 3 * (a->depth - 1), y, 0xB3, A_PANEL);
                j = a->parent;
            }
            int cx = x + 2 + 3 * (e->depth - 1);
            putc_(cx, y, e->last ? 0xC0 : 0xC3, A_PANEL);
            putc_(cx + 1, y, 0xC4, A_PANEL); putc_(cx + 2, y, 0xC4, A_PANEL);
            nx = cx + 3;
        }
        int l = strlen(e->name);
        if (nx + l > x + 39) l = x + 39 - nx;
        putn(nx, y, e->name, l, i == p->tcur && active ? A_CURSOR : A_PANEL);
    }
    if (S->ministatus) {
        hsep(p, 2 + rows, 0, 0);
        tree_path(p, p->tcur, t);
        putn(x + 1, 3 + rows, t, 38, A_PANEL);
    }
}

void panel_draw(panel *p, int active)
{
    if (!p->st->visible) return;
    int x = p->x;
    fill(x, 0, 40, 23, ' ', A_PANEL);
    box(x, 0, 40, 23, A_PANEL, 1);
    switch (p->st->mode) {
    case M_BRIEF: case M_FULL:
        draw_files(p, active);
        draw_title(p, p->st->path, active);
        break;
    case M_INFO:
        draw_info(p);
        draw_title(p, "Info", active);
        break;
    case M_TREE:
        draw_tree(p, active);
        draw_title(p, "Tree", active);
        break;
    }
}
