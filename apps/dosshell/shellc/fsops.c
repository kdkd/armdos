/*
 * fsops.c - the File System's actions: Open, Print, Associate, Move, Copy,
 * Delete, Rename, Change attribute, View, Create directory, Select all,
 * Deselect all; Display options, File options, Show information.
 * Everything is plain INT 21h, as the real Shell did it.
 */
#include <dos.h>
#include "fs.h"

extern void fs_keep_state(void);

const char *dos_errtext(int err)
{
    switch (err) {
    case 0: return "Disk is write-protected.";
    case 2: return "Drive not ready.";
    default: return "General error.";
    }
}

static int dos_err(void)
{
    if (crit_error >= 0) { int e = crit_error; crit_error = -1; return 0x100 | e; }
    return 0;
}

static void show_error(int doserr)
{
    int ce = dos_err();
    if (ce) { message(dos_errtext(ce & 0xFF), H_MESSAGE); return; }
    switch (doserr) {
    case 2: message("File not found.", H_MESSAGE); break;
    case 3: message("Path not found.", H_MESSAGE); break;
    case 5: message("Access denied.", H_MESSAGE); break;
    case 0x13: message("Disk is write-protected.", H_MESSAGE); break;
    case 0x15: message("Drive not ready.", H_MESSAGE); break;
    case 0x27: case 0x70: message("Disk is full.", H_MESSAGE); break;
    default: message("General error.", H_MESSAGE); break;
    }
}

static int i21(struct armregs *r) { crit_error = -1; return _armdos_int21(r); }

int fs_selected_count(uint32_t *bytes)
{
    uint32_t b = 0;
    for (int i = 0; i < nsel; i++) {
        struct find_t f;
        if (!_dos_findfirst(selset[i], 0x07, &f)) b += f.size;
    }
    if (bytes) *bytes = b;
    return nsel;
}

static const char *base_name(const char *path)
{
    const char *b = strrchr(path, '\\');
    return b ? b + 1 : path;
}

static int is_program(const char *name)
{
    const char *d = strrchr(name, '.');
    return d && (!strcasecmp(d, ".COM") || !strcasecmp(d, ".EXE") || !strcasecmp(d, ".BAT"));
}

/* ---------------------------------------------------------- SHELL.ASC --- */
/* ARM-DOS's association file: text lines "PROGRAM.EXT=EXT EXT ... [/P]" */
static char ascbuf[2048];
static int asclen = -1;

static void asc_load(void)
{
    char path[96];
    home_path(path, opt.asc);
    asclen = file_read(path, ascbuf, sizeof ascbuf - 1);
    if (asclen < 0) asclen = 0;
    ascbuf[asclen] = 0;
}

/* the program associated with ext (without the dot); prompt = /P */
static int asc_find(const char *ext, char *prog, int *prompt)
{
    if (asclen < 0) asc_load();
    const char *l = ascbuf;
    while (*l) {
        const char *e = l;
        while (*e && *e != '\r' && *e != '\n') e++;
        const char *eq = memchr(l, '=', e - l);
        if (eq) {
            const char *q = eq + 1;
            while (q < e) {
                while (q < e && *q == ' ') q++;
                const char *w = q;
                while (q < e && *q != ' ') q++;
                if (q - w == 2 && w[0] == '/' && upc(w[1]) == 'P') continue;
                if ((int)strlen(ext) == q - w && !strncasecmp(w, ext, q - w)) {
                    int n = eq - l;
                    memcpy(prog, l, n);
                    prog[n] = 0;
                    *prompt = 0;
                    for (const char *s = eq; s + 1 < e; s++) if (s[0] == '/' && upc(s[1]) == 'P') *prompt = 1;
                    return 1;
                }
            }
        }
        l = e;
        while (*l == '\r' || *l == '\n') l++;
    }
    return 0;
}

static void asc_set(const char *prog, const char *exts, int prompt)
{
    char out[2048], path[96];
    int o = 0;
    if (asclen < 0) asc_load();
    const char *l = ascbuf;
    while (*l) {                        /* keep the other programs' lines */
        const char *e = l;
        while (*e && *e != '\r' && *e != '\n') e++;
        const char *eq = memchr(l, '=', e - l);
        if (!(eq && (int)strlen(prog) == eq - l && !strncasecmp(l, prog, eq - l)) && e > l && o + (e - l) + 2 < 2000) {
            memcpy(out + o, l, e - l);
            o += e - l;
            out[o++] = '\r';
            out[o++] = '\n';
        }
        l = e;
        while (*l == '\r' || *l == '\n') l++;
    }
    if (exts[0]) o += snprintf(out + o, sizeof out - o, "%s=%s%s\r\n", prog, exts, prompt ? " /P" : "");
    home_path(path, opt.asc);
    if (file_write(path, out, o) < 0) { message("Associate file missing or unreadable.", H_MESSAGE); return; }
    memcpy(ascbuf, out, o);
    ascbuf[o] = 0;
    asclen = o;
}

/* ------------------------------------------------------------- dialogs --- */
static struct dialog mkdlg(const char *title, int row, int col, int h, int w, struct dfield *f, int nf, int help)
{
    struct dialog d = { title, row, col, h, w, C_DLG, f, nf, BTN_ENTER, help, 0, 0, 0, 0 };
    return d;
}

/* the names of the selected files as one line (for From: / Delete fields) */
static void sel_names(char *out, int max)
{
    int o = 0;
    out[0] = 0;
    for (int i = 0; i < nsel && o < max - 14; i++) {
        const char *b = base_name(selset[i]);
        if (o) out[o++] = ' ';
        strcpy(out + o, b);
        o += strlen(b);
    }
}

/* ---------------------------------------------------------------- Open --- */
void fs_open(struct pane *p, int i)
{
    char full[96], prog[96], assoc[96], opts[128], line[260];
    static char cmds[300];
    int prompt = 1;
    file_path(p, i, full);
    assoc[0] = 0;
    if (is_program(full)) strcpy(prog, full);
    else {
        const char *d = strrchr(p->files[i].name, '.');
        if (d && asc_find(d + 1, prog, &prompt)) strcpy(assoc, full);
        else strcpy(prog, full);        /* as the real Shell: tried as a program */
    }
    opts[0] = 0;
    if (prompt || !assoc[0]) {
        struct dfield f[4] = {
            { DF_TEXT, 3, 3, "Starting program:  ", 0, 0, 0, 0, 0, 0 },
            { DF_TEXT, 5, 3, "Associated file :  ", 0, 0, 0, 0, 0, 0 },
            { DF_INPUT, 7, 3, "Options. .  ", opts, 126, 28, 0, H_OPENOPT, 0 },
            { DF_TEXT, 3, 22, base_name(prog), 0, 0, 0, 0, 0, 0 },
        };
        struct dfield g[5];
        memcpy(g, f, sizeof f);
        g[4] = (struct dfield){ DF_TEXT, 5, 22, assoc[0] ? base_name(assoc) : "", 0, 0, 0, 0, 0, 0 };
        struct dialog d = mkdlg("Open File", 7, 14, 14, 50, g, 5, H_OPEN);
        d.focus = 2;
        if (dialog_run(&d) != K_ENTER) return;
    }
    /* start it in its own directory */
    char dir[96];
    strcpy(dir, full);
    *(char *)base_name(dir) = 0;
    int dl = strlen(dir);
    if (dl > 3) dir[dl - 1] = 0;
    dos_setdrive(dir[0] - 'A');
    dos_chdir(dir);
    snprintf(line, sizeof line, "%s%s%s%s%s", prog, assoc[0] ? " " : "", assoc[0] ? base_name(assoc) : "",
             opts[0] ? " " : "", opts);
    int n = strlen(line);
    memcpy(cmds, line, n + 1);
    cmds[n + 1] = 0;
    fs_keep_state();
    launch(cmds, 1);
    fs_refresh();
}

/* ------------------------------------------------- the progress box -------- */
/* The operation's own box (rows 7-20, columns 14-63) shows the file being
   worked on ("Copying file:  NAME      1 of     3"); a question or an error
   appears below it with two numbered choices. Without a question the box is
   only drawn. Returns 1 or 2, or 0 for Esc. */
struct opbox { const char *title, *verb, *name, *text, *c1, *c2; int i, n, help, namecol, def; };
static struct opbox ob;
static char ob_list[128], ob_count[24];

static void opbox_draw(void *p)
{
    (void)p;
    int at = CLR(C_DLG), r = 7, c = 14, w = 50, h = 14;
    s_fill(r, c, h, w, ' ', at);
    s_box(r, c, h, w, at);
    int n = strlen(ob.title);
    s_put(r + 1, c + 1 + (w - 2 - n + 1) / 2, ob.title, at);
    s_put(r + 3, c + 3, ob.verb, at);
    s_put(r + 3, c + (ob.namecol ? ob.namecol : 3 + (int)strlen(ob.verb) + 2), ob.name, at);
    snprintf(ob_count, sizeof ob_count, "%5d of %5d", ob.i, ob.n);
    s_put(r + 3, c + 34, ob_count, at);
    s_hdiv(r + h - 3, c, w, at);
    s_put(r + h - 2, c + 1, BTN_ENTER, at);
}

/* error: the text on the box's 8th row and the choices two rows lower;
   question (err = 0): the text on the 6th row */
static int opbox_at(const char *text, const char *c1, const char *c2, int help, int err);
static int opbox(const char *text, const char *c1, const char *c2, int help) { return opbox_at(text, c1, c2, help, 0); }
static int opbox_err(const char *text, const char *c1, const char *c2, int help) { return opbox_at(text, c1, c2, help, 1); }

static int opbox_at(const char *text, const char *c1, const char *c2, int help, int err)
{
    ob.text = text;
    if (!text) {
        push_layer(opbox_draw, 0);
        redraw();
        pop_layer();
        return 0;
    }
    snprintf(ob_list, sizeof ob_list, "1. %s\n2. %s", c1, c2);
    struct dfield f[5] = {
        { DF_TEXT, 3, 3, ob.verb, 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, ob.namecol ? ob.namecol : 3 + (int)strlen(ob.verb) + 2, ob.name, 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 34, ob_count, 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, err ? 7 : 5, 3, text, 0, 0, 0, 0, 0, 0 },
        { DF_CHOICE, err ? 9 : 7, 4, ob_list, 0, 0, 0, ob.def, help, 45 },
    };
    snprintf(ob_count, sizeof ob_count, "%5d of %5d", ob.i, ob.n);
    struct dialog d = { ob.title, 7, 14, 14, 50, C_DLG, f, 5, BTN_ENTER, help, 4, 0, 0, 0 };
    beep();
    if (dialog_run(&d) != K_ENTER) return 0;
    return f[4].value + 1;
}

/* ------------------------------------------------------- Copy and Move --- */
/* -1 error (reported), -2 cancelled, 1 skipped, 0 done */
static int copy_one(const char *src, const char *dst, int move)
{
    struct armregs r = {0};
    ob.name = base_name(src);
    for (;;) {
        if (strcasecmp(src, dst)) break;
        int c = opbox_err("File cannot be copied to itself.", "Skip this file or directory and continue",
                      "Try this file or directory again", H_SKIP);
        if (c != 2) return c ? 1 : -2;
    }
    /* replace? */
    r.r0 = 0x4300;
    r.r3 = (uint32_t)dst;
    if (!i21(&r)) {
        if (fso.confirm_replace) {
            int c = opbox("Filename already exists.  Select an option.", "Skip this file and continue",
                          "Replace this file", H_REPLACE);
            if (c == 0) return -2;
            if (c == 1) return 1;
        }
    }
    opbox(0, 0, 0, 0);
    if (move && upc(src[0]) == upc(dst[0])) {       /* same drive: rename */
        memset(&r, 0, sizeof r);
        r.r0 = 0x4100;
        r.r3 = (uint32_t)dst;
        i21(&r);
        memset(&r, 0, sizeof r);
        r.r0 = 0x5600;
        r.r3 = (uint32_t)src;
        r.r5 = (uint32_t)dst;
        if (i21(&r)) { show_error(r.r0 & 0xFFFF); return -1; }
        return 0;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3D00;
    r.r3 = (uint32_t)src;
    if (i21(&r)) { show_error(r.r0 & 0xFFFF); return -1; }
    int in = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x5700;                      /* keep the date and time */
    r.r1 = in;
    i21(&r);
    uint32_t ftime = r.r2 & 0xFFFF, fdate = r.r3 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4300;
    r.r3 = (uint32_t)src;
    i21(&r);
    int attr = r.r2 & 0x27;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3C00;
    r.r2 = 0;
    r.r3 = (uint32_t)dst;
    if (i21(&r)) {
        int e = r.r0 & 0xFFFF;
        memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = in; i21(&r);
        show_error(e);
        return -1;
    }
    int out = r.r0 & 0xFFFF, err = 0;
    static uint8_t buf[16384];
    for (;;) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = in; r.r2 = sizeof buf; r.r3 = (uint32_t)buf;
        if (i21(&r)) { err = r.r0 & 0xFFFF; break; }
        int n = r.r0 & 0xFFFF;
        if (!n) break;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4000; r.r1 = out; r.r2 = n; r.r3 = (uint32_t)buf;
        if (i21(&r)) { err = r.r0 & 0xFFFF; break; }
        if ((int)(r.r0 & 0xFFFF) != n) { err = 0x27; break; }
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x5701; r.r1 = out; r.r2 = ftime; r.r3 = fdate;
    i21(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = out; i21(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = in; i21(&r);
    if (err) {
        memset(&r, 0, sizeof r); r.r0 = 0x4100; r.r3 = (uint32_t)dst; i21(&r);
        show_error(err);
        return -1;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x4301; r.r2 = attr | 0x20; r.r3 = (uint32_t)dst;
    i21(&r);
    if (move) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4100;
        r.r3 = (uint32_t)src;
        i21(&r);
    }
    return 0;
}

static int is_dir(const char *p)
{
    struct armregs r = {0};
    if ((p[1] == ':' && (!p[2] || (p[2] == '\\' && !p[3])))) return 1;
    r.r0 = 0x4300;
    r.r3 = (uint32_t)p;
    if (_armdos_int21(&r)) return 0;
    return (r.r2 & 0x10) != 0;
}

static int copy_move(int move)
{
    char from[128], to[80];
    struct pane *p = &panes[active];
    sel_names(from, sizeof from);
    dir_path(p, p->cur, to);
    if (fso.arrange == AR_MULTIPLE) {   /* the other list's directory is the natural target */
        struct pane *o = &panes[1 - active];
        dir_path(o, o->cur, to);
    }
    struct dfield f[2] = {
        { DF_INPUT, 4, 6, "From:   ", from, 127, 31, 0, move ? H_MOVEFROM : H_COPYFROM, 0 },
        { DF_INPUT, 6, 6, "To:     ", to, 78, 31, 0, move ? H_MOVETO : H_COPYTO, 0 },
    };
    struct dialog d = mkdlg(move ? "Move File" : "Copy File", 7, 14, 14, 50, f, 2, move ? H_MOVE : H_COPY);
    d.focus = 1;
    d.noenter = 0;
    if (dialog_run(&d) != K_ENTER) return 0;
    strupr_(to);
    if (!to[0]) return 0;
    if (to[1] != ':') {                 /* relative to the current directory */
        char t[80];
        dir_path(p, p->cur, t);
        int l = strlen(t);
        if (to[0] == '\\') { t[2] = 0; l = 2; }
        else if (t[l - 1] != '\\') t[l++] = '\\', t[l] = 0;
        strncat(t, to, 79 - l);
        strcpy(to, t);
    }
    int todir = is_dir(to);
    int nfiles = nsel;
    char (*list)[80] = malloc(nfiles * 80 + 1);
    if (!list) return 0;
    memcpy(list, selset, nfiles * 80);
    int changed = 0;
    ob.title = move ? "Move File" : "Copy File";
    ob.verb = move ? "Moving file:" : "Copying file:";
    ob.n = nfiles;
    for (int i = 0; i < nfiles; i++) {
        char dst[96];
        ob.i = i + 1;
        if (todir) {
            strcpy(dst, to);
            int l = strlen(dst);
            if (dst[l - 1] != '\\') dst[l++] = '\\', dst[l] = 0;
            strcat(dst, base_name(list[i]));
        } else {
            if (nfiles > 1) { message("Destination path incorrect.", H_MESSAGE); break; }
            strcpy(dst, to);
        }
        int rc = copy_one(list[i], dst, move);
        if (rc == -2) break;
        if (rc == 0) changed = 1;
    }
    free(list);
    if (move) sel_clear();
    return changed;
}

/* -------------------------------------------------------------- Delete --- */
static int delete_files(void)
{
    char names[128];
    sel_names(names, sizeof names);
    struct dfield f[1] = { { DF_INPUT, 3, 4, "Delete . .    ", names, 127, 31, 0, H_DELFILES, 0 } };
    struct dialog d = mkdlg("Delete File", 7, 10, 8, 58, f, 1, H_DELETE);
    d.tcol = 24;
    if (dialog_run(&d) != K_ENTER) return 0;
    int changed = 0, n = nsel;
    char (*list)[80] = malloc(n * 80 + 1);
    if (!list) return 0;
    memcpy(list, selset, n * 80);
    ob.title = "Delete File";
    ob.verb = "Deleting file:";
    ob.n = n;
    for (int i = 0; i < n; i++) {
        ob.i = i + 1;
        ob.name = base_name(list[i]);
        if (fso.confirm_delete) {
            int c = opbox("Select an option.", "Skip this file and continue", "Delete this file", H_DELFILE);
            if (c == 0) break;
            if (c == 1) continue;
        } else
            opbox(0, 0, 0, 0);
        struct armregs r = {0};
        r.r0 = 0x4100;
        r.r3 = (uint32_t)list[i];
        if (i21(&r)) { show_error(r.r0 & 0xFFFF); continue; }
        changed = 1;
    }
    free(list);
    return changed;                     /* (fs_refresh drops the deleted files from the selection) */
}

static int delete_dir(void)
{
    struct pane *p = &panes[active];
    char path[80];
    dir_path(p, p->cur, path);
    static char list[80];
    strcpy(list, "1. Do not delete this directory\n2. Delete this directory");
    struct dfield f[4] = {
        { DF_TEXT, 3, 3, "Directory:", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 15, path, 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 5, 3, "Select an option.", 0, 0, 0, 0, 0, 0 },
        { DF_CHOICE, 7, 4, list, 0, 0, 0, 0, H_NODELDIR, 45 },
    };
    struct dialog d = mkdlg("Delete Directory", 7, 14, 14, 50, f, 4, H_DELETE);
    if (dialog_run(&d) != K_ENTER || f[3].value != 1) return 0;
    struct armregs r = {0};
    dos_chdir("\\");
    r.r0 = 0x3A00;
    r.r3 = (uint32_t)path;
    if (i21(&r)) {
        dos_chdir(path);
        if ((r.r0 & 0xFFFF) == 5 || (r.r0 & 0xFFFF) == 0x10) message("Access denied.", H_MESSAGE);
        else show_error(r.r0 & 0xFFFF);
        return 0;
    }
    p->cur = p->tcur = p->dirs[p->cur].parent;
    dir_path(p, p->cur, path);
    dos_chdir(path);
    return 1;
}

/* -------------------------------------------------------------- Rename --- */
static int rename_files(void)
{
    int changed = 0, n = nsel;
    char (*list)[80] = malloc(n * 80 + 1);
    if (!list) return 0;
    memcpy(list, selset, n * 80);
    for (int i = 0; i < n; i++) {
        char newname[13] = "", cnt[24];
        snprintf(cnt, sizeof cnt, "%5d of %5d", i + 1, n);
        struct dfield f[4] = {
            { DF_TEXT, 3, 3, "Current filename:", 0, 0, 0, 0, 0, 0 },
            { DF_TEXT, 3, 22, base_name(list[i]), 0, 0, 0, 0, 0, 0 },
            { DF_TEXT, 3, 34, cnt, 0, 0, 0, 0, 0, 0 },
            { DF_FIXED, 5, 3, "New filename. .  ", newname, 12, 12, 0, H_NEWNAME, 0 },
        };
        struct dialog d = mkdlg("Rename File", 7, 14, 14, 50, f, 4, H_RENAME);
        if (dialog_run(&d) != K_ENTER) break;
        if (!newname[0]) continue;
        char dst[96];
        strcpy(dst, list[i]);
        strcpy((char *)base_name(dst), newname);
        strupr_(dst);
        struct armregs r = {0};
        r.r0 = 0x5600;
        r.r3 = (uint32_t)list[i];
        r.r5 = (uint32_t)dst;
        if (i21(&r)) { show_error(r.r0 & 0xFFFF); continue; }
        changed = 1;
    }
    free(list);
    if (changed) sel_clear();
    return changed;
}

static int rename_dir(void)
{
    struct pane *p = &panes[active];
    char path[80], newname[13] = "", dst[96];
    dir_path(p, p->cur, path);
    struct dfield f[3] = {
        { DF_TEXT, 3, 3, "Current name:", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 18, p->dirs[p->cur].name, 0, 0, 0, 0, 0, 0 },
        { DF_FIXED, 5, 3, "New name. .  ", newname, 12, 12, 0, H_RENDIR, 0 },
    };
    struct dialog d = mkdlg("Rename Directory", 7, 14, 14, 50, f, 3, H_RENDIR);
    if (dialog_run(&d) != K_ENTER || !newname[0]) return 0;
    strcpy(dst, path);
    strcpy((char *)base_name(dst), newname);
    strupr_(dst);
    dos_chdir("\\");
    struct armregs r = {0};
    r.r0 = 0x5600;
    r.r3 = (uint32_t)path;
    r.r5 = (uint32_t)dst;
    if (i21(&r)) { dos_chdir(path); show_error(r.r0 & 0xFFFF); return 0; }
    dos_chdir(dst);
    strcpy(p->dirs[p->cur].name, base_name(dst));
    return 1;
}

/* ---------------------------------------------------- Change attribute --- */
static int attr_dialog(const char *name, int *attr, const char *counter)
{
    char list[40];
    strcpy(list, "Hidden\nRead only\nArchive");
    int bits = ((*attr & 2) ? 1 : 0) | ((*attr & 1) ? 2 : 0) | ((*attr & 0x20) ? 4 : 0);
    struct dfield f[6] = {
        { DF_TEXT, 3, 3, name ? "File:" : "", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 10, name ? name : "", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 34, counter ? counter : "", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 5, 3, "To change attribute highlight item and", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 6, 3, "press Spacebar.  Press Enter when complete.", 0, 0, 0, 0, 0, 0 },
        { DF_CHECK, 8, 2, list, 0, 0, 2, bits, H_HIDDEN, 27 },
    };
    struct dialog d = mkdlg("Change Attribute", 7, 14, 14, 50, f, 6, H_ATTR);
    if (dialog_run(&d) != K_ENTER) return 0;
    bits = f[5].value;
    *attr = (*attr & ~0x23) | ((bits & 1) ? 2 : 0) | ((bits & 2) ? 1 : 0) | ((bits & 4) ? 0x20 : 0);
    return 1;
}

static int change_attr(void)
{
    int n = nsel, all = 0;
    {
        char list[96];
        strcpy(list, "1. Change selected files one at a time\n2. Change all selected files at once");
        struct dfield f[1] = { { DF_CHOICE, 3, 4, list, 0, 0, 0, 0, H_ONEATATIME, 53 } };
        struct dialog d = mkdlg("Change Attribute", 7, 10, 8, 58, f, 1, H_ATTR);
        d.tcol = 21;
        if (dialog_run(&d) != K_ENTER) return 0;
        all = f[0].value == 1;
    }
    int changed = 0;
    int common = -1;
    for (int i = 0; i < n; i++) {
        struct armregs r = {0};
        r.r0 = 0x4300;
        r.r3 = (uint32_t)selset[i];
        if (i21(&r)) continue;
        int attr = r.r2 & 0x27;
        if (all) {
            if (common < 0) {
                common = attr;
                if (!attr_dialog(0, &common, 0)) return changed;
            }
            attr = (attr & ~0x23) | (common & 0x23);
        } else {
            char cnt[24];
            snprintf(cnt, sizeof cnt, "%5d of %5d", i + 1, n);
            if (!attr_dialog(base_name(selset[i]), &attr, cnt)) break;
        }
        memset(&r, 0, sizeof r);
        r.r0 = 0x4301;
        r.r2 = attr;
        r.r3 = (uint32_t)selset[i];
        if (i21(&r)) show_error(r.r0 & 0xFFFF);
        else changed = 1;
    }
    return changed;
}

/* ---------------------------------------------------------------- View --- */
struct viewctx { const char *path; uint8_t *buf; int len, top, hex, lines; int *linestart; };

static void view_draw(void *p)
{
    struct viewctx *v = p;
    int fr = FSC(0), w = CLR(C_WORK);
    draw_title("File System");
    s_fill(1, 0, 1, COLS, ' ', CLR(C_BAR));
    s_ch(1, 66, B_V, CLR(C_BAR));
    s_put(1, 69, "F1=Help", CLR(C_BAR));
    s_fill(2, 0, 22, COLS, ' ', w);
    s_fill(2, 0, 9, COLS, ' ', fr);
    s_box(2, 0, 9, COLS, fr);
    s_put(3, 35, "File View", fr);
    s_put(5, 4, "To view a file's content press PgUp or PgDn.", fr);
    s_put(7, 4, "Viewing file:  ", fr);
    s_put(7, 19, v->path, fr);
    if (v->hex) {
        /* │  000000  │   E9054543   6F6E7665   72746564   00000000  │  ..EConverted....  │ */
        s_ch(10, 0, B_LT, fr);
        s_ch(10, 11, B_TT, fr);
        s_ch(10, 58, B_TT, fr);
        s_ch(10, 79, B_RT, fr);
        for (int i = 0; i < 13; i++) {
            int r = 11 + i, off = (v->top + i) * 16;
            s_fill(r, 0, 1, COLS, ' ', fr);
            s_ch(r, 0, B_V, fr);
            s_ch(r, 11, B_V, fr);
            s_ch(r, 58, B_V, fr);
            s_ch(r, 79, B_V, fr);
            if (off >= v->len) continue;
            char b[12];
            snprintf(b, sizeof b, "%06X", off);
            s_put(r, 3, b, fr);
            for (int k = 0; k < 16 && off + k < v->len; k++) {
                int c = v->buf[off + k];
                snprintf(b, sizeof b, "%02X", c);
                s_put(r, 15 + (k / 4) * 11 + (k % 4) * 2, b, fr);
                s_ch(r, 61 + k, (c >= 0x20 && c < 0x7F) ? c : '.', fr);
            }
        }
    } else {
        for (int i = 0; i < 13; i++) {
            int l = v->top + i;
            if (l >= v->lines) break;
            int s = v->linestart[l], c = 0;
            for (int k = s; k < v->len && c < 80; k++) {
                int ch = v->buf[k];
                if (ch == '\n') break;
                if (ch == '\r') continue;
                if (ch == '\t') { do s_ch(11 + i, c++, ' ', w); while (c % 8 && c < 80); continue; }
                s_ch(11 + i, c++, ch < 0x20 ? ' ' : ch, w);
            }
        }
    }
    draw_fkeys("  <\xC4\xD9=Enter  Esc=Cancel  F9=Hex/ASCII");
}

void fs_view(struct pane *p, int i)
{
    char full[96];
    file_path(p, i, full);
    int cap = 60000;
    uint8_t *buf = malloc(cap);
    if (!buf) { message("Not enough memory to continue - SHELL.", H_MESSAGE); return; }
    int len = file_read(full, buf, cap);
    if (len < 0) { free(buf); show_error(2); return; }
    if (len == 0) { free(buf); message("File is empty.", H_MESSAGE); return; }
    static int starts[4000];
    int nl = 0, col = 0;
    starts[nl++] = 0;
    for (int k = 0; k < len && nl < 4000; k++) {     /* lines end at LF or after 80 columns */
        int ch = buf[k];
        if (ch == '\r') continue;
        if (ch == '\n') { if (k + 1 < len) starts[nl++] = k + 1; col = 0; continue; }
        col += ch == '\t' ? 8 - col % 8 : 1;
        if (col >= 80 && k + 1 < len && buf[k + 1] != '\r' && buf[k + 1] != '\n') { starts[nl++] = k + 1; col = 0; }
    }
    struct viewctx v = { full, buf, len, 0, 0, nl, starts };
    push_layer(view_draw, &v);
    for (;;) {
        redraw();
        struct ev e;
        ev_get(&e);
        int k = 0;
        if (e.type == EV_KEY) k = e.key;
        else if (e.type == EV_DOWN || e.type == EV_DBL) {
            if (e.row == 24) k = fkey_hit("  <\xC4\xD9=Enter  Esc=Cancel  F9=Hex/ASCII", e.col);
            else if (e.row >= 11 && e.row < 17) k = K_PGUP;
            else if (e.row >= 17 && e.row < 24) k = K_PGDN;
            else if (e.row == 1 && e.col >= 69 && e.col <= 75) k = K_F1;
        }
        int total = v.hex ? (v.len + 15) / 16 : v.lines;
        if (k == K_ESC || k == K_ENTER) break;
        if (k == K_F9) { v.hex = !v.hex; v.top = 0; continue; }
        if (k == K_F1) { help_show(H_VIEWHELP, 0, 0); continue; }
        if (k == K_PGDN) { if (v.top + 13 < total) v.top += 13; }
        else if (k == K_PGUP) { v.top -= 13; if (v.top < 0) v.top = 0; }
        else if (k == K_DOWN) { if (v.top + 13 < total) v.top++; }
        else if (k == K_UP) { if (v.top) v.top--; }
        else if (k == K_CHOME || k == K_HOME) v.top = 0;
        else if (k == K_CEND || k == K_END) v.top = total > 13 ? total - 13 : 0;
    }
    pop_layer();
    free(buf);
}

/* ---------------------------------------------------- Create directory --- */
static int create_dir(void)
{
    struct pane *p = &panes[active];
    char name[13] = "", path[96];
    struct dfield f[1] = { { DF_FIXED, 3, 3, "New directory name. .  ", name, 12, 12, 0, H_NEWDIR, 0 } };
    struct dialog d = mkdlg("Create Directory", 7, 14, 14, 50, f, 1, H_MKDIR);
    if (dialog_run(&d) != K_ENTER || !name[0]) return 0;
    dir_path(p, p->cur, path);
    int l = strlen(path);
    if (path[l - 1] != '\\') path[l++] = '\\', path[l] = 0;
    strcat(path, name);
    strupr_(path);
    struct armregs r = {0};
    r.r0 = 0x3900;
    r.r3 = (uint32_t)path;
    if (i21(&r)) {
        int e = r.r0 & 0xFFFF;
        if (e == 5) message("Access denied.", H_MESSAGE); else show_error(e);
        return 0;
    }
    return 1;
}

/* ----------------------------------------------------------- Associate --- */
static void associate(void)
{
    char exts[40] = "";
    int prompt = 1;
    const char *prog = base_name(selset[0]);
    if (!is_program(prog)) { message("Program extension invalid.", H_MESSAGE); return; }
    /* the extensions already associated with this program */
    if (asclen < 0) asc_load();
    const char *l = ascbuf;
    while (*l) {
        const char *e = l;
        while (*e && *e != '\r' && *e != '\n') e++;
        const char *eq = memchr(l, '=', e - l);
        if (eq && (int)strlen(prog) == eq - l && !strncasecmp(l, prog, eq - l)) {
            int n = e - eq - 1;
            if (n > 39) n = 39;
            memcpy(exts, eq + 1, n);
            exts[n] = 0;
            char *pp = strstr(exts, " /P");
            if (pp) *pp = 0; else prompt = 0;
        }
        l = e;
        while (*l == '\r' || *l == '\n') l++;
    }
    snprintf(ob_count, sizeof ob_count, "%5d of %5d", 1, 1);
    struct dfield f[4] = {
        { DF_TEXT, 3, 3, "Filename .  .:", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 18, prog, 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 3, 34, ob_count, 0, 0, 0, 0, 0, 0 },
        { DF_INPUT, 5, 3, "Extensions. .  ", exts, 39, 24, 0, H_ASSOCEXT, 0 },
    };
    struct dialog d = mkdlg("Associate File", 7, 14, 14, 50, f, 4, H_ASSOC);
    if (dialog_run(&d) != K_ENTER) return;
    strupr_(exts);
    for (char *q = exts; *q; q++) if (*q == '.' || *q == ',') *q = ' ';
    int c = 2;
    if (exts[0]) {                      /* then: prompt for options? */
        static char list[64];
        strcpy(list, "1. Prompt for options\n2. Do not prompt for options");
        struct dfield g[1] = { { DF_CHOICE, 3, 4, list, 0, 0, 0, prompt ? 0 : 1, H_ASSOC, 53 } };
        struct dialog d2 = mkdlg("Associate File", 7, 10, 8, 58, g, 1, H_ASSOC);
        if (dialog_run(&d2) != K_ENTER) return;
        c = g[0].value + 1;
    }
    asc_set(prog, exts, c == 1);
}

/* --------------------------------------------------------------- Print --- */
static void print_files(void)
{
    for (int i = 0; i < nsel; i++) {
        uint8_t pk[5];
        uint32_t ptr = (uint32_t)selset[i];
        pk[0] = 0;
        memcpy(pk + 1, &ptr, 4);
        status_box("Print File", "Submitting file:", selset[i]);
        struct armregs r = {0};
        r.r0 = 0x0101;
        r.r3 = (uint32_t)pk;
        int cf = _armdos_int2f(&r);
        status_box(0, 0, 0);
        if (cf) {
            message((r.r0 & 0xFFFF) == 8 ? "PRINT queue is full." : "File can not be submitted.", H_MESSAGE);
            break;
        }
    }
}

/* ------------------------------------------------------------ the menu --- */
int fs_file_action(int item)
{
    struct pane *p = &panes[active];
    switch (item) {
    case 0:
        if (nsel > 1) { message("You have more than one file selected.", H_MESSAGE); return 0; }
        if (nsel == 1) {
            for (int i = 0; i < p->nfiles; i++) {
                char full[96];
                file_path(p, i, full);
                if (!strcmp(full, selset[0])) { fs_open(p, i); return 0; }
            }
        }
        if (p->nfiles) fs_open(p, p->fcur);
        return 0;
    case 1: print_files(); return 0;
    case 2:
        if (nsel > 1) { message("You have more than one file selected.", H_MESSAGE); return 0; }
        associate();
        return 0;
    case 4: return copy_move(1);
    case 5: return copy_move(0);
    case 6: return focus == 1 ? delete_dir() : delete_files();
    case 7: return focus == 1 ? rename_dir() : rename_files();
    case 8: return change_attr();
    case 9:
        if (nsel > 1) { message("You have more than one file selected.", H_MESSAGE); return 0; }
        for (int i = 0; i < p->nfiles; i++) {
            char full[96];
            file_path(p, i, full);
            if (nsel ? !strcmp(full, selset[0]) : i == p->fcur) { fs_view(p, i); return 0; }
        }
        return 0;
    case 11: return create_dir();
    case 12:
        for (int i = 0; i < p->nfiles; i++) {
            char full[96];
            file_path(p, i, full);
            if (!sel_has(full)) sel_toggle(full);
        }
        return 0;
    case 13: sel_clear(); return 0;
    }
    return 0;
}

/* ----------------------------------------------------------- Options ----- */
int fs_display_options(void)
{
    char mask[13];
    char list[48];
    strcpy(mask, fso.mask);
    strcpy(list, "Name\nExtension\nDate\nSize\nDisk order");
    struct dfield f[3] = {
        { DF_FIXED, 2, 4, "Name: ", mask, 12, 12, 0, H_NAME, 0 },
        { DF_TEXT, 4, 31, "Sort by:", 0, 0, 0, 0, 0, 0 },
        { DF_RADIO, 6, 31, list, 0, 0, 0, fso.sort, H_SORTNAME, 0 },
    };
    f[2].max = fso.sort;
    struct dialog d = mkdlg("Display Options", 7, 14, 14, 50, f, 3, H_DISPOPT);
    if (dialog_run(&d) != K_ENTER) return 0;
    strupr_(mask);
    if (!mask[0]) strcpy(mask, "*.*");
    strcpy(fso.mask, mask);
    fso.sort = f[2].value;
    return 1;
}

void fs_file_options(void)
{
    char list[80];
    strcpy(list, "Confirm on delete\nConfirm on replace\nSelect across directories");
    int bits = (fso.confirm_delete ? 1 : 0) | (fso.confirm_replace ? 2 : 0) | (fso.across ? 4 : 0);
    struct dfield f[1] = { { DF_CHECK, 3, 5, list, 0, 0, 0, bits, H_CONFDEL, 0 } };
    struct dialog d = mkdlg("File Options", 5, 5, 11, 38, f, 1, H_FILEOPT);
    if (dialog_run(&d) != K_ENTER) return;
    fso.confirm_delete = f[0].value & 1;
    fso.confirm_replace = (f[0].value >> 1) & 1;
    fso.across = (f[0].value >> 2) & 1;
}

static void info_draw(void *p)
{
    (void)p;
    int at = CLR(C_SAMPLE);
    s_fill(2, 7, 21, 28, ' ', at);
    s_box(2, 7, 21, 28, at);
    s_put(3, 8, "     Show Information     ", at);
    fs_info_block(4, 9, at, 29, 19);
    s_hdiv(20, 7, 28, at);
    s_put(21, 8, "  Esc=Cancel   F1=Help    ", at);
}

void fs_show_info(void)
{
    push_layer(info_draw, 0);
    for (;;) {
        redraw();
        struct ev e;
        ev_get(&e);
        int k = 0;
        if (e.type == EV_KEY) k = e.key;
        else if (e.type == EV_DOWN || e.type == EV_DBL) {
            if (e.row == 21) k = fkey_hit("  Esc=Cancel   F1=Help    ", e.col - 8);
            else if (e.row < 2 || e.row > 22 || e.col < 7 || e.col > 34) k = K_ESC;
        }
        if (k == K_ESC || k == K_ENTER) break;
        if (k == K_F1) help_show(H_SHOWINFO, 0, 0);
    }
    pop_layer();
}
