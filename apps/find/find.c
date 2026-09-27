/*
 * FIND - ARM-DOS re-creation of the MS-DOS 4.00 FIND filter.
 *
 *   FIND [/V] [/C] [/N] "string" [[d:][path]filename ...]
 *
 * Behaviour follows CMD/FIND/FIND.ASM of the MS-DOS 4.0 source (MIT
 * licence, (C) Microsoft Corp.): switches are collected by a first pass over
 * the whole line (so they count wherever they appear); every file gets the
 * heading CR LF "---------- NAME"; lines are matched case-sensitively; the
 * data is read in 4 KB blocks, ^Z ends it, a last line without LF gets
 * CR LF; /C prints ": count" after the heading (just the count for stdin).
 * Parse errors are prefixed "FIND: " until a file name has been seen.
 */
#include "u4.h"

#define BUFSZ 4096

static int v_flag, c_flag, n_flag;
static int got_filename, got_srch_str, got_eol, did_file;
static uint16_t mtch_cntr, line_cntr;
static char st_buffer[160];
static int st_length;
static char fname[160];
static uint8_t buffer[BUFSZ + 2];
static int errlevel;

static void prt_find(void)
{
    if (!got_filename) u4_puts(STDERR, "FIND: ");
}

/* display_and_die: class 2 parse message (or the utility's own message 2,
 * "Insufficient memory", for class -1) */
static __attribute__((noreturn)) void die_parse(int msg, int utility_class)
{
    prt_find();
    if (utility_class) u4_puts(STDERR, "Insufficient memory");
    else u4_parse_err(STDERR, msg, 0);
    u4_exit(2);
}

static const char n_sw[] = "/N";
static const char v_sw[] = "/V";
static const char c_sw[] = "/C";
static const struct u4_ctl sw1 = { 0, 2, "/N\0/V\0/C\0", 0, 0 };
static const struct u4_ctl pos1 = { P_QUOTED, 0, 0, 0, 0 };
static const struct u4_ctl pos2 = { P_FILE | P_REPEAT | P_OPTIONAL, P_CAP_FILE, 0, 0, 0 };
static const struct u4_ctl *const pos_tab[] = { &pos1, &pos2 };
static const struct u4_ctl *const sw_tab[] = { &sw1 };
static const struct u4_parms parms = { 1, 2, pos_tab, 1, sw_tab, 0, 0, ";", 0 };
static const struct u4_parms parms1 = { 0, 0, 0, 1, sw_tab, 0, 0, ";", 0 };

static int which_switch(const char *syn)
{
    if (!strcmp(syn, n_sw)) return 'N';
    if (!strcmp(syn, v_sw)) return 'V';
    if (!strcmp(syn, c_sw)) return 'C';
    return 0;
}

static void pre_parse(const char *line)
{
    struct u4_pstate st = { line, 0, 0, { 0 } };
    struct u4_result r;
    for (;;) {
        int rc = u4_parse(&parms1, &st, &r);
        if (rc == P_RC_EOL) break;
        if (rc == P_BAD_SWITCH) die_parse(P_BAD_SWITCH, 0);
        if (rc != P_OK) continue;
        if (r.type != R_STRING || !r.synonym) continue;
        switch (which_switch(r.synonym)) {
        case 'N': n_flag = 1; break;
        case 'V': v_flag = 1; break;
        case 'C': c_flag = 1; break;
        default: die_parse(P_BAD_SWITCH, 0);
        }
    }
}

static struct u4_pstate pst;

/* returns with a file name in fname (got_filename), or at EOL with none */
static void parse(void)
{
    struct u4_result r;
    for (;;) {
        int rc = u4_parse(&parms, &pst, &r);
        if (rc == P_RC_EOL) {
            got_eol = 1;
            if (did_file) u4_exit(0);
            break;
        }
        if (rc != P_OK) die_parse(rc, 0);
        if (r.type == R_QUOTED) {
            if (got_srch_str) die_parse(10, 0);
            strcpy(st_buffer, r.str);
            st_length = strlen(st_buffer);
            got_srch_str = 1;
            continue;
        }
        if (r.type == R_FILE) {
            strcpy(fname, r.str);
            got_filename = 1;
            break;
        }
        if (r.synonym) {            /* a switch: already handled */
            if (!which_switch(r.synonym)) die_parse(P_BAD_SWITCH, 0);
            continue;
        }
        die_parse(10, 0);
    }
    if (!got_srch_str) die_parse(2, 1);
}

static void prout(const void *p, unsigned n) { u4_write(STDOUT, p, n); }

static void print_count(int h)
{
    char num[12];
    if (h) prout(": ", 2);
    u4_utoa(mtch_cntr, num);
    prout(num, strlen(num));
    prout("\r\n", 2);
}

static int match(const uint8_t *line, int len)
{
    if (st_length == 0) return 0;
    for (int i = 0; i + st_length <= len; i++)
        if (!memcmp(line + i, st_buffer, st_length)) return 1;
    return 0;
}

/* scan one open file/stdin; returns 0 at the end, -1 on a read error */
static int scan(int h)
{
    for (;;) {
        int n = u4_read(h, buffer, BUFSZ);
        if (n < 0) return -1;
        uint8_t *z = n ? memchr(buffer, 0x1A, n) : 0;
        if (z) n = z - buffer;
        if (n == 0) {
            if (c_flag) print_count(h);
            return 0;
        }
        if (!memchr(buffer, '\n', n) || n <= BUFSZ - 1) {
            if (buffer[n - 1] != '\n') { buffer[n++] = '\r'; buffer[n++] = '\n'; }
        }
        int pos = 0, left = n;
        for (;;) {
            if (!left) break;
            uint8_t *lf = memchr(buffer + pos, '\n', left);
            if (!lf) break;
            int total = lf - (buffer + pos) + 1;
            left -= total;
            int cx = total - 1;
            if (lf > buffer && lf[-1] == '\r' && cx) cx--;
            line_cntr++;
            int m = cx ? match(buffer + pos, cx) : 0;
            if (m != v_flag) {
                if (c_flag) mtch_cntr++;
                else {
                    if (n_flag) {
                        char num[14];
                        num[0] = '[';
                        u4_utoa(line_cntr, num + 1);
                        strcat(num, "]");
                        prout(num, strlen(num));
                    }
                    prout(buffer + pos, total);
                }
            }
            pos += total;
        }
        if (left && u4_lseek(h, -(long)left, 1) < 0) return -1;
    }
}

int main(void)
{
    if (!u4_version_ok()) {
        prt_find();
        u4_puts(STDERR, "Incorrect DOS version\r\n");
        u4_exit(0);
    }
    char *line = u4_cmdline();
    if (_armdos_psp->cmdtail[0] == 0) die_parse(P_MISSING, 0);
    pre_parse(line);
    /* pre_parse ran on a copy: the parser may modify nothing, but restart */
    line = u4_cmdline();
    pst.si = line;
    pst.ordinal = 0;
    for (;;) {
        mtch_cntr &= 0xFF00;            /* FIND clears only the low bytes */
        line_cntr &= 0xFF00;
        parse();
        int h = 0;
        if (got_filename) {
            h = u4_open(fname, 0);
            if (h < 0) {
                u4_exterr(STDERR, u4_err == 5 ? 5 : 2, fname);
                goto scan_rest;
            }
            prout("\r\n---------- ", 13);
            prout(fname, strlen(fname));
            if (!c_flag) prout("\r\n", 2);
        }
        if (scan(h) < 0) {
            if (!h) break;
            u4_close(h);
            u4_exterr(STDERR, 30, fname);
            goto scan_rest;
        }
        if (!h) break;
        u4_close(h);
scan_rest:
        did_file = 1;
        if (got_eol) break;
    }
    u4_exit(errlevel);
}
