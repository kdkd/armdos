/*
 * main.c - ARM Disk Optimizer (DEFRAG.EXE): command line and program flow.
 *
 * DEFRAG [d:] [/F | /U] [/S[:]order[-]] [/H] [/FAST] [/AUTO] [/BW]
 *
 *   /F      Full Optimization (defragments files and packs them together)
 *   /U      Unfragment Files Only
 *   /S      sort the directory entries: N name, E extension, D date & time,
 *           S size; a "-" after the letter sorts in descending order
 *   /H      move hidden (not system) files too
 *   /FAST   no pacing (as fast as the disk goes)
 *   /AUTO   no questions: analyze, optimize, exit (exit code 0 done,
 *           1 error, 2 stopped); a one-line report goes to standard output
 *   /BW     black and white
 */
#include "defrag.h"

static int method = -1, sortsw = -1, drive = -1;

static void usage(void)
{
    outs("ARM Disk Optimizer  Version " VERSION "\r\n"
         "Copyright (C) 1990 Europa Micro Systems.  All rights reserved.\r\n\r\n"
         "Reorganizes the files on a disk so that each one occupies a single\r\n"
         "contiguous area.\r\n\r\n"
         "DEFRAG [d:] [/F | /U] [/S[:]order[-]] [/H] [/FAST] [/AUTO] [/BW]\r\n\r\n"
         "  d:      Drive to optimize.\r\n"
         "  /F      Full Optimization: defragments files and packs them together.\r\n"
         "  /U      Unfragment Files Only: may leave gaps between files.\r\n"
         "  /S      Sorts the files in each directory:\r\n"
         "            N  by name        D  by date and time\r\n"
         "            E  by extension   S  by size\r\n"
         "            -  after a letter reverses the order\r\n"
         "  /H      Moves hidden files.\r\n"
         "  /FAST   Runs at full speed.\r\n"
         "  /AUTO   Runs without asking; exits when finished.\r\n"
         "  /BW     Uses a black and white color scheme.\r\n");
}

static void parse(void)
{
    static struct arg a[10];
    int n = parse_tail(a, 10);
    for (int i = 0; i < n; i++) {
        struct arg *x = &a[i];
        char s[16];
        strncpy(s, x->text, 15); s[15] = 0;
        upcase(s);
        if (x->sw) {
            if (!strcmp(s, "/?")) { usage(); dos_exit(0); }
            else if (!strcmp(s, "/F")) method = M_FULL;
            else if (!strcmp(s, "/U")) method = M_UNFRAG;
            else if (!strcmp(s, "/H")) opt_hidden = 1;
            else if (!strcmp(s, "/FAST")) fast = 1;
            else if (!strcmp(s, "/AUTO")) autorun = 1;
            else if (!strcmp(s, "/BW")) bw = 1;
            else if (s[1] == 'S') {
                const char *v = x->val ? x->val : s + 2;
                char c = *v & 0xDF;
                sortsw = c == 'N' ? S_NAME : c == 'E' ? S_EXT : c == 'D' ? S_DATE : c == 'S' ? S_SIZE : -1;
                if (sortsw < 0 || (v[1] && v[1] != '-')) { parse_err(M_INVPARM, x->shown); dos_exit(1); }
                opt_desc = v[1] == '-';
            } else { parse_err(M_INVSW, x->shown); dos_exit(1); }
            continue;
        }
        if (drive >= 0) { parse_err(M_TOOMANY, x->shown); dos_exit(1); }
        if (!is_drive_spec(s)) { parse_err(M_INVPARM, x->shown); dos_exit(1); }
        drive = s[0] - 'A';
        if (!drive_valid(drive)) { errs(M_INVDRIVE); errs("\r\n"); dos_exit(1); }
    }
    if (sortsw >= 0) opt_sort = sortsw;
}

/* ------------------------------------------------------------- dialogs */

static int choose_drive(int cur)
{
    static char names[26][16];
    static const char *list[26];
    int map_[26], n = 0, sel = 0;
    for (int d = 0; d < 26; d++) {
        unsigned attr;
        if (ioctl_remote(d, &attr) || (attr & 0x9200)) continue;
        if (d == 1 && get_logical(1)) continue;         /* the single-drive B: */
        int rem = ioctl_removable(d);
        strcpy(names[n], "&A:  ");
        names[n][1] = 'A' + d;
        strcat(names[n], rem == 1 ? "Diskette " : "Hard disk");
        if (d == cur) sel = n;
        list[n] = names[n];
        map_[n++] = d;
    }
    if (!n) return -1;
    static const char *const l[] = { "Select the drive to optimize:" };
    static const char *const b[] = { "OK", "Cancel" };
    if (ui_dialog("Select Drive", l, 1, list, n, &sel, b, 2, 0) != 0) return -1;
    return map_[sel];
}

static void choose_method(void)
{
    static const char *const l[] = { "Choose the optimization method:" };
    static const char *const r[] = { "&Full Optimization", "&Unfragment Files Only" };
    static const char *const b[] = { "OK", "Cancel" };
    int sel = method == M_UNFRAG ? 1 : 0;
    if (ui_dialog("Optimization Method", l, 1, r, 2, &sel, b, 2, 0) == 0) method = sel ? M_UNFRAG : M_FULL;
}

static void choose_sort(void)
{
    static const char *const l[] = { "Sort the files in each directory by:" };
    static const char *const r[] = { "&Unsorted", "&Name (A to Z)", "Name (&Z to A)", "&Extension",
        "&Date & Time (oldest first)", "Date & &Time (newest first)", "&Size (smallest first)", "Size (&largest first)" };
    static const char *const b[] = { "OK", "Cancel" };
    int sel = opt_sort == S_NONE ? 0 : opt_sort == S_NAME ? 1 + opt_desc : opt_sort == S_EXT ? 3
            : opt_sort == S_DATE ? 4 + opt_desc : 6 + opt_desc;
    if (ui_dialog("File Sort", l, 1, r, 8, &sel, b, 2, 0) != 0) return;
    static const int so[] = { S_NONE, S_NAME, S_NAME, S_EXT, S_DATE, S_DATE, S_SIZE, S_SIZE };
    static const int de[] = { 0, 0, 1, 0, 0, 1, 0, 1 };
    opt_sort = so[sel]; opt_desc = de[sel];
}

static void about(void)
{
    static const char *const l[] = {
        "ARM Disk Optimizer",
        "Version " VERSION,
        "",
        "Copyright (C) 1990 Europa Micro Systems.",
        "All rights reserved.",
        "",
        "Shareware.  Downloaded from The ARM Pit BBS.",
    };
    ui_message("About Defrag", l, 7);
}

static void legend(void)
{
    static const char *const l[] = {
        "Each block on the map stands for one or more",
        "clusters (allocation units) of the disk.",
        "",
        "\xFE  Used: part of a file or directory.     ",
        "\xB0  Unused: free space.                    ",
        "r  Reading: data being read.              ",
        "W  Writing: data being written.           ",
        "B  Bad: marked bad, never used.           ",
        "X  Unmovable: system, hidden or open files",
    };
    ui_message("Map Legend", l, 9);
}

static void help(void)
{
    static const char *const l[] = {
        "Alt or F10    Opens the Optimize menu.",
        "Alt+B         Begins optimization.     ",
        "Alt+X         Exits Defrag.            ",
        "Esc           Stops an optimization.   ",
        "",
        "Close all other programs before you",
        "optimize a drive.",
    };
    ui_message("Help", l, 7);
}

/* ------------------------------------------------------------ the work */

static const char *mname(int m)
{
    return m == M_UNFRAG ? "Unfragment Files Only" : m == M_FULL ? "Full Optimization" : "No optimization necessary";
}

static int analyzed;

static int analyze(int d)
{
    char t[64];
    analyzed = 0;
    ui_status(0, 0, 0);
    ui_frame();
    strcpy(t, "Reading drive A: information...");
    t[14] = 'A' + d;
    ui_bar(t);
    const char *err = eng_open(d);
    if (!err) {
        ui_map_setup();
        err = eng_analyze();
    }
    if (err) {
        eng_close();
        ui_map_all(0);
        ui_bar("");
        const char *l[] = { err };
        if (!autorun) ui_message("Error", l, 1);
        return -1;
    }
    ui_map_all(1);
    analyzed = 1;
    ui_bar("");
    return 0;
}

static const char *run_err;

static int optimize(int m)
{
    char cwd[70], path[80];
    R r = { 0 };
    r.r0 = 0x4700; r.r3 = drive + 1; r.r4 = (uint32_t)cwd;
    if (dos(&r)) cwd[0] = 0;
    path[0] = 'A' + drive; path[1] = ':'; path[2] = '\\'; path[3] = 0;
    r.r0 = 0x3B00; r.r3 = (uint32_t)path;
    dos(&r);
    run_err = eng_optimize(m);
    strcpy(path + 3, cwd);
    r.r0 = 0x3B00; r.r3 = (uint32_t)path;
    if (dos(&r)) { path[3] = 0; r.r0 = 0x3B00; r.r3 = (uint32_t)path; dos(&r); }
    if (run_err == (const char *)1) { ui_bar("Optimization stopped"); return 2; }
    if (run_err) { ui_bar("Optimization failed"); return 1; }
    ui_bar("Finished");
    return 0;
}

static int after_run(int rc)
{
    char a[64], b2[64], *p;
    if (autorun) return 0;
    if (rc == 1) {
        const char *l[] = { run_err, "", "The disk is consistent: every step that", "finished was written completely." };
        ui_message("Error", l, 4);
        return 0;
    }
    p = a;
    strcpy(p, rc == 2 ? "Optimization stopped: " : "Finished condensing: ");
    p += strlen(p);
    p = commas(p, moved_clusters);
    strcpy(p, " clusters moved.");
    p = b2;
    p = commas(p, st.notfrag_pct);
    strcpy(p, "% of the drive is not fragmented.");
    const char *l[] = { a, b2, "", "Do you want to:" };
    static const char *const r[] = { "Optimize &another drive", "&Configure", "E&xit DEFRAG" };
    static const char *const bt[] = { "OK" };
    int sel = 2;
    ui_dialog(rc == 2 ? "Optimization Stopped" : "Finished Condensing", l, 4, r, 3, &sel, bt, 1, 0);
    return sel == 0 ? 1 : sel == 2 ? 2 : 0;
}

static int recommend(void)
{
    char a[64], b2[64], *p;
    p = a;
    p = commas(p, st.notfrag_pct);
    strcpy(p, "% of drive A: is not fragmented.");
    p[11] = 'A' + drive;
    p = b2;
    strcpy(p, mname(st.recommend));
    strcat(p, ".");
    int none = st.recommend == M_NONE;
    const char *l[] = { a, "", none ? "Recommendation:" : "Recommended optimization method:", b2, "",
                        "Close all other programs before optimizing." };
    static const char *const b[] = { "&Optimize", "&Configure" };
    static const char *const bn[] = { "&Configure", "E&xit" };
    int r = ui_dialog("Recommendation", l, none ? 4 : 6, 0, 0, 0, none ? bn : b, 2, 0);
    if (none) return r == 1 ? 2 : 0;
    if (r == 0) { method = st.recommend; return 1; }
    return 0;
}

static void report_line(int rc)
{
    char t[160], *p = t;
    strcpy(p, "ARM Disk Optimizer: drive A: ");
    p[26] = 'A' + drive;
    p += strlen(p);
    if (rc == 1) { strcpy(p, run_err ? run_err : "error"); strcat(p, "\r\n"); outs(t); return; }
    strcpy(p, mname(method)); p += strlen(p);
    strcpy(p, rc == 2 ? " stopped.\r\n" : " complete.\r\n"); p += strlen(p);
    p = commas(p, moved_clusters);
    strcpy(p, " clusters moved, "); p += strlen(p);
    p = commas(p, st.notfrag_pct);
    strcpy(p, "% of the drive is not fragmented.\r\n");
    outs(t);
}

static int quit, exitcode, rc = -1, fresh;

static void do_run(void)
{
    rc = optimize(method);
    if (autorun) { exitcode = rc; quit = 1; return; }
    int k = after_run(rc);
    if (k == 2) quit = 1;
    else if (k == 1) {
        int d = choose_drive(drive);
        if (d >= 0) { drive = d; fresh = 1; }
    }
}

int main(void)
{
    check_version();
    parse();
    if (drive < 0 && autorun) drive = cur_drive();
    ui_init();
    ui_frame();
    ui_flush();
    if (drive < 0) drive = choose_drive(cur_drive());
    if (drive < 0) quit = 1;
    int first = 1;
    fresh = 1;
    while (!quit) {
        if (fresh) {
            fresh = 0;
            if (analyze(drive)) {
                if (autorun) { exitcode = rc = 1; run_err = "cannot read the drive"; break; }
            } else {
                int go;
                if (autorun) { if (method < 0) method = st.recommend == M_NONE ? M_FULL : st.recommend; go = 1; }
                else if (first && method >= 0) go = 1;
                else {
                    go = recommend();
                    if (go == 2) break;
                }
                first = 0;
                if (go) { do_run(); continue; }
            }
            first = 0;
        }
        ui_bar("Alt=Optimize menu   Alt+B=Begin   Alt+X=Exit   F1=Help");
        int key = 0, e = ui_wait_event(&key), item = -1;
        if (e == 2) item = ui_menu();
        else if (e == 1) {
            int c = key & 0xFF, s = key >> 8;
            if (s == 0x18 && !c) item = ui_menu();              /* Alt+O */
            else if (s == 0x44 && !c) item = ui_menu();         /* F10 */
            else if (s == 0x30 && !c) item = MI_BEGIN;
            else if (s == 0x2D && !c) item = MI_EXIT;
            else if (s == 0x3B && !c) help();
            else if (c == 27) item = MI_EXIT;
        }
        switch (item) {
        case MI_BEGIN:
            if (!analyzed && analyze(drive)) break;
            if (method < 0) method = st.recommend == M_NONE ? M_FULL : st.recommend;
            do_run();
            break;
        case MI_DRIVE: {
            int d = choose_drive(drive);
            if (d >= 0) { drive = d; fresh = 1; }
            break;
        }
        case MI_METHOD: choose_method(); break;
        case MI_SORT: choose_sort(); break;
        case MI_LEGEND: legend(); break;
        case MI_FAST: fast = !fast; break;
        case MI_ABOUT: about(); break;
        case MI_EXIT: quit = 1; break;
        }
    }
    eng_close();
    ui_exit();
    if (autorun) report_line(rc < 0 ? 1 : rc);
    return exitcode;
}
