/*
 * ops.c - file operations (copy, rename/move, mkdir, delete), selection,
 * compare directories, find file, history, filter, drive menu, configuration.
 */
#include <stdlib.h>
#include "acm.h"

#define BUFSZ 32768
static uint8_t *buf;
static int ow_all, del_all, cancelled;

static int truename(const char *in, char *out)
{
    struct armregs r = {0};
    r.r0 = 0x6000; r.r4 = (uint32_t)in; r.r5 = (uint32_t)out;
    crit_err = 0;
    if (_armdos_int21(&r) || crit_err) return -1;
    return 0;
}

static int esc_pressed(void)
{
    while (key_ready()) {
        int k = getkey();
        if (k == K_ESC) {
            static const char *const yn[] = { "Yes", "No" };
            if (message("Cancel", "Do you want to cancel the operation?", 0, &PAL_RED, yn, 2) == 0) return 1;
        }
    }
    return 0;
}

/* the files an operation works on: the selection, or the file under the cursor */
static int gather(panel *p, int **list)
{
    int n = 0;
    *list = malloc((p->n + 1) * sizeof(int));
    if (!*list) return 0;
    if (p->nsel) {
        for (int i = 0; i < p->n; i++) if (p->f[i].sel && p->f[i].name[0] != '.') (*list)[n++] = i;
    } else {
        fent *f = panel_curfile(p);
        if (f && f->name[0] != '.') (*list)[n++] = p->st->cur;
    }
    return n;
}

static void lowname(char *o, const char *n) { strcpy(o, n); str_lower(o); }

/* ---- progress ---- */
static int pbx, pby, pbw;
static void progress(const char *what, const char *src, const char *dst)
{
    char s[16], d[80];
    const char *lines[5];
    int n = 0;
    lowname(s, basename_(src));
    lines[n++] = what;
    lines[n++] = s;
    if (dst) { lowname(d, dst); lines[n++] = "to"; lines[n++] = d; }
    lines[n++] = "";
    msgbox_draw(what[0] == 'C' ? "Copy" : what[0] == 'M' ? "Rename/Move" : "Delete", lines, n, &PAL_GREY, &pbx, &pby, &pbw);
    if (dst) fill(pbx + 5, pby + 6, pbw - 10, 1, 0xB0, 0x70);
    flush();
}

static void progress_bar(uint32_t done, uint32_t total)
{
    int w = pbw - 10;
    int k = total ? (int)((uint64_t)done * w / total) : w;
    fill(pbx + 5, pby + 6, k, 1, 0xDB, 0x70);
    flush();
}

static int copy_one(const char *src, const char *dst)
{
    int attr = d_getattr(dst);
    if (attr >= 0) {
        if (attr & 0x10) { error_box("Cannot overwrite the directory", dst); return -1; }
        if (!ow_all) {
            static const char *const b[] = { "Overwrite", "All", "Skip", "Cancel" };
            char l1[80], n[16];
            lowname(n, basename_(dst));
            strcpy(l1, "The file "); strcat(l1, n); strcat(l1, " exists.");
            int rc = message("Warning", l1, "Do you wish to write over the old file?", &PAL_RED, b, 4);
            if (rc == 1) ow_all = 1;
            else if (rc == 2) return 0;
            else if (rc != 0) { cancelled = 1; return -1; }
        }
        if (attr & 1) d_setattr(dst, attr & ~1);
    }
    int hs = d_open(src, 0);
    if (hs < 0) { error_box("Cannot open the file", src); return -1; }
    uint16_t t = 0, d = 0;
    d_getftime(hs, &t, &d);
    uint32_t total = d_filesize(hs), done = 0;
    int sattr = d_getattr(src);
    int hd = d_creat(dst, 0);
    if (hd < 0) { d_close(hs); error_box("Cannot create the file", dst); return -1; }
    if (!buf) buf = malloc(BUFSZ);
    int rc = 0;
    for (;;) {
        int n = d_read(hs, buf, BUFSZ);
        if (n < 0) { error_box("Error reading", src); rc = -1; break; }
        if (!n) break;
        int w = d_write(hd, buf, n);
        if (w != n) { error_box(w < 0 ? dos_errmsg(doserr) : "Disk full", dst); rc = -1; break; }
        done += n;
        if (total > BUFSZ) progress_bar(done, total);
        if (esc_pressed()) { cancelled = 1; rc = -1; break; }
    }
    d_setftime(hd, t, d);
    d_close(hd);
    d_close(hs);
    if (rc) d_unlink(dst);
    else if (sattr >= 0) d_setattr(dst, (sattr & 0x07) | 0x20);
    return rc;
}

/* the entries of a directory (names), without . and .. */
static int listdir(const char *dir, char (**names)[13], uint8_t **attrs)
{
    struct dta d;
    char spec[128];
    int n = 0, cap = 32;
    *names = malloc(cap * 13);
    *attrs = malloc(cap);
    path_join(spec, dir, "*.*");
    if (!d_findfirst(spec, 0x16, &d)) do {
        if (d.name[0] == '.') continue;
        if (n == cap) {
            cap *= 2;
            *names = realloc(*names, cap * 13);
            *attrs = realloc(*attrs, cap);
        }
        strcpy((*names)[n], d.name);
        (*attrs)[n++] = d.attr;
    } while (!d_findnext(&d));
    return n;
}

static int copy_tree(const char *src, const char *dst, int move)
{
    char (*names)[13];
    uint8_t *attrs;
    char s[128], t[128];
    int sl = strlen(src);
    if (!strncmp(dst, src, sl) && dst[sl] == '\\') { error_box("Cannot copy a directory into itself", src); return -1; }
    int a = d_getattr(dst);
    if (a < 0) { if (d_mkdir(dst)) { error_box("Cannot create the directory", dst); return -1; } }
    else if (!(a & 0x10)) { error_box("A file of that name exists", dst); return -1; }
    int n = listdir(src, &names, &attrs), rc = 0;
    for (int i = 0; i < n && !rc; i++) {
        path_join(s, src, names[i]);
        path_join(t, dst, names[i]);
        if (attrs[i] & 0x10) rc = copy_tree(s, t, move);
        else {
            progress(move ? "Moving the file" : "Copying the file", s, t);
            rc = copy_one(s, t);
            if (!rc && move) { d_setattr(s, 0); d_unlink(s); }
        }
    }
    free(names); free(attrs);
    if (!rc && move) d_rmdir(src);
    return rc;
}

void op_copy(int move)
{
    panel *a = ACT, *o = OTH;
    int *list, n = gather(a, &list);
    char dest[128], text[64], nm[16];
    if (!n) { free(list); return; }
    if (o->st->visible && (o->st->mode == M_BRIEF || o->st->mode == M_FULL) && S->panels_on)
        strcpy(dest, o->st->path);
    else if (move && n == 1) lowname(dest, a->f[list[0]].name), dest[0] = dest[0];
    else strcpy(dest, a->st->path);
    if (dest[1] == ':' && dest[strlen(dest) - 1] != '\\') strcat(dest, "\\");
    if (n == 1) {
        lowname(nm, a->f[list[0]].name);
        strcpy(text, move ? "Rename or move \"" : "Copy \"");
        strcat(text, nm);
        strcat(text, "\" to");
    } else {
        char c[8];
        u2s(c, n);
        strcpy(text, move ? "Rename or move " : "Copy ");
        strcat(text, c);
        strcat(text, " files to");
    }
    ditem it[] = {
        { DI_TEXT, 3, 2, 0, text, 0, 0, 0 },
        { DI_INPUT, 3, 3, 50, 0, dest, 80, 0 },
        { DI_BUTTON, -1, 5, 0, move ? "Rename/Move" : "Copy", 0, 0, 0 },
        { DI_BUTTON, -1, 5, 0, "Cancel", 0, 0, 0 },
    };
    if (dialog(move ? "Rename/Move" : "Copy", 56, 7, &PAL_GREY, it, 4, 1) != 0 || !dest[0]) { free(list); return; }
    /* where to */
    char in[128], full[128];
    str_upper(dest);
    int l = strlen(dest);
    int wantdir = l > 1 && dest[l - 1] == '\\' && !(l == 3 && dest[1] == ':');
    if (wantdir) dest[--l] = 0;
    if (dest[1] == ':' || dest[0] == '\\') strcpy(in, dest);
    else path_join(in, a->st->path, dest);
    if (truename(in, full)) { error_box("Invalid path", dest); free(list); return; }
    int attr = d_getattr(full);
    int isdir = (attr >= 0 && (attr & 0x10)) || (strlen(full) == 3);
    if (!isdir && (n > 1 || wantdir)) {
        error_box(n > 1 ? "The destination must be a directory:" : "No such directory:", full);
        free(list); return;
    }
    ow_all = 0; cancelled = 0;
    char keep[13] = "";
    for (int k = 0; k < n && !cancelled; k++) {
        fent *f = &a->f[list[k]];
        char src[128], dst[128];
        path_join(src, a->st->path, f->name);
        if (isdir) path_join(dst, full, f->name); else strcpy(dst, full);
        if (!strcmp(src, dst)) {
            error_box(move ? "Cannot move a file to itself" : "Cannot copy a file to itself", src);
            break;
        }
        int rc;
        if (f->attr & 0x10) {
            if (move && src[0] == dst[0] && !d_rename(src, dst)) rc = 0;
            else {
                progress(move ? "Moving the file" : "Copying the file", src, dst);
                rc = copy_tree(src, dst, move);
            }
        } else if (move && src[0] == dst[0]) {
            int da = d_getattr(dst);
            rc = 0;
            if (da >= 0 && !ow_all) {
                static const char *const b[] = { "Overwrite", "All", "Skip", "Cancel" };
                char l1[64], nn[16];
                lowname(nn, basename_(dst));
                strcpy(l1, "The file "); strcat(l1, nn); strcat(l1, " exists.");
                int r = message("Warning", l1, "Do you wish to write over the old file?", &PAL_RED, b, 4);
                if (r == 1) ow_all = 1;
                else if (r == 2) continue;
                else if (r != 0) break;
            }
            if (da >= 0) { if (da & 0x10) { error_box("Cannot overwrite the directory", dst); break; } d_setattr(dst, 0); d_unlink(dst); }
            if (d_rename(src, dst)) { error_box(dos_errmsg(doserr), src); rc = -1; }
        } else {
            progress(move ? "Moving the file" : "Copying the file", src, dst);
            rc = copy_one(src, dst);
            if (!rc && move) { d_setattr(src, 0); d_unlink(src); }
        }
        if (rc) break;
        f->sel = 0;
        if (n == 1 && move && !isdir) strcpy(keep, basename_(dst));
    }
    free(list);
    panel_selcount(a);
    panels_reread_all();
    if (keep[0]) panel_goto_name(a, keep);
}

void op_mkdir(void)
{
    panel *a = ACT;
    char name[80] = "", full[128];
    ditem it[] = {
        { DI_TEXT, 3, 2, 0, "Create the directory", 0, 0, 0 },
        { DI_INPUT, 3, 3, 44, 0, name, 67, 0 },
        { DI_BUTTON, -1, 5, 0, "Make directory", 0, 0, 0 },
        { DI_BUTTON, -1, 5, 0, "Cancel", 0, 0, 0 },
    };
    if (dialog("Make directory", 50, 7, &PAL_GREY, it, 4, 1) != 0 || !name[0]) return;
    str_upper(name);
    if (name[1] == ':' || name[0] == '\\') strcpy(full, name); else path_join(full, a->st->path, name);
    if (d_mkdir(full)) { error_box("Cannot create the directory", full); return; }
    panels_reread_all();
    panel_goto_name(a, basename_(full));
}

static int delete_tree(const char *path)
{
    char (*names)[13];
    uint8_t *attrs;
    char s[128];
    int n = listdir(path, &names, &attrs), rc = 0;
    for (int i = 0; i < n && !rc; i++) {
        path_join(s, path, names[i]);
        if (attrs[i] & 0x10) rc = delete_tree(s);
        else {
            progress("Deleting the file", s, 0);
            if (attrs[i] & 7) d_setattr(s, 0);
            if (d_unlink(s)) { error_box("Cannot delete the file", s); rc = -1; }
            if (esc_pressed()) rc = -1;
        }
    }
    free(names); free(attrs);
    if (!rc && d_rmdir(path)) { error_box("Cannot remove the directory", path); rc = -1; }
    return rc;
}

void op_delete(void)
{
    panel *a = ACT;
    int *list, n = gather(a, &list);
    char l2[64], nm[16];
    static const char *const b[] = { "Delete", "Cancel" };
    if (!n) { free(list); return; }
    if (S->confirm_del) {
        if (n == 1) { lowname(nm, a->f[list[0]].name); strcpy(l2, nm); }
        else { u2s(nm, n); strcpy(l2, nm); strcat(l2, " files"); }
        if (message("Delete", "Do you wish to delete", l2, &PAL_RED, b, 2) != 0) { free(list); return; }
    }
    del_all = 0;
    for (int k = 0; k < n; k++) {
        fent *f = &a->f[list[k]];
        char path[128];
        path_join(path, a->st->path, f->name);
        if (f->attr & 0x10) {
            if (!d_rmdir(path)) { f->sel = 0; continue; }
            if (!del_all) {
                static const char *const bb[] = { "Delete", "All", "Skip", "Cancel" };
                lowname(nm, f->name);
                strcpy(l2, nm);
                int r = message("Delete", "The following directory is not empty:", l2, &PAL_RED, bb, 4);
                if (r == 1) del_all = 1;
                else if (r == 2) continue;
                else if (r != 0) break;
            }
            if (delete_tree(path)) break;
        } else {
            progress("Deleting the file", path, 0);
            if (f->attr & 1) d_setattr(path, f->attr & ~1);
            if (d_unlink(path)) { error_box("Cannot delete the file", path); break; }
        }
        f->sel = 0;
    }
    free(list);
    panel_selcount(a);
    panels_reread_all();
}

void op_select(int how)
{
    panel *a = ACT;
    static char pat[16] = "*.*";
    if (a->st->mode != M_BRIEF && a->st->mode != M_FULL) return;
    if (how) {
        ditem it[] = { { DI_INPUT, 4, 1, 16, 0, pat, 13, 0 } };
        if (dialog(how > 0 ? "Select" : "Unselect", 26, 3, &PAL_CYAN, it, 1, 0) != 0) return;
        str_upper(pat);
    }
    for (int i = 0; i < a->n; i++) {
        fent *f = &a->f[i];
        if (f->name[0] == '.') continue;
        if (how == 0) { if (!(f->attr & 0x10)) f->sel = !f->sel; }
        else if (wildmatch(pat, f->name)) {
            if (how > 0) { if (!(f->attr & 0x10)) f->sel = 1; }
            else f->sel = 0;
        }
    }
    panel_selcount(a);
}

void op_compare(void)
{
    panel *x = &P[0], *y = &P[1];
    if ((x->st->mode != M_BRIEF && x->st->mode != M_FULL) || (y->st->mode != M_BRIEF && y->st->mode != M_FULL)) {
        error_box("Both panels must show files", 0);
        return;
    }
    int diffs = 0;
    for (int s = 0; s < 2; s++) {
        panel *p = s ? y : x, *q = s ? x : y;
        for (int i = 0; i < p->n; i++) {
            fent *f = &p->f[i];
            f->sel = 0;
            if (f->attr & 0x10) continue;
            int found = -1;
            for (int j = 0; j < q->n; j++) if (!strcmp(q->f[j].name, f->name)) { found = j; break; }
            if (found < 0) f->sel = 1;
            else {
                fent *g = &q->f[found];
                uint32_t tf = ((uint32_t)f->date << 16) | f->time, tg = ((uint32_t)g->date << 16) | g->time;
                if (tf > tg || (tf == tg && f->size != g->size)) f->sel = 1;
            }
            diffs += f->sel;
        }
        panel_selcount(p);
    }
    if (!diffs) {
        static const char *const ok[] = { "OK" };
        message("Compare", "The two directories are identical", 0, &PAL_CYAN, ok, 1);
    }
}

/* ---- find file ---- */
static char (*found)[80];
static int nfound, capfound;

static void find_scan(char *path, const char *pat, int depth)
{
    struct dta d;
    int l = strlen(path);
    char (*sub)[13] = 0;
    int ns = 0, cs = 0;
    if (depth > 16 || cancelled) return;
    fill(14, 12, 52, 1, ' ', 0x30);
    putn(15, 12, path, 50, 0x30);
    flush();
    if (esc_pressed()) { cancelled = 1; return; }
    path_join(path, path, "*.*");
    if (!d_findfirst(path, 0x16, &d)) do {
        if (d.name[0] == '.') continue;
        if (d.attr & 0x10) {
            if (ns == cs) { cs = cs ? cs * 2 : 16; sub = realloc(sub, cs * 13); }
            strcpy(sub[ns++], d.name);
        }
        if (wildmatch(pat, d.name) && strlen(path) < 76) {
            if (nfound == capfound) { capfound = capfound ? capfound * 2 : 64; found = realloc(found, capfound * 80); }
            path[l] = 0;
            path_join(found[nfound], path, d.name);
            path_join(path, path, "*.*");
            nfound++;
        }
    } while (!d_findnext(&d));
    path[l] = 0;
    for (int i = 0; i < ns && !cancelled; i++) {
        path_join(path, path, sub[i]);
        find_scan(path, pat, depth + 1);
        path[l] = 0;
    }
    free(sub);
}

void op_findfile(void)
{
    static char pat[16] = "*.*";
    ditem it[] = {
        { DI_TEXT, 3, 2, 0, "File to find:", 0, 0, 0 },
        { DI_INPUT, 18, 2, 16, 0, pat, 13, 0 },
        { DI_BUTTON, -1, 4, 0, "Start", 0, 0, 0 },
        { DI_BUTTON, -1, 4, 0, "Cancel", 0, 0, 0 },
    };
    if (dialog("Find File", 40, 6, &PAL_CYAN, it, 4, 1) != 0 || !pat[0]) return;
    str_upper(pat);
    char path[128] = { ACT->st->path[0], ':', '\\', 0 };
    nfound = 0; cancelled = 0;
    uint16_t save[SCR_W * SCR_H];
    save_scr(save);
    const char *lines[] = { "Searching the drive...", "" };
    msgbox_draw("Find File", lines, 2, &PAL_CYAN, 0, 0, 0);
    find_scan(path, pat, 0);
    restore_scr(save);
    if (!nfound) {
        static const char *const ok[] = { "OK" };
        message("Find File", "No files found", 0, &PAL_CYAN, ok, 1);
        return;
    }
    const char **lp = malloc(nfound * sizeof(char *));
    for (int i = 0; i < nfound; i++) lp[i] = found[i];
    int sel = 0;
    int k = listbox("Find File", lp, nfound, 60, 18, &sel, &PAL_CYAN, "Enter = go to the file   Esc = close");
    free(lp);
    if (k == K_ENTER) {
        char dir[80], name[13];
        strcpy(dir, found[sel]);
        char *b = strrchr(dir, '\\');
        strcpy(name, b + 1);
        if (b == dir + 2) b[1] = 0; else *b = 0;
        panel *a = ACT;
        if (a->st->mode != M_BRIEF && a->st->mode != M_FULL) a->st->mode = M_BRIEF;
        a->st->visible = 1;
        S->panels_on = 1;
        panel_setpath(a, dir, name);
        set_active_dir();
    }
}

void op_history(void)
{
    if (!S->nhist) return;
    const char *lp[AC_HIST];
    for (int i = 0; i < S->nhist; i++) lp[i] = S->hist[i];
    int sel = S->nhist - 1;
    int k = listbox("History", lp, S->nhist, 60, S->nhist + 4 < 16 ? S->nhist + 4 : 16, &sel, &PAL_CYAN, "Enter = run   \x1A = to the command line");
    if (k == K_ENTER) {
        char c[AC_HISTLEN];
        strcpy(c, S->hist[sel]);
        execute(c);
    } else if (k == K_RIGHT || k == K_INS) {
        strcpy(cmdline, S->hist[sel]);
        cmdpos = strlen(cmdline);
    }
}

void op_filter(panel *p)
{
    char pat[16];
    strcpy(pat, p->st->filter);
    ditem it[] = {
        { DI_TEXT, 3, 2, 0, "Show the files matching:", 0, 0, 0 },
        { DI_INPUT, 3, 3, 16, 0, pat, 13, 0 },
        { DI_BUTTON, -1, 5, 0, "OK", 0, 0, 0 },
        { DI_BUTTON, -1, 5, 0, "All files", 0, 0, 0 },
        { DI_BUTTON, -1, 5, 0, "Cancel", 0, 0, 0 },
    };
    int rc = dialog("Filter", 40, 7, &PAL_GREY, it, 5, 1);
    if (rc == 1) strcpy(pat, "*.*");
    else if (rc != 0) return;
    str_upper(pat);
    strcpy(p->st->filter, pat[0] ? pat : "*.*");
    if (p->st->mode == M_BRIEF || p->st->mode == M_FULL) panel_reread(p);
}

void op_drive(panel *p)
{
    char drives[26];
    int n = 0, sel = 0;
    for (int d = 0; d < 26; d++)
        if (d_drive_valid(d)) { if (d == p->st->path[0] - 'A') sel = n; drives[n++] = d; }
    uint16_t save[SCR_W * SCR_H];
    save_scr(save);
    int w = n * 4 + 8;
    if (w < 26) w = 26;
    int bx = p->x + (40 - w) / 2, by = 8;
    for (;;) {
        dlg_frame(bx, by, w, 6, "Drive letter", &PAL_CYAN);
        put(bx + 3, by + 2, p == &P[0] ? "Choose left drive:" : "Choose right drive:", 0x30);
        int x0 = bx + (w - n * 4) / 2;
        for (int i = 0; i < n; i++) {
            int a = i == sel ? 0x0F : 0x30;
            putc_(x0 + i * 4, by + 3, ' ', a);
            putc_(x0 + i * 4 + 1, by + 3, 'A' + drives[i], a);
            putc_(x0 + i * 4 + 2, by + 3, ' ', a);
        }
        flush();
        cursor_at(0, -1);
        int k = getkey();
        if (k == K_ESC) { restore_scr(save); return; }
        if (k == K_LEFT || k == K_UP) { sel = (sel + n - 1) % n; continue; }
        if (k == K_RIGHT || k == K_DOWN || k == K_TAB) { sel = (sel + 1) % n; continue; }
        if (k == K_MOUSE) {
            if (ms_y == by + 3 && ms_x >= x0 && ms_x < x0 + n * 4) { sel = (ms_x - x0) / 4; break; }
            if (ms_y < by || ms_y >= by + 6 || ms_x < bx || ms_x >= bx + w) { restore_scr(save); return; }
            continue;
        }
        int c = k & 0xFF;
        if (c >= 'a' && c <= 'z') c -= 32;
        int hit = -1;
        for (int i = 0; i < n; i++) if (c == 'A' + drives[i]) hit = i;
        if (hit >= 0) { sel = hit; break; }
        if (k == K_ENTER) break;
    }
    restore_scr(save);
    char cwd[80];
    int d = drives[sel];
    for (;;) {
        crit_err = 0;
        if (!d_getcwd(d, cwd)) {
            /* can we read it? (a floppy drive without a disk) */
            uint32_t t, f;
            if (!d_diskfree(d, &t, &f)) break;
        }
        static const char *const b[] = { "Retry", "Cancel" };
        char l1[40];
        strcpy(l1, "Drive X: is not ready");
        l1[6] = 'A' + d;
        if (message("Error", l1, 0, &PAL_RED, b, 2) != 0) return;
    }
    if (p->st->mode == M_INFO) p->st->mode = M_BRIEF;
    strcpy(p->st->path, cwd);
    if (p->st->mode == M_TREE) tree_read(p);
    else panel_setpath(p, cwd, 0);
}

void op_config(void)
{
    int hid = S->hidden, conf = S->confirm_del, clk = S->clock, ms = S->ministatus, kb = S->keybar;
    ditem it[] = {
        { DI_CHECK, 4, 2, 0, "Show hidden and system files", 0, 0, &hid },
        { DI_CHECK, 4, 3, 0, "Confirm before deleting", 0, 0, &conf },
        { DI_CHECK, 4, 4, 0, "Mini status line", 0, 0, &ms },
        { DI_CHECK, 4, 5, 0, "Key bar", 0, 0, &kb },
        { DI_CHECK, 4, 6, 0, "Clock", 0, 0, &clk },
        { DI_BUTTON, -1, 8, 0, "OK", 0, 0, 0 },
        { DI_BUTTON, -1, 8, 0, "Save", 0, 0, 0 },
        { DI_BUTTON, -1, 8, 0, "Cancel", 0, 0, 0 },
    };
    int rc = dialog("Configuration", 44, 10, &PAL_GREY, it, 8, 0);
    if (rc != 0 && rc != 1) return;
    int reread = hid != S->hidden;
    S->hidden = hid; S->confirm_del = conf; S->clock = clk; S->ministatus = ms; S->keybar = kb;
    if (reread) panels_reread_all();
    panel_fix(&P[0]); panel_fix(&P[1]);
    if (rc == 1) save_setup();
}

void op_attrib(void)
{
    panel *a = ACT;
    fent *f = panel_curfile(a);
    if (!f || f->name[0] == '.') return;
    char path[128], l[48], nm[16];
    path_join(path, a->st->path, f->name);
    int at = d_getattr(path);
    if (at < 0) return;
    int ro = at & 1, hi = (at >> 1) & 1, sy = (at >> 2) & 1, ar = (at >> 5) & 1;
    lowname(nm, f->name);
    strcpy(l, "Change the attributes of "); strcat(l, nm);
    ditem it[] = {
        { DI_TEXT, 3, 2, 0, l, 0, 0, 0 },
        { DI_CHECK, 5, 4, 0, "Read only", 0, 0, &ro },
        { DI_CHECK, 5, 5, 0, "Archive", 0, 0, &ar },
        { DI_CHECK, 5, 6, 0, "Hidden", 0, 0, &hi },
        { DI_CHECK, 5, 7, 0, "System", 0, 0, &sy },
        { DI_BUTTON, -1, 9, 0, "Set", 0, 0, 0 },
        { DI_BUTTON, -1, 9, 0, "Cancel", 0, 0, 0 },
    };
    if (dialog("File attributes", 46, 11, &PAL_GREY, it, 7, 1) != 0) return;
    int na = (at & 0x10) | ro | (hi << 1) | (sy << 2) | (ar << 5);
    if (d_setattr(path, na & ~0x10)) error_box("Cannot change the attributes", path);
    panel_reread(a);
}
