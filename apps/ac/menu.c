/*
 * menu.c - F9 pull-down menus, F1 help, F2 user menu (AC.MNU).
 */
#include <stdlib.h>
#include "acm.h"

typedef struct { const char *text; const char *key; int id; } mitem;

#define SEP { 0, 0, 0 }
static const mitem m_left[] = {
    { "&Brief", "", C_BRIEF_L }, { "&Full", "", C_FULL_L }, { "&Info", "", C_INFO_L },
    { "&Tree", "", C_TREE_L }, { "&On/Off", "Ctrl-F1", C_ONOFF_L }, SEP,
    { "&Name", "", C_SNAME_L }, { "e&Xtension", "", C_SEXT_L }, { "ti&Me", "", C_STIME_L },
    { "&Size", "", C_SSIZE_L }, { "&Unsorted", "", C_SUNS_L }, SEP,
    { "&Re-read", "", C_REREAD_L }, { "fi&Lter...", "", C_FILTER_L }, { "&Drive...", "Alt-F1", C_DRIVE_L },
    { 0, 0, -1 }
};
static const mitem m_right[] = {
    { "&Brief", "", C_BRIEF_R }, { "&Full", "", C_FULL_R }, { "&Info", "", C_INFO_R },
    { "&Tree", "", C_TREE_R }, { "&On/Off", "Ctrl-F2", C_ONOFF_R }, SEP,
    { "&Name", "", C_SNAME_R }, { "e&Xtension", "", C_SEXT_R }, { "ti&Me", "", C_STIME_R },
    { "&Size", "", C_SSIZE_R }, { "&Unsorted", "", C_SUNS_R }, SEP,
    { "&Re-read", "", C_REREAD_R }, { "fi&Lter...", "", C_FILTER_R }, { "&Drive...", "Alt-F2", C_DRIVE_R },
    { 0, 0, -1 }
};
static const mitem m_files[] = {
    { "&Help", "F1", C_HELP }, { "&User menu", "F2", C_USERMENU }, { "&View", "F3", C_VIEW },
    { "&Edit", "F4", C_EDIT }, { "&Copy", "F5", C_COPY }, { "&Rename or move", "F6", C_RENMOV },
    { "&Make directory", "F7", C_MKDIR }, { "&Delete", "F8", C_DELETE }, SEP,
    { "File &attributes", "", C_ATTRIB }, SEP,
    { "&Select group", "Gray +", C_SELECT }, { "U&nselect group", "Gray -", C_UNSELECT },
    { "&Invert selection", "Gray *", C_INVERT }, SEP,
    { "&Quit", "F10", C_QUIT },
    { 0, 0, -1 }
};
static const mitem m_cmds[] = {
    { "&Find file", "Alt-F7", C_FIND }, { "&History", "Alt-F8", C_HISTORY }, SEP,
    { "&Swap panels", "Ctrl-U", C_SWAP }, { "&Panels on/off", "Ctrl-O", C_PANELS },
    { "&Compare directories", "", C_COMPARE },
    { 0, 0, -1 }
};
static const mitem m_opts[] = {
    { "&Configuration...", "", C_CONFIG }, SEP,
    { "&Key bar", "Ctrl-B", C_KEYBAR }, { "&Mini status", "", C_MINISTATUS }, { "C&lock", "", C_CLOCK }, SEP,
    { "&Save setup", "Shift-F9", C_SAVESETUP },
    { 0, 0, -1 }
};

static const struct { const char *name; int x; const mitem *items; } bar[5] = {
    { "Left", 4, m_left }, { "Files", 12, m_files }, { "Commands", 21, m_cmds },
    { "Options", 33, m_opts }, { "Right", 44, m_right },
};

static int checked(int id)
{
    for (int s = 0; s < 2; s++) {
        struct acpanel *p = &S->p[s];
        int base = s ? C_BRIEF_R : C_BRIEF_L;
        int off = id - base;
        if (off >= 0 && off <= 3) return p->mode == off && p->visible;
        if (off >= 5 && off <= 9) return p->sort == off - 5 && (p->mode == M_BRIEF || p->mode == M_FULL);
    }
    switch (id) {
    case C_KEYBAR: return S->keybar;
    case C_MINISTATUS: return S->ministatus;
    case C_CLOCK: return S->clock;
    }
    return 0;
}

static int nitems(const mitem *m) { int n = 0; while (m[n].id >= 0) n++; return n; }

static int hotletter(const char *t)
{
    const char *a = strchr(t, '&');
    int c = a ? a[1] : t[0];
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

static void draw_bar(int which)
{
    fill(0, 0, SCR_W, 1, ' ', A_MENU);
    for (int i = 0; i < 5; i++) {
        int a = i == which ? A_MENUSEL : A_MENU;
        fill(bar[i].x - 2, 0, strlen(bar[i].name) + 4, 1, ' ', a);
        put(bar[i].x, 0, bar[i].name, a);
    }
}

static void geometry(int which, int *bx, int *w)
{
    const mitem *m = bar[which].items;
    int tw = 0, kw = 0;
    for (int i = 0; m[i].id >= 0; i++) {
        if (!m[i].text) continue;
        int t = strwidth_hot(m[i].text), k = strlen(m[i].key);
        if (t > tw) tw = t;
        if (k > kw) kw = k;
    }
    *w = tw + kw + 7 + (kw ? 1 : 0);
    if (*w < 22) *w = 22;
    *bx = bar[which].x - 2;
    if (*bx + *w + 2 > SCR_W) *bx = SCR_W - *w - 2;
}

int menu_run(int which)
{
    uint16_t save[SCR_W * SCR_H];
    int sel = 0;
    save_scr(save);
    void (*oldidle)(void) = idle_hook;
    idle_hook = 0;
    if (which < 0 || which > 4) which = 0;
    int id = 0;
    for (;;) {
        const mitem *m = bar[which].items;
        int n = nitems(m), bx, w;
        if (sel >= n) sel = 0;
        while (!m[sel].text) sel = (sel + 1) % n;
        restore_scr(save);
        draw_bar(which);
        geometry(which, &bx, &w);
        int h = n + 2;
        fill(bx, 1, w, h, ' ', A_MENU);
        box(bx, 1, w, h, A_MENU, 0);
        shadow(bx, 1, w, h);
        for (int i = 0; i < n; i++) {
            int y = 2 + i;
            if (!m[i].text) { fill(bx + 2, y, w - 4, 1, 0xC4, A_MENU); continue; }
            int s = i == sel;
            int a = s ? A_MENUSEL : A_MENU, ha = s ? A_MENUSELHOT : A_MENUHOT;
            if (s) fill(bx + 1, y, w - 2, 1, ' ', a);
            if (checked(m[i].id)) putc_(bx + 1, y, 0xFB, a);
            puthot(bx + 3, y, m[i].text, a, ha);
            put(bx + w - 2 - strlen(m[i].key), y, m[i].key, a);
        }
        flush();
        cursor_at(0, -1);
        int k = getkey();
        if (k == K_ESC || k == K_F(9) || k == K_F(10)) break;
        if (k == K_LEFT) { which = (which + 4) % 5; sel = 0; continue; }
        if (k == K_RIGHT) { which = (which + 1) % 5; sel = 0; continue; }
        if (k == K_UP) { do sel = (sel + n - 1) % n; while (!m[sel].text); continue; }
        if (k == K_DOWN) { do sel = (sel + 1) % n; while (!m[sel].text); continue; }
        if (k == K_HOME || k == K_PGUP) { sel = 0; continue; }
        if (k == K_END || k == K_PGDN) { sel = n - 1; continue; }
        if (k == K_ENTER) { id = m[sel].id; break; }
        if (k == K_MOUSE) {
            if (ms_y == 0) {
                for (int i = 0; i < 5; i++)
                    if (ms_x >= bar[i].x - 2 && ms_x < bar[i].x + (int)strlen(bar[i].name) + 2) { which = i; sel = 0; }
                continue;
            }
            if (ms_x > bx && ms_x < bx + w - 1 && ms_y >= 2 && ms_y < 2 + n && m[ms_y - 2].text) {
                sel = ms_y - 2;
                id = m[sel].id;
                wait_release();
                break;
            }
            break;
        }
        int c = k & 0xFF;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c > ' ') {
            int hit = -1;
            for (int i = 0; i < n; i++) if (m[i].text && hotletter(m[i].text) == c) { hit = i; break; }
            if (hit >= 0) { id = m[hit].id; sel = hit; break; }
        }
    }
    S->lastmenu = which;
    idle_hook = oldidle;
    restore_scr(save);
    return id;
}

/* ---- F1 ---- */
static const char *const helptext[] = {
    "The ARM Commander, Version 1.0",
    "(C) 1989 Europa Micro Systems",
    "",
    "Panels",
    "  Tab             other panel",
    "  Enter           enter a directory / run a program",
    "  Ctrl-PgUp       parent directory",
    "  Ctrl-\\          root directory",
    "  Alt+letter      quick search",
    "  Ctrl-O          panels off / on (user screen)",
    "  Ctrl-F1 Ctrl-F2 left / right panel off / on",
    "  Alt-F1 Alt-F2   left / right drive",
    "  Ctrl-L          info panel",
    "  Ctrl-U          swap panels",
    "  Ctrl-P          other panel off / on",
    "  Ctrl-R          re-read",
    "  Ctrl-F3..F7     sort by name, extension, time, size,",
    "                  unsorted",
    "",
    "Selecting files",
    "  Ins             select / unselect the file",
    "  Gray +  Gray -  select / unselect a group",
    "  Gray *          invert the selection",
    "",
    "The command line",
    "  Ctrl-Enter      the file name to the command line",
    "  Ctrl-[ Ctrl-]   left / right path to the command line",
    "  Ctrl-E Ctrl-X   previous / next command",
    "  Esc             clear the command line",
    "",
    "Function keys",
    "  F1 Help  F2 User menu  F3 View  F4 Edit  F5 Copy",
    "  F6 Rename or move  F7 Make directory  F8 Delete",
    "  F9 Pull-down menus  F10 Quit",
    "  Shift-F4 edit a new file  Shift-F9 save the setup",
    "  Alt-F7 Find file  Alt-F8 History",
    "",
    "The mouse (MOUSE.COM)",
    "  Left button     move the cursor / double click = Enter",
    "  Right button    select / unselect",
    "  Top line        the pull-down menus",
    "  Key bar         the function keys",
};

void help_show(void)
{
    int sel = 0;
    listbox("Help", helptext, sizeof helptext / sizeof helptext[0], 62, 20, &sel, &PAL_CYAN, 0);
}

/* ---- F2: the user menu, AC.MNU ---- */
static char *read_text(const char *path)
{
    int h = d_open(path, 0);
    if (h < 0) return 0;
    uint32_t sz = d_filesize(h);
    if (sz > 16384) sz = 16384;
    char *b = malloc(sz + 1);
    if (!b) { d_close(h); return 0; }
    int n = d_read(h, b, sz);
    d_close(h);
    b[n < 0 ? 0 : n] = 0;
    return b;
}

static void subst(char *out, const char *in, const char *name)
{
    char base[13];
    strcpy(base, name);
    char *d = strchr(base, '.');
    if (d) *d = 0;
    int n = 0;
    while (*in && n < 120) {
        if (in[0] == '!' && in[1] == '.' && in[2] == '!') {
            for (const char *s = name; *s && n < 120; s++) out[n++] = *s;
            in += 3;
        } else if (in[0] == '!') {
            for (const char *s = base; *s && n < 120; s++) out[n++] = *s;
            in++;
        } else out[n++] = *in++;
    }
    out[n] = 0;
}

void usermenu(void)
{
    char path[96];
    d_getcwd(d_getdrive(), path);
    path_join(path, path, "AC.MNU");
    char *t = read_text(path);
    if (!t) { path_join(path, progdir, "AC.MNU"); t = read_text(path); }
    if (!t) {
        static const char *const ok[] = { "OK" };
        message("User menu", "No user menu file (AC.MNU) was found", 0, &PAL_CYAN, ok, 1);
        return;
    }
    /* entries: "K: title" lines, followed by indented command lines */
    char *titles[32], *cmds[32];
    char *end = t + strlen(t);
    int n = 0;
    for (char *p = t; *p && n < 32;) {
        char *e = p;
        while (*e && *e != '\n') e++;
        char save = *e;
        *e = 0;
        if (e > p && e[-1] == '\r') e[-1] = 0;
        if (*p && *p != ' ' && *p != '\t') { titles[n] = p; cmds[n] = 0; n++; }
        else if (n && *p) { if (!cmds[n - 1]) cmds[n - 1] = p; }
        p = save ? e + 1 : e;
    }
    if (!n) { free(t); return; }
    char disp[32][60];
    const char *lp[32];
    for (int i = 0; i < n; i++) {
        char *c = strchr(titles[i], ':');
        disp[i][0] = 0;
        if (c && c - titles[i] <= 3) {
            memcpy(disp[i], titles[i], c - titles[i]);
            disp[i][c - titles[i]] = 0;
            strcat(disp[i], "  ");
            char *s = c + 1;
            while (*s == ' ') s++;
            strncat(disp[i], s, 50);
        } else strncpy(disp[i], titles[i], 55);
        lp[i] = disp[i];
    }
    int sel = 0, k;
    for (;;) {
        k = listbox("User menu", lp, n, 50, n + 2 < 18 ? n + 2 : 18, &sel, &PAL_CYAN, 0);
        if (k == K_ENTER || k == K_ESC || k == K_MOUSE) break;
        int c = k & 0xFF, hit = -1;
        if (c >= 'a' && c <= 'z') c -= 32;
        for (int i = 0; i < n; i++) if ((titles[i][0] & 0xDF) == c && titles[i][1] == ':') hit = i;
        if (hit >= 0) { sel = hit; k = K_ENTER; break; }
    }
    if (k == K_ENTER && cmds[sel]) {
        /* count the command lines of the entry */
        fent *f = panel_curfile(ACT);
        const char *nm = f ? f->name : "";
        char lines[8][128];
        int nlines = 0;
        for (char *p = cmds[sel]; nlines < 8; ) {
            while (*p == ' ' || *p == '\t') p++;
            subst(lines[nlines++], p, nm);
            p += strlen(p) + 1;
            while (p < end && !*p) p++;
            if (p >= end || (*p != ' ' && *p != '\t')) break;
        }
        if (nlines == 1) { free(t); execute(lines[0]); return; }
        char bat[96];
        path_join(bat, progdir, "AC$MENU.BAT");
        int h = d_creat(bat, 0);
        if (h >= 0) {
            d_write(h, "@ECHO OFF\r\n", 11);
            for (int i = 0; i < nlines; i++) { d_write(h, lines[i], strlen(lines[i])); d_write(h, "\r\n", 2); }
            d_close(h);
            free(t);
            execute(bat);
            return;
        }
    }
    free(t);
}
