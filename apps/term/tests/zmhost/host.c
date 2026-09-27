/* host.c - runs apps/term/lib/zmodem.c natively on the build host over
 * stdin/stdout, so it can be checked against lrzsz (sz/rz) and itself.
 *   host send FILE...      host recv DIR       (see run.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/ioctl.h>
#include "../../lib/zmodem.h"
#include "../../lib/xmodem.h"

uint32_t zm_host_ticks(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec * 1000 + ts.tv_nsec / 1000000) * 182 / 10000);
}
static int hrx(void *ctx, int to)
{
    (void)ctx;
    struct pollfd p = { 0, POLLIN, 0 };
    if (poll(&p, 1, to) <= 0) return ZM_TIMEOUT;
    unsigned char c;
    if (read(0, &c, 1) != 1) return ZM_CARRIER;
    return c;
}
static int hready(void *ctx) { (void)ctx; int n = 0; ioctl(0, FIONREAD, &n); return n; }
static void htx(void *ctx, const uint8_t *b, int n) { (void)ctx; while (n > 0) { int k = (int)write(1, b, n); if (k <= 0) exit(9); b += k; n -= k; } }
static void hev(struct zm *z, int e)
{
    if (e == ZE_FILE) fprintf(stderr, "[host] file %s size %ld start %ld\n", z->fname, z->fsize, z->pos);
    if (e == ZE_MSG) fprintf(stderr, "[host] %s\n", z->msg);
    if (e == ZE_DONEFILE) fprintf(stderr, "[host] done %s %ld errors %d\n", z->fname, z->pos, z->errors);
}
int main(int argc, char **argv)
{
    static struct zm z;
    zm_init(&z);
    z.rx = hrx; z.rxready = hready; z.tx = htx; z.event = hev;
    if (getenv("ZBLK")) z.blksize = atoi(getenv("ZBLK"));
    if (argc >= 2 && !strcmp(argv[1], "send")) {
        if (getenv("ZRECOVER")) z.recover = 1;
        int rc = zm_send(&z, argv + 2, argc - 2);
        fprintf(stderr, "[host] send rc=%d %s files=%d\n", rc, zm_errstr(rc), z.files);
        return rc ? 1 : 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "recv")) {
        z.dir = argv[2];
        if (getenv("ZNO32")) z.no32 = 1;
        int rc = zm_receive(&z);
        fprintf(stderr, "[host] recv rc=%d %s files=%d\n", rc, zm_errstr(rc), z.files);
        return rc ? 1 : 0;
    }
    static struct xm x;
    xm_init(&x);
    x.rx = hrx; x.tx = htx;
    if (argc >= 3 && !strcmp(argv[1], "xsend")) { x.onek = getenv("X1K") != NULL; int rc = xm_send(&x, argv[2]); fprintf(stderr, "[host] xsend rc=%d errors=%d\n", rc, x.errors); return rc ? 1 : 0; }
    if (argc >= 3 && !strcmp(argv[1], "xrecv")) { int rc = xm_recv(&x, argv[2]); fprintf(stderr, "[host] xrecv rc=%d errors=%d\n", rc, x.errors); return rc ? 1 : 0; }
    if (argc >= 3 && !strcmp(argv[1], "ysend")) { x.gmode = getenv("YG") != NULL; int rc = xm_send_batch(&x, argv + 2, argc - 2); fprintf(stderr, "[host] ysend rc=%d files=%d errors=%d\n", rc, x.files, x.errors); return rc ? 1 : 0; }
    if (argc >= 3 && !strcmp(argv[1], "yrecv")) { x.dir = argv[2]; x.gmode = getenv("YG") != NULL; int rc = xm_recv_batch(&x); fprintf(stderr, "[host] yrecv rc=%d files=%d errors=%d\n", rc, x.files, x.errors); return rc ? 1 : 0; }
    fprintf(stderr, "usage: host send FILE... | host recv DIR\n");
    return 2;
}
