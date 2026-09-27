/*
 * menu.c - Start Programs: the Main Group and its groups, starting programs,
 * and the Program / Group / Exit actions (Add, Change, Delete, Copy,
 * Reorder), all kept in the group's .MEU file.
 */
#include "shell.h"

static struct meugroup grp;         /* the group on the screen */
static char grpfile[80];            /* its .MEU file (full path) */
static int insub;                   /* 0 = Main Group */
static int sel;
static int mode;                    /* 0 normal, 1 reorder, 2 copy */
static struct meuitem copyitem;
static int reorder_from;

static const struct mitem prog_items[] = {
    { "Start", 0, 0 }, { 0, 0, 0 }, { "Add...", 0, 0 }, { "Change...", 3, 0 },
    { "Delete...", 0, 0 }, { "Copy...", 0, 0 },
};
static const struct mitem group_items[] = {
    { "Add...", 0, 0 }, { "Change...", 3, 0 }, { "Delete...", 0, 0 }, { "Reorder...", 0, 0 },
};
static const struct mitem exit_items[] = {
    { "Exit Shell", 1, "F3" }, { "Resume Start Programs", 0, 0 },
};
static const struct menu sp_menus[] = {
    { "Program", 0, 3, 18, prog_items, 6 },
    { "Group", 0, 12, 18, group_items, 4 },
    { "Exit", 1, 19, 29, exit_items, 2 },
};

static int is_builtin(const struct meuitem *it)
{
    return it->isprog && (uint8_t)it->cmd[0] >= 0xFC && !it->cmd[1];
}

static int sp_avail(int m, int i)
{
    const struct meuitem *it = grp.n ? &grp.it[sel] : 0;
    int maint = opt.maint;
    if (m == 0) {
        switch (i) {
        case 0: return grp.n > 0;
        case 2: return maint && grp.n < MEU_MAX;
        case 3: case 4: case 5: return maint && it && it->isprog && !is_builtin(it);
        }
    } else if (m == 1) {
        switch (i) {
        case 0: return maint && !insub && grp.n < MEU_MAX;
        case 1: case 2: return maint && it && !it->isprog;
        case 3: return maint && grp.n > 1;
        }
    } else if (m == 2) {
        return i == 0 ? opt.exit : 1;
    }
    return 1;
}

static void load_group(const char *file)
{
    home_path(grpfile, file);
    while (meu_load(grpfile, &grp) < 0) {
        grp.n = 0;
        int c = warn_choice("Group file missing or unreadable.", "Try to read group file again",
                            insub ? "Return to the Main Group" : "Exit Shell to DOS", H_MESSAGE);
        if (c == 1) continue;
        if (insub) { insub = 0; st.group = 0; sel = st.mainsel; home_path(grpfile, opt.meu); continue; }
        shell_exit();
    }
    if (sel >= grp.n) sel = grp.n ? grp.n - 1 : 0;
}

static void save_group(void)
{
    if (meu_save(grpfile, &grp) < 0) message("Access denied.", H_MESSAGE);
}

static const char *sp_fkeys(void)
{
    if (!insub)
        return opt.prompt ? "  F10=Actions              Shift+F9=Command Prompt" : "  F10=Actions";
    return opt.prompt ? "  F10=Actions  Esc=Cancel  Shift+F9=Command Prompt" : "  F10=Actions  Esc=Cancel";
}

static void center_line(int r, const char *s, int attr)
{
    int n = strlen(s);
    s_put(r, (79 - n) / 2, s, attr);
}

static int bar_active = -1;

static void sp_draw(void *ctx)
{
    (void)ctx;
    title_mode = 0;
    draw_title("Start Programs");
    draw_actionbar(sp_menus, 3, bar_active, -1);
    s_fill(2, 0, 22, COLS, ' ', CLR(C_WORK));
    {                                   /* the group title (centred one column to the left) */
        const char *gt = insub ? st.subtitle : "Main Group";
        s_put(2, (78 - (int)strlen(gt)) / 2, gt, CLR(C_WORK));
    }
    if (mode == 1) {
        s_put(3, 17, "To complete the reorder, highlight the new", CLR(C_INSTR));
        s_put(4, 14, "position, then press Enter.  Press Esc to cancel.", CLR(C_INSTR));
    } else if (mode == 2) {
        center_line(3, "To complete the copy, display the destination group,", CLR(C_INSTR));
        s_put(4, 18, "then press F2.  Press F3 to cancel copy.", CLR(C_INSTR));
    } else {
        center_line(3, "To select an item, use the up and down arrows.", CLR(C_INSTR));
        center_line(4, "To start a program or display a new group, press Enter.", CLR(C_INSTR));
    }
    if (!grp.n) {
        s_put(6, 1, "Group is empty.", CLR(C_ITEM));
    }
    for (int i = 0; i < grp.n; i++) {
        int s = i == sel;
        if (s) s_fill(6 + i, 0, 1, 41, ' ', CLR(C_ITEMSEL));
        s_put(6 + i, 1, grp.it[i].title, s ? CLR(C_ITEMSEL) : CLR(C_ITEM));
    }
    draw_fkeys(sp_fkeys());
}

/* --------------------------------------------------------- passwords ---- */
static int check_password(const struct meuitem *it)
{
    char pw[9];
    if (!it->password[0]) return 1;
    pw[0] = 0;
    struct dfield f[3] = {
        { DF_TEXT, 3, 5, "Type password then press", 0, 0, 0, 0, 0, 0 },
        { DF_TEXT, 4, 5, "Enter.", 0, 0, 0, 0, 0, 0 },
        { DF_PASSWORD, 6, 5, "Password . . ", pw, 8, 8, 0, H_PASSWORD, 0 },
    };
    struct dialog d = { "Password", 5, 5, 11, 38, C_DLG, f, 3, BTN_ENTER, H_PASSWORD, 2, 0, 0, 0 };
    for (;;) {
        if (dialog_run(&d) != K_ENTER) return 0;
        if (!strcasecmp(pw, it->password)) return 1;
        message("Password incorrect.", H_MESSAGE);
        pw[0] = 0;
    }
}

/* --------------------------------------------------------- start --------- */
static void open_group(int i)
{
    const struct meuitem *it = &grp.it[i];
    if (!check_password(it)) return;
    st.mainsel = sel;
    strncpy(st.subfile, it->cmd, 12);
    strncpy(st.subtitle, it->title, 40);
    insub = 1;
    st.group = 1;
    sel = 0;
    load_group(st.subfile);
}

static void back_to_main(void)
{
    insub = 0;
    st.group = 0;
    sel = st.mainsel;
    load_group(opt.meu);
}

static void start_item(int i)
{
    static char cmds[SB_CMDSIZE];
    struct meuitem *it = &grp.it[i];
    if (!it->isprog) { open_group(i); return; }
    if (!check_password(it)) return;
    if (is_builtin(it)) {
        switch ((uint8_t)it->cmd[0]) {
        case 0xFC:
            if (!opt.prompt) { message("This Shell function is not active.", H_MESSAGE); return; }
            st.mainsel = insub ? st.mainsel : sel;
            command_prompt();
            return;
        case 0xFD:
            if (!opt.dos) { message("This Shell function is not active.", H_MESSAGE); return; }
            st.screen = 1;
            st.fs_from_sp = 1;
            if (!insub) st.mainsel = sel; else st.subsel = sel;
            if (filesys() == 1) shell_exit();
            st.screen = 0;
            return;
        case 0xFE:
            if (!opt.color) { message("This Shell function is not active.", H_MESSAGE); return; }
            change_colors();
            return;
        }
    }
    if (!psc_expand(it->title, it->cmd, cmds, sizeof cmds)) return;
    if (insub) st.subsel = sel; else st.mainsel = sel;
    launch(cmds, 0);
}

/* -------------------------------------------------------- maintenance --- */
static int item_dialog(const char *title, struct meuitem *it, int isprog)
{
    char t[MEU_TITLE + 1], c[MEU_CMD + 1], h[MEU_HELP + 1], pw[9];
    strcpy(t, it->title);
    strcpy(c, it->cmd);
    strcpy(h, it->help);
    strcpy(pw, it->password);
    struct dfield f[6] = {
        { DF_TEXT, 2, 2, "Required", 0, 0, 0, 0, 0, 0 },
        { DF_INPUT, 4, 3, "Title . . . .  ", t, isprog ? 40 : 37, 22, 0, H_TITLE, 0 },
        { isprog ? DF_INPUT : DF_FIXED, 6, 3, isprog ? "Commands  . .  " : "Filename  . .  ", c,
          isprog ? MEU_CMD : 8, isprog ? 22 : 8, 0, isprog ? H_COMMANDS : H_GRPFILE, 0 },
        { DF_TEXT, 8, 2, "Optional", 0, 0, 0, 0, 0, 0 },
        { DF_INPUT, 10, 3, "Help text . .  ", h, MEU_HELP, 22, 0, H_HELPTEXT, 0 },
        { DF_PASSWORD, 12, 3, "Password  . .  ", pw, 8, 8, 0, H_PASSWORD, 0 },
    };
    if (!isprog) {                      /* the file name is shown without .MEU */
        char *d = strchr(c, '.');
        if (d) *d = 0;
    }
    struct dialog d = { title, 7, 16, 16, 44, C_DLG, f, 6, "  Esc=Cancel   F1=Help   F2=Save",
                        isprog ? H_ADDPROG : H_ADDGRP, 1, 1, 1, 0 };
    for (;;) {
        if (dialog_run(&d) != K_F2) return 0;
        while (t[0] == ' ') memmove(t, t + 1, strlen(t));
        if (!t[0]) { d.focus = 1; continue; }
        if (!isprog) {
            strupr_(c);
            if (!c[0] || strchr(c, '.') || strchr(c, ' ')) {
                message("Filename extension invalid.", H_MESSAGE);
                d.focus = 2;
                continue;
            }
            strcat(c, ".MEU");
        } else if (!c[0]) { d.focus = 2; continue; }
        break;
    }
    strcpy(it->title, t);
    if (!isprog) {
        int n = strlen(it->title);
        if (n < 3 || strcmp(it->title + n - 3, "..."))
            strcat(it->title, "...");
    }
    strcpy(it->cmd, c);
    strcpy(it->help, h);
    strcpy(it->password, pw);
    it->isprog = isprog;
    return 1;
}

static void add_item(int isprog)
{
    struct meuitem it;
    memset(&it, 0, sizeof it);
    if (!item_dialog(isprog ? "Add Program" : "Add Group", &it, isprog)) return;
    if (grp.n >= MEU_MAX) { message("Group is full.", H_MESSAGE); return; }
    if (!isprog) {                      /* a new, empty group file */
        char path[96];
        static struct meugroup empty;       /* (16 KB: not on the stack) */
        memset(&empty, 0, sizeof empty);
        home_path(path, it.cmd);
        struct armregs r = {0};
        r.r0 = 0x4300;
        r.r3 = (uint32_t)path;
        if (_armdos_int21(&r)) meu_save(path, &empty);
    }
    grp.it[grp.n++] = it;               /* the selection stays where it was */
    save_group();
}

static void change_item(void)
{
    struct meuitem *it = &grp.it[sel];
    if (!check_password(it)) return;
    struct meuitem tmp = *it;
    if (item_dialog(it->isprog ? "Change Program" : "Change Group", &tmp, it->isprog)) {
        *it = tmp;
        save_group();
    }
}

static void delete_item(void)
{
    struct meuitem *it = &grp.it[sel];
    if (!check_password(it)) return;
    char list[64];
    strcpy(list, "1. Delete this item\n2. Do not delete this item");
    struct dfield f[1] = { { DF_CHOICE, 3, 4, list, 0, 0, 0, 0, H_DELITEM, 53 } };
    struct dialog d = { "Delete Item", 7, 10, 8, 58, C_DLG, f, 1, BTN_ENTER, H_DELITEM, 0, 0, 0, 23 };
    if (dialog_run(&d) != K_ENTER || f[0].value != 0) return;
    memmove(&grp.it[sel], &grp.it[sel + 1], (grp.n - sel - 1) * sizeof grp.it[0]);
    grp.n--;
    if (sel >= grp.n && sel) sel--;
    save_group();
}

static void finish_reorder(void)
{
    struct meuitem t = grp.it[reorder_from];
    if (sel == reorder_from) { mode = 0; return; }
    if (sel > reorder_from)
        memmove(&grp.it[reorder_from], &grp.it[reorder_from + 1], (sel - reorder_from) * sizeof t);
    else
        memmove(&grp.it[sel + 1], &grp.it[sel], (reorder_from - sel) * sizeof t);
    grp.it[sel] = t;
    mode = 0;
    save_group();
}

static void finish_copy(void)
{
    if (grp.n >= MEU_MAX) { message("Group is full.", H_MESSAGE); return; }
    grp.it[grp.n] = copyitem;
    sel = grp.n++;
    mode = 0;
    save_group();
}

static void item_help(void)
{
    if (!grp.n) { help_show(H_SPINSTR, 0, 0); return; }
    const struct meuitem *it = &grp.it[sel];
    help_item(it->title, it->help);     /* an item without help text: an empty panel */
}

static const int menu_help[3][6] = {
    { H_START, 0, H_ADDPROG, H_CHGPROG, H_DELPROG, H_COPYPROG },
    { H_ADDGRP, H_CHGGRP, H_DELGRP, H_REORDER, 0, 0 },
    { H_EXITSHELL, H_RESUMESP, 0, 0, 0, 0 },
};
static const int bar_help[3] = { H_PROGRAM, H_GROUP, H_SPEXIT };

/* 1 = leave the Shell */
static int do_action(int a)
{
    int m = a >> 8, i = a & 0xFF;
    if (m == 0) {
        switch (i) {
        case 0: start_item(sel); break;
        case 2: add_item(1); break;
        case 3: change_item(); break;
        case 4: delete_item(); break;
        case 5:
            if (!check_password(&grp.it[sel])) break;
            copyitem = grp.it[sel];
            mode = 2;
            break;
        }
    } else if (m == 1) {
        switch (i) {
        case 0: add_item(0); break;
        case 1: change_item(); break;
        case 2: delete_item(); break;
        case 3: reorder_from = sel; mode = 1; break;
        }
    } else if (m == 2 && i == 0)
        return 1;
    return 0;
}

static int action_bar(int start, int opened)
{
    int helpid;
    for (;;) {
        bar_active = start;
        int a = run_actionbar(sp_menus, 3, sp_avail, start, opened, &helpid);
        bar_active = -1;
        if (a == -2) {
            int m = helpid >> 8, i = helpid & 0xFF;
            help_show(i == 0xFF ? bar_help[m] : menu_help[m][i] ? menu_help[m][i] : bar_help[m], 0, 0);
            start = m;
            opened = i != 0xFF;
            continue;
        }
        if (a < 0) return 0;
        return do_action(a);
    }
}

static int menu_at_col(int col)
{
    for (int i = 0; i < 3; i++) {
        int len = strlen(sp_menus[i].name);
        if (col >= sp_menus[i].col - 1 && col <= sp_menus[i].col + len) return i;
    }
    return -1;
}

int sp_run(void)
{
    insub = st.group;
    if (insub) {
        sel = st.subsel;
        load_group(st.subfile);
    } else {
        sel = st.mainsel;
        load_group(opt.meu);
    }
    push_layer(sp_draw, 0);
    for (;;) {
        st.screen = 0;
        redraw();
        struct ev e;
        ev_get(&e);
        int k = 0;
        if (e.type == EV_KEY) k = e.key;
        else if (e.type == EV_DOWN || e.type == EV_DBL) {
            if (e.row == 1) {
                int m = menu_at_col(e.col);
                if (m >= 0) { if (action_bar(m, 1)) break; continue; }
                if (e.col >= 69 && e.col <= 75) k = K_F1;
            } else if (e.row == 24) {
                k = fkey_hit(sp_fkeys(), e.col);
            } else if (e.row >= 6 && e.row < 6 + grp.n && e.col <= 40) {
                sel = e.row - 6;
                if (e.type == EV_DBL) k = mode == 1 ? K_ENTER : mode ? 0 : K_ENTER;
            }
        }
        if (!k) continue;
        if (mode == 1) {                /* reordering */
            if (k == K_ESC) mode = 0;
            else if (k == K_ENTER) finish_reorder();
            else if (k == K_UP && sel > 0) sel--;
            else if (k == K_DOWN && sel < grp.n - 1) sel++;
            continue;
        }
        switch (k) {
        case K_UP: if (grp.n) sel = (sel + grp.n - 1) % grp.n; break;     /* the list wraps */
        case K_DOWN: if (grp.n) sel = (sel + 1) % grp.n; break;
        case K_HOME: case K_PGUP: sel = 0; break;
        case K_END: case K_PGDN: sel = grp.n ? grp.n - 1 : 0; break;
        case K_ENTER:
            if (!grp.n) break;
            if (mode == 2 && !grp.it[sel].isprog) { open_group(sel); break; }
            if (mode == 2) break;
            start_item(sel);
            break;
        case K_ESC:
            if (insub) back_to_main();
            break;
        case K_F2:
            if (mode == 2) finish_copy();
            break;
        case K_F3:
            if (mode == 2) { mode = 0; break; }
            if (opt.exit) goto out;
            break;
        case K_F1: item_help(); break;
        case K_F10:
            if (action_bar(0, 0)) goto out;
            break;
        case K_SF9:
            if (opt.prompt) {
                if (insub) st.subsel = sel; else st.mainsel = sel;
                command_prompt();
            }
            break;
        default: {
            int a = KASCII(k);
            if (a > ' ' && grp.n) {     /* jump to the next title starting with the letter */
                for (int j = 1; j <= grp.n; j++) {
                    int t = (sel + j) % grp.n;
                    if (upc((uint8_t)grp.it[t].title[0]) == upc(a)) { sel = t; break; }
                }
            }
        }
        }
        if (insub) st.subsel = sel; else st.mainsel = sel;
    }
out:
    pop_layer();
    return 1;
}
