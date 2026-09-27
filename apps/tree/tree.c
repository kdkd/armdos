/*
 * TREE - ARM-DOS re-creation of the MS-DOS 4.00 TREE command.
 *
 *   TREE [d:][path] [/F] [/A]
 *
 * A port of CMD/TREE/TREE.ASM + TREEPAR.ASM of the MS-DOS 4.0 source (MIT
 * licence, (C) Microsoft Corp.), kept close to the original so its output
 * is the same byte for byte: the line buffer BUF with one 4-column slot per
 * level, the per-level work area (search state, graphic, path), the look-
 * ahead for "more subdirectories" with Find Next in the shared DTA, the
 * "blank" line after a directory's files (which shows whatever deeper
 * levels left in BUF), the NUL written after the starting path, "attribute
 * exactly 10h" for directories.
 */
#include "u4.h"

#define DASH_NUM 3
#define MAX_PATH 64
#define LEVEL_LIMIT (MAX_PATH / 2)

#define F_DEF_PAT_TAR 0x40
#define F_SUBDIR      0x20
#define F_FAILING     0x10
#define F_FLN         0x08
#define F_FIRSTIME    0x04
#define F_SWITCH      0x02

#define ATTR_NORMAL 0x00
#define ATTR_DIR    0x10
#define ATTR_VOLID  0x08

static uint8_t graf_elbo = 0xC0, graf_dash = 0xC4, graf_tee = 0xC3, graf_bar = 0xB3;
static uint8_t flags;
static int current_col = 1;
static uint8_t buf[(DASH_NUM + 1) * LEVEL_LIMIT + 64];
static int exitfl;
static char default_dr;
static int start_dr_num;
static char default_path[1 + MAX_PATH + 64] = "\\";

static struct dta {
    uint8_t res[21];
    uint8_t attr;
    uint16_t time, date, lsiz, hsiz;
    char filn[13];
} __attribute__((packed)) dta;
static char savefiln[13];

/* START_DRIVE, ":", START_PATH, contiguous as in the original */
static char start[2 + MAX_PATH + 13 + 64] = { 0, ':', '.' };
#define start_drive (start[0])
#define start_path  (start + 2)

struct frame {
    uint8_t res[21];
    uint8_t chr;
    char curr_path[MAX_PATH + 2];
};

/* ---------------------------------------------------------------- DOS */
static int dos(struct armregs *r) { return u4_int21(r); }

static int chdir_(const char *p)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x3B00; r.r3 = (uint32_t)p;
    return dos(&r);
}

static void getcurdir(int drive, char *p)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4700; r.r3 = drive; r.r4 = (uint32_t)p;
    dos(&r);
}

static int findfirst(int attr)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4E00; r.r2 = attr; r.r3 = (uint32_t)"*.*";
    return dos(&r);
}

static int findnext(void)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4F00;
    return dos(&r);
}

static void select_disk(int d)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x0E00; r.r3 = d;
    dos(&r);
}

static int currdisk(void) { return u4_curdrive(); }

static int extended_error(void)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x5900;
    dos(&r);
    return r.r0 & 0xFFFF;
}

/* CY clear = "no more files", else EXITFL is set */
static int if_nomorefiles(void)
{
    if (extended_error() == 18) return 0;
    exitfl = 1;
    return 1;
}

static void do_write(const void *p, unsigned n)
{
    u4_write(STDOUT, p, n);
    u4_write(STDOUT, "\r\n", 2);
}

/* ------------------------------------------------------------ the tree */
static void execute(void);

static void fix_graf(struct frame *f)
{
    if (f->chr == graf_elbo) f->chr = ' ';
    else if (f->chr == graf_tee) f->chr = graf_bar;
}

static void any_more_subdir(struct frame *f)
{
    memcpy(savefiln, dta.filn, 13);
    int cy = (flags & F_FIRSTIME) ? findnext() : findfirst(ATTR_DIR);
    for (;;) {
        if (cy) { if_nomorefiles(); f->chr = graf_elbo; break; }
        if (dta.attr == ATTR_DIR && dta.filn[0] != '.') { f->chr = graf_tee; break; }
        cy = findnext();
    }
    if (flags & F_FIRSTIME) memcpy(dta.res, f->res, 21);
    memcpy(dta.filn, savefiln, 13);
}

static void fln_to_buf(int bl)
{
    uint8_t *d = buf + current_col + DASH_NUM;      /* FLN_INDENT = 0 for both */
    (void)bl;
    for (int i = 0; i < 13; i++) {
        uint8_t c = dta.filn[i];
        *d++ = c;
        if (!c) break;
    }
}

static void graf_to_buf(struct frame *f, int bl)
{
    if (!(flags & F_SWITCH)) flags |= F_FIRSTIME;
    if (bl == ATTR_DIR) any_more_subdir(f);
    else if (!(flags & F_FIRSTIME)) fix_graf(f);
    flags |= F_FIRSTIME;
    buf[current_col - 1] = f->chr;
}

static void blank_dash(int bl)
{
    memset(buf + current_col, bl != ATTR_DIR ? ' ' : graf_dash, DASH_NUM);
}

static void show_fn(struct frame *f, int al)
{
    if (dta.filn[0] == '.') return;
    int bl = al;
    fln_to_buf(bl);
    graf_to_buf(f, bl);
    blank_dash(bl);
    if (flags & F_FLN) do_write(buf, strlen((char *)buf));
    fix_graf(f);
    buf[current_col - 1] = f->chr;
    if (bl == ATTR_DIR) {
        flags |= F_SUBDIR;
        memset(buf + current_col, ' ', DASH_NUM);
    }
}

static int begin_find(struct frame *f, int attr)
{
    int cy = findfirst(attr);
    memcpy(f->res, dta.res, 21);
    return cy;
}

static int find_next(struct frame *f)
{
    memcpy(dta.res, f->res, 21);
    int cy = findnext();
    memcpy(f->res, dta.res, 21);
    return cy;
}

static void find_type_normal(struct frame *f)
{
    flags &= ~F_FLN;
    int cy = begin_find(f, ATTR_NORMAL);
    while (!cy) {
        flags |= F_FLN;
        show_fn(f, ATTR_NORMAL);
        cy = find_next(f);
    }
    if (!if_nomorefiles()) {
        memset(dta.filn, ' ', 13);
        show_fn(f, ATTR_NORMAL);
    }
}

static void next_level(void)
{
    getcurdir(0, start_path + 1);
    char *s = start_path;
    int dl = 0;
    while (*s) dl = (uint8_t)*s++;
    if (dl != '\\') *s++ = '\\';
    memcpy(s, dta.filn, 13);
    flags &= ~F_FIRSTIME;
}

static void find_type_dir(struct frame *f)
{
    flags |= F_FLN;
    int cy = begin_find(f, ATTR_DIR);
    for (;;) {
        if (cy) {
            if (exitfl == 0) if_nomorefiles();
            return;
        }
        if (dta.filn[0] != '.' && dta.attr == ATTR_DIR) {
            show_fn(f, ATTR_DIR);
            current_col += DASH_NUM + 1;
            memcpy(dta.filn, buf + current_col - 1, 13);
            next_level();
            execute();
            current_col -= DASH_NUM + 1;
            chdir_(f->curr_path);
        }
        if (exitfl) return;
        cy = find_next(f);
    }
}

static void invalid_path(void)
{
    u4_class_msg(STDERR, "Invalid path", start_path);   /* COMMON25 + " - " %0 */
}

static void execute(void)
{
    struct frame f;
    int cy;
    memset(&f, 0, sizeof f);
    if (start_path[0] != '\\') {
        cy = chdir_(start_path);
        if (!cy) {
            f.curr_path[0] = '\\';
            getcurdir(start_drive - 'A' + 1, f.curr_path + 1);
            memcpy(start_path, f.curr_path, MAX_PATH + 1);
        }
    } else {
        memcpy(f.curr_path, start_path, MAX_PATH + 1);
        cy = chdir_(start_path);
    }
    if (!cy) {
        any_more_subdir(&f);
        if (flags & F_SWITCH) find_type_normal(&f);
        find_type_dir(&f);
    } else {
        invalid_path();
        exitfl = 1;
    }
}

/* ----------------------------------------------------------- the rest */
static void get_vol_label(void)
{
    struct armregs r; u4_clr(&r);
    r.r0 = 0x4E00; r.r2 = ATTR_VOLID; r.r3 = (uint32_t)"*.*";
    if (!dos(&r)) {
        dta.filn[8] = dta.filn[9]; dta.filn[9] = dta.filn[10];
        dta.filn[10] = dta.filn[11]; dta.filn[11] = dta.filn[12];
        u4_msg1(STDOUT, "Directory PATH listing for Volume %1\r\n", dta.filn);
    } else {
        u4_puts(STDOUT, "Directory PATH listing\r\n");
    }
    /* VOLSER: Get Media ID */
    struct { uint16_t level; uint32_t serial; char label[11]; char fs[8]; } __attribute__((packed)) mid;
    u4_clr(&r);
    r.r0 = 0x6900; r.r1 = start_drive - 'A' + 1; r.r3 = (uint32_t)&mid;
    if (!dos(&r)) {
        char a[6], b[6];
        u4_hex(mid.serial >> 16, 4, a);
        u4_hex(mid.serial & 0xFFFF, 4, b);
        const char *args[2] = { a, b };
        u4_msg(STDOUT, "Volume Serial Number is %1-%2\r\n", args);
    }
    do_write(start, strlen(start) + 1);         /* LEN_ASCIIZ counts the NUL */
}

static void restore(void)
{
    if (!(flags & F_FAILING) && (flags & F_DEF_PAT_TAR)) chdir_(default_path);
    select_disk(start_dr_num);
}

static int verify_drive(void)
{
    if (start_drive == default_dr) return 0;
    select_disk(start_drive - 'A');
    if (currdisk() == start_dr_num) {
        u4_exterr(STDERR, 15, 0);
        return 1;
    }
    return 0;
}

/* the parser: one optional file spec, /F and /A (each only once) */
static char f_sw[] = "/F\0/A\0";
static const struct u4_ctl ctl_pos = { P_FILE | P_OPTIONAL, P_CAP_FILE, 0, 0, 0 };
static const struct u4_ctl ctl_sw = { 0, P_CAP_CHAR, f_sw, 0, 0 };
static const struct u4_ctl *const pos_tab[] = { &ctl_pos };
static const struct u4_ctl *const sw_tab[] = { &ctl_sw };
static const struct u4_parms parms = { 0, 1, pos_tab, 1, sw_tab, 0, 0, 0, 0 };

static int check_path(const struct u4_result *r)
{
    if (r->ctl == &ctl_pos) {
        const char *si = r->str;
        char *di = start_path;
        if (si[0] && si[1] == ':') {
            start_drive = si[0];
            si += 2;
        }
        int dl = 0, dh = 0;
        for (const char *q = si; *q; q++) { dh = dl; dl = (uint8_t)*q; }
        int cx = MAX_PATH + 1;
        for (;;) {
            char c = *si++;
            if (!c) {
                if (dl == '\\' && dh != '\\' && cx != MAX_PATH) di[-1] = 0;
                return 0;
            }
            if (--cx == 0) break;
            *di++ = c;
        }
        invalid_path();
        return 1;
    }
    if (r->synonym == f_sw) {
        f_sw[0] = ' ';                  /* strike /F: a second one is invalid */
        flags |= F_SWITCH;
    } else {
        f_sw[3] = ' ';
        graf_elbo = '\\'; graf_dash = '-'; graf_tee = '+'; graf_bar = '|';
    }
    return 0;
}

static int parse(void)
{
    char *line = (char *)_armdos_psp->cmdtail + 1;
    line[_armdos_psp->cmdtail[0] < 127 ? _armdos_psp->cmdtail[0] : 126] = '\r';
    struct u4_pstate st = { line, 0, 0, { 0 } };
    struct u4_result r;
    for (;;) {
        const char *current_parm = st.si;
        int rc = u4_parse(&parms, &st, &r);
        if (rc == P_RC_EOL) break;
        if (rc) {
            if (st.si != current_parm) {
                *(char *)st.si = 0;
                u4_parse_err(STDERR, rc, current_parm);
            } else u4_parse_err(STDERR, rc, 0);
            return 1;
        }
        if (check_path(&r)) return 1;
    }
    if (!start_drive) start_drive = default_dr;
    else if (verify_drive()) return 1;
    if (!start_path[0]) {
        start_path[0] = '\\';
        getcurdir(0, start_path + 1);
    }
    return 0;
}

int main(void)
{
    if (!u4_version_ok()) { u4_puts(STDERR, "Incorrect DOS version\r\n"); return 2; }
    start_dr_num = currdisk();
    default_dr = 'A' + start_dr_num;
    {
        struct armregs r; u4_clr(&r);
        r.r0 = 0x1A00; r.r3 = (uint32_t)&dta;
        dos(&r);
    }
    if (!parse()) {
        getcurdir(start_drive - 'A' + 1, default_path + 1);
        flags |= F_DEF_PAT_TAR;
        get_vol_label();
        execute();
        if (!(flags & F_SUBDIR)) u4_puts(STDOUT, "No sub-directories exist\r\n\n");
    } else exitfl = 1;
    restore();
    return exitfl;
}
