/*
 * CRASH.EXE - a guided tour of ARM exceptions under ARM-DOS.
 *
 *   CRASH              menu
 *   CRASH n | name     go straight to one (1 UNDEF, 2 DATA, 3 PREFETCH,
 *                      4 DIVIDE, 5 STACK, 6 ALIGN, 7 VECTOR)
 *
 * Each choice prints a short explanation (through DOS, so it stays on the
 * screen) and then does the deed; ARM-DOS reports the exception, ends the
 * program and COMMAND.COM carries on - as DOS did with "Divide overflow".
 * Only the last one, a stray write into the interrupt vector table, takes
 * the whole machine down (the BIOS crash screen), and it asks first.
 *
 * Copyright (C) 1989 Europa Micro Systems (ARM-DOS project).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "armdos.h"

void do_undefined(void);
void do_data_abort(void);
void do_prefetch_abort(void);
uint32_t do_unaligned(const void *p);
uint32_t get_sp(void);
void do_vector_smash(void);
extern char __bss_end__[];

struct item {
    const char *key, *name, *tag;
    const char *text[6];            /* the explanation, 72 columns */
};

static const struct item items[] = {
    { "UNDEF", "Undefined instruction", "INT 06h",
      { "Executes E7F198F8h, an encoding the ARM architecture keeps undefined",
        "for ever (later assemblers call it UDF). No ARM and no coprocessor will",
        "run it, so the CPU takes the undefined instruction exception (04h).",
        "The BIOS turns it into INT 06h, like the 80286's invalid opcode;",
        "ARM-DOS names the culprit, dumps the registers and ends the program.", 0 } },
    { "DATA", "Data abort (bad load)", "INT 0Dh",
      { "Loads a word from address 20000000h. RAM ends at 16 MB and the ISA",
        "I/O window starts at 10000000h: nothing answers at 20000000h, so the",
        "bus aborts the access (data abort, vector 10h). ARM-DOS reports it as",
        "INT 0Dh - the 286's general protection fault - with the address from",
        "the CP15 fault address register.", 0 } },
    { "PREFETCH", "Prefetch abort (bad jump)", "INT 0Eh",
      { "Jumps (BLX) to address 30000000h. The CPU cannot fetch an instruction",
        "there: prefetch abort (vector 0Ch), INT 0Eh. PC shows the bad address,",
        "LR shows where the jump came from - the usual way to find the bug.", 0 } },
    { "DIVIDE", "Divide by zero", "INT 00h",
      { "The ARM926 has no divide instruction: C's '/' calls the compiler's",
        "division routine, which checks for zero and calls __aeabi_idiv0.",
        "The ARM-DOS C library makes that an INT 00h, exactly the interrupt an",
        "8086 raises for DIV by zero - and DOS answers as it always has.", 0 } },
    { "STACK", "Stack overflow", "R6000",
      { "Recursion without end. There is no MMU and no guard page: the stack",
        "would run down over the program's own data. So, like Microsoft C with",
        "stack checking on, every call compares SP with the end of the stack",
        "area and stops the program with run-time error R6000.", 0 } },
    { "ALIGN", "Unaligned load (no crash!)", "none",
      { "LDR from an address that is not a multiple of 4. An 8086 would just",
        "take two bus cycles. The ARMv5 loads the aligned word and ROTATES it",
        "so the addressed byte lands in bits 0-7 - no exception, a quietly",
        "surprising value. (Portable code uses bytes or memcpy.)", 0 } },
    { "VECTOR", "Stray write to the vector table", "fatal",
      { "Writes DEADBEE0h into the INT 21h vector at address 00000084h, then",
        "calls DOS. The BIOS dispatcher jumps there in SVC mode, inside the",
        "system: prefetch abort in the kernel, and the BIOS crash screen.",
        "Real mode DOS had exactly this weakness. You will need Ctrl+Alt+Del.", 0 } },
};
#define NITEMS (int)(sizeof items / sizeof items[0])

/* ------------------------------------------------------------ the deeds */

static void say(const char *s) { fputs(s, stdout); fflush(stdout); }

static void explain(int i)
{
    printf("\nCRASH: %s (%s)\n\n", items[i].name, items[i].tag);
    for (int k = 0; items[i].text[k]; k++) printf("  %s\n", items[i].text[k]);
    printf("\n");
    fflush(stdout);
}

static uint32_t stack_floor;
static unsigned depth;

static void r6000(void)
{
    /* Microsoft C's message, word for word */
    say("\nrun-time error R6000\n- stack overflow\n");
    exit(255);
}

__attribute__((noinline)) static unsigned recurse(unsigned n)
{
    volatile char frame[64];
    if (get_sp() < stack_floor + 1024) r6000();      /* the __chkstk probe */
    frame[0] = n;
    depth = n;
    if (!(n % 20)) printf("  depth %3u, SP = %08lX\n", n, (unsigned long)get_sp());
    return recurse(n + 1) + frame[0];
}

static int ask_yes(void)
{
    say("Continue (Y/N)? ");
    for (;;) {
        struct armregs r = { 0 };
        r.r0 = 0x0800; _armdos_int21(&r);
        int c = r.r0 & 0xFF;
        if (c == 'y' || c == 'Y') { say("Y\n"); return 1; }
        if (c == 'n' || c == 'N' || c == 27 || c == 3) { say("N\n"); return 0; }
    }
}

static int run(int i)
{
    explain(i);
    switch (i) {
    case 0: do_undefined(); break;
    case 1: do_data_abort(); break;
    case 2: do_prefetch_abort(); break;
    case 3: {
        volatile int a = 1988, b = 0;
        printf("  1988 / 0 = %d\n", a / b);
        break;
    }
    case 4:
        stack_floor = (uint32_t)__bss_end__;
        printf("  stack from %08lX down to %08lX\n", (unsigned long)get_sp(), (unsigned long)stack_floor);
        recurse(1);
        break;
    case 5: {
        static const uint8_t bytes[8] __attribute__((aligned(4))) = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
        printf("  memory at %08lX: 11 22 33 44 55 66 77 88\n\n", (unsigned long)bytes);
        for (int o = 0; o < 4; o++) {
            uint32_t v = do_unaligned(bytes + o);
            uint32_t want = bytes[o] | bytes[o + 1] << 8 | bytes[o + 2] << 16 | (uint32_t)bytes[o + 3] << 24;
            printf("  LDR r0,[%08lX]  ->  %08lX   (an 8086 would read %08lX)\n",
                   (unsigned long)(bytes + o), (unsigned long)v, (unsigned long)want);
        }
        printf("\n  No exception - the program just goes on with odd numbers.\n");
        return 0;
    }
    case 6:
        say("  WARNING: this stops the computer. Unsaved work in resident programs\n"
            "  will be lost. The machine must be restarted with Ctrl+Alt+Del.\n\n  ");
        if (!ask_yes()) return 0;
        do_vector_smash();
        break;
    }
    say("  ... and nothing happened?!\n");
    return 1;
}

/* ------------------------------------------------------------ the menu */

#define V ((volatile uint16_t *)(*(volatile uint8_t *)0x449 == 7 ? 0xB0000 : 0xB8000))   /* mode 7: Hercules/MDA */

static void put(int x, int y, const char *s, uint8_t a)
{
    while (*s && x < 80) V[y * 80 + x++] = (uint8_t)*s++ | (a << 8);
}
static void fill(int x, int y, int w, int h, uint8_t c, uint8_t a)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) V[(y + j) * 80 + x + i] = c | (a << 8);
}
static void frame(int x, int y, int w, int h, uint8_t a)
{
    fill(x, y, w, h, ' ', a);
    for (int i = 1; i < w - 1; i++) { V[y * 80 + x + i] = 0xC4 | (a << 8); V[(y + h - 1) * 80 + x + i] = 0xC4 | (a << 8); }
    for (int j = 1; j < h - 1; j++) { V[(y + j) * 80 + x] = 0xB3 | (a << 8); V[(y + j) * 80 + x + w - 1] = 0xB3 | (a << 8); }
    V[y * 80 + x] = 0xDA | (a << 8); V[y * 80 + x + w - 1] = 0xBF | (a << 8);
    V[(y + h - 1) * 80 + x] = 0xC0 | (a << 8); V[(y + h - 1) * 80 + x + w - 1] = 0xD9 | (a << 8);
}

static int getkey(void)
{
    struct armregs r = { 0 };
    _armdos_int16(&r);
    return r.r0 & 0xFFFF;
}

static void bios_cursor(int hide)
{
    struct armregs r = { 0 };
    r.r0 = 0x0100; r.r2 = hide ? 0x2000 : 0x0607;
    _armdos_int10(&r);
}

static void cls(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0600; r.r1 = 0x0700; r.r2 = 0; r.r3 = 0x184F;
    _armdos_int10(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0200; r.r3 = 0;
    _armdos_int10(&r);
}

static void draw(int sel)
{
    fill(0, 0, 80, 25, ' ', 0x17);
    fill(0, 0, 80, 1, ' ', 0x4F);
    put(1, 0, "CRASH", 0x4E);
    put(7, 0, "\xB3 A Tour of ARM Exceptions", 0x4F);
    put(52, 0, "Europa Micro Systems 1989", 0x4F);
    frame(3, 2, 74, NITEMS + 4, 0x1F);
    put(5, 2, " Choose your disaster ", 0x1E);
    put(52, 3, "Reported as", 0x1B);
    for (int i = 0; i < NITEMS; i++) {
        char b[80];
        int y = 4 + i;
        uint8_t a = i == sel ? 0x70 : 0x1F;
        fill(4, y, 72, 1, ' ', a);
        snprintf(b, sizeof b, " %d ", i + 1);
        put(5, y, b, i == sel ? 0x74 : 0x1E);
        put(9, y, items[i].name, a);
        const char *tag = items[i].tag;
        uint8_t ta = i == sel ? 0x70 : (i == 6 ? 0x1C : i == 5 ? 0x1A : 0x1B);
        put(52, y, tag, ta);
        if (i == 6) put(60, y, "!! stops the PC", i == sel ? 0x74 : 0x1C);
    }
    int ty = 4 + NITEMS + 3;
    frame(3, ty - 1, 74, 8, 0x1F);
    put(5, ty - 1, " Explanation ", 0x1E);
    for (int k = 0; items[sel].text[k]; k++) put(5, ty + k, items[sel].text[k], 0x17);
    fill(0, 24, 80, 1, ' ', 0x30);
    put(1, 24, "\x18\x19", 0x34); put(4, 24, "Select", 0x30);
    put(12, 24, "Enter", 0x34); put(18, 24, "Crash it", 0x30);
    put(28, 24, "1-7", 0x34); put(32, 24, "Direct", 0x30);
    put(40, 24, "Esc", 0x34); put(44, 24, "Quit to DOS", 0x30);
}

static int menu(void)
{
    static int sel;
    bios_cursor(1);
    for (;;) {
        draw(sel);
        int k = getkey(), c = k & 0xFF, sc = k >> 8;
        if (c == 27) { cls(); bios_cursor(0); return -1; }
        if (sc == 0x48) sel = (sel + NITEMS - 1) % NITEMS;
        else if (sc == 0x50) sel = (sel + 1) % NITEMS;
        else if (c == 13) break;
        else if (c >= '1' && c < '1' + NITEMS) { sel = c - '1'; break; }
    }
    cls();
    bios_cursor(0);
    return sel;
}

static int lookup(const char *a)
{
    if (a[0] == '/' || a[0] == '-') a++;
    if (a[0] >= '1' && a[0] < '1' + NITEMS && !a[1]) return a[0] - '1';
    for (int i = 0; i < NITEMS; i++) {
        const char *k = items[i].key, *p = a;
        while (*k && (*p & ~0x20) == *k) k++, p++;
        if (!*k && !*p) return i;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        if (!strcmp(argv[1], "/?")) {
            say("CRASH - a tour of ARM exceptions\n\n"
                "CRASH [n | UNDEF | DATA | PREFETCH | DIVIDE | STACK | ALIGN | VECTOR]\n\n"
                "Without an argument CRASH shows a menu.\n");
            return 0;
        }
        int i = lookup(argv[1]);
        if (i < 0) { printf("Invalid parameter - %s\n", argv[1]); return 1; }
        return run(i);
    }
    int i = menu();
    if (i < 0) return 0;
    return run(i);
}
