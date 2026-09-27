/*
 * XCOPY - ARM-DOS re-creation of the MS-DOS 4.00 XCOPY command.
 *
 *   XCOPY source [destination] [/A] [/D:date] [/E] [/M] [/P] [/S] [/V] [/W]
 *
 * Behaviour follows CMD/XCOPY/XCOPY.ASM, XCPYINIT.ASM and XCOPYPAR.ASM of the
 * MS-DOS 4.0 source (MIT licence, (C) Microsoft Corp.); no code is copied.
 * Like the original, XCOPY reads as many source files (and directory
 * entries) as fit in a memory buffer taken from DOS, then writes them all,
 * printing each name as it is written; "Reading source file(s)..." is shown
 * before each reading pass.  The target path is created ("Does X specify a
 * file name or directory name on the target") before anything is read;
 * directories that stayed empty are not created (or removed again) unless
 * /E is given.  The current directories of the drives involved are restored
 * at the end.
 */
#include "u4.h"

/* ---------------------------------------------------------------- state */

#define OPT_A 0x01
#define OPT_D 0x02
#define OPT_E 0x04
#define OPT_M 0x08
#define OPT_P 0x10
#define OPT_S 0x20
#define OPT_V 0x40
#define OPT_W 0x80

static unsigned option;
static int init_error, inv_date;
static int errorlevel;
static int single_copy;             /* /P: read one file, write it */
static int one_disk;                /* source and target on the same drive */
static int maybe_itself;            /* same starting directory */
static int s_file_flag, t_file_flag;
static int found_file;              /* FOUND_FILE_FLAG */
static int reading_flag = 1;
static int missing_link;
static int created;                 /* target file handle open */
static int mkdir_error, disk_full;
static int turn_verify_off;
static uint32_t file_count;
static uint16_t input_date;

static int sav_default_drv;         /* 1 = A: */
static char sav_default_dir[80];
static int s_drv_number, t_drv_number;
static char so_drive = ' ', tar_drive = ' ';
static char sav_s_path[84], sav_t_path[84];   /* "C:\..." */
static int sav_s_saved, sav_t_saved, default_drv_set;

static char s_input_parm[84], t_input_parm[84];
static int first_parm, second_parm;
static char s_drv_path[140];        /* "C:\PATH" - the source directory */
static char s_file[20] = "????????.???";
static int s_depth;
static char s_arc_path[140];
static int s_arc_depth;
static char t_drv_path[140];
static int t_depth;
static char t_filename[16];
static char t_template[12];
static char temp_t_filename[16];
static int t_mkdir_lvl;
static int t_handle, s_handle = -1;
static char disp_s_path[140];
static char disp_s_file[16];
static char disp_t_path[140];

static uint8_t date_month, date_day;
static uint16_t date_year;

/* -------------------------------------------------------- DOS helpers */

static int dos(struct armregs *r) { return u4_int21(r); }

static int chdir_(const char *p)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3B00; r.r3 = (uint32_t)p;
    if (dos(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return 0;
}

static int mkdir_(const char *p)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3900; r.r3 = (uint32_t)p;
    if (dos(&r)) { u4_err = r.r0 & 0xFFFF; return -1; }
    return 0;
}

static int rmdir_(const char *p)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3A00; r.r3 = (uint32_t)p;
    return dos(&r) ? -1 : 0;
}

/* INT 21h AH=47h: current directory of drive (1 = A:), without the "\" */
static void getcwd_(int drive, char *buf)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4700; r.r3 = drive; r.r4 = (uint32_t)buf;
    if (dos(&r)) buf[0] = 0;
}

static void set_default_drv(int d0)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x0E00; r.r3 = d0;
    dos(&r);
    default_drv_set = 1;
}

static void set_dta(void *p)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x1A00; r.r3 = (uint32_t)p;
    dos(&r);
}

static int find(int first, const char *spec, int attr)
{
    struct armregs r;
    u4_clr(&r);
    if (first) { r.r0 = 0x4E00; r.r2 = attr; r.r3 = (uint32_t)spec; }
    else r.r0 = 0x4F00;
    return dos(&r) ? -1 : 0;
}

static int ext_error(void)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x5900;
    dos(&r);
    return r.r0 & 0xFFFF;
}

static int getkey1(void)             /* INT 21h AH=01h: read with echo */
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x0100;
    dos(&r);
    return r.r0 & 0xFF;
}

/* ------------------------------------------------------ string helpers */

/* CHK_DRV_LETTER: "X:" at s (upper case letter only) -> pointer after it */
static char *skip_drv(char *s)
{
    if (s[0] >= 'A' && s[0] <= 'Z' && s[1] == ':') return s + 2;
    return 0;
}

/* LAST_DIR_OUT: cut at the last "\"; returns the tail or NULL */
static char *last_dir_out(char *s)
{
    char *b = strrchr(s, '\\');
    if (!b) return 0;
    *b = 0;
    return b + 1;
}

/* CONCAT_ASCIIZ with a delimiter (0 = none) */
static void concat(char *dst, char delim, const char *src)
{
    int n = strlen(dst);
    if (delim) dst[n++] = delim;
    strcpy(dst + n, src);
}

/* a "C:" left by last_dir_out becomes "C:\" again (CHANGE_S_DIR/T_DIR) */
static void fix_root(char *p)
{
    if (p[2] == 0) { p[2] = '\\'; p[3] = 0; }
}

/* ---------------------------------------------------------- messages */

static void out(const char *s) { u4_puts(STDOUT, s); }
static void err(const char *s) { u4_puts(STDERR, s); }

static void msg2(int h, const char *a, const char *sep, const char *b, const char *tail)
{
    u4_puts(h, a); u4_puts(h, sep); u4_puts(h, b); u4_puts(h, tail);
}

/* ------------------------------------------------------------ exit */

static void restore_dirs(void)
{
    if (turn_verify_off) {
        struct armregs r;
        u4_clr(&r); r.r0 = 0x2E00; dos(&r);
        turn_verify_off = 0;
    }
    if (default_drv_set) {
        set_default_drv(sav_default_drv - 1);
        char root[84] = "\\";
        strcpy(root + 1, sav_default_dir);
        chdir_(root);
    }
    if (sav_s_saved) chdir_(sav_s_path);
    if (sav_t_saved) chdir_(sav_t_path);
}

/* T_RM_STARTING_DIR: remove the target directories XCOPY created */
static void chk_mkdir_lvl(void)
{
    if (t_mkdir_lvl > 0 && !(option & OPT_E) && !found_file) {
        char par[5] = { (char)('@' + t_drv_number), ':', '.', '.', 0 };
        t_drv_path[0] = '@' + t_drv_number; t_drv_path[1] = ':'; t_drv_path[2] = '\\';
        getcwd_(t_drv_number, t_drv_path + 3);
        while (t_mkdir_lvl > 0) {
            chdir_(par);
            rmdir_(t_drv_path);
            last_dir_out(t_drv_path);
            t_mkdir_lvl--;
        }
    }
}

static __attribute__((noreturn)) void main_exit_a(void)
{
    chk_mkdir_lvl();
    restore_dirs();
    u4_exit(errorlevel);
}

/* COMPRESS_FILENAME: FCB name (11 chars) -> "NAME.EXT" */
static void compress_filename(const char *f, char *d)
{
    int i;
    for (i = 0; i < 8 && f[i] != ' '; i++) *d++ = f[i];
    if (f[8] != ' ') {
        *d++ = '.';
        for (i = 8; i < 11 && f[i] != ' '; i++) *d++ = f[i];
    }
    *d = 0;
}

static __attribute__((noreturn)) void main_exit(void)
{
    char num[12], pad[16];
    if (!init_error && file_count == 0 && !found_file) {
        uint8_t fcb[40];
        struct armregs r;
        memset(fcb, 0, sizeof fcb);
        u4_clr(&r);
        r.r0 = 0x2900; r.r4 = (uint32_t)s_file; r.r5 = (uint32_t)fcb;
        dos(&r);
        compress_filename((char *)fcb + 1, disp_s_file);
        msg2(STDOUT, "File not found", " - ", disp_s_file, "\r\n");
    }
    u4_utoa(file_count, num);
    out(u4_pad(num, 9, pad));
    out(" File(s) copied\r\n");
    main_exit_a();
}

/* SHOW_ERROR_MESSAGE */
static void show_error_message(int code)
{
    const char *t;
    switch (code) {
    case 5: case 65: t = "Access denied \r\n"; break;
    case 4: t = "Too many open files\r\n"; break;
    case 31: t = "General failure\r\n"; break;
    case 32: t = "Sharing violation\r\n"; break;
    case 33: t = "Lock violation\r\n"; break;
    case 3: t = "Path not found\r\n"; break;
    case 2: t = "File not found\r\n"; break;
    case 82: t = "File creation error\r\n"; break;
    default: return;
    }
    err(t);
}

/* ------------------------------------------------------------ Ctrl-C */

static void ctrl_break(struct armregs *f)
{
    errorlevel = 2;
    chk_mkdir_lvl();
    restore_dirs();
    f->cpsr |= ARM_CPSR_C;              /* abort the program */
}

/* ------------------------------------------------------------ parsing */

static char sw_a[] = "/A", sw_e[] = "/E", sw_m[] = "/M", sw_p[] = "/P", sw_s[] = "/S",
            sw_v[] = "/V", sw_w[] = "/W", sw_d[] = "/D";
static char sw_names[32];
static char sw8_names[4];
static const struct u4_ctl pos1 = { P_FILE, P_CAP_CHAR, 0, 0, 0 };
static const struct u4_ctl pos2 = { P_FILE | P_OPTIONAL, P_CAP_CHAR, 0, 0, 0 };
static struct u4_ctl sw1_7 = { P_OPTIONAL, P_CAP_CHAR, sw_names, 0, 0 };
static struct u4_ctl sw8 = { P_DATE, 0, sw8_names, 0, 0 };
static const struct u4_ctl *const pos_tab[] = { &pos1, &pos2 };
static const struct u4_ctl *const sw_tab[] = { &sw1_7, &sw8 };
static const struct u4_parms parms = { 1, 2, pos_tab, 2, sw_tab, 0, 0, ";", 0 };

/* the switch lists as the parser sees them; a used switch becomes " X"
 * (XCOPY disallows duplicates by blanking its synonym) */
static void build_names(void)
{
    char *p = sw_names;
    const char *l[7] = { sw_a, sw_e, sw_m, sw_p, sw_s, sw_v, sw_w };
    for (int i = 0; i < 7; i++) { strcpy(p, l[i]); p += 3; }
    *p = 0;
    strcpy(sw8_names, sw_d);
    sw8_names[3] = 0;
}

static int days_in(int m, int y)
{
    static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && (y % 4) == 0) return 29;
    return md[m - 1];
}

static int chk_max_length(const char *s)
{
    int n = strlen(s);
    if (*s == '\\') n--;
    return n > 63;
}

static const char *msg_for;         /* init error message (NULL = none) */
static int msg_param;               /* append " - parameter" */
static char bad_parm[130];

static void get_parms(struct u4_result *r)
{
    if (r->ctl == &pos1 || r->ctl == &pos2) {
        char *dst = r->ctl == &pos1 ? s_input_parm : t_input_parm;
        const char *s = r->str;
        if (s[0] && s[1] == ':') {
            if (r->ctl == &pos1) so_drive = s[0]; else tar_drive = s[0];
        }
        strncpy(dst, s, 83);
        dst[83] = 0;
        /* CHK_MAX_LENGTH counts the name without the drive */
        const char *q = (s[0] && s[1] == ':') ? s + 2 : s;
        if (chk_max_length(q)) { msg_for = "Path too long\r\n"; init_error = 1; }
        else if (r->ctl == &pos1) first_parm = 1; else second_parm = 1;
        return;
    }
    if (r->ctl == &sw1_7) {
        char c = r->synonym[1];
        char *name = (char *)r->synonym;       /* points into sw_names */
        name[0] = ' ';
        switch (c) {
        case 'S': option |= OPT_S; break;
        case 'A': option &= ~OPT_M; option |= OPT_A; break;
        case 'M': option &= ~OPT_A; option |= OPT_M; break;
        case 'P': option |= OPT_P; single_copy = 1; break;
        case 'E': option |= OPT_E; break;
        case 'V': {
            struct armregs q;
            u4_clr(&q); q.r0 = 0x5400; dos(&q);
            if (!(q.r0 & 0xFF)) {
                u4_clr(&q); q.r0 = 0x2E01; dos(&q);
                turn_verify_off = 1;
            }
            break;
        }
        default: option |= OPT_W; break;
        }
        return;
    }
    /* /D:date - validated as DOS 4 SET DATE would */
    sw8_names[0] = ' ';
    date_year = r->year; date_month = r->month; date_day = r->day;
    if (date_year < 1980 || date_year > 2099 || date_month < 1 || date_month > 12 ||
        date_day < 1 || date_day > days_in(date_month, date_year)) {
        inv_date = 1;
        init_error = 1;
        return;
    }
    input_date = (uint16_t)((((date_year - 1980) << 4 | date_month) << 5) | date_day);
    option |= OPT_D;
}

static void parse_line(void)
{
    const char *line = u4_cmdline();
    struct u4_pstate st;
    struct u4_result r;
    const char *current = line;
    memset(&st, 0, sizeof st);
    st.si = line;
    build_names();
    for (;;) {
        st.si = current;
        int rc = u4_parse(&parms, &st, &r);
        if (rc == P_RC_EOL) return;
        if (rc == P_OK) {
            current = st.si;
            get_parms(&r);
            if (init_error) {
                if (inv_date) msg_for = "Invalid date\r\n";
                return;
            }
            continue;
        }
        init_error = 1;
        int n = st.si - current;
        memcpy(bad_parm, current, n);
        bad_parm[n] = 0;
        if (rc == P_TOO_MANY) { msg_for = "Invalid number of parameters"; msg_param = 1; }
        else if (rc == P_BAD_SWITCH) { msg_for = "Invalid switch"; msg_param = 1; }
        else if (rc == P_MISSING) { msg_for = "Invalid number of parameters\r\n"; }
        else { msg_for = "Invalid parameter"; msg_param = 1; }
        return;
    }
}

/* the drive letters of the first two arguments, as the shell's FCB
 * parse reports them to a program in AL/AH at entry */
static int fcb_drives_ok(void)
{
    const char *p = u4_cmdline();
    int n = 0;
    while (n < 2) {
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';' || *p == '=') p++;
        if (*p == '\r' || !*p) break;
        if (*p != '/') {
            uint8_t fcb[40];
            struct armregs r;
            u4_clr(&r);
            r.r0 = 0x2900; r.r4 = (uint32_t)p; r.r5 = (uint32_t)fcb;
            dos(&r);
            if ((r.r0 & 0xFF) == 0xFF) return 0;
            n++;
        }
        while (*p && *p != '\r' && *p != ' ' && *p != '\t' && *p != ',' && *p != ';' && *p != '=') p++;
    }
    return 1;
}

/* TAKE_PATH_TAIL: the last element of parm goes to tail, parm becomes
 * the directory ("." or with a "\" for the root) */
static void take_path_tail(char *parm, char *tail)
{
    char *t = last_dir_out(parm);
    if (!t) {
        char *s = skip_drv(parm);
        if (!s) s = parm;
        if (*s) { strncpy(tail, s, 12); tail[12] = 0; }
        s[0] = '.'; s[1] = 0;
    } else {
        strncpy(tail, t, 12);
        tail[12] = 0;
        char *s = skip_drv(parm);
        if (!s) s = parm;
        if (!*s) { s[0] = '\\'; s[1] = 0; }
    }
}

/* PROMPT_CREATE_DIR: F or D */
static int prompt_create_dir(const char *name)
{
    for (;;) {
        msg2(STDOUT, "Does ", name, " specify a file name\r\nor directory name on the target\r\n(F = file, D = directory)?", "");
        int c = getkey1();
        out("\r\n");
        c = u4_upcase(c);
        if (c == 'F' || c == 'D') return c;
    }
}

static uint8_t tfcb[40];

static void last_t_path(char *dx)
{
    if (chdir_(dx) == 0) return;
    take_path_tail(t_input_parm, temp_t_filename);
    if (!temp_t_filename[0]) return;
    struct armregs r;
    memset(tfcb, 0, sizeof tfcb);
    u4_clr(&r);
    r.r0 = 0x2900; r.r4 = (uint32_t)temp_t_filename; r.r5 = (uint32_t)tfcb;
    dos(&r);
    if ((r.r0 & 0xFF) == 0) {
        char up[16];
        strcpy(up, temp_t_filename);
        if (prompt_create_dir(up) == 'D') {
            if (mkdir_(temp_t_filename) == 0) {
                t_mkdir_lvl++;
                chdir_(temp_t_filename);
                temp_t_filename[0] = 0;
            } else {
                msg_for = "Unable to create directory\r\n";
                init_error = 1;
            }
        } else t_file_flag = 1;
    } else {
        temp_t_filename[0] = 0;
        memcpy(t_template, tfcb + 1, 11);
        t_file_flag = 1;
    }
}

/* NEXT_PATH_DELIM */
static char *next_path_delim(char *s)
{
    while (*s && *s != '\\' && *s != ':') s++;
    return s;
}

static void parse_second_parm(void)
{
    char *si = t_input_parm;
    /* CHK_HEAD_PARM */
    char *d = skip_drv(si);
    if (d) si = d;
    if (*si == 0) {
        si[0] = '.'; si[1] = 0;
    } else if (*si == '\\') {
        si++;
        if (*si == '\\') { msg_for = "Invalid path\r\n"; init_error = 1; }
        else if (*si == '.') {
            if (si[1] == '.' || si[1] != '\\') { msg_for = "Invalid path\r\n"; init_error = 1; }
        }
        char root[4] = { (char)('@' + t_drv_number), ':', '\\', 0 };
        chdir_(root);
    }
    if (init_error) return;
    si = next_path_delim(si);
    char *dx = t_input_parm;
    /* PARSING_T_PATH */
    set_default_drv(t_drv_number - 1);
    for (;;) {
        if (*si == 0 || *si == ':') {
            *si = 0;
            last_t_path(dx);
            return;
        }
        *si = 0;
        while (chdir_(dx) != 0) {
            if (mkdir_(dx) != 0) {
                msg_for = "Unable to create directory\r\n";
                init_error = 1;
                return;
            }
            t_mkdir_lvl++;
        }
        *si = '\\';
        dx = si + 1;
        si = next_path_delim(dx);
    }
}

/* CHK_CYCLIC_COPY (after TRANS_NAMES) */
static void truename(const char *in, char *outp)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x6000; r.r4 = (uint32_t)in; r.r5 = (uint32_t)outp;
    if (dos(&r)) strcpy(outp, in);
}

static void chk_cyclic_copy(void)
{
    char tt[140], st[140];
    truename(t_drv_path, tt);
    truename(s_drv_path, st);
    if (!strcmp(tt, st)) one_disk = 1;
    if (!one_disk) return;
    int tl = strlen(tt), sl = strlen(st);
    if (tl < sl) return;
    if (memcmp(st, tt, sl)) return;
    int tree = option & (OPT_S | OPT_E);
    if (tl == sl) {
        if (tree) { msg_for = "Cannot perform a cyclic copy\r\n"; init_error = 1; }
        else maybe_itself = 1;
    } else if (tree) {
        if (tt[sl] == '\\' || st[sl - 1] == '\\') { msg_for = "Cannot perform a cyclic copy\r\n"; init_error = 1; }
    }
}

static void chk_s_reserved_name(void)
{
    char name[24];
    name[0] = '@' + s_drv_number; name[1] = ':';
    strcpy(name + 2, s_file);
    int h = u4_open(name, 0);
    if (h < 0) return;
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4400; r.r1 = h;
    dos(&r);
    if (r.r3 & 0x80) {
        err("Cannot XCOPY from a reserved device\r\n");
        errorlevel = 4;
        init_error = 1;
        main_exit();
    }
    u4_close(h);
}

static void chk_set_parms(void)
{
    char tmp[84];
    /* the source */
    sav_s_path[0] = '@' + s_drv_number; sav_s_path[1] = ':'; sav_s_path[2] = '\\';
    getcwd_(s_drv_number, sav_s_path + 3);
    sav_s_saved = 1;
    s_drv_path[0] = '@' + s_drv_number; s_drv_path[1] = ':'; s_drv_path[2] = '\\';
    if (chdir_(s_input_parm) == 0) {
        getcwd_(s_drv_number, s_drv_path + 3);
    } else {
        take_path_tail(s_input_parm, s_file);
        if (chdir_(s_input_parm) == 0 && s_file[0]) {
            getcwd_(s_drv_number, s_drv_path + 3);
            s_file_flag = 1;
            chk_s_reserved_name();
        } else {
            msg_for = "Invalid path\r\n";
            init_error = 1;
        }
    }
    if (init_error) return;
    if (one_disk) chdir_(sav_s_path);
    /* the target */
    sav_t_path[0] = '@' + t_drv_number; sav_t_path[1] = ':'; sav_t_path[2] = '\\';
    getcwd_(t_drv_number, sav_t_path + 3);
    sav_t_saved = 1;
    t_drv_path[0] = '@' + t_drv_number; t_drv_path[1] = ':'; t_drv_path[2] = '\\';
    if (!second_parm) {
        getcwd_(t_drv_number, t_drv_path + 3);
    } else {
        strcpy(tmp, t_input_parm);
        if (chdir_(t_input_parm) != 0) parse_second_parm();
        if (!init_error) {
            getcwd_(t_drv_number, t_drv_path + 3);
            if (temp_t_filename[0]) {
                strcpy(t_filename, temp_t_filename);
                u4_strupr(t_filename);
            }
        }
    }
    if (init_error) return;
    chk_cyclic_copy();
    if (!init_error && one_disk) chdir_(s_drv_path);
}

/* MODIFY_FOR_DISPLAY */
static void massage_disp_path(char *p)
{
    char *t = last_dir_out(p);
    if (!t) {
        char *s = skip_drv(p);
        if (s) *s = 0; else p[0] = 0;
    } else {
        t[-1] = '\\';
        t[0] = 0;
    }
}

static void chk_tail_chr(char *p)
{
    int n = strlen(p);
    if (n && p[n - 1] == '\\') return;
    p[n] = '\\'; p[n + 1] = 0;
}

static void modify_one(char *p, int file)
{
    if (file) { massage_disp_path(p); return; }
    char *s = skip_drv(p);
    if (s) {
        if (*s == 0) return;
        if (*s == '\\' && s[1] == 0) return;
    }
    chk_tail_chr(p);
}

/* ---------------------------------------------------------- the buffer */
/*
 * Entries as XCOPY.ASM's HEADER: a directory to create, or a (part of a)
 * file.  Space is counted in paragraphs exactly as the original does
 * (header 3 paragraphs, data rounded up plus one), so buffer-full points
 * and the "Reading source file(s)..." passes follow the same rules.
 */
#define HDR_PARAS 3
#define HDR_BYTES (HDR_PARAS * 16)

struct hdr {
    uint8_t cont;               /* 0 whole, 1 small part, 2 big part, 3 last part */
    uint8_t depth;
    uint8_t attr;
    uint8_t pad;
    uint16_t time, date;
    uint32_t size;
    uint32_t cx_bytes;
    uint32_t next;              /* paragraph offset of the next entry */
    uint32_t before;
    char name[14];
    char tname[2];
};

static uint8_t *arena;          /* buffer base */
static uint32_t top_paras;      /* size in paragraphs */
static uint32_t buffer_ptr;     /* paragraph offsets into arena */
static uint32_t old_buffer_ptr; /* NONE when empty */
static uint32_t buffer_left;
static uint32_t max_buffer_size;
#define NONE 0xFFFFFFFFu

static struct hdr *H(uint32_t p) { return (struct hdr *)(arena + p * 16); }

static uint8_t file_dta[48];
static uint8_t dtas[33][48];
#define DTA_ATTR(d) ((d)[21])
#define DTA_TIME(d) ((d)[22] | ((d)[23] << 8))
#define DTA_DATE(d) ((d)[24] | ((d)[25] << 8))
#define DTA_SIZE(d) ((uint32_t)((d)[26] | ((d)[27] << 8) | ((d)[28] << 16) | ((uint32_t)(d)[29] << 24)))
#define DTA_NAME(d) ((char *)(d) + 30)

static uint32_t file_size_left;
static uint32_t act_bytes;
static int f_cont, f_eof, f_big, f_bigger;
static int open_file_count;
static int findfile_mode;       /* make_header: a file (1) or a directory */
static int bp;                  /* DTAS stack index */

static void write_from_buffer(void);

static void set_buffer_ptr(uint32_t bytes)
{
    old_buffer_ptr = buffer_ptr;
    buffer_ptr += (bytes >> 4) + 1 + HDR_PARAS;
    if (buffer_ptr < top_paras) buffer_left = top_paras - buffer_ptr;
    else buffer_left = 0;
}

/* SHOW_S_PATH_FILE_ERR + SHOW_ERROR_MESSAGE, then MAIN_EXIT */
static __attribute__((noreturn)) void source_error(int code)
{
    errorlevel = 4;
    char name[14];
    strcpy(name, DTA_NAME(file_dta));
    if (s_depth == 0) msg2(STDERR, name, "", "", "\r\n");
    else msg2(STDERR, s_drv_path, "\\", name, "\r\n");
    show_error_message(code);
    main_exit();
}

static void open_a_file(void)
{
    char path[160];
    strcpy(path, s_drv_path);
    if (path[strlen(path) - 1] != '\\') strcat(path, "\\");
    strcat(path, DTA_NAME(file_dta));
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x6C00; r.r1 = 0x0000; r.r2 = 0; r.r3 = 0x0001; r.r4 = (uint32_t)path;
    if (dos(&r)) source_error(ext_error());
    s_handle = r.r0 & 0xFFFF;
    open_file_count++;
}

static void close_a_file(int h)
{
    if (open_file_count > 0) open_file_count--;
    u4_close(h);
}

static void cmp_filesize_to_buffer_left(void)
{
    if (open_file_count == 0) open_a_file();
    f_bigger = 0;
    if (buffer_left >= HDR_PARAS) {
        uint32_t room = (buffer_left - HDR_PARAS) * 16;
        if (file_size_left > room) f_bigger = 1;
    } else f_bigger = 1;
}

static void read_a_file(uint32_t cx)
{
    uint8_t *dst = arena + (buffer_ptr + HDR_PARAS) * 16;
    uint32_t got = 0;
    while (got < cx) {
        unsigned chunk = cx - got > 0xFFF0 ? 0xFFF0 : cx - got;
        int n = u4_read(s_handle, dst + got, chunk);
        if (n < 0) {
            int e = ext_error();
            close_a_file(s_handle);
            source_error(e);
        }
        got += n;
        if ((unsigned)n != chunk) break;
    }
    if (got != cx) f_eof = 1;
    act_bytes = got;
}

static void make_header(const uint8_t *dta)
{
    while (buffer_left < HDR_PARAS) write_from_buffer();
    struct hdr *h = H(buffer_ptr);
    memset(h, 0, sizeof *h);
    if (!findfile_mode) {
        h->cont = 0;
        h->before = old_buffer_ptr;
        old_buffer_ptr = buffer_ptr;
        buffer_ptr += HDR_PARAS;
        h->next = buffer_ptr;
        buffer_left -= HDR_PARAS;
        if (buffer_left < HDR_PARAS) buffer_left = 0;
        h->depth = s_depth;
        h->attr = DTA_ATTR(dta);
        h->tname[0] = '@' + t_drv_number;
        memcpy(h->name, DTA_NAME(dta), 13);
        return;
    }
    if (f_cont) h->cont = f_eof ? 3 : f_big ? 2 : 1;
    else h->cont = 0;
    h->time = DTA_TIME(file_dta);
    h->date = DTA_DATE(file_dta);
    h->size = DTA_SIZE(file_dta);
    h->tname[0] = '@' + t_drv_number;
    memcpy(h->name, DTA_NAME(file_dta), 13);
    h->before = old_buffer_ptr;
    h->cx_bytes = act_bytes;
    uint32_t me = buffer_ptr;
    set_buffer_ptr(act_bytes);
    h = H(me);
    h->next = buffer_ptr;
    h->depth = s_depth;
    h->attr = DTA_ATTR(file_dta);
}

static void small_file(void)
{
    for (;;) {
        if (file_size_left > 0xFFD0) {
            f_cont = 1;
            read_a_file(0xFFD0);
            make_header(file_dta);
            file_size_left -= act_bytes;
            continue;
        }
        f_eof = 1;
        read_a_file(file_size_left);
        make_header(file_dta);
        close_a_file(s_handle);
        return;
    }
}

static void big_file(void)
{
    f_big = 1; f_cont = 1;
    open_a_file();
    if (max_buffer_size > 0xFFF) {
        uint32_t cx = 0xFFD0;
        for (;;) {
            read_a_file(cx);
            make_header(file_dta);
            file_size_left -= act_bytes;
            for (;;) {
                cmp_filesize_to_buffer_left();
                if (!f_bigger) {
                    small_file();
                    write_from_buffer();
                    return;
                }
                if (buffer_left >= HDR_PARAS + 0xFFF) { cx = 0xFFD0; break; }
                if (buffer_left >= HDR_PARAS + 0x140) { cx = (buffer_left - HDR_PARAS) * 16; break; }
                write_from_buffer();
            }
        }
    } else {
        uint32_t max_cx = max_buffer_size * 16 - 544;
        do {
            read_a_file(max_cx);
            make_header(file_dta);
            write_from_buffer();
        } while (!f_eof);
        close_a_file(s_handle);
    }
}

static void read_into_buffer(void)
{
    if (!single_copy && reading_flag) {
        out("Reading source file(s)...\r\n");
        reading_flag = 0;
    }
    f_cont = f_eof = f_big = f_bigger = 0;
    file_size_left = DTA_SIZE(file_dta);
    cmp_filesize_to_buffer_left();
    if (f_bigger) {
        close_a_file(s_handle);
        write_from_buffer();
        cmp_filesize_to_buffer_left();
        if (f_bigger) {
            close_a_file(s_handle);
            big_file();
            goto done;
        }
    }
    small_file();
done:
    if (single_copy) write_from_buffer();
}

/* -------------------------------------------------------------- writing */

static int chdir_t(void)
{
    fix_root(t_drv_path);
    return chdir_(t_drv_path);
}

static void concat_display_path(const char *name)
{
    if (option & OPT_P) return;
    concat(disp_s_path, t_depth == 0 ? 0 : '\\', name);
}

static void cut_display_path(void)
{
    if (option & OPT_P) return;
    char *t = last_dir_out(disp_s_path);
    if (!t) {
        char *s = skip_drv(disp_s_path);
        if (s) *s = 0; else disp_s_path[0] = 0;
    } else if (t_depth == 1) {
        t[-1] = '\\';
        t[0] = 0;
    }
}

/* MAKE_DIR: "D:NAME" in the current target directory */
static int make_dir(struct hdr *h)
{
    char name[20];
    name[0] = h->tname[0]; name[1] = ':';
    strcpy(name + 2, h->name);
    if (mkdir_(name) == 0) return 0;
    int full = 0, exists_dir = 0;
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x3600; r.r3 = t_drv_number;
    dos(&r);
    if ((r.r1 & 0xFFFF) == 0) { full = 1; disk_full = 1; }
    uint8_t dta[48];
    set_dta(dta);
    if (!full) {
        if (find(1, name, 6) != 0) exists_dir = 1;   /* no file by that name */
    } else {
        if (find(1, name, 0x10) == 0) exists_dir = 1;
    }
    if (exists_dir) return 0;
    errorlevel = 4;
    if (disk_full) err("Insufficient disk space\r\n");
    else { err("Unable to create directory\r\n"); mkdir_error = 1; }
    return -1;
}

static void rm_empty_dir(void)
{
    if (option & OPT_E) return;
    if (!missing_link) return;
    char par[5] = { (char)('@' + t_drv_number), ':', '.', '.', 0 };
    chdir_(par);
    rmdir_(t_drv_path);
}

static void comp_filename(const char *a, const char *b)
{
    if (!strcmp(a, b)) {
        err("File cannot be copied onto itself\r\n");
        main_exit();
    }
}

static void modify_filename(struct hdr *h)
{
    char in[20];
    uint8_t fcb[40];
    in[0] = h->tname[0]; in[1] = ':';
    strcpy(in + 2, h->name);
    memset(fcb, 0, sizeof fcb);
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x2900; r.r4 = (uint32_t)in; r.r5 = (uint32_t)fcb;
    dos(&r);
    for (int i = 0; i < 11; i++) if (t_template[i] != '?') fcb[1 + i] = t_template[i];
    compress_filename((char *)fcb + 1, h->name);
}

static void create_a_file(struct hdr *h)
{
    strcpy(disp_s_file, h->name);
    if (!(option & OPT_P)) {
        if (t_depth == 0) msg2(STDOUT, disp_s_path, "", disp_s_file, "\r\n");
        else msg2(STDOUT, disp_s_path, "\\", disp_s_file, "\r\n");
    }
    if (t_filename[0]) {
        if (maybe_itself) comp_filename(h->name, t_filename);
        strcpy(h->name, t_filename);
    } else if (t_template[0]) {
        modify_filename(h);
        if (maybe_itself) comp_filename(h->name, disp_s_file);
    } else if (maybe_itself) {
        err("File cannot be copied onto itself\r\n");
        main_exit();
    }
    char name[20];
    name[0] = h->tname[0]; name[1] = ':';
    strcpy(name + 2, h->name);
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x6C00; r.r1 = 0x0002; r.r2 = 0; r.r3 = 0x0012; r.r4 = (uint32_t)name;
    if (dos(&r)) {
        int e = ext_error();
        errorlevel = 4;
        if (e == 2) err("File creation error\r\n");
        else show_error_message(e);
        main_exit();
    }
    t_handle = r.r0 & 0xFFFF;
    if (t_filename[0] || t_template[0]) {
        u4_clr(&r);
        r.r0 = 0x4400; r.r1 = t_handle;
        dos(&r);
        if (r.r3 & 0x80) {
            out("Cannot XCOPY to a reserved device\r\n");
            main_exit();
        }
    }
    created = 1;
}

static void close_delete_file(struct hdr *h)
{
    close_a_file(t_handle);
    char name[20];
    name[0] = h->tname[0]; name[1] = ':';
    strcpy(name + 2, h->name);
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4100; r.r3 = (uint32_t)name;
    dos(&r);
}

static int write_a_file(struct hdr *h)
{
    uint8_t *data = (uint8_t *)h + HDR_BYTES;
    uint32_t done = 0;
    while (done < h->cx_bytes) {
        unsigned chunk = h->cx_bytes - done > 0xFFF0 ? 0xFFF0 : h->cx_bytes - done;
        int n = u4_write(t_handle, data + done, chunk);
        if (n < 0) {
            int e = ext_error();
            close_delete_file(h);
            errorlevel = 4;
            show_error_message(e);
            main_exit();
        }
        done += n;
        if ((unsigned)n != chunk) break;
    }
    if (done != h->cx_bytes) {
        errorlevel = 4;
        err("Insufficient disk space\r\n");
        disk_full = 1;
        close_delete_file(h);
        return -1;
    }
    return 0;
}

static void change_s_filemode(const char *dir, const char *name)
{
    char path[160];
    strcpy(path, dir);
    if (path[strlen(path) - 1] != '\\') strcat(path, "\\");
    strcat(path, name);
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4300; r.r3 = (uint32_t)path;
    dos(&r);
    uint32_t cx = r.r2 & 0xFFDF;
    u4_clr(&r);
    r.r0 = 0x4301; r.r2 = cx; r.r3 = (uint32_t)path;
    dos(&r);
}

/* RESET_S_ARCHIEVE: clear the source archive bits of what was written */
static void reset_s_archieve(uint32_t trouble)
{
    uint32_t last = old_buffer_ptr;
    if (disk_full) {
        if (trouble == 0) return;
        last = H(trouble)->before;
    }
    uint32_t p = 0;
    for (;;) {
        fix_root(s_arc_path);
        struct hdr *h = H(p);
        if (h->attr == 0x10) {
            if (h->depth > s_arc_depth) {
                concat(s_arc_path, s_arc_path[3] ? '\\' : 0, h->name);
                s_arc_depth++;
                if (p == last) break;
                p = h->next;
            } else {
                last_dir_out(s_arc_path);
                s_arc_depth--;
            }
            continue;
        }
        if (h->cont == 0 || h->cont == 3) change_s_filemode(s_arc_path, h->name);
        if (p == last) break;
        p = h->next;
    }
}

static void restore_filename_found(struct hdr *h)
{
    if (t_filename[0] || t_template[0]) strcpy(h->name, disp_s_file);
}

static void wfb_exit_a(uint32_t p)
{
    if (option & OPT_M) reset_s_archieve(p);
    if (mkdir_error || disk_full) main_exit();
}

static void write_from_buffer(void)
{
    reading_flag = 1;
    if (old_buffer_ptr == NONE) goto wfb_exit;
    uint32_t p = 0;
    for (;;) {
        if (chdir_t() != 0) main_exit();
chatt:  ;
        struct hdr *h = H(p);
        if (h->attr == 0x10) {
            if (h->depth > t_depth) {
                concat(t_drv_path, t_drv_path[3] ? '\\' : 0, h->name);
                concat_display_path(h->name);
                t_depth++;
                if (make_dir(h) != 0) { wfb_exit_a(p); goto wfb_exit; }
                if (p == old_buffer_ptr) { missing_link = 1; wfb_exit_a(p); goto wfb_exit; }
                p = h->next;
                continue;
            }
            rm_empty_dir();
            last_dir_out(t_drv_path);
            cut_display_path();
            t_depth--;
            continue;
        }
        missing_link = 0;
        if (!created) create_a_file(h);
        if (write_a_file(h) != 0) { wfb_exit_a(p); goto wfb_exit; }
        if (h->cont == 0 || h->cont == 3) {
            struct armregs r;
            u4_clr(&r);
            r.r0 = 0x5701; r.r1 = t_handle; r.r2 = h->time; r.r3 = h->date;
            dos(&r);
            close_a_file(t_handle);
            restore_filename_found(h);
            created = 0;
            file_count++;
        }
        if (p == old_buffer_ptr) { wfb_exit_a(p); goto wfb_exit; }
        p = h->next;
        goto chatt;
    }
wfb_exit:
    old_buffer_ptr = NONE;
    buffer_ptr = 0;
    buffer_left = max_buffer_size;
    /* the original walks the source with CHDIR; on one drive it goes back
     * to the source directory after writing (CHK_MKDIR_LVL relies on it) */
    if (one_disk) { fix_root(s_drv_path); chdir_(s_drv_path); }
}

/* ----------------------------------------------------------- the walk */

static void p_concat_display_path(const char *name)
{
    concat(disp_s_path, s_depth == 0 ? 0 : '\\', name);
}

static void p_cut_display_path(void)
{
    char *t = last_dir_out(disp_s_path);
    if (!t) {
        char *s = skip_drv(disp_s_path);
        if (s) *s = 0; else disp_s_path[0] = 0;
    } else if (s_depth == 0) {
        t[-1] = '\\';
        t[0] = 0;
    }
}

/* PROMPT_PATH_FILE: 1 = copy it */
static int prompt_path_file(void)
{
    strcpy(disp_s_file, DTA_NAME(file_dta));
    for (;;) {
        if (s_depth == 0) msg2(STDOUT, disp_s_path, "", disp_s_file, " (Y/N)?");
        else msg2(STDOUT, disp_s_path, "\\", disp_s_file, " (Y/N)?");
        int c = u4_upcase(getkey1());
        out("\r\n");
        if (c == 'Y') return 1;
        if (c == 'N') return 0;
    }
}

static int filter_files(void)
{
    if (option & (OPT_A | OPT_M))
        if (!(DTA_ATTR(file_dta) & 0x20)) return 0;
    if (option & OPT_D)
        if (DTA_DATE(file_dta) < input_date) return 0;
    if (option & OPT_P)
        if (!prompt_path_file()) return 0;
    return 1;
}

static void spec(char *buf, const char *name)
{
    strcpy(buf, s_drv_path);
    fix_root(buf);
    if (buf[strlen(buf) - 1] != '\\') strcat(buf, "\\");
    strcat(buf, name);
}

static void tree_copy(void)
{
    char sp[170];
    int first = 1;
    findfile_mode = 1;
    for (;;) {
        set_dta(file_dta);
        spec(sp, s_file);
        if (find(first, sp, 0) != 0) break;
        first = 0;
        found_file = 1;
        if (!filter_files()) continue;
        findfile_mode = 1;
        read_into_buffer();
    }
    if (!(option & OPT_S)) return;
    first = 1;
    for (;;) {
        findfile_mode = 0;
        set_dta(dtas[bp]);
        spec(sp, "????????.???");
        int rc = find(first, sp, 0x16);
        first = 0;
        if (rc != 0) break;
        if (DTA_ATTR(dtas[bp]) != 0x10 || DTA_NAME(dtas[bp])[0] == '.') continue;
        fix_root(s_drv_path);
        concat(s_drv_path, s_drv_path[3] ? '\\' : 0, DTA_NAME(dtas[bp]));
        if (option & OPT_P) p_concat_display_path(DTA_NAME(dtas[bp]));
        s_depth++;
        findfile_mode = 0;
        make_header(dtas[bp]);
        if (bp < 32) bp++;
        tree_copy();
    }
    if (s_depth == 0) return;
    s_depth--;
    if (!(option & OPT_E)) {
        /* DEL_EMPTY: drop a directory entry that got nothing after it */
        if (old_buffer_ptr != NONE && H(old_buffer_ptr)->attr & 0x10) {
            buffer_ptr = old_buffer_ptr;
            old_buffer_ptr = H(old_buffer_ptr)->before;
            buffer_left += HDR_PARAS;
        }
    }
    last_dir_out(s_drv_path);
    fix_root(s_drv_path);
    if (option & OPT_P) p_cut_display_path();
    bp--;
}

/* --------------------------------------------------------------- main */

static void init_buffer(void)
{
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x4800; r.r1 = 0xFFFF;
    dos(&r);
    uint32_t avail = r.r1 & 0xFFFF;
    if (avail < 0x140 * 2) goto nomem;
    uint32_t want = avail - 0x140;
    u4_clr(&r);
    r.r0 = 0x4800; r.r1 = want;
    if (dos(&r)) goto nomem;
    arena = (uint8_t *)((r.r0 & 0xFFFF) << 4);
    top_paras = want;
    buffer_ptr = 0;
    old_buffer_ptr = NONE;
    buffer_left = max_buffer_size = top_paras;
    return;
nomem:
    err("Insufficient memory\r\n");
    errorlevel = 4;
    main_exit_a();
}

int main(void)
{
    if (!u4_version_ok()) {
        err("Incorrect DOS version\r\n");
        return 0;
    }
    if (!fcb_drives_ok()) {
        init_error = 1;
        msg_for = "Invalid drive specification\r\n";
    } else {
        struct armregs r;
        u4_clr(&r);
        r.r0 = 0x2523; r.r3 = (uint32_t)ctrl_break;
        dos(&r);
        sav_default_drv = u4_curdrive() + 1;
        getcwd_(0, sav_default_dir);
        parse_line();
        if (!init_error) {
            /* GET_DRIVES */
            s_drv_number = so_drive != ' ' ? u4_upcase(so_drive) - '@' : sav_default_drv;
            t_drv_number = tar_drive != ' ' ? u4_upcase(tar_drive) - '@' : sav_default_drv;
            if (s_drv_number == t_drv_number) one_disk = 1;
            if (option & OPT_W) {
                out("Press any key to begin copying file(s)");
                getkey1();
                out("\r\n");
            }
            strcpy(disp_s_path, s_input_parm);
            strcpy(disp_t_path, t_input_parm);
            if (!first_parm && !second_parm) {
                msg_for = "Invalid number of parameters\r\n";
                init_error = 1;
            } else chk_set_parms();
            if (!init_error) {
                modify_one(disp_s_path, s_file_flag);
                modify_one(disp_t_path, t_file_flag);
            }
        }
        strcpy(s_arc_path, s_drv_path);
        if (!init_error) {
            init_buffer();
            set_default_drv(s_drv_number - 1);
        }
    }
    if (init_error) {
        if (msg_param) {
            u4_puts(STDERR, msg_for);
            u4_puts(STDERR, " - ");
            u4_puts(STDERR, bad_parm);
            u4_puts(STDERR, "\r\n");
        } else if (msg_for) u4_puts(STDERR, msg_for);
        errorlevel = 4;
        main_exit();
    }
    bp = 0;
    reading_flag = 1;
    tree_copy();
    if (sav_s_saved) chdir_(sav_s_path);
    write_from_buffer();
    main_exit();
}
