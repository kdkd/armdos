/*
 * main.c - ELBOW.EXE ("Emulated Legacy Binaries On Workstation"), the
 * ARM-DOS 8086 Compatibility Box.
 *
 *   ELBOW [/JIT | /NOJIT] [/STATS] [/MEM:n] program[.COM|.EXE] [arguments]
 *
 * Runs a genuine x86 real-mode DOS program on the ARM PC: an 80386-class
 * real-mode CPU (interpreter, plus an x86-to-ARM translator with /JIT), a
 * DOS personality that turns the program's INT 21h calls into ARM-DOS calls
 * (so files, handles, redirection and devices are the real ones), the BIOS
 * services, the real text/graphics memory at B800h/A000h, port I/O and the
 * program's own interrupt hooks.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <armdos.h>
#include "x86.h"
#include "dos86.h"
#include "jit.h"

/* the heap: 1 MB + 64 KB of x86 memory and the translation cache, from XMS */
unsigned _armdos_xms_kb = 4608 + 2560 + 192;      /* + the EMS pool (ems.c), allocated on first use */
unsigned _armdos_raw_extmem = 1;

int opt_trace;
uint32_t watch_req;
extern void x86_t22(void);
extern uint32_t x86_orig22;
extern void dosdata_init(void);
extern void x86_emergency_cleanup(void);

static const char usage[] =
    "ELBOW - Emulated Legacy Binaries On Workstation, Version 1.00\r\n"
    "Runs a program written for the Intel 8086 family on the ARM PC.\r\n\r\n"
    "ELBOW [/JIT | /NOJIT] [/STATS] [/MEM:n] [/NOEMS] [drive:][path]program [parameters]\r\n\r\n"
    "  /JIT     translate x86 code into ARM code as it runs (default)\r\n"
    "  /NOJIT   interpret every x86 instruction\r\n"
    "  /STATS   report instructions executed and speed when the program ends\r\n"
    "  /MEM:n   give the program n KB of conventional memory (64-640)\r\n"
    "  /NOEMS   no expanded memory (EMS) for the program\r\n\r\n"
    "8086, 80186, 80286 and 80386 real-mode programs are supported, with\r\n"
    "2.5 MB of LIM EMS 4.0 expanded memory.  Protected mode, XMS and a math\r\n"
    "coprocessor are not.  Your old programs, now with a little ELBOW grease.\r\n";

static int exists(const char *p)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4300; r.r3 = (uint32_t)p;
    if (_armdos_int21(&r)) return 0;
    return !(r.r2 & 0x18);
}

static int try_ext(char *out, const char *base)
{
    strcpy(out, base);
    const char *s = strrchr(base, '\\'), *d = strrchr(base, '.');
    if (d && (!s || d > s)) return exists(out);
    static const char *ext[] = { ".COM", ".EXE" };
    for (int i = 0; i < 2; i++) {
        strcpy(out, base);
        strcat(out, ext[i]);
        if (exists(out)) return 1;
    }
    return 0;
}

static int resolve(char *out, const char *name)
{
    if (try_ext(out, name)) return 1;
    if (strchr(name, '\\') || strchr(name, ':')) return 0;
    const char *path = getenv("PATH");
    while (path && *path) {
        char dir[80], cand[128];
        int n = 0;
        while (*path && *path != ';' && n < 79) dir[n++] = *path++;
        dir[n] = 0;
        if (*path == ';') path++;
        if (!n) continue;
        snprintf(cand, sizeof cand, "%s%s%s", dir, dir[n - 1] == '\\' ? "" : "\\", name);
        if (try_ext(out, cand)) return 1;
    }
    return 0;
}

static int trace_at_set(void) { extern long trace_at; extern int seq_at; return trace_at != 0 && !seq_at; }
static unsigned long ticks(void) { return *(volatile uint32_t *)0x46C; }
static unsigned long arm_minsn(void)
{
    uint32_t v = armdos_inb(0xF8);
    v |= armdos_inb(0xF9) << 8; v |= armdos_inb(0xFA) << 16; v |= (uint32_t)armdos_inb(0xFB) << 24;
    return v;
}

int main(void)
{
    const uint8_t *ct = _armdos_psp->cmdtail;
    char tail[130];
    int tl = ct[0] > 126 ? 126 : ct[0];
    memcpy(tail, ct + 1, tl);
    tail[tl] = 0;

    int stats = 0, want_jit = 1, mem_given = 0;
    char *p = tail;
    for (;;) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '/') break;
        char opt[16];
        int n = 0;
        p++;
        while (*p && *p != ' ' && *p != '\t' && *p != '/' && n < 15) {
            char c = *p++;
            if (c >= 'a' && c <= 'z') c -= 32;
            opt[n++] = c;
        }
        opt[n] = 0;
        if (!strcmp(opt, "JIT")) want_jit = 1;
        else if (!strcmp(opt, "NOJIT")) want_jit = 0;
        else if (!strcmp(opt, "STATS")) { extern int jit_count; stats = 1; jit_count = 1; }
        else if (!strcmp(opt, "TRACE")) opt_trace = 1;
        else if (!strncmp(opt, "JITOFF:", 7)) { extern unsigned jit_off; jit_off = atoi(opt + 7); }
        else if (!strcmp(opt, "FARLOG")) { extern int farlog_on; farlog_on = 1; }
        else if (!strcmp(opt, "NOEMS")) { extern int ems_enabled; ems_enabled = 0; }
        else if (!strcmp(opt, "TRACE2")) opt_trace = 2;
        else if (!strncmp(opt, "WATCH:", 6)) { extern uint32_t watch_req; watch_req = strtoul(opt + 6, 0, 16); }
        else if (!strcmp(opt, "JITDUMP")) { extern int jit_dump; jit_dump = 1; }
        else if (!strcmp(opt, "JITINVAL")) { extern int jit_dump; jit_dump = 2; }
        else if (!strncmp(opt, "WEIP:", 5)) { extern uint32_t jit_watch_eip; jit_watch_eip = strtoul(opt + 5, 0, 16); }
        else if (!strcmp(opt, "SEQLOG")) { extern int jit_seqlog; jit_seqlog = 1; }
        else if (!strcmp(opt, "OPHIST")) { extern uint32_t *ophist; ophist = calloc(768, 4); }
        else if (!strcmp(opt, "RING")) { extern int ring_on; ring_on = 1; }
        else if (!strcmp(opt, "TRACEKB")) { extern int trace_kb; trace_kb = 1; }
        else if (!strncmp(opt, "TRACEAT:", 8)) { extern long trace_at; trace_at = atol(opt + 8); }
        else if (!strncmp(opt, "SEQAT:", 6)) { extern long trace_at; extern int seq_at; trace_at = atol(opt + 6); seq_at = 1; }
        else if (!strncmp(opt, "MEM:", 4) && atoi(opt + 4) >= 64 && atoi(opt + 4) <= 640) { arena_end = atoi(opt + 4) * 64; mem_given = 1; }
        else if (!strcmp(opt, "?")) { x86_msg(usage); return 0; }
        else { x86_msg("Invalid switch - /"); x86_msg(opt); x86_msg("\r\n"); return 1; }
    }
    if (!*p) { x86_msg(usage); return 0; }
    char name[128];
    int n = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != '/' && *p != ',' && *p != ';' && *p != '=' && n < 127) {
        char c = *p++;
        if (c >= 'a' && c <= 'z') c -= 32;
        name[n++] = c;
    }
    name[n] = 0;
    char path[128];
    if (!resolve(path, name)) { x86_msg("Bad command or file name\r\n"); return 1; }
    /* the full path, as DOS puts it after the environment */
    {
        struct armregs r;
        char full[128];
        memset(&r, 0, sizeof r);
        r.r0 = 0x6000; r.r4 = (uint32_t)path; r.r5 = (uint32_t)full;
        if (!_armdos_int21(&r)) strcpy(path, full);
    }

    /* known programs that need help (the way Windows keeps compatibility
       shims): MASM 1.10 / LINK 2.00 compare the free paragraphs signed and
       break with more than 512 KB free */
    if (!mem_given) {
        static const struct { const char *name; long size; uint16_t kb; } shim[] = {
            { "MASM.EXE", 77440, 512 }, { "LINK.EXE", 42368, 512 },
        };
        const char *b = strrchr(path, '\\');
        b = b ? b + 1 : path;
        FILE *f = fopen(path, "rb");
        long sz = -1;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
        for (unsigned i = 0; i < sizeof shim / sizeof shim[0]; i++)
            if (!strcmp(b, shim[i].name) && sz == shim[i].size) arena_end = shim[i].kb * 64;
    }

    /* 128 KB aligned, so that x86 linear address L is physical address
       base + L with the same low 16 (17) bits: 8237 DMA transfers the x86
       program programs need only their page registers moved (hle.c) */
    uint8_t *buf = malloc(X86_MEMSIZE + 0x20000);
    if (!buf) { x86_msg("Not enough memory\r\n"); return 8; }
    buf = (uint8_t *)(((uintptr_t)buf + 0x1FFFF) & ~(uintptr_t)0x1FFFF);
    memset(buf, 0, X86_MEMSIZE);
    cpu_reset();
    mem_init(buf);
    world_init();
    dosdata_init();
    { extern int ems_enabled; extern void ems_init(void); if (ems_enabled) ems_init(); }
    extern int jit_enabled;
    if (want_jit && jit_init() == 0) jit_enabled = 1;

    { extern uint32_t watch_req; extern void mem_watch(uint32_t); if (watch_req) mem_watch(watch_req); }
    int e = x86_load_top(path, p);
    if (e < 0) {
        char m[80];
        if (-e == 8) snprintf(m, sizeof m, "Not enough memory\r\n");
        else if (-e == 11) snprintf(m, sizeof m, "Bad format: %s\r\n", path);
        else snprintf(m, sizeof m, "Cannot load %s (error %d)\r\n", path, -e);
        x86_msg(m);
        return 1;
    }

    /* however we end, the ARM vectors come back (t22.S) */
    x86_orig22 = _armdos_psp->int22;
    _armdos_psp->int22 = (uint32_t)x86_t22;
    irq_install();
    elbow_desc_announce();                   /* (cleared by irq.c's cleanup, however we end) */
    { extern int ring_on; extern uint32_t *ophist; if (ring_on || ophist || opt_trace > 1 || trace_at_set()) pend_set(PEND_DEBUG); }

    unsigned long t0 = ticks(), a0 = arm_minsn();
    x86_active = 1;
    alt_led_set(1);
    while (!x86_exited) {
        cpu_run();
        cpu.stop = 0;
        x86_run_deferred();
    }
    x86_active = 0;
    alt_led_set(0);
    unsigned long t1 = ticks(), a1 = arm_minsn();
    irq_remove();

    { extern uint32_t *ophist;
      if (ophist) for (int k = 0; k < 20; k++) {
          int best = 0;
          for (int j = 1; j < 768; j++) if (ophist[j] > ophist[best]) best = j;
          if (!ophist[best]) break;
          char mm[48];
          snprintf(mm, sizeof mm, "%s%02X:%lu ", best >= 512 ? "p" : best >= 256 ? "0F " : "", best & 255, (unsigned long)ophist[best]); x86_msg(mm); ophist[best] = 0; } }
    if (stats) {
        char m[240], js[80];
        unsigned long dt = t1 - t0;
        unsigned long ms = dt * 10000UL / 182UL;
        unsigned long long ic = cpu.icount + (jit_enabled ? cpu.jit_icount : 0);
        unsigned long kips = ms ? (unsigned long)(ic / ms) : 0;
        jit_stats(js, sizeof js);
        unsigned long pct = ic ? (unsigned long)((ic - cpu.icount) * 1000 / ic) : 0;
        snprintf(m, sizeof m, "\r\nELBOW: %lu x86 instructions in %lu.%02lu s = %lu.%02lu MIPS; %lu M ARM instructions\r\n",
                 (unsigned long)ic, ms / 1000, (ms % 1000) / 10, kips / 1000, (kips % 1000) / 10, a1 - a0);
        x86_msg(m);
        if (jit_enabled) {
            snprintf(m, sizeof m, "       %lu.%lu%% translated (%s)\r\n", pct / 10, pct % 10, js);
            x86_msg(m);
        }
    }
    return exit_code;
}
