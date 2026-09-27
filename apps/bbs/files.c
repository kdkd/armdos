/* files.c - file areas (FILES\<area>\ with a FILES.BBS list) and the
 * protocol glue for downloads/uploads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dos.h>
#include "../term/lib/comm.h"
#include "../term/lib/scr.h"
#include "../term/lib/zmodem.h"
#include "../term/lib/xmodem.h"
#include "bbs.h"

static int cur_farea;

/* ------------------------------------------------------------ transfers */
static const char *xname;
static uint32_t lastdraw;

static void xfer_status(const char *proto, const char *name, long pos, long size, uint32_t t0, int errors, const char *msg)
{
    if (TICKS() - lastdraw < 5) return;
    lastdraw = TICKS();
    char a[16], b[16], line[96];
    commas(a, pos); commas(b, size < 0 ? 0 : size);
    uint32_t el = TICKS() - t0;
    long cps = el > 9 ? (long)((long long)pos * 182 / (long long)el / 10) : 0;   /* 64-bit: bytes*182 overflows 32 bits past 11.8 MB */
    snprintf(line, sizeof line, " %s %-12.12s %s of %s bytes  %ld cps  errors %d  %-.16s", proto, name, a, b, cps, errors, msg ? msg : "");
    scr_fill(0, 24, 80, 1, ' ', 0x4F);
    scr_puts(0, 24, 0x4F, line);
}

static int io_rx(void *ctx, int to)
{
    (void)ctx;
    uint32_t t0 = TICKS(), lim = ms2ticks((uint32_t)to);
    for (;;) {
        int c = com_getc();
        if (c >= 0) return c;
        if (sio_remote && !com_carrier()) return ZM_CARRIER;
        if (TICKS() - t0 >= lim) return ZM_TIMEOUT;
        idle();
    }
}
static int io_ready(void *ctx) { (void)ctx; return com_avail(); }
static void io_tx(void *ctx, const uint8_t *b, int n) { (void)ctx; com_write(b, n); }
static void io_flush(void *ctx) { (void)ctx; com_txflush(); }
static void io_purge(void *ctx) { (void)ctx; com_txpurge(); }
static int io_abort(void *ctx) { (void)ctx; return key_ready() && key_get() == K_ESC; }
static void zev(struct zm *z, int e)
{
    (void)e;
    xfer_status(xname, z->fname, z->pos, z->fsize, z->t0, z->errors, z->msg);
}
static void xev(struct xm *x, int e)
{
    (void)e;
    xfer_status(xname, x->fname, x->pos, x->fsize, x->t0, x->errors, x->msg);
}

int pick_proto(void)
{
    sio_puts("@X0EProtocol: @X0F[@X0EZ@X0F]@X07MODEM  @X0F[@X0EY@X0F]@X07MODEM  @X0F[@X0EX@X0F]@X07MODEM  @X0F[@X0EQ@X0F]@X07uit @X0B(Enter = Z)@X07: ");
    int k = sio_hotkey("ZYXQ\r");
    if (k == '\r') k = 'Z';
    return k == 'Q' ? 0 : k;
}

static const char *pname(int p) { return p == 'Z' ? "ZMODEM" : p == 'Y' ? "YMODEM" : "XMODEM"; }

int xfer_send(int proto, char *const *paths, int n)
{
    static struct zm z;
    static struct xm x;
    int rc;
    xname = pname(proto);
    lastdraw = 0;
    if (proto == 'Z') {
        zm_init(&z);
        z.rx = io_rx; z.rxready = io_ready; z.tx = io_tx; z.txflush = io_flush; z.txpurge = io_purge; z.aborted = io_abort; z.event = zev;
        rc = zm_send(&z, paths, n);
    } else {
        xm_init(&x);
        x.rx = io_rx; x.tx = io_tx; x.txflush = io_flush; x.txpurge = io_purge; x.aborted = io_abort; x.event = xev;
        x.onek = 1;
        rc = proto == 'Y' ? xm_send_batch(&x, paths, n) : xm_send(&x, paths[0]);
    }
    delay_ms(500);
    com_rxpurge();
    sio_touch();                        /* the transfer was activity, not 5 idle minutes */
    local_status();
    return rc;
}

int xfer_recv(int proto, const char *dir, char *got, int gotmax)
{
    static struct zm z;
    static struct xm x;
    int rc;
    xname = pname(proto);
    lastdraw = 0;
    got[0] = 0;
    if (proto == 'Z') {
        zm_init(&z);
        z.rx = io_rx; z.rxready = io_ready; z.tx = io_tx; z.txflush = io_flush; z.txpurge = io_purge; z.aborted = io_abort; z.event = zev;
        z.dir = dir; z.resume = 1;
        rc = zm_receive(&z);
        if (z.files) snprintf(got, gotmax, "%s", z.fname);
        if (!rc && !z.files) rc = ZM_ERROR;
    } else if (proto == 'Y') {
        xm_init(&x);
        x.rx = io_rx; x.tx = io_tx; x.txflush = io_flush; x.txpurge = io_purge; x.aborted = io_abort; x.event = xev;
        x.dir = dir;
        rc = xm_recv_batch(&x);
        if (x.files) snprintf(got, gotmax, "%s", x.fname);
    } else {
        xm_init(&x);
        x.rx = io_rx; x.tx = io_tx; x.txflush = io_flush; x.txpurge = io_purge; x.aborted = io_abort; x.event = xev;
        char path[96];
        snprintf(path, sizeof path, "%s\\%s", dir, got[0] ? got : "UPLOAD.DAT");
        rc = xm_recv(&x, path);
    }
    delay_ms(500);
    com_rxpurge();
    sio_touch();                        /* the transfer was activity, not 5 idle minutes */
    local_status();
    return rc;
}

/* ------------------------------------------------------------ file areas */
static int list_file_count(const char *dir)
{
    char p[80], line[160];
    snprintf(p, sizeof p, "%s\\FILES.BBS", dir);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    int n = 0;
    while (fgets(line, sizeof line, f)) if (isalnum((unsigned char)line[0])) n++;
    fclose(f);
    return n;
}

int file_count(void)
{
    int n = 0;
    for (int i = 0; i < nfileareas; i++) n += list_file_count(fileareas[i].path);
    return n;
}

/* how much of s fits in w columns, breaking at a space */
static int desc_chunk(const char *s, int w)
{
    int n = (int)strlen(s);
    if (n <= w) return n;
    int k = w;
    while (k > 0 && s[k] != ' ') k--;
    return k > 0 ? k : w;
}

static void list_files(int a)
{
    char p[80], line[160];
    snprintf(p, sizeof p, "%s\\FILES.BBS", fileareas[a].path);
    FILE *f = fopen(p, "r");
    sio_cls();
    sio_printf("@X1F Files in area %d: %s @X07\n\n", fileareas[a].num, fileareas[a].name);
    sio_puts("@X0FFilename        Size    Date     Description@X07\n");
    sio_puts("@X08\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4@X07\n");
    sio_linecount = 4;
    if (!f) { sio_puts("@X0C(no files)@X07\n"); return; }
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (!line[0]) continue;
        if (!isalnum((unsigned char)line[0])) {           /* comment / continuation line */
            sio_printf("@X08%s@X07\n", line);
            if (!sio_line_done()) break;
            continue;
        }
        char name[16]; int i = 0;
        while (line[i] && line[i] != ' ' && i < 12) { name[i] = line[i]; i++; }
        name[i] = 0;
        const char *desc = line + i; while (*desc == ' ') desc++;
        char fp[96]; snprintf(fp, sizeof fp, "%s\\%s", fileareas[a].path, name);
        struct find_t ft;
        int n = desc_chunk(desc, 46);
        if (_dos_findfirst(fp, _A_NORMAL | _A_RDONLY | _A_ARCH, &ft) == 0) {
            sio_printf("@X0E%-12s @X0B%8lu @X0A%02u-%02u-%02u @X07%.*s\n", name, (unsigned long)ft.size,
                       (ft.wr_date >> 5) & 15, ft.wr_date & 31, ((ft.wr_date >> 9) + 80) % 100, n, desc);
        } else sio_printf("@X0E%-12s @X0C  offline          @X07%.*s\n", name, n, desc);
        if (!sio_line_done()) break;
        int stop = 0;
        for (desc += n; !stop; desc += n) {                     /* the rest, wrapped under the description */
            while (*desc == ' ') desc++;
            if (!*desc) break;
            n = desc_chunk(desc, 46);
            sio_printf("%32s@X07%.*s\n", "", n, desc);
            stop = !sio_line_done();
        }
        if (stop) break;
    }
    fclose(f);
}

static int find_file(const char *name, char *path, int max, long *size)
{
    struct find_t ft;
    for (int k = 0; k < nfileareas; k++) {
        int a = (cur_farea + k) % nfileareas;
        snprintf(path, max, "%s\\%s", fileareas[a].path, name);
        if (_dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_ARCH, &ft) == 0) { *size = (long)ft.size; return a; }
    }
    return -1;
}

static void download(void)
{
    char name[16], path[96];
    long size;
    sio_puts("\n@X0EFilename to download (Enter = cancel): @X0F");
    sio_getline(name, 12, GL_UPPER);
    if (!name[0]) return;
    if (strpbrk(name, "\\/:*?")) { sio_puts("@X0CJust the file name, please.@X07\n"); return; }
    int a = find_file(name, path, sizeof path, &size);
    if (a < 0) { sio_printf("@X0CSorry, %s is not on this board.@X07\n", name); return; }
    char sz[16]; commas(sz, size);
    long secs = size / (session_baud > 0 ? session_baud / 10 : 240) + 3;
    sio_printf("@X0B%s@X07 (%s bytes) found in @X0F%s@X07.\n", name, sz, fileareas[a].name);
    sio_printf("Estimated transfer time: @X0F%ld:%02ld@X07 at %ld baud.\n", secs / 60, secs % 60, session_baud);
    int p = pick_proto();
    if (!p) return;
    sio_printf("\n@X0AReady to send %s using %s.@X07\n", name, pname(p));
    sio_puts(p == 'Z' ? "Your terminal should start receiving by itself.\n" : "Start your download now.\n");
    sio_puts("Press Ctrl-X several times to cancel.\n");
    sio_flush();
    delay_ms(500);
    char *list[1] = { path };
    int rc = xfer_send(p, list, 1);
    sio_puts("\n");
    if (rc == 0) {
        user.downs++;
        user.downk += (uint32_t)((size + 1023) / 1024);
        user_save(usernum, &user);
        sio_puts("@X0ATransfer successful.  Thank you!@X07\n");
        sysop_log("%s downloaded %s (%s)", user.name, name, pname(p));
    } else {
        sio_printf("@X0CTransfer aborted: %s.@X07\n", zm_errstr(rc));
        sysop_log("%s: download of %s failed (%s)", user.name, name, zm_errstr(rc));
    }
}

static int upload_area(void)
{
    for (int i = 0; i < nfileareas; i++) if (!strcasecmp(fileareas[i].extra, "UPLOAD")) return i;
    return nfileareas - 1;
}

static void upload(void)
{
    char desc[50], name[16] = "", got[80];
    int a = upload_area();
    if (a < 0) return;
    sio_printf("\n@X0BUploads go to @X0F%s@X0B.  Thank you for sharing!@X07\n", fileareas[a].name);
    int p = pick_proto();
    if (!p) return;
    if (p == 'X') {
        sio_puts("@X0EFilename: @X0F");
        sio_getline(name, 12, GL_UPPER);
        if (!name[0] || strpbrk(name, "\\/:*?")) return;
    }
    sio_puts("@X0EDescribe your file (one line): @X0F");
    sio_getline(desc, 45, 0);
    sio_printf("\n@X0ABegin your %s upload now (in TERM: PgUp).@X07  Press Ctrl-X several times to cancel.\n", pname(p));
    sio_flush();
    strcpy(got, name);
    int rc = xfer_recv(p, fileareas[a].path, got, sizeof got);
    sio_puts("\n");
    if (rc == 0 && got[0]) {
        char lp[80];
        snprintf(lp, sizeof lp, "%s\\FILES.BBS", fileareas[a].path);
        FILE *f = fopen(lp, "a");
        if (f) { fprintf(f, "%-12s  %s [from %s]\n", got, desc[0] ? desc : "Uploaded file", user.name); fclose(f); }
        user.ups++;
        user_save(usernum, &user);
        sio_printf("@X0AGot it: %s.  Your upload credit has been noted!@X07\n", got);
        sysop_log("%s uploaded %s", user.name, got);
    } else sio_printf("@X0CUpload failed: %s.@X07\n", zm_errstr(rc));
}

static void change_farea(void)
{
    char b[8];
    sio_puts("\n@X0FFile areas:@X07\n");
    for (int i = 0; i < nfileareas; i++)
        sio_printf("  @X0E%d@X07  %-30s @X08(%d files)@X07\n", fileareas[i].num, fileareas[i].name, list_file_count(fileareas[i].path));
    sio_puts("@X0EArea # (Enter = no change): @X0F");
    sio_getline(b, 3, GL_DIGITS);
    for (int i = 0; i < nfileareas; i++) if (b[0] && atoi(b) == fileareas[i].num) cur_farea = i;
}

void file_menu(void)
{
    if (!nfileareas) { sio_puts("@X0CNo file areas are set up.@X07\n"); return; }
    for (;;) {
        sio_cls();
        sio_puts("\n@X1F  FILE MENU  @X07\n\n");
        sio_puts("  @X0F[@X0EL@X0F]@X07 List files           @X0F[@X0ED@X0F]@X07 Download a file\n");
        sio_puts("  @X0F[@X0EU@X0F]@X07 Upload a file        @X0F[@X0EA@X0F]@X07 Change area\n");
        sio_puts("  @X0F[@X0EQ@X0F]@X07 Quit to main menu\n\n");
        sio_printf("@X0BCurrent area: @X0F%d - %s@X0B  (%d files)@X07\n", fileareas[cur_farea].num, fileareas[cur_farea].name,
                   list_file_count(fileareas[cur_farea].path));
        sio_printf("\n@X0BFile Menu @X0F[@X0EL D U A Q@X0F]@X0B (%lu min left): @X0F", (unsigned long)sio_minutes_left());
        int k = sio_hotkey("LDUAQ");
        switch (k) {
        case 'L': list_files(cur_farea); sio_pause(); break;
        case 'D': download(); sio_pause(); break;
        case 'U': upload(); sio_pause(); break;
        case 'A': change_farea(); break;
        case 'Q': return;
        }
    }
}
