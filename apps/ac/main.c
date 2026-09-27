/*
 * main.c - ACMAIN.EXE: the ARM Commander, a two-panel file manager in the
 * style of the 1989 Norton Commander 3.0. Started by AC.EXE (ac.c), which
 * runs the commands while ACMAIN is out of memory.
 *
 * Original code for ARM-DOS (Europa Micro Systems); no Symantec code.
 */
#include <stdlib.h>
#include <stddef.h>
#include "acm.h"

struct acstate *S;
static struct acblk *blk;
static struct acblk ownblk;
int standalone;
char cmdline[128];
int cmdpos;
char progdir[80];
static int cmdoff;
static int histpos = -1;
static int lastshift = -1;
static int pending = 0;
static uint8_t prevmode[2] = { M_BRIEF, M_BRIEF };
static int lastsec = -1;

static const char *const kb_norm[10] = { "Help", "Menu", "View", "Edit", "Copy", "RenMov", "Mkdir", "Delete", "PullDn", "Quit" };
static const char *const kb_alt[10] = { "Left", "Right", "", "", "", "", "Find", "Histry", "", "" };
static const char *const kb_ctrl[10] = { "Left", "Right", "Name", "Extens", "Time", "Size", "Unsort", "", "", "" };
static const char *const kb_shift[10] = { "", "", "", "Edit..", "", "", "", "", "Save", "" };

/* ---- the prompt ---- */
static void make_prompt(char *o, int max)
{
    const char *p = getenv("PROMPT");
    char cwd[80];
    int n = 0;
    if (!p) p = "$n$g";
    for (; *p && n < max - 1; p++) {
        if (*p != '$') { o[n++] = *p; continue; }
        p++;
        if (!*p) break;
        const char *ins = 0;
        char tmp[16];
        switch (*p | 0x20) {
        case 'p': d_getcwd(d_getdrive(), cwd); ins = cwd; break;
        case 'n': tmp[0] = 'A' + d_getdrive(); tmp[1] = 0; ins = tmp; break;
        case 'g': ins = ">"; break;
        case 'l': ins = "<"; break;
        case 'b': ins = "|"; break;
        case 'q': ins = "="; break;
        case '$': ins = "$"; break;
        case 'v': ins = "ARM-DOS Version 4.00"; break;
        case 'h': if (n) n--; break;
        case 'e': tmp[0] = 27; tmp[1] = 0; ins = tmp; break;
        case '_': n = 0; break;              /* only the last line of a two-line prompt */
        case 't': case 'd': break;
        }
        while (ins && *ins && n < max - 1) o[n++] = *ins++;
    }
    o[n] = 0;
}

/* ---- the user screen ---- */
static void capture_user_screen(void)
{
    volatile uint16_t *v = ARMDOS_TEXT_VRAM;
    int crow = ARMDOS_BDA[0x51];
    int shift = crow - CMDROW;
    for (int r = 0; r < SCR_H; r++) {
        int from = r + shift;
        for (int c = 0; c < SCR_W; c++)
            usr[r * SCR_W + c] = (from >= 0 && from < SCR_H && r < CMDROW) ? v[from * SCR_W + c] : 0x0720;
    }
}

static void show_user_screen(void)
{
    volatile uint16_t *v = ARMDOS_TEXT_VRAM;
    mouse_show(0);
    for (int i = 0; i < SCR_W * SCR_H; i++) v[i] = i < CMDROW * SCR_W ? usr[i] : 0x0720;
    cursor_at(0, CMDROW);
}

static void tty(const char *s)
{
    for (; *s; s++) {
        struct armregs r = {0};
        r.r0 = 0x0E00 | (uint8_t)*s;
        _armdos_int10(&r);
    }
}

/* ---- drawing ---- */
static const char *const *keylabels(void)
{
    int s = shift_state();
    if (s & 8) return kb_alt;
    if (s & 4) return kb_ctrl;
    if (s & 3) return kb_shift;
    return kb_norm;
}

static void draw_clock(void)
{
    struct armregs r = {0};
    r.r0 = 0x2C00;
    _armdos_int21(&r);
    int h = (r.r2 >> 8) & 0xFF, m = r.r2 & 0xFF;
    char t[8];
    uint16_t dt = (h << 11) | (m << 5);
    fmt_time(t, dt);
    put(74, 0, t, A_CURSOR);
    lastsec = r.r3 >> 8;
}

void redraw(void)
{
    char pr[80];
    memcpy(sb, usr, sizeof sb);
    if (S->panels_on)
        for (int i = 0; i < 2; i++)
            if (P[i].st->visible) panel_draw(&P[i], i == S->active);
    fill(0, CMDROW, SCR_W, 1, ' ', A_CMD);
    make_prompt(pr, sizeof pr);
    int pl = strlen(pr);
    if (pl > 60) { memmove(pr, pr + pl - 60, 61); pl = 60; }
    put(0, CMDROW, pr, A_CMD);
    int room = SCR_W - pl - 1;
    if (cmdpos < cmdoff) cmdoff = cmdpos;
    if (cmdpos > cmdoff + room) cmdoff = cmdpos - room;
    int l = strlen(cmdline);
    if (cmdoff > l) cmdoff = 0;
    put(pl, CMDROW, cmdline + cmdoff, A_CMD);
    if (S->keybar) keybar_draw(keylabels());
    if (S->clock && S->panels_on) draw_clock();
    lastshift = shift_state() & 0x0F;
    flush();
    cursor_at(pl + cmdpos - cmdoff, CMDROW);
}

static void idle(void)
{
    if (S->keybar) {
        lastshift = shift_state() & 0x0F;
        keybar_draw(keylabels());
        flush();
    }
    if (S->clock && S->panels_on) {
        struct armregs r = {0};
        r.r0 = 0x2C00;
        _armdos_int21(&r);
        if ((int)(r.r3 >> 8) != lastsec) { draw_clock(); flush(); }
    }
}

/* ---- state ---- */
static void defaults(void)
{
    memset(S, 0, sizeof *S);
    S->magic = AC_MAGIC;
    S->panels_on = 1;
    S->keybar = 1;
    S->ministatus = 1;
    S->confirm_del = 1;
    for (int i = 0; i < 2; i++) {
        S->p[i].mode = M_BRIEF;
        S->p[i].visible = 1;
        strcpy(S->p[i].filter, "*.*");
    }
}

static void ini_path(char *o) { path_join(o, progdir, "AC.INI"); }

void save_setup(void)
{
    char f[96];
    ini_path(f);
    int h = d_creat(f, 0);
    if (h < 0) { error_box("Cannot save the setup", f); return; }
    struct acstate t = *S;
    t.nhist = 0;
    d_write(h, &t, offsetof(struct acstate, hist));
    d_close(h);
}

static void load_setup(void)
{
    char f[96];
    ini_path(f);
    int h = d_open(f, 0);
    if (h < 0) return;
    struct acstate t;
    if (d_read(h, &t, offsetof(struct acstate, hist)) == (int)offsetof(struct acstate, hist) && t.magic == AC_MAGIC) {
        memcpy(S, &t, offsetof(struct acstate, hist));
        S->nhist = 0;
    }
    d_close(h);
}

static void save_state(void)
{
    for (int i = 0; i < 2; i++) {
        fent *f = P[i].n ? &P[i].f[P[i].st->cur] : 0;
        strcpy(P[i].st->curname, f ? f->name : "");
    }
}

void set_active_dir(void)
{
    panel *p = ACT;
    char path[128];
    if (p->st->mode == M_TREE && p->tn) tree_path(p, p->tcur, path);
    else strcpy(path, p->st->path);
    d_setdrive(path[0] - 'A');
    d_chdir(path);
}

void panels_reread_all(void)
{
    for (int i = 0; i < 2; i++) {
        if (P[i].st->mode == M_TREE) tree_read(&P[i]);
        else panel_reread(&P[i]);
    }
}

void hist_add(const char *c)
{
    if (S->nhist && !strcmp(S->hist[S->nhist - 1], c)) return;
    if (S->nhist == AC_HIST) { memmove(S->hist[0], S->hist[1], (AC_HIST - 1) * AC_HISTLEN); S->nhist--; }
    strncpy(S->hist[S->nhist], c, AC_HISTLEN - 1);
    S->hist[S->nhist][AC_HISTLEN - 1] = 0;
    S->nhist++;
}

/* ---- running commands ---- */
static int upc(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int internal(const char *c)
{
    char w[8];
    int i = 0;
    while (*c == ' ') c++;
    /* "X:" alone: change drive */
    if (((c[0] | 0x20) >= 'a' && (c[0] | 0x20) <= 'z') && c[1] == ':') {
        const char *e = c + 2;
        while (*e == ' ') e++;
        if (!*e) {
            int d = upc(c[0]) - 'A';
            char cwd[80];
            uint32_t tot, fr;
            if (!d_drive_valid(d) || d_getcwd(d, cwd) || d_diskfree(d, &tot, &fr)) return 0;
            d_setdrive(d);
            if (ACT->st->mode == M_TREE) ACT->st->mode = prevmode[S->active];
            panel_setpath(ACT, cwd, 0);
            return 1;
        }
    }
    while (i < 6 && ((c[i] >= 'a' && c[i] <= 'z') || (c[i] >= 'A' && c[i] <= 'Z'))) { w[i] = upc(c[i]); i++; }
    w[i] = 0;
    if (strcmp(w, "CD") && strcmp(w, "CHDIR")) return 0;
    const char *a = c + i;
    while (*a == ' ') a++;
    if (!*a) return 0;              /* plain CD prints the directory: COMMAND does that */
    if (a[1] == ':' && upc(a[0]) - 'A' != d_getdrive()) return 0;
    char arg[80];
    strncpy(arg, a, 79); arg[79] = 0;
    for (int k = strlen(arg); k > 0 && arg[k - 1] == ' '; k--) arg[k - 1] = 0;
    if (d_chdir(arg)) return 0;     /* let COMMAND say "Invalid directory" */
    char cwd[80];
    d_getcwd(d_getdrive(), cwd);
    if (ACT->st->mode == M_TREE) tree_goto_path(ACT, cwd);
    else panel_setpath(ACT, cwd, 0);
    return 1;
}

static void restart_panels(void);

void execute(const char *cmd)
{
    char c[128], pr[80];
    while (*cmd == ' ') cmd++;
    strncpy(c, cmd, 127); c[127] = 0;
    for (int k = strlen(c); k > 0 && c[k - 1] == ' '; k--) c[k - 1] = 0;
    cmdline[0] = 0; cmdpos = 0; histpos = -1;
    if (!c[0]) return;
    hist_add(c);
    if (internal(c)) return;
    make_prompt(pr, sizeof pr);
    save_state();
    show_user_screen();
    tty(pr); tty(c); tty("\r\n");
    if (standalone) {
        system(c);
        restart_panels();
        return;
    }
    strcpy(blk->cmd, c);
    blk->action = ACT_RUN;
    exit(0);
}

static void quit(void)
{
    static const char *const yn[] = { "Yes", "No" };
    if (message("The ARM Commander", "Do you want to quit the ARM Commander?", 0, &PAL_CYAN, yn, 2) != 0) return;
    save_state();
    show_user_screen();
    if (blk) blk->action = ACT_QUIT;
    exit(0);
}

/* ---- panels ---- */
static int panel_usable(int i) { return S->panels_on && P[i].st->visible; }

static void enter_dir(panel *p, const char *name)
{
    char np[128], keep[13] = "";
    strcpy(np, p->st->path);
    if (!strcmp(name, "..")) {
        char *b = strrchr(np, '\\');
        if (!b) return;
        strcpy(keep, b + 1);
        if (b == np + 2) b[1] = 0; else *b = 0;
    } else {
        if (strlen(np) + strlen(name) + 2 > 66) return;
        path_join(np, np, name);
    }
    char old[80];
    strcpy(old, p->st->path);
    strcpy(p->st->path, np);
    struct dta d;
    char spec[96];
    path_join(spec, np, "*.*");
    if (d_findfirst(spec, 0x16, &d) && doserr == 3) {
        strcpy(p->st->path, old);
        error_box("Cannot change to", np);
        return;
    }
    panel_setpath(p, np, keep);
    set_active_dir();
}

static void enter_key(void)
{
    panel *p = ACT;
    if (!panel_usable(S->active)) return;
    if (p->st->mode == M_TREE) return;
    fent *f = panel_curfile(p);
    if (!f) return;
    if (f->attr & 0x10) { enter_dir(p, f->name); return; }
    if (is_exec(f->name)) {
        char n[16];
        strcpy(n, f->name);
        str_lower(n);
        execute(n);
    }
}

static void tree_follow(void)
{
    panel *p = ACT;
    char path[128];
    if (p->st->mode != M_TREE || !p->tn) return;
    tree_path(p, p->tcur, path);
    strcpy(p->st->path, path);
    set_active_dir();
    panel *o = OTH;
    if (o->st->mode == M_BRIEF || o->st->mode == M_FULL) panel_setpath(o, path, 0);
}

static void set_mode(int side, int mode)
{
    panel *p = &P[side];
    if (p->st->mode == mode && p->st->visible) { p->st->visible = 1; return; }
    int old = p->st->mode;
    if (old == M_BRIEF || old == M_FULL) prevmode[side] = old;
    p->st->visible = 1;
    p->st->mode = mode;
    if (mode == M_TREE) tree_read(p);
    else if (mode == M_INFO) { if (S->active == side && P[!side].st->visible && P[!side].st->mode != M_INFO) S->active = !side; }
    else if (old == M_TREE || old == M_INFO) panel_read(p, 0);
    else panel_fix(p);
    if (side == S->active) set_active_dir();
}

static void set_sort(int side, int sort)
{
    P[side].st->sort = sort;
    if (P[side].st->mode == M_BRIEF || P[side].st->mode == M_FULL) panel_sort(&P[side]);
}

static void toggle_panel(int side)
{
    P[side].st->visible = !P[side].st->visible;
    if (!P[side].st->visible && S->active == side && P[!side].st->visible) {
        S->active = !side;
        set_active_dir();
    }
    if (P[side].st->visible && !P[!side].st->visible) { S->active = side; set_active_dir(); }
}

static void swap_panels(void)
{
    struct acpanel t = S->p[0];
    S->p[0] = S->p[1];
    S->p[1] = t;
    panel tp = P[0];
    P[0] = P[1];
    P[1] = tp;
    P[0].x = 0; P[1].x = 40;
    P[0].st = &S->p[0]; P[1].st = &S->p[1];
    uint8_t m = prevmode[0]; prevmode[0] = prevmode[1]; prevmode[1] = m;
    S->active = !S->active;
}

static void quick_search(int c)
{
    char s[13];
    int n = 0;
    panel *p = ACT;
    if (p->st->mode != M_BRIEF && p->st->mode != M_FULL) return;
    for (;;) {
        if (c) {
            if (n < 12) {
                s[n++] = upc(c); s[n] = 0;
                int found = -1;
                for (int i = 0; i < p->n && found < 0; i++)
                    if (!strncmp(p->f[i].name, s, n)) found = i;
                if (found >= 0) { p->st->cur = found; panel_fix(p); }
                else s[--n] = 0;
            }
        }
        redraw();
        int bx = p->x + 7, by = 20;
        fill(bx, by, 26, 3, ' ', 0x30);
        box(bx + 1, by, 24, 3, 0x30, 0);
        put(bx + 3, by + 1, "Search:", 0x30);
        putn(bx + 11, by + 1, s, 12, 0x07);
        shadow(bx, by, 26, 3);
        flush();
        cursor_at(bx + 11 + n, by + 1);
        int k = getkey();
        static const char qrow[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";
        c = 0;
        int sc = k >> 8, lo = k & 0xFF;
        if (lo == 0 && sc >= 0x10 && sc <= 0x32 && qrow[sc - 0x10]) c = qrow[sc - 0x10];
        else if (lo == 0 && sc >= 0x78 && sc <= 0x81) c = sc == 0x81 ? '0' : '1' + sc - 0x78;
        else if (k < 0x10000 && lo > ' ' && lo < 0x7F && lo != '\\' && lo != '/' && lo != '*' && lo != '+') c = lo;
        else if (k == K_BS) { if (n) s[--n] = 0; continue; }
        else { if (k != K_ESC && k != K_ENTER) pending = k; else if (k == K_ENTER) pending = K_ENTER; break; }
        if (!c) break;
    }
}

static void do_drive(int side)
{
    panel *p = &P[side];
    if (!p->st->visible) p->st->visible = 1;
    op_drive(p);
    if (side == S->active || !P[S->active].st->visible) { S->active = side; set_active_dir(); }
}

void do_command(int id)
{
    panel *a = ACT;
    fent *f;
    char full[128];
    switch (id) {
    case C_HELP: help_show(); break;
    case C_USERMENU: usermenu(); break;
    case C_VIEW:
    case C_EDIT:
        if (!panel_usable(S->active)) break;
        f = panel_curfile(a);
        if (!f || (f->attr & 0x10)) break;
        path_join(full, a->st->path, f->name);
        if (id == C_VIEW) view_file(full); else edit_file(full);
        panels_reread_all();
        break;
    case C_EDITNEW: {
        char name[80] = "";
        ditem it[] = {
            { DI_TEXT, 3, 2, 0, "Edit the file:", 0, 0, 0 },
            { DI_INPUT, 3, 3, 44, 0, name, 67, 0 },
        };
        if (dialog("Edit", 50, 6, &PAL_GREY, it, 2, 1) != 0 || !name[0]) break;
        str_upper(name);
        if (name[1] == ':' || name[0] == '\\') strcpy(full, name); else path_join(full, a->st->path, name);
        edit_file(full);
        panels_reread_all();
        panel_goto_name(a, basename_(full));
        break;
    }
    case C_COPY: if (panel_usable(S->active)) op_copy(0); break;
    case C_RENMOV: if (panel_usable(S->active)) op_copy(1); break;
    case C_MKDIR: if (panel_usable(S->active)) op_mkdir(); break;
    case C_DELETE: if (panel_usable(S->active)) op_delete(); break;
    case C_PULLDN: {
        int c = menu_run(S->lastmenu == 0xFF ? (S->active ? 4 : 0) : S->lastmenu);
        if (c) do_command(c);
        break;
    }
    case C_QUIT: quit(); break;
    case C_BRIEF_L: case C_BRIEF_R: set_mode(id == C_BRIEF_R, M_BRIEF); break;
    case C_FULL_L: case C_FULL_R: set_mode(id == C_FULL_R, M_FULL); break;
    case C_INFO_L: case C_INFO_R: set_mode(id == C_INFO_R, M_INFO); break;
    case C_TREE_L: case C_TREE_R: set_mode(id == C_TREE_R, M_TREE); break;
    case C_ONOFF_L: case C_ONOFF_R: toggle_panel(id == C_ONOFF_R); break;
    case C_SNAME_L: case C_SNAME_R: set_sort(id == C_SNAME_R, S_NAME); break;
    case C_SEXT_L: case C_SEXT_R: set_sort(id == C_SEXT_R, S_EXT); break;
    case C_STIME_L: case C_STIME_R: set_sort(id == C_STIME_R, S_TIME); break;
    case C_SSIZE_L: case C_SSIZE_R: set_sort(id == C_SSIZE_R, S_SIZE); break;
    case C_SUNS_L: case C_SUNS_R: set_sort(id == C_SUNS_R, S_UNSORTED); break;
    case C_REREAD_L: case C_REREAD_R: {
        panel *p = &P[id == C_REREAD_R];
        if (p->st->mode == M_TREE) tree_read(p); else panel_reread(p);
        break;
    }
    case C_FILTER_L: case C_FILTER_R: op_filter(&P[id == C_FILTER_R]); break;
    case C_DRIVE_L: case C_DRIVE_R: do_drive(id == C_DRIVE_R); break;
    case C_SELECT: op_select(1); break;
    case C_UNSELECT: op_select(-1); break;
    case C_INVERT: op_select(0); break;
    case C_ATTRIB: op_attrib(); break;
    case C_FIND: op_findfile(); break;
    case C_HISTORY: op_history(); break;
    case C_SWAP: swap_panels(); break;
    case C_PANELS: S->panels_on = !S->panels_on; break;
    case C_COMPARE: op_compare(); break;
    case C_CONFIG: op_config(); break;
    case C_KEYBAR: S->keybar = !S->keybar; break;
    case C_MINISTATUS:
        S->ministatus = !S->ministatus;
        panel_fix(&P[0]); panel_fix(&P[1]);
        break;
    case C_CLOCK: S->clock = !S->clock; break;
    case C_SAVESETUP: save_setup(); break;
    }
}

/* ---- the mouse on the main screen ---- */
static void mouse_main(void)
{
    if (ms_y == KEYROW && S->keybar) {
        int slot = ms_x / 8;
        if (slot > 9) slot = 9;
        do_command(C_HELP + slot);
        return;
    }
    if (ms_y == 0 && S->panels_on) {
        int c = menu_run(ms_x < 10 ? 0 : ms_x < 19 ? 1 : ms_x < 31 ? 2 : ms_x < 42 ? 3 : 4);
        if (c) do_command(c);
        return;
    }
    if (!S->panels_on || ms_y >= CMDROW) return;
    int side = ms_x >= 40;
    panel *p = &P[side];
    if (!p->st->visible) return;
    if (p->st->mode == M_INFO) return;
    if (S->active != side) { S->active = side; set_active_dir(); }
    int i = panel_hit(p, ms_x, ms_y);
    if (p->st->mode == M_TREE) {
        if (i >= 0) { p->tcur = i; tree_follow(); }
        return;
    }
    if (i < 0) {
        if (ms_y == 1) panel_move(p, K_PGUP);
        else if (ms_y >= 2 + list_rows()) panel_move(p, K_PGDN);
        return;
    }
    p->st->cur = i;
    panel_fix(p);
    if (ms_btn == 2) {
        fent *f = &p->f[i];
        if (f->name[0] != '.') { f->sel = !f->sel; panel_selcount(p); }
    } else if (ms_dbl) {
        redraw();
        wait_release();
        enter_key();
    }
}

/* ---- startup ---- */
static void restart_panels(void)
{
    struct armregs r = {0};
    char cwd[80];
    /* a program may have left another video mode */
    r.r0 = 0x0F00;
    _armdos_int10(&r);
    if ((r.r0 & 0x7F) != 3 && (r.r0 & 0x7F) != 2) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x0003;
        _armdos_int10(&r);
    }
    /* the normal underline cursor */
    memset(&r, 0, sizeof r);
    r.r0 = 0x0100; r.r2 = 0x0D0E;
    _armdos_int10(&r);
    scr_init();
    capture_user_screen();
    d_getcwd(d_getdrive(), cwd);
    for (int i = 0; i < 2; i++) {
        P[i].st = &S->p[i];
        P[i].x = i * 40;
        char *path = P[i].st->path;
        char t[80];
        if (!path[0] || (i == S->active && P[i].st->mode != M_TREE)) strcpy(path, cwd);
        else if (d_getcwd(path[0] - 'A', t)) strcpy(path, cwd);
        if (P[i].st->mode == M_TREE) {
            tree_read(&P[i]);
            if (i == S->active) tree_goto_path(&P[i], cwd);
        }
        struct dta d;
        char spec[96];
        path_join(spec, path, "*.*");
        if (d_findfirst(spec, 0x16, &d) && doserr == 3) strcpy(path, cwd);
        int top = P[i].st->top;
        panel_read(&P[i], P[i].st->curname);
        P[i].st->top = top;
        panel_fix(&P[i]);
    }
    if (!P[S->active].st->visible && P[!S->active].st->visible) S->active = !S->active;
    set_active_dir();
    mouse_show(S->panels_on || 1);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (argv[i][0] == '/' && argv[i][1] == '$') {
            struct acblk *b = (struct acblk *)strtoul(argv[i] + 2, 0, 16);
            if (b && b->magic == AC_MAGIC) blk = b;
        }
    standalone = !blk;
    if (standalone) { blk = &ownblk; memset(blk, 0, sizeof *blk); }
    S = &blk->st;
    {
        const char *pp = _armdos_progpath;
        strncpy(progdir, pp && *pp ? pp : "C:\\DOS\\ACMAIN.EXE", 79);
        char *b = (char *)basename_(progdir);
        if (b > progdir && b[-1] == '\\' && b - progdir > 3) b[-1] = 0; else *b = 0;
    }
    if (S->magic != AC_MAGIC) {
        defaults();
        load_setup();
        S->lastmenu = 0xFF;
    }
    dos_hooks();
    idle_hook = idle;
    restart_panels();

    for (;;) {
        int k;
        panel *a = ACT;
        int usable = panel_usable(S->active);
        redraw();
        if (pending) { k = pending; pending = 0; }
        else k = getkey();
        int lo = k & 0xFF, hi = (k >> 8) & 0xFF;

        if (k == K_MOUSE) { mouse_main(); continue; }
        if (hi >= 0x3B && hi <= 0x44 && lo == 0) { do_command(C_HELP + hi - 0x3B); continue; }
        if (k == K_SF(4)) { do_command(C_EDITNEW); continue; }
        if (k == K_SF(9)) { do_command(C_SAVESETUP); continue; }
        if (k == K_CF(1)) { toggle_panel(0); continue; }
        if (k == K_CF(2)) { toggle_panel(1); continue; }
        if (k >= K_CF(3) && k <= K_CF(7) && lo == 0) { set_sort(S->active, S_NAME + (hi - 0x60)); continue; }
        if (k == K_AF(1)) { do_drive(0); continue; }
        if (k == K_AF(2)) { do_drive(1); continue; }
        if (k == K_AF(7)) { do_command(C_FIND); continue; }
        if (k == K_AF(8)) { do_command(C_HISTORY); continue; }
        if (k == K_TAB) {
            int o = !S->active;
            if (panel_usable(o) && P[o].st->mode != M_INFO) { S->active = o; set_active_dir(); }
            continue;
        }
        if (k == K_ESC) { cmdline[0] = 0; cmdpos = 0; histpos = -1; continue; }
        if (k == K_ENTER) {
            if (cmdline[0]) execute(cmdline);
            else enter_key();
            continue;
        }
        if (k == K_CENTER) {
            fent *f = usable ? panel_curfile(a) : 0;
            if (f && f->name[0] != '.') {
                char n[16];
                strcpy(n, f->name);
                str_lower(n);
                strcat(n, " ");
                int l = strlen(cmdline), nl = strlen(n);
                if (l + nl < (int)sizeof cmdline - 1) {
                    memmove(cmdline + cmdpos + nl, cmdline + cmdpos, l - cmdpos + 1);
                    memcpy(cmdline + cmdpos, n, nl);
                    cmdpos += nl;
                }
            }
            continue;
        }
        if (k == 0x1A1B || k == 0x1B1D) {       /* Ctrl-[ / Ctrl-]: paste the left/right path */
            const char *pth = P[k == 0x1B1D].st->path;
            int l = strlen(cmdline), nl = strlen(pth);
            if (l + nl < (int)sizeof cmdline - 1) {
                memmove(cmdline + cmdpos + nl, cmdline + cmdpos, l - cmdpos + 1);
                memcpy(cmdline + cmdpos, pth, nl);
                cmdpos += nl;
            }
            continue;
        }
        if (lo == 0x0F && hi == 0x18) { do_command(C_PANELS); continue; }         /* Ctrl-O */
        if (lo == 0x0C && hi == 0x26) {                                          /* Ctrl-L */
            int o = !S->active;
            if (P[o].st->mode == M_INFO) set_mode(o, prevmode[o]);
            else set_mode(o, M_INFO);
            continue;
        }
        if (lo == 0x15 && hi == 0x16) { do_command(C_SWAP); continue; }          /* Ctrl-U */
        if (lo == 0x12 && hi == 0x13) { do_command(S->active ? C_REREAD_R : C_REREAD_L); continue; } /* Ctrl-R */
        if (lo == 0x10 && hi == 0x19) { toggle_panel(!S->active); continue; }    /* Ctrl-P */
        if (lo == 0x02 && hi == 0x30) { do_command(C_KEYBAR); continue; }        /* Ctrl-B */
        if ((lo == 0x05 && hi == 0x12) || (lo == 0x18 && hi == 0x2D)) {         /* Ctrl-E / Ctrl-X */
            if (!S->nhist) continue;
            if (lo == 0x05) histpos = histpos < 0 ? S->nhist - 1 : (histpos > 0 ? histpos - 1 : 0);
            else { if (histpos < 0) continue; histpos++; if (histpos >= S->nhist) { histpos = -1; cmdline[0] = 0; cmdpos = 0; continue; } }
            strcpy(cmdline, S->hist[histpos]);
            cmdpos = strlen(cmdline);
            continue;
        }
        if (k == 0x2B1C && usable && a->st->mode != M_TREE) {                  /* Ctrl-\ : root */
            char root[4] = { a->st->path[0], ':', '\\', 0 };
            panel_setpath(a, root, 0);
            set_active_dir();
            continue;
        }
        if (k == K_CPGUP && usable && a->st->mode != M_TREE) { if (a->st->path[3]) enter_dir(a, ".."); continue; }
        if (k == K_CPGDN && usable) { fent *f = panel_curfile(a); if (f && (f->attr & 0x10)) enter_dir(a, f->name); continue; }

        /* selection */
        if (usable && (a->st->mode == M_BRIEF || a->st->mode == M_FULL) && !cmdline[0]) {
            if (k == K_INS) {
                fent *f = panel_curfile(a);
                if (f && f->name[0] != '.') { f->sel = !f->sel; panel_selcount(a); }
                panel_move(a, K_DOWN);
                continue;
            }
            if (k == K_GPLUS) { do_command(C_SELECT); continue; }
            if (k == K_GMINUS) { do_command(C_UNSELECT); continue; }
            if (k == K_GSTAR) { do_command(C_INVERT); continue; }
        }

        /* quick search: Alt+letter */
        if (lo == 0 && usable && ((hi >= 0x10 && hi <= 0x32) || (hi >= 0x78 && hi <= 0x81))) {
            static const char qrow[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0zxcvbnm";
            int c = 0;
            if (hi <= 0x32) c = qrow[hi - 0x10];
            else c = hi == 0x81 ? '0' : '1' + hi - 0x78;
            if (c) { quick_search(c); continue; }
        }

        /* the command line and the panel cursor */
        if (cmdline[0] && (k == K_LEFT || k == K_RIGHT || k == K_HOME || k == K_END ||
                           k == K_DEL || k == K_CLEFT || k == K_CRIGHT)) {
            edit_line(cmdline, sizeof cmdline, &cmdpos, k);
            continue;
        }
        if (k == K_UP || k == K_DOWN || k == K_LEFT || k == K_RIGHT || k == K_PGUP ||
            k == K_PGDN || k == K_HOME || k == K_END) {
            if (usable) {
                panel_move(a, k);
                if (a->st->mode == M_TREE) tree_follow();
            }
            continue;
        }
        if (k == K_BS) { edit_line(cmdline, sizeof cmdline, &cmdpos, k); continue; }
        if (lo >= 0x20 && hi != 0) edit_line(cmdline, sizeof cmdline, &cmdpos, k);
    }
}
