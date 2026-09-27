/* k_con.c - console input, Ctrl-C and critical errors, driven by keys the
   test harness types.  Roles (argv[1]):
     LINE      AH=0Ah in a loop with one buffer (so the template carries over);
               logs T:LINE [text]; ends on an empty line
     CHARS     AH=01h 06h 07h 08h 0Bh 0Ch
     CTRLC     AH=01h until ^C kills us (exit type 1)
     HANDLER   own INT 23h that says "continue": ^C then 'x' -> AH=01h returns 'x'
     BREAK     BREAK ON, a loop of AH=2Ch: ^C ends it even without console I/O
     CRIT      INT 24h on the empty drive A: with each possible answer
     CRITWP    INT 24h writing to a write-protected diskette in A:
     COOKED    handle reads of CON in cooked and raw mode
*/
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)

static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{
    memset(&R, 0, sizeof R);
    R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx;
    return _armdos_int21(&R);
}

static void role_line(void)
{
    static uint8_t buf[130];
    buf[0] = 60;
    for (;;) {
        d21(0x0900, 0, 0, (uint32_t)"L>$");
        d21(0x0A00, 0, 0, (uint32_t)buf);
        d21(0x0900, 0, 0, (uint32_t)"\r\n$");
        char s[130];
        int n = buf[1];
        for (int i = 0; i < n; i++) s[i] = buf[2 + i] ? buf[2 + i] : '@';
        s[n] = 0;
        t_log("T:LINE [%s] %d cr=%d\n", s, n, buf[2 + n] == '\r');
        if (n == 0) break;
        if (buf[2] == 'Q') break;
    }
}

static void role_chars(void)
{
    /* the harness types: a b c d e (f waits) */
    d21(0x0100, 0, 0, 0); t_log("T:C01 %02x\n", (unsigned)(R.r0 & 0xFF));   /* echoed */
    d21(0x0800, 0, 0, 0); t_log("T:C08 %02x\n", (unsigned)(R.r0 & 0xFF));
    d21(0x0700, 0, 0, 0); t_log("T:C07 %02x\n", (unsigned)(R.r0 & 0xFF));
    for (;;) { d21(0x0600, 0, 0, 0xFF); if (!(R.cpsr & ARM_CPSR_Z)) break; }
    t_log("T:C06 %02x\n", (unsigned)(R.r0 & 0xFF));
    /* 0Bh: nothing waiting now */
    d21(0x0B00, 0, 0, 0); t_log("T:C0B %02x\n", (unsigned)(R.r0 & 0xFF));
    d21(0x0200, 0, 0, '*');
    t_log("T:READY\n");
    /* the harness types 'e' and 'f'; 0Ch flushes 'e', then waits for 'g' */
    for (;;) { d21(0x0B00, 0, 0, 0); if (R.r0 & 0xFF) break; }
    armdos_halt(); armdos_halt(); armdos_halt();
    d21(0x0C08, 0, 0, 0); t_log("T:C0C %02x\n", (unsigned)(R.r0 & 0xFF));
    /* extended key: F1 gives 00 3B */
    d21(0x0800, 0, 0, 0); unsigned a = R.r0 & 0xFF;
    d21(0x0800, 0, 0, 0); unsigned b = R.r0 & 0xFF;
    t_log("T:FKEY %02x %02x\n", a, b);
}

static void role_ctrlc(void)
{
    for (;;) d21(0x0100, 0, 0, 0);
}

static volatile int hcount;
static void my23(struct armregs *f)
{
    hcount++;
    f->cpsr &= ~ARM_CPSR_C;                 /* continue: retry the call */
}

static void exit23(struct armregs *f)
{
    (void)f;
    struct armregs r = { 0 };
    r.r0 = 0x4C2A;                           /* exit(42) from inside INT 23h */
    _armdos_int21(&r);
}

static void role_handler(void)
{
    d21(0x2523, 0, 0, (uint32_t)my23);
    d21(0x0100, 0, 0, 0);
    t_log("T:HANDLER count=%d char=%02x\n", hcount, (unsigned)(R.r0 & 0xFF));
}

static void role_break(void)
{
    d21(0x3301, 0, 0, 1);                    /* BREAK ON */
    d21(0x3300, 0, 0, 0);
    t_log("T:BREAKON %d\n", (int)(R.r3 & 0xFF));
    for (unsigned long i = 0;; i++) {
        d21(0x2C00, 0, 0, 0);
        if (i == 50) t_log("T:LOOPING\n");
    }
}

static volatile int ccount, cah, cal, cdi, canswer;
static volatile uint32_t cdev;
static void my24(struct armregs *f)
{
    ccount++;
    cah = (f->r0 >> 8) & 0xFF;
    cal = f->r0 & 0xFF;
    cdi = f->r5 & 0xFF;
    cdev = f->r4;
    int a = canswer;
    if (a == 1 && ccount > 2) a = 3;        /* retry twice, then give up */
    f->r0 = (f->r0 & ~0xFF) | a;
}

static void role_crit(void)
{
    d21(0x2524, 0, 0, (uint32_t)my24);
    /* Fail */
    canswer = 3;
    int cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\X.TXT");
    t_log("T:CRIT fail cf=%d ax=%u count=%d ah=%02x al=%d di=%d\n", cf, AXV, ccount, cah, cal, cdi);
    d21(0x5900, 0, 0, 0);
    t_log("T:CRIT exterr %u\n", AXV);
    const char *name = (const char *)cdev + 10;
    t_log("T:CRIT dev %s\n", ((const uint16_t *)cdev)[2] & 0x8000 ? "char" : "block");
    (void)name;
    /* Retry (3 times), then Fail */
    ccount = 0;
    canswer = 1;
    d21(0x3600, 0, 0, 1);                    /* free space on A: -> AX=FFFF after fail */
    t_log("T:CRIT df ax=%x count=%d\n", AXV, ccount);
    /* Abort: the program ends with exit type 2 */
    canswer = 2;
    d21(0x3D00, 0, 0, (uint32_t)"A:\\X.TXT");
    t_log("T:NOT REACHED\n");
}

static volatile int rcount;
static void retry24(struct armregs *f)
{
    rcount++;
    if (rcount == 1) t_log("T:I24WAIT\n");
    armdos_halt();                           /* give the harness a moment */
    f->r0 = (f->r0 & ~0xFFu) | 1;            /* retry */
}

static void role_critretry(void)
{
    d21(0x2524, 0, 0, (uint32_t)retry24);
    int cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\README.TXT");
    t_log("T:RETRYOK cf=%d retries>0=%d\n", cf, rcount > 0);
}

static void role_critwp(void)
{
    d21(0x2524, 0, 0, (uint32_t)my24);
    canswer = 3;
    int cf = d21(0x3C00, 0, 0, (uint32_t)"A:\\NEW.TXT");
    t_log("T:WP cf=%d ax=%u count=%d ah=%02x al=%d di=%d\n", cf, AXV, ccount, cah, cal, cdi);
    /* reading still works */
    cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\README.TXT");
    int h = AXV;
    char b[32] = { 0 };
    d21(0x3F00, h, 20, (uint32_t)b);
    t_log("T:WP read cf=%d [%s]\n", cf, b);
}

static void role_cooked(void)
{
    char b[200];
    /* cooked: a whole line with CR LF, editing applies */
    d21(0x3F00, 0, 100, (uint32_t)b);
    int n = AXV;
    b[n] = 0;
    for (int i = 0; i < n; i++) if (b[i] == '\r') b[i] = 'R'; else if (b[i] == '\n') b[i] = 'N';
    t_log("T:COOKED %d [%s]\n", n, b);
    /* raw: set bit 5 on CON, read 3 bytes without waiting for Enter */
    d21(0x4400, 0, 0, 0);
    unsigned info = R.r3 & 0xFF;
    d21(0x4401, 0, 0, info | 0x20);
    d21(0x3F00, 0, 3, (uint32_t)b);
    n = AXV;
    b[n] = 0;
    t_log("T:RAW %d [%s]\n", n, b);
    d21(0x4401, 0, 0, info);
}

static void role_stdin(void)
{
    static char b[4096];
    unsigned n = 0;
    for (;;) {
        d21(0x3F00, 0, 100, (uint32_t)(b + n));
        if ((R.cpsr & ARM_CPSR_C) || !AXV) break;
        n += AXV;
        if (n > 3900) break;
    }
    b[n] = 0;
    for (unsigned i = 0; i < n; i++) if (b[i] == '\r' || b[i] == '\n') b[i] = '|';
    t_log("T:STDIN %u [%s]\n", n, b);
}

static void role_stdin01(void)
{
    char b[200];
    int n = 0;
    for (;;) {
        d21(0x0100, 0, 0, 0);
        int c = R.r0 & 0xFF;
        if (c == 0x1A || n > 150) break;
        b[n++] = c == '\r' || c == '\n' ? '|' : c;
    }
    b[n] = 0;
    t_log("T:STDIN01 [%s]\n", b);
}

int main(int argc, char **argv)
{
    if (argc < 2) return 100;
    t_log("T:ROLE %s\n", argv[1]);
    if (!strcmp(argv[1], "LINE")) role_line();
    else if (!strcmp(argv[1], "CHARS")) role_chars();
    else if (!strcmp(argv[1], "CTRLC")) role_ctrlc();
    else if (!strcmp(argv[1], "HANDLER")) role_handler();
    else if (!strcmp(argv[1], "EXIT23")) { d21(0x2523, 0, 0, (uint32_t)exit23); d21(0x0100, 0, 0, 0); t_log("T:NOT REACHED\n"); }
    else if (!strcmp(argv[1], "BREAK")) role_break();
    else if (!strcmp(argv[1], "CRIT")) role_crit();
    else if (!strcmp(argv[1], "CRITWP")) role_critwp();
    else if (!strcmp(argv[1], "COOKED")) role_cooked();
    else if (!strcmp(argv[1], "STDIN")) role_stdin();
    else if (!strcmp(argv[1], "CRITRETRY")) role_critretry();
    else if (!strcmp(argv[1], "PRN")) {
        d21(0x0500, 0, 0, 'P');
        d21(0x4000, 4, 6, (uint32_t)"rinter");
        FILE *f = fopen("PRN", "w");
        if (f) { fprintf(f, " via fopen\n"); fclose(f); }
        /* 6Ch with no-INT-24h on the empty A: fails quietly */
        memset(&R, 0, sizeof R);
        R.r0 = 0x6C00; R.r1 = 0x2000; R.r3 = 0x01; R.r4 = (uint32_t)"A:\\X.TXT";
        int cf = _armdos_int21(&R);
        t_log("T:PRN done ext-open cf=%d ax=%u\n", cf, (unsigned)(R.r0 & 0xFFFF));
    }
    else if (!strcmp(argv[1], "SPEW")) {
        char line[40];
        for (int i = 0; i < 3000; i++) {
            sprintf(line, "line %04d of the spew$", i);
            d21(0x0900, 0, 0, (uint32_t)line);
            d21(0x0900, 0, 0, (uint32_t)"\r\n$");
            if (i % 100 == 0) t_log("T:SPEW %d\n", i);
        }
        t_log("T:SPEW done\n");
    }
    else if (!strcmp(argv[1], "BREAKQ")) { d21(0x3300, 0, 0, 0); t_log("T:BREAKQ %d\n", (int)(R.r3 & 0xFF)); }
    else if (!strcmp(argv[1], "STDIN01")) role_stdin01();
    return 0;
}
