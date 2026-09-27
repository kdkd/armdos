/* k_exec.c - EXEC nesting, exit codes, environments, TSR, AL=01/03, INT 20h,
   program faults.  Run as "K_EXEC" (the parent); it runs copies of itself
   with a role in argv[1]. */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)

static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{
    memset(&R, 0, sizeof R);
    R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx;
    return _armdos_int21(&R);
}

struct execpb { uint16_t env; uint32_t tail, fcb1, fcb2, sp, pc; } __attribute__((packed));
static struct execpb pb;
static uint8_t tailbuf[130];

/* EXEC; returns AX of 4Dh (type<<8 | code) or -error */
static int run(const char *prog, const char *args, uint16_t env, int al)
{
    int n = strlen(args);
    tailbuf[0] = n;
    memcpy(tailbuf + 1, args, n);
    tailbuf[n + 1] = '\r';
    pb.env = env;
    pb.tail = (uint32_t)tailbuf;
    pb.fcb1 = pb.fcb2 = 0;
    memset(&R, 0, sizeof R);
    R.r0 = 0x4B00 | al;
    R.r1 = (uint32_t)&pb;
    R.r3 = (uint32_t)prog;
    if (_armdos_int21(&R)) return -(int)AXV;
    if (al) return 0;
    d21(0x4D00, 0, 0, 0);
    return AXV;
}

static unsigned largest(void)
{
    d21(0x4800, 0xFFFF, 0, 0);
    return R.r1 & 0xFFFF;
}

/* ------------------------------------------------------------ roles */

static int role_child(int argc, char **argv)
{
    /* child: runs the grandchild, returns 40 + its code */
    int r = run("\\T\\K_EXEC.EXE", " GRAND 7", 0, 0);
    if (r < 0) return 99;
    struct psp *p = _armdos_psp;
    struct psp *parent = (struct psp *)((uint32_t)p->parent << 4);
    (void)argc; (void)argv;
    if (parent->int20 != 0xDF20) return 98;
    return 40 + (r & 0xFF);
}

static int role_env(void)
{
    const char *v = getenv("FOO");
    if (!v || strcmp(v, "BAR")) return 1;
    if (getenv("COMSPEC")) return 2;             /* only what we passed */
    return 0;
}

static int role_tsr(void)
{
    /* keep 64 paragraphs and go resident */
    d21(0x3100 | 5, 0, 0, 64);
    return 77;                                   /* not reached */
}

static int role_int20(void)
{
    __asm__ volatile("svc #0x20");
    return 55;
}

static int role_int27(void)
{
    /* keep up to the end of our image+bss+stack: DX = a flat pointer */
    register uint32_t r3 __asm__("r3") = (uint32_t)_armdos_heap_start;
    __asm__ volatile("svc #0x27" : "+r"(r3) :: "r0", "r1", "r2", "r12", "lr", "memory", "cc");
    return 44;
}

static int role_trash(void)
{
    /* wreck an MCB, then exit: DOS finds the arena broken and stops */
    d21(0x4800, 16, 0, 0);
    unsigned seg = AXV;
    *(volatile char *)((seg - 1) << 4) = 'X';
    return 0;
}

static int role_crash(const char *how)
{
    volatile int zero = 0, x = 5;
    if (!strcmp(how, "DIV")) x = x / zero;
    else if (!strcmp(how, "UNDEF")) __asm__ volatile(".word 0xE7F000F0");
    else if (!strcmp(how, "ABORT")) x = *(volatile int *)0x20000000;
    else if (!strcmp(how, "PABORT")) ((void (*)(void))0x30000000)();
    printf("NOT TERMINATED %d\n", x);
    return 66;
}

/* ------------------------------------------------------------ parent */

static void test_nesting(void)
{
    unsigned before = largest();
    int r = run("\\T\\K_EXEC.EXE", " CHILD", 0, 0);
    T_EQ(r, 47);                                 /* 40 + grandchild's 7, type 0 */
    T_EQ(largest(), before);                     /* everything freed */
    /* get-return-code is read once */
    d21(0x4D00, 0, 0, 0);
    T_EQ(AXV, 0);
    /* not found / bad format */
    T_EQ(run("\\T\\NOSUCH.EXE", "", 0, 0), -2);
    T_EQ(run("\\NODIR\\X.EXE", "", 0, 0), -3);
}

static void test_env(void)
{
    static const char env[] = "FOO=BAR\0PATH=C:\\\0";
    d21(0x4800, 2, 0, 0);
    unsigned seg = AXV;
    memcpy((void *)(seg << 4), env, sizeof env);
    T_EQ(run("\\T\\K_EXEC.EXE", " ENV", seg, 0), 0);
    memset(&R, 0, sizeof R); R.r0 = 0x4900; R.r8 = seg;
    T_EQ(_armdos_int21(&R), 0);
    /* our own environment: COMSPEC, then word 1 and our path */
    const char *e = (const char *)((uint32_t)_armdos_psp->envseg << 4);
    int n = 0;
    while (*e) { e += strlen(e) + 1; n++; }
    e++;
    T_EQ(e[0] | (e[1] << 8), 1);
    T_CHECK(!strcmp(e + 2, "C:\\T\\K_EXEC.EXE"), "program path %s", e + 2);
    /* our MCB name */
    struct mcb *m = (struct mcb *)(((uint32_t)((uint32_t)_armdos_psp >> 4) - 1) << 4);
    T_CHECK(!memcmp(m->name, "K_EXEC\0", 7), "MCB name %.8s", m->name);
    T_EQ(m->owner, (uint32_t)_armdos_psp >> 4);
}

static void test_tsr(void)
{
    unsigned before = largest();
    int r = run("\\T\\K_EXEC.EXE", " TSR", 0, 0);
    T_EQ(r, 0x0305);                             /* type 3, code 5 */
    unsigned after = largest();
    T_CHECK(after < before, "TSR kept memory: %u -> %u", before, after);
    /* find the resident block: 64 paragraphs owned by a PSP that is not ours */
    struct armregs l;
    memset(&l, 0, sizeof l); l.r0 = 0x5200;
    _armdos_int21(&l);
    uint16_t seg = *(uint16_t *)(l.r1 - 2);
    int found = 0;
    for (int guard = 0; guard < 1000; guard++) {
        struct mcb *m = (struct mcb *)((uint32_t)seg << 4);
        if (m->owner == seg + 1 && m->size == 64 && !memcmp(m->name, "K_EXEC", 6)) found = 1;
        if (m->type == 'Z') break;
        if (m->type != 'M') { T_CHECK(0, "arena broken at %x", seg); break; }
        seg += m->size + 1;
    }
    T_EQ(found, 1);
    before = largest();
    T_EQ(run("\\T\\K_EXEC.EXE", " INT27", 0, 0), 0x0300);
    T_CHECK(largest() < before, "INT 27h kept memory");
}

static void test_int20_and_com(void)
{
    T_EQ(run("\\T\\K_EXEC.EXE", " INT20", 0, 0), 0);
    T_EQ(run("\\T\\K_RET.COM", "", 0, 0), 0);     /* bx lr -> PSP:0 -> INT 20h */
    T_EQ(run("\\T\\K_CALL5.COM", "", 0, 0), 0x21);/* INT 21h through PSP:50h, then 4Ch */
}

static void test_load_only(void)
{
    unsigned before = largest();
    d21(0x6200, 0, 0, 0);
    unsigned me = R.r1 & 0xFFFF;
    int r = run("\\T\\K_HELLO.EXE", " X", 0, 1);
    T_EQ(r, 0);
    d21(0x6200, 0, 0, 0);
    unsigned child = R.r1 & 0xFFFF;
    T_CHECK(child != me, "AL=01 makes the child current");
    T_CHECK(pb.pc >= (child << 4) + 0x100 && pb.sp > pb.pc, "entry pc %lx sp %lx", (unsigned long)pb.pc, (unsigned long)pb.sp);
    struct psp *cp = (struct psp *)(child << 4);
    T_EQ(cp->parent, me);
    T_EQ(cp->cmdtail[0], 2);
    d21(0x5000, me, 0, 0);
    d21(0x6200, 0, 0, 0);
    T_EQ(R.r1 & 0xFFFF, me);
    /* free the child's blocks */
    unsigned env = cp->envseg;
    memset(&R, 0, sizeof R); R.r0 = 0x4900; R.r8 = child;
    _armdos_int21(&R);
    memset(&R, 0, sizeof R); R.r0 = 0x4900; R.r8 = env;
    _armdos_int21(&R);
    T_EQ(largest(), before);
}

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void test_overlay(void)
{
    /* The file's image bytes to compare with (the image starts at the AR1
     * image offset), read first: the overlay block below is allocated right
     * after our own, so from then on our heap cannot grow (no XMS here), and
     * fopen/fread need heap (the FILE glue, the buffer). Reading afterwards
     * only worked while the heap's last 4 KB step happened to leave ~1.5 KB
     * spare - a matter of the program's size: with Arm's smaller newlib it
     * left 88 bytes, fopen returned NULL and newlib crashed on the NULL FILE. */
    static uint8_t hdr[0x100], img[64];
    FILE *f = fopen("\\T\\K_HELLO.EXE", "rb");
    T_CHECK(f != 0, "fopen K_HELLO.EXE");
    if (!f) return;
    fread(hdr, 1, sizeof hdr, f);
    uint32_t lfa = rd32(hdr + 0x3C);            /* (byte arrays: no alignment) */
    uint32_t ioff = rd32(hdr + lfa + 8);
    fseek(f, ioff, SEEK_SET);
    fread(img, 1, 64, f);
    fclose(f);
    /* load K_HELLO.EXE's image as an overlay at a block of ours */
    d21(0x4800, 0x1000, 0, 0);
    unsigned seg = AXV;
    struct { uint16_t seg, reloc; } ov = { seg, seg };
    memset(&R, 0, sizeof R);
    R.r0 = 0x4B03; R.r1 = (uint32_t)&ov; R.r3 = (uint32_t)"\\T\\K_HELLO.EXE";
    T_EQ(_armdos_int21(&R), 0);
    T_CHECK(!memcmp(img, (void *)(seg << 4), 16), "overlay bytes");
    memset(&R, 0, sizeof R); R.r0 = 0x4900; R.r8 = seg;
    _armdos_int21(&R);
}

static void test_crashes(void)
{
    static const char *const how[] = { " DIV", " UNDEF", " ABORT", " PABORT" };
    unsigned before = largest();
    for (int i = 0; i < 4; i++) {
        char a[16];
        sprintf(a, " CRASH%s", how[i]);
        int r = run("\\T\\K_EXEC.EXE", a, 0, 0);
        T_CHECK(r == 0x0100, "crash %s -> %x", how[i], r);
    }
    T_EQ(largest(), before);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "CHILD")) return role_child(argc, argv);
        if (!strcmp(argv[1], "GRAND")) return atoi(argv[2]);
        if (!strcmp(argv[1], "ENV")) return role_env();
        if (!strcmp(argv[1], "TSR")) return role_tsr();
        if (!strcmp(argv[1], "INT20")) return role_int20();
        if (!strcmp(argv[1], "CRASH")) return role_crash(argv[2]);
        if (!strcmp(argv[1], "INT27")) return role_int27();
        if (!strcmp(argv[1], "TRASH")) return role_trash();
        return 100;
    }
    t_begin("exec");
    test_nesting();
    test_env();
    test_int20_and_com();
    test_load_only();
    test_overlay();
    test_crashes();
    test_tsr();
    return t_end();
}
