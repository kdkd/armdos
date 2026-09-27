/*
 * fs.c - the File System: drive letters, directory tree and file list in the
 * single, multiple and system file list arrangements, the keys and the mouse,
 * and the File / Options / Arrange / Exit action bar.
 */
#include <dos.h>
#include "fs.h"

struct pane panes[2];
int active, focus;
struct fsopts fso = { AR_SINGLE, SORT_NAME, "*.*", 1, 1, 0 };
char (*selset)[80];
int nsel;
static int ndrives;
static char drives[26];
static int bar_active = -1;
static int from_sp;
static int fs_layer;
static void reading_box(void);
extern int layer_count(void);
static int in_menu;           /* the action bar, or an action chosen from it, is running */
#define FS_TOP() (!in_menu)

struct fsstate {
    uint8_t valid, arrange, sort, focus, active, cd, ca, cr;
    char mask[13];
    uint8_t drive[2];
    char cwd[2][68];
    char curname[2][13];
};

_Static_assert(sizeof(struct fsstate) <= sizeof(((struct savestate *)0)->fs), "fsstate must fit st.fs");

/* ------------------------------------------------------------ selection -- */
int sel_has(const char *path)
{
    for (int i = 0; i < nsel; i++) if (!strcmp(selset[i], path)) return 1;
    return 0;
}

void sel_toggle(const char *path)
{
    for (int i = 0; i < nsel; i++)
        if (!strcmp(selset[i], path)) {
            memmove(selset[i], selset[i + 1], (nsel - i - 1) * sizeof selset[0]);
            nsel--;
            return;
        }
    if (nsel < MAXSEL) strcpy(selset[nsel++], path);
}

void sel_clear(void) { nsel = 0; }

void dir_path(struct pane *p, int d, char *out)
{
    int chain[40], n = 0;
    for (int k = d; k > 0 && n < 40; k = p->dirs[k].parent) chain[n++] = k;
    out[0] = 'A' + p->drive;
    out[1] = ':';
    out[2] = '\\';
    int l = 3;
    for (int i = n - 1; i >= 0; i--) {
        int len = strlen(p->dirs[chain[i]].name);
        if (l + len + 2 > 78) break;
        memcpy(out + l, p->dirs[chain[i]].name, len);
        l += len;
        if (i) out[l++] = '\\';
    }
    out[l] = 0;
}

void file_path(struct pane *p, int i, char *out)
{
    dir_path(p, p->files[i].dir, out);
    int l = strlen(out);
    if (out[l - 1] != '\\') out[l++] = '\\';
    strcpy(out + l, p->files[i].name);
}

/* -------------------------------------------------------------- reading -- */
static void find_drives(void)
{
    ndrives = 0;
    for (int d = 1; d <= 26; d++) {
        struct armregs r = {0};
        r.r0 = 0x4408;
        r.r1 = d;
        _armdos_int21(&r);
        if ((r.cpsr & ARM_CPSR_C) && (r.r0 & 0xFFFF) == 0x0F) continue;
        drives[ndrives++] = d - 1;
    }
}

static int scan_dir(struct pane *p, int parent, char *path, int depth)
{
    struct find_t f;
    int l = strlen(path);
    int first = p->ndirs;
    strcpy(path + l, "*.*");
    unsigned rc = _dos_findfirst(path, 0x16, &f);
    path[l] = 0;
    /* collect this directory's subdirectories, then sort them, then recurse */
    while (!rc) {
        if ((f.attrib & 0x10) && f.name[0] != '.' && p->ndirs < MAXDIRS) {
            struct dnode *n = &p->dirs[p->ndirs++];
            strcpy(n->name, f.name);
            n->parent = parent;
            n->depth = depth;
            n->last = 0;
        } else if (!(f.attrib & 0x18)) {
            p->dirs[parent].nfiles++;
            p->dirs[parent].bytes += f.size;
            p->disk_files++;
            p->disk_bytes += f.size;
        }
        rc = _dos_findnext(&f);
    }
    int cnt = p->ndirs - first;
    for (int i = 0; i < cnt; i++)       /* insertion sort by name */
        for (int j = i; j > 0 && strcmp(p->dirs[first + j - 1].name, p->dirs[first + j].name) > 0; j--) {
            struct dnode t = p->dirs[first + j];
            p->dirs[first + j] = p->dirs[first + j - 1];
            p->dirs[first + j - 1] = t;
        }
    if (!cnt) return 0;
    /* pre-order: move the children one by one to the end with their subtrees */
    struct dnode kids[64];
    int nk = cnt > 64 ? 64 : cnt;
    memcpy(kids, &p->dirs[first], nk * sizeof kids[0]);
    p->ndirs = first;
    for (int i = 0; i < nk; i++) {
        if (p->ndirs >= MAXDIRS) break;
        int me = p->ndirs++;
        p->dirs[me] = kids[i];
        p->dirs[me].last = i == nk - 1;
        p->dirs[me].nfiles = 0;
        p->dirs[me].bytes = 0;
        int pl = strlen(path);
        strcpy(path + pl, kids[i].name);
        strcat(path, "\\");
        if (depth < 30) scan_dir(p, me, path, depth + 1);
        path[pl] = 0;
    }
    return cnt;
}

void pane_load(struct pane *p, int drive)
{
    char path[160];
    if (!p->dirs) p->dirs = malloc(MAXDIRS * sizeof *p->dirs);
    if (!p->files) p->files = malloc(MAXFILES * sizeof *p->files);
    p->drive = drive;
    p->ndirs = 1;
    memset(&p->dirs[0], 0, sizeof p->dirs[0]);
    p->dirs[0].name[0] = 0;
    p->dirs[0].parent = -1;
    p->dirs[0].last = 1;
    p->disk_files = p->disk_bytes = 0;
    p->cur = p->tcur = p->ttop = 0;
    p->fcur = p->ftop = 0;
    path[0] = 'A' + drive;
    path[1] = ':';
    path[2] = '\\';
    path[3] = 0;
    crit_error = -1;
    scan_dir(p, 0, path, 1);
    p->loaded = 1;
}

static int wild_match(const char *mask, const char *name)
{
    /* DOS-style: name and extension parts, * and ? */
    char mn[9], me[4], nn[9], ne[4];
    const char *d;
    for (int k = 0; k < 2; k++) {
        const char *s = k ? name : mask;
        char *b = k ? nn : mn, *e = k ? ne : me;
        d = strchr(s, '.');
        int i = 0;
        for (const char *q = s; *q && q != d && i < 8; q++) b[i++] = upc(*q);
        b[i] = 0;
        i = 0;
        if (d) for (const char *q = d + 1; *q && i < 3; q++) e[i++] = upc(*q);
        e[i] = 0;
    }
    for (int part = 0; part < 2; part++) {
        const char *m = part ? me : mn, *n = part ? ne : nn;
        int len = part ? 3 : 8;
        for (int i = 0; i < len; i++) {
            if (*m == '*') break;
            char mc = *m ? *m : ' ', nc = *n ? *n : ' ';
            if (mc != '?' && mc != nc) return 0;
            if (*m) m++;
            if (*n) n++;
        }
    }
    return 1;
}

static int cmp_fent(const struct fent *a, const struct fent *b)
{
    const char *ea = strchr(a->name, '.'), *eb = strchr(b->name, '.');
    char na[9], nb[9];
    int la = ea ? ea - a->name : (int)strlen(a->name), lb = eb ? eb - b->name : (int)strlen(b->name);
    memcpy(na, a->name, la); na[la] = 0;
    memcpy(nb, b->name, lb); nb[lb] = 0;
    int c;
    switch (fso.sort) {
    case SORT_EXT:
        c = strcmp(ea ? ea : "", eb ? eb : "");
        if (c) return c;
        return strcmp(na, nb);
    case SORT_DATE:                     /* newest first (the date only), then the last on disk first */
        if (a->date != b->date) return a->date > b->date ? -1 : 1;
        return (int)b->order - (int)a->order;
    case SORT_SIZE:
        if (a->size != b->size) return a->size > b->size ? -1 : 1;
        return (int)b->order - (int)a->order;
    case SORT_DISK:
        return (int)a->order - (int)b->order;
    }
    c = strcmp(na, nb);
    if (c) return c;
    return strcmp(ea ? ea : "", eb ? eb : "");
}

static void sort_files(struct pane *p)
{
    /* shell sort */
    for (int gap = p->nfiles / 2; gap > 0; gap /= 2)
        for (int i = gap; i < p->nfiles; i++)
            for (int j = i - gap; j >= 0 && cmp_fent(&p->files[j], &p->files[j + gap]) > 0; j -= gap) {
                struct fent t = p->files[j];
                p->files[j] = p->files[j + gap];
                p->files[j + gap] = t;
            }
}

static void add_files(struct pane *p, int d)
{
    char path[160];
    struct find_t f;
    dir_path(p, d, path);
    int l = strlen(path);
    if (path[l - 1] != '\\') path[l++] = '\\';
    strcpy(path + l, "*.*");
    unsigned rc = _dos_findfirst(path, 0x07, &f);
    int order = 0;
    while (!rc && p->nfiles < MAXFILES) {
        if (!(f.attrib & 0x18) && wild_match(fso.mask, f.name)) {
            struct fent *e = &p->files[p->nfiles++];
            strcpy(e->name, f.name);
            e->attr = f.attrib;
            e->date = f.wr_date;
            e->time = f.wr_time;
            e->size = f.size;
            e->dir = d;
            e->order = order;
        }
        order++;
        rc = _dos_findnext(&f);
    }
}

void pane_files(struct pane *p)
{
    p->nfiles = 0;
    if (fso.arrange == AR_SYSTEM) {
        for (int d = 0; d < p->ndirs; d++) add_files(p, d);
    } else
        add_files(p, p->cur);
    sort_files(p);
    if (p->fcur >= p->nfiles) p->fcur = p->nfiles ? p->nfiles - 1 : 0;
    if (p->ftop > p->fcur) p->ftop = p->fcur;
}

int pane_find_dir(struct pane *p, const char *path)
{
    char t[80];
    for (int d = 0; d < p->ndirs; d++) {
        dir_path(p, d, t);
        if (!strcasecmp(t, path)) return d;
    }
    return 0;
}

static void set_dir(struct pane *p, int d)
{
    char path[80];
    p->tcur = d;
    if (d == p->cur && p->nfiles) return;
    if (!fso.across) sel_clear();
    p->cur = d;
    p->fcur = p->ftop = 0;
    pane_files(p);
    dir_path(p, d, path);
    if (p == &panes[active]) {
        dos_setdrive(p->drive);
        dos_chdir(path);
    }
}

static int select_drive(struct pane *p, int drive)
{
    for (;;) {
        crit_error = -1;
        char root[4] = { 'A' + drive, ':', '\\', 0 };
        struct armregs r = {0};
        r.r0 = 0x3600;              /* get free space: touches the disk */
        r.r3 = drive + 1;
        _armdos_int21(&r);
        if ((r.r0 & 0xFFFF) != 0xFFFF && crit_error < 0) {
            (void)root;
            break;
        }
        int c = warn_choice(crit_error == 2 || crit_error < 0 ? "Drive not ready." : dos_errtext(crit_error),
                            "Try to read this disk again", "Do not try to read disk again", H_MESSAGE);
        if (c != 1) return -1;
    }
    if (!fso.across) sel_clear();
    reading_box();
    pane_load(p, drive);
    pane_files(p);
    if (p == &panes[active]) {
        dos_setdrive(drive);
        dos_chdir("\\");
    }
    return 0;
}

void fs_refresh(void)
{
    for (int k = 0; k < (fso.arrange == AR_MULTIPLE ? 2 : 1); k++) {
        struct pane *p = &panes[k];
        char cur[80], name[13] = "";
        dir_path(p, p->cur, cur);
        if (p->nfiles) strcpy(name, p->files[p->fcur].name);
        int fcur = p->fcur;
        pane_load(p, p->drive);
        p->cur = p->tcur = pane_find_dir(p, cur);
        pane_files(p);
        p->fcur = fcur < p->nfiles ? fcur : (p->nfiles ? p->nfiles - 1 : 0);
        for (int i = 0; i < p->nfiles; i++) if (!strcmp(p->files[i].name, name)) p->fcur = i;
        if (p->ftop > p->fcur) p->ftop = p->fcur;
    }
    /* drop selections of files that are gone */
    for (int i = 0; i < nsel; ) {
        struct armregs r = {0};
        r.r0 = 0x4300;
        r.r3 = (uint32_t)selset[i];
        if (_armdos_int21(&r)) {
            memmove(selset[i], selset[i + 1], (nsel - i - 1) * sizeof selset[0]);
            nsel--;
        } else i++;
    }
}

/* -------------------------------------------------------------- drawing -- */
struct geom {               /* where one pane is drawn */
    int drow;               /* drive letter row */
    int hrow, top, rows;    /* heading row, first list row, list rows */
    int split;              /* column of the │ between tree and list */
    int tree_head, tmore;   /* "Directory Tree" col, tree More: col */
    int mask_center0, mask_w;
    int name_col, ext_col, size_end, date_col, bar_end, mark_col;
};

static const struct geom g_single = { 3, 7, 8, 15, 34, 8, 25, 35, 44, 38, 46, 62, 67, 75, 36 };
static const struct geom g_multi[2] = {
    { 3, 7, 8, 5, 38, 11, 29, 39, 40, 43, 51, 65, 68, 76, 41 },
    { 14, 16, 17, 6, 38, 11, 29, 39, 40, 43, 51, 65, 68, 76, 41 },
};

static void put_more(int r, int c, int up, int dn, int at, int arrow)
{
    s_put(r, c, "More:", at);
    if (up) s_ch(r, c + 5, G_UP, arrow);
    if (dn) s_ch(r, c + 7, G_DN, arrow);
}

static void draw_drives(int r, int pi)
{
    struct pane *p = &panes[pi];
    int foc = focus == 0 && pi == active && FS_TOP();
    for (int i = 0; i < ndrives; i++) {
        int c = 2 + 3 * i;
        if (foc && drives[i] == p->drive) s_fill(r, c - 1, 1, 3, ' ', FSC(2));
        s_ch(r, c, 'A' + drives[i], foc && drives[i] == p->drive ? FSC(2) : FSC(1));
    }
}

static void draw_tree(const struct geom *g, int pi)
{
    struct pane *p = &panes[pi];
    int foc = focus == 1 && pi == active && FS_TOP();
    int fr = FSC(0), tx = FSC(1);
    s_put(g->hrow, g->tree_head, "Directory Tree", fr);
    put_more(g->hrow, g->tmore, p->tcur > 0, p->tcur < p->ndirs - 1, fr, scheme == 0 ? 0x71 : FSC(1));
    if (p->tcur < p->ttop) p->ttop = p->tcur;
    if (p->tcur >= p->ttop + g->rows) p->ttop = p->tcur - g->rows + 1;
    for (int i = 0; i < g->rows; i++) {
        int d = p->ttop + i, r = g->top + i;
        if (d >= p->ndirs) break;
        struct dnode *n = &p->dirs[d];
        int c;
        if (d == p->cur) s_ch(r, 1, G_TRI, tx);
        if (d == 0) {
            char root[4] = { 'A' + p->drive, ':', '\\', 0 };
            c = 3;
            s_put(r, c, root, (foc && d == p->tcur) ? FSC(2) : tx);
            continue;
        }
        /* the vertical lines of the ancestors that have siblings after them */
        int anc = n->parent;
        for (int lv = n->depth - 1; lv >= 1; lv--) {
            if (anc > 0 && !p->dirs[anc].last) s_ch(r, 3 + 2 * (lv - 1), B_V, tx);
            anc = p->dirs[anc].parent;
        }
        c = 3 + 2 * (n->depth - 1);
        if (c + 2 >= g->split) continue;
        s_ch(r, c, n->last ? B_BL : B_LT, tx);
        s_ch(r, c + 1, B_H, tx);
        int nl = strlen(n->name);
        for (int k = 0; k < nl && c + 2 + k < g->split; k++)
            s_ch(r, c + 2 + k, (uint8_t)n->name[k], (foc && d == p->tcur) ? FSC(2) : tx);
    }
}

static void fmt_date(char *b, uint16_t d)
{
    int y = ((d >> 9) + 80) % 100, m = (d >> 5) & 15, dd = d & 31;
    b[0] = '0' + m / 10; b[1] = '0' + m % 10; b[2] = '-';
    b[3] = '0' + dd / 10; b[4] = '0' + dd % 10; b[5] = '-';
    b[6] = '0' + y / 10; b[7] = '0' + y % 10; b[8] = 0;
}

static void fmt_time(char *b, uint16_t t)
{
    int h = t >> 11, m = (t >> 5) & 63, pm = h >= 12;
    h %= 12;
    if (!h) h = 12;
    b[0] = h >= 10 ? '1' : ' ';
    b[1] = '0' + h % 10;
    b[2] = ':';
    b[3] = '0' + m / 10;
    b[4] = '0' + m % 10;
    b[5] = pm ? 'p' : 'a';
    b[6] = 'm';
    b[7] = 0;
}

static void draw_list(const struct geom *g, int pi, int system)
{
    struct pane *p = &panes[pi];
    int foc = focus == 2 && pi == active && FS_TOP();
    int fr = FSC(0), tx = FSC(1);
    int ml = strlen(fso.mask);
    s_put(g->hrow, system ? 27 + (52 - ml) / 2 : g->mask_center0 + (g->mask_w - ml + (g == &g_single ? 0 : 1)) / 2,
          fso.mask, fr);
    put_more(g->hrow, 70, p->fcur > 0, p->fcur < p->nfiles - 1, fr, scheme == 0 ? 0x71 : tx);
    if (!p->nfiles) {
        const char *m = strcmp(fso.mask, "*.*") ? "No files match file specifier" : "No files in selected directory";
        s_put(g->top, system ? 30 : g->name_col, m, tx);
        return;
    }
    if (p->fcur < p->ftop) p->ftop = p->fcur;
    if (p->fcur >= p->ftop + g->rows) p->ftop = p->fcur - g->rows + 1;
    for (int i = 0; i < g->rows; i++) {
        int k = p->ftop + i, r = g->top + i;
        if (k >= p->nfiles) break;
        struct fent *e = &p->files[k];
        char full[96], b[16], nm[9], ex[5];
        file_path(p, k, full);
        int at = (foc && k == p->fcur) ? FSC(2) : tx;
        int nc = system ? 30 : g->name_col, ec = system ? 38 : g->ext_col;
        int se = system ? 54 : g->size_end, dc = system ? 58 : g->date_col;
        int be = system ? 76 : g->bar_end;
        if (sel_has(full)) s_ch(r, system ? 28 : g->mark_col, G_TRI, tx);
        if (at != tx) s_fill(r, nc, 1, be - nc + 1, ' ', at);
        const char *d = strchr(e->name, '.');
        int nl = d ? d - e->name : (int)strlen(e->name);
        memcpy(nm, e->name, nl);
        nm[nl] = 0;
        s_putn(r, nc, nm, 8, at);
        if (d) { strncpy(ex, d, 4); ex[4] = 0; s_put(r, ec, ex, at); }
        fmt_num(b, e->size, 1);
        s_put(r, se + 1 - strlen(b), b, at);
        fmt_date(b, e->date);
        s_put(r, dc, b, at);
        if (system) {
            fmt_time(b, e->time);
            s_put(r, 69, b, at);
        }
    }
}

void fs_info_block(int r, int c, int attr, int ncol, int vcol)
{
    /* the File / Selected / Directory / Disk block (system file list, Show Information) */
    struct pane *p = &panes[active];
    char b[20];
    uint32_t selbytes;
    int nselc = fs_selected_count(&selbytes);
    s_put(r, c, "File", attr);
    s_put(r + 1, c + 2, "Name  : ", attr);
    s_put(r + 2, c + 2, "Attr  : ", attr);
    if (p->nfiles) {
        struct fent *e = &p->files[p->fcur];
        s_put(r + 1, vcol, e->name, attr);
        char a[5] = { e->attr & 2 ? 'h' : '.', e->attr & 1 ? 'r' : '.', e->attr & 4 ? 's' : '.', e->attr & 0x20 ? 'a' : '.', 0 };
        s_put(r + 2, vcol, a, attr);
    }
    s_put(r + 3, c, "Selected", attr);
    s_ch(r + 3, ncol, 'A' + p->drive, attr);
    s_put(r + 4, c + 2, "Number:", attr);
    fmt_num(b, nselc, 1);
    s_put(r + 4, ncol + 1 - strlen(b), b, attr);
    s_put(r + 5, c + 2, "Size  :", attr);
    fmt_num(b, selbytes, 1);
    s_put(r + 5, ncol + 1 - strlen(b), b, attr);
    /* the directory: the current one, or the highlighted file's in the system file list */
    int dir = p->cur;
    if (fso.arrange == AR_SYSTEM && p->nfiles) dir = p->files[p->fcur].dir;
    s_put(r + 6, c, "Directory", attr);
    s_put(r + 7, c + 2, "Name  : ", attr);
    s_put(r + 7, vcol, dir ? p->dirs[dir].name : "ROOT", attr);
    s_put(r + 8, c + 2, "Size  :", attr);
    fmt_num(b, p->dirs[dir].bytes, 1);
    s_put(r + 8, ncol + 1 - strlen(b), b, attr);
    s_put(r + 9, c + 2, "Files :", attr);
    fmt_num(b, p->dirs[dir].nfiles, 1);
    s_put(r + 9, ncol + 1 - strlen(b), b, attr);
    s_put(r + 10, c, "Disk", attr);
    s_put(r + 11, c + 2, "Name  :", attr);
    {
        struct find_t f;
        char q[8] = { 'A' + p->drive, ':', '\\', '*', '.', '*', 0 };
        if (!_dos_findfirst(q, 0x08, &f)) {
            char *dot = strchr(f.name, '.');
            if (dot) memmove(dot, dot + 1, strlen(dot));
            s_put(r + 11, vcol, f.name, attr);
        }
    }
    struct armregs x = {0};
    x.r0 = 0x3600;
    x.r3 = p->drive + 1;
    _armdos_int21(&x);
    uint32_t spc = x.r0 & 0xFFFF, avail = x.r1 & 0xFFFF, bps = x.r2 & 0xFFFF, total = x.r3 & 0xFFFF;
    if (spc == 0xFFFF) spc = avail = total = bps = 0;
    s_put(r + 12, c + 2, "Size  :", attr);
    fmt_num(b, spc * bps * total, 1);
    s_put(r + 12, ncol + 1 - strlen(b), b, attr);
    s_put(r + 13, c + 2, "Avail :", attr);
    fmt_num(b, spc * bps * avail, 1);
    s_put(r + 13, ncol + 1 - strlen(b), b, attr);
    s_put(r + 14, c + 2, "Files :", attr);
    fmt_num(b, p->disk_files, 1);
    s_put(r + 14, ncol + 1 - strlen(b), b, attr);
    s_put(r + 15, c + 2, "Dirs  :", attr);
    fmt_num(b, p->ndirs, 1);
    s_put(r + 15, ncol + 1 - strlen(b), b, attr);
}

static const struct mitem file_items[] = {
    { "Open (start)...", 0, 0 }, { "Print...", 0, 0 }, { "Associate...", 0, 0 }, { 0, 0, 0 },
    { "Move...", 0, 0 }, { "Copy...", 0, 0 }, { "Delete...", 0, 0 }, { "Rename...", 0, 0 },
    { "Change attribute...", 1, 0 }, { "View", 0, 0 }, { 0, 0, 0 },
    { "Create directory...", 2, 0 }, { "Select all", 0, 0 }, { "Deselect all", 4, 0 },
};
static const struct mitem opt_items[] = {
    { "Display options...", 0, 0 }, { "File options...", 0, 0 }, { "Show information...", 0, 0 },
};
static const struct mitem arr_items[] = {
    { "Single file list", 0, 0 }, { "Multiple file list", 0, 0 }, { "System file list", 3, 0 },
};
static const struct mitem fsexit_items[] = {
    { "Exit File System", 1, "F3" }, { "Resume File System", 0, 0 },
};
static const struct menu fs_menus[] = {
    { "File", 0, 3, 24, file_items, 14 },
    { "Options", 0, 9, 25, opt_items, 3 },
    { "Arrange", 0, 18, 24, arr_items, 3 },
    { "Exit", 1, 27, 29, fsexit_items, 2 },
};

static const char fs_fkeys[] = "  F10=Actions Shift+F9=Command Prompt";

static void fs_draw(void *ctx)
{
    (void)ctx;
    int fr = FSC(0);
    char path[80];
    title_mode = 1;
    draw_title("File System");
    draw_actionbar(fs_menus, 4, bar_active, -1);
    s_fill(2, 0, 22, COLS, ' ', fr);
    s_box(2, 0, 22, COLS, fr);
    s_hdiv(4, 0, COLS, fr);
    s_hdiv(6, 0, COLS, fr);
    draw_drives(3, fso.arrange == AR_MULTIPLE ? 0 : active);
    dir_path(&panes[active], panes[active].cur, path);
    s_put(5, 2, path, FSC(1));
    if (fso.arrange == AR_SINGLE) {
        s_ch(6, 34, B_TT, fr);
        for (int r = 7; r < 23; r++) s_ch(r, 34, B_V, fr);
        s_ch(23, 34, B_BT, fr);
        draw_tree(&g_single, 0);
        draw_list(&g_single, 0, 0);
    } else if (fso.arrange == AR_MULTIPLE) {
        s_hdiv(13, 0, COLS, fr);
        s_hdiv(15, 0, COLS, fr);
        for (int k = 0; k < 2; k++) {
            const struct geom *g = &g_multi[k];
            s_ch(g->hrow - 1, 38, B_TT, fr);
            for (int r = g->hrow; r < g->top + g->rows; r++) s_ch(r, 38, B_V, fr);
            draw_tree(g, k);
            draw_list(g, k, 0);
        }
        s_ch(13, 38, B_BT, fr);
        draw_drives(14, 1);
        s_ch(23, 38, B_BT, fr);
    } else {
        s_ch(6, 26, B_TT, fr);
        for (int r = 7; r < 23; r++) s_ch(r, 26, B_V, fr);
        s_ch(23, 26, B_BT, fr);
        fs_info_block(7, 2, FSC(1), 22, 12);
        s_put(7, 2, "File", fr);
        draw_list(&g_single, 0, 1);
    }
    /* the drive row of pane 1 in multiple mode: letters */
    draw_fkeys(fs_fkeys);
}

/* the empty File System and "Reading disk information." while a drive is read */
static void reading_box(void)
{
    int fr = FSC(0);
    title_mode = 1;
    draw_title("File System");
    draw_actionbar(fs_menus, 4, -1, -1);
    s_fill(2, 0, 22, COLS, ' ', fr);
    s_box(2, 0, 22, COLS, fr);
    s_hdiv(4, 0, COLS, fr);
    s_hdiv(6, 0, COLS, fr);
    s_ch(6, 34, B_TT, fr);
    for (int r = 7; r < 23; r++) s_ch(r, 34, B_V, fr);
    s_ch(23, 34, B_BT, fr);
    s_put(7, 8, "Directory Tree", fr);
    s_put(7, 25, "More:", fr);
    s_put(7, 70, "More:", fr);
    s_fill(10, 23, 5, 33, ' ', CLR(C_DLG));
    s_box(10, 23, 5, 33, CLR(C_DLG));
    s_put(12, 27, "Reading disk information.", CLR(C_DLG));
    draw_fkeys(fs_fkeys);
    s_flush();
}

/* --------------------------------------------------------------- state --- */
static void fs_save(void)
{
    struct fsstate *s = (struct fsstate *)st.fs;
    s->valid = 1;
    s->arrange = fso.arrange;
    s->sort = fso.sort;
    s->cd = fso.confirm_delete;
    s->cr = fso.confirm_replace;
    s->ca = fso.across;
    strcpy(s->mask, fso.mask);
    s->focus = focus;
    s->active = active;
    for (int k = 0; k < 2; k++) {
        struct pane *p = &panes[k];
        s->drive[k] = p->drive;
        if (p->loaded) dir_path(p, p->cur, s->cwd[k]); else s->cwd[k][0] = 0;
        s->curname[k][0] = 0;
        if (p->loaded && p->nfiles) strcpy(s->curname[k], p->files[p->fcur].name);
    }
}

static void fs_restore(void)
{
    struct fsstate *s = (struct fsstate *)st.fs;
    int drv = dos_curdrive();
    reading_box();
    if (!s->valid) {
        for (int k = 0; k < 2; k++) {
            char cwd[80];
            dos_getcwd(drv, cwd);
            pane_load(&panes[k], drv);
            panes[k].cur = panes[k].tcur = pane_find_dir(&panes[k], cwd);
            pane_files(&panes[k]);
        }
        focus = 0;
        active = 0;
        return;
    }
    fso.arrange = s->arrange;
    fso.sort = s->sort;
    fso.confirm_delete = s->cd;
    fso.confirm_replace = s->cr;
    fso.across = s->ca;
    strcpy(fso.mask, s->mask);
    focus = s->focus;
    active = s->active;
    for (int k = 0; k < 2; k++) {
        struct pane *p = &panes[k];
        pane_load(p, s->cwd[k][0] ? s->drive[k] : drv);
        if (s->cwd[k][0]) p->cur = p->tcur = pane_find_dir(p, s->cwd[k]);
        pane_files(p);
        for (int i = 0; i < p->nfiles; i++)
            if (!strcmp(p->files[i].name, s->curname[k])) p->fcur = i;
    }
}

/* ---------------------------------------------------------------- keys --- */
static int nareas(void) { return fso.arrange == AR_MULTIPLE ? 6 : 3; }

static void tab(int dir)
{
    int a = fso.arrange == AR_MULTIPLE ? active * 3 + focus : focus;
    if (fso.arrange == AR_SYSTEM) {     /* drives and the file list only */
        focus = focus == 0 ? 2 : 0;
        return;
    }
    a = (a + dir + nareas()) % nareas();
    if (fso.arrange == AR_MULTIPLE) {
        int na = a / 3;
        if (na != active) {
            active = na;
            char path[80];
            dir_path(&panes[active], panes[active].cur, path);
            dos_setdrive(panes[active].drive);
            dos_chdir(path);
        }
        focus = a % 3;
    } else
        focus = a;
}

static int drive_index(int d)
{
    for (int i = 0; i < ndrives; i++) if (drives[i] == d) return i;
    return 0;
}

static int fs_avail(int m, int i)
{
    struct pane *p = &panes[active];
    int nsl = nsel, infiles = focus == 2, intree = focus == 1;
    if (m == 0) {
        switch (i) {
        case 0: case 2: return infiles && nsl > 0;
        case 1: {
            struct armregs r = {0};
            r.r0 = 0x0100;
            _armdos_int2f(&r);
            return infiles && nsl > 0 && (r.r0 & 0xFF) == 0xFF;
        }
        case 4: case 5: case 8: case 9: return infiles && nsl > 0;
        case 6: case 7: return (infiles && nsl > 0) || (intree && p->cur > 0);
        case 11: return 1;
        case 12: return p->nfiles > 0 && fso.arrange != AR_SYSTEM ? 1 : p->nfiles > 0;
        case 13: return nsl > 0;
        }
    }
    if (m == 2) {
        if (i == fso.arrange) return 0;
        if (i == 1 && !opt.mul) return 0;
        return 1;
    }
    if (m == 3 && i == 0) return 1;
    return 1;
}

static const int fs_menu_help[4][14] = {
    { H_OPEN, H_PRINT, H_ASSOC, 0, H_MOVE, H_COPY, H_DELETE, H_RENAME, H_ATTR, H_VIEW, 0, H_MKDIR, H_SELALL, H_DESELALL },
    { H_DISPOPT, H_FILEOPT, H_SHOWINFO },
    { H_SINGLE, H_MULTIPLE, H_SYSTEM },
    { H_EXITFS, H_RESUMEFS },
};
static const int fs_bar_help[4] = { H_FILE, H_OPTIONS, H_ARRANGE, H_FSEXIT };

/* 1 = leave the File System */
static int fs_action(int a)
{
    int m = a >> 8, i = a & 0xFF;
    if (m == 0) {
        if (fs_file_action(i)) fs_refresh();
    } else if (m == 1) {
        if (i == 0) { if (fs_display_options()) { pane_files(&panes[0]); pane_files(&panes[1]); } }
        else if (i == 1) fs_file_options();
        else fs_show_info();
    } else if (m == 2) {
        int old = fso.arrange;
        fso.arrange = i;
        if (i == AR_SYSTEM) { active = 0; if (focus == 1) focus = 2; }
        if (i == AR_MULTIPLE && old != i) {    /* the second list starts as a copy of the first */
            pane_load(&panes[1], panes[0].drive);
            panes[1].cur = panes[1].tcur = panes[0].cur;
        }
        if (i != AR_MULTIPLE) active = 0;
        if (old != i) { pane_files(&panes[0]); pane_files(&panes[1]); }
    } else if (m == 3 && i == 0)
        return 1;
    return 0;
}

static int fs_actionbar1(int start, int opened);

static int fs_actionbar(int start, int opened)
{
    in_menu = 1;
    int r = fs_actionbar1(start, opened);
    in_menu = 0;
    return r;
}

static int fs_actionbar1(int start, int opened)
{
    int helpid;
    for (;;) {
        bar_active = start;
        int a = run_actionbar(fs_menus, 4, fs_avail, start, opened, &helpid);
        bar_active = -1;
        if (a == -2) {
            int m = helpid >> 8, i = helpid & 0xFF;
            help_show(i == 0xFF ? fs_bar_help[m] : fs_menu_help[m][i] ? fs_menu_help[m][i] : fs_bar_help[m], 0, 0);
            start = m;
            opened = i != 0xFF;
            continue;
        }
        if (a < 0) return 0;
        if (fs_action(a)) return 1;
        if ((a >> 8) >= 2) return 0;
        /* after a File or Options action the File System comes back to the action bar */
        start = a >> 8;
        opened = 0;
    }
}

static void area_help(void)
{
    help_show(focus == 0 ? H_DRIVE : focus == 1 ? H_TREE : H_FILELIST, 0, 0);
}

/* which pane/area is at a screen position */
static int hit(int row, int col, int *pane, int *area, int *index)
{
    if (fso.arrange == AR_MULTIPLE) {
        for (int k = 0; k < 2; k++) {
            const struct geom *g = &g_multi[k];
            if (row == g->drow) { *pane = k; *area = 0; *index = (col - 1) / 3; return 1; }
            if (row >= g->top && row < g->top + g->rows) {
                *pane = k;
                *area = col < g->split ? 1 : 2;
                *index = (*area == 1 ? panes[k].ttop : panes[k].ftop) + row - g->top;
                return 1;
            }
            if (row == g->hrow) { *pane = k; *area = col < g->split ? 11 : 12; *index = col; return 1; }
        }
        return 0;
    }
    const struct geom *g = &g_single;
    int split = fso.arrange == AR_SYSTEM ? 26 : 34;
    *pane = 0;
    if (row == 3) { *area = 0; *index = (col - 1) / 3; return 1; }
    if (row >= 8 && row < 23) {
        *area = col < split ? 1 : 2;
        if (fso.arrange == AR_SYSTEM && *area == 1) return 0;
        *index = (*area == 1 ? panes[0].ttop : panes[0].ftop) + row - g->top;
        return 1;
    }
    if (row == 7) { *area = col < split ? 11 : 12; *index = col; return 1; }
    return 0;
}

static void move_cursor(struct pane *p, int k, int rows)
{
    if (focus == 0) {
        int i = drive_index(p->drive);
        if (k == K_LEFT && i > 0) i--;
        else if (k == K_RIGHT && i < ndrives - 1) i++;
        else return;
        /* the drive bar moves; Enter reads the drive */
        p->drive = drives[i];
        p->loaded = 0;
        return;
    }
    if (focus == 1) {
        int d = p->tcur;
        if (k == K_UP) d--;
        else if (k == K_DOWN) d++;
        else if (k == K_PGUP) d -= rows;
        else if (k == K_PGDN) d += rows;
        else if (k == K_HOME || k == K_CHOME) d = 0;
        else if (k == K_END || k == K_CEND) d = p->ndirs - 1;
        else return;
        if (d < 0) d = 0;
        if (d >= p->ndirs) d = p->ndirs - 1;
        p->tcur = d;                    /* Enter (or leaving the tree) lists it */
        return;
    }
    int f = p->fcur;
    if (k == K_UP) f--;
    else if (k == K_DOWN) f++;
    else if (k == K_PGUP) f -= rows;
    else if (k == K_PGDN) f += rows;
    else if (k == K_HOME || k == K_CHOME) f = 0;
    else if (k == K_END || k == K_CEND) f = p->nfiles - 1;
    else return;
    if (f >= p->nfiles) f = p->nfiles - 1;
    if (f < 0) f = 0;
    p->fcur = f;
}

static int list_rows(void)
{
    if (fso.arrange == AR_MULTIPLE) return g_multi[active].rows;
    return 15;
}

int filesys(void)
{
    int result = 0;
    from_sp = opt.menu && st.fs_from_sp;
    if (!selset) selset = malloc(MAXSEL * sizeof selset[0]);
    find_drives();
    fs_restore();
    fs_layer = layer_count();
    push_layer(fs_draw, 0);
    for (;;) {
        st.screen = 1;
        struct pane *p = &panes[active];
        if (!p->loaded) {               /* the drive bar moved to a drive not read yet */
            redraw();
        }
        redraw();
        struct ev e;
        ev_get(&e);
        int k = 0;
        if (e.type == EV_KEY) k = e.key;
        else if (e.type == EV_DOWN || e.type == EV_DBL) {
            int pn, area, idx;
            if (e.row == 1) {
                for (int m = 0; m < 4; m++) {
                    int len = strlen(fs_menus[m].name);
                    if (e.col >= fs_menus[m].col - 1 && e.col <= fs_menus[m].col + len) {
                        if (fs_actionbar(m, 1)) { result = 0; goto out; }
                        break;
                    }
                }
                if (e.col >= 69 && e.col <= 75) area_help();
                continue;
            }
            if (e.row == 24) { k = fkey_hit(fs_fkeys, e.col); }
            else if (hit(e.row, e.col, &pn, &area, &idx)) {
                if (pn != active) {
                    active = pn;
                    char path[80];
                    dir_path(&panes[active], panes[active].cur, path);
                    dos_setdrive(panes[active].drive);
                    dos_chdir(path);
                }
                p = &panes[active];
                if (area == 0) {
                    focus = 0;
                    if (idx >= 0 && idx < ndrives && (e.col - 1) % 3 != 2) select_drive(p, drives[idx]);
                } else if (area == 1) {
                    focus = 1;
                    if (idx < p->ndirs) set_dir(p, idx);
                } else if (area == 2) {
                    focus = 2;
                    if (idx < p->nfiles) {
                        p->fcur = idx;
                        char full[96];
                        file_path(p, idx, full);
                        if (e.type == EV_DBL) { fs_save(); fs_open(p, idx); }
                        else {
                            if (!fso.across && nsel && !sel_has(full)) {
                                /* a click selects just this file */
                            }
                            sel_toggle(full);
                        }
                    }
                } else if (area == 11 || area == 12) {
                    /* clicks on the More: arrows scroll */
                    int more = area == 11 ? (fso.arrange == AR_MULTIPLE ? 29 : 25) : 70;
                    int foc = area == 11 ? 1 : 2;
                    focus = foc;
                    if (idx == more + 5) move_cursor(p, K_UP, 1);
                    else if (idx == more + 7) move_cursor(p, K_DOWN, 1);
                }
                continue;
            }
        }
        if (!k) continue;
        int a = KASCII(k);
        if (k == K_TAB || k == K_STAB) { tab(k == K_TAB ? 1 : -1); continue; }
        if (k == K_F10) { if (fs_actionbar(0, 0)) { result = 0; goto out; } continue; }
        if (k == K_F3) { result = 0; goto out; }
        if (k == K_F1) { area_help(); continue; }
        if (k == K_SF9) {
            if (opt.prompt) { fs_save(); command_prompt(); fs_refresh(); }
            continue;
        }
        if (a >= 1 && a <= 26 && KSCAN(k) != 0x1C && KSCAN(k) != 0x0F && KSCAN(k) != 0x0E) {  /* Ctrl+letter: drive */
            int d = a - 1;
            for (int i = 0; i < ndrives; i++)
                if (drives[i] == d) { select_drive(p, d); break; }
            continue;
        }
        if (k == 0x3500 + '/' || k == 0x352F) { }
        if ((KSCAN(k) == 0x35 && a == 0) ) { fs_file_action(12); continue; }     /* Alt+/ */
        if ((KSCAN(k) == 0x2B && a == 0)) { fs_file_action(13); continue; }      /* Alt+\ */
        if (focus == 0) {
            if (k == K_ENTER) { select_drive(p, p->drive); continue; }
            if (k == K_LEFT || k == K_RIGHT) {
                int i = drive_index(p->drive);
                if (k == K_LEFT && i > 0) i--;
                else if (k == K_RIGHT && i < ndrives - 1) i++;
                if (drives[i] != p->drive) select_drive(p, drives[i]);
                continue;
            }
            if (a > ' ') {
                int d = upc(a) - 'A';
                for (int i = 0; i < ndrives; i++) if (drives[i] == d) select_drive(p, d);
            }
            continue;
        }
        if (focus == 2) {
            if (k == K_SPACE && p->nfiles) {
                char full[96];
                file_path(p, p->fcur, full);
                sel_toggle(full);
                continue;
            }
            if (k == K_ENTER && p->nfiles) {
                fs_save();
                fs_open(p, p->fcur);
                continue;
            }
            if (a > ' ' && p->nfiles) {     /* jump to the next file starting with the letter */
                for (int j = 1; j <= p->nfiles; j++) {
                    int t = (p->fcur + j) % p->nfiles;
                    if (upc((uint8_t)p->files[t].name[0]) == upc(a)) { p->fcur = t; break; }
                }
                continue;
            }
        }
        if (focus == 1) {
            if (k == K_ENTER) { set_dir(p, p->tcur); continue; }
            if (a > ' ') {
                for (int j = 1; j <= p->ndirs; j++) {
                    int t = (p->tcur + j) % p->ndirs;
                    if (upc((uint8_t)p->dirs[t].name[0]) == upc(a)) { p->tcur = t; break; }
                }
                continue;
            }
        }
        move_cursor(p, k, list_rows());
    }
out:
    fs_save();
    pop_layer();
    st.screen = 0;
    if (!from_sp) return 1;
    return result;
}

/* called by fsops before starting a program: keep our place */
void fs_keep_state(void) { fs_save(); }
