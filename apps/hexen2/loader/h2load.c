/*
 * h2load.c - the extender stub of H2.EXE (after apps/duke3d/loader/d3dload.c).
 *
 * H2DOS.EXE, uHexen2's DOS build of Hexen II, was a DJGPP program: a small
 * real-mode stub (CWSDSTUB) that started CWSDPMI and loaded the 32-bit
 * program appended to it into extended memory, where its 530 KB of code and
 * 2.2 MB of tables had room. H2.EXE on the ARM-PC does the same thing the
 * ARM-DOS way, exactly as DUKE3D.EXE's stub does:
 *
 *   H2.EXE = [this stub, an ordinary ARM-DOS EXE, ~15 KB]
 *          + [the game, an ARM-DOS EXE image of its own]
 *
 * The stub finds the game image after its own relocation table, allocates
 * an XMS block (HIMEM.SYS, INT 2Fh AX=4310h) for image + bss + stack, reads
 * and relocates the image there exactly as the DOS loader would (ARCH.md 8:
 * add the load base to every listed word, zero the bss) and enters it with
 * the EXE entry registers: r0 = our PSP (command tail, environment and file
 * handles are the program's), r1 = load base, r2 = block end = stack top,
 * sp = stack top. The game's C runtime then finds no DOS block to grow and
 * takes its heap from XMS; its exit() terminates the process (INT 21h
 * AH=4Ch) as usual. The XMS handle of the image block is left in the PSP's
 * reserved bytes 58h-5Bh ("DX" + handle) so the game gives it back at exit
 * (sys_armdos.c).
 *
 * Only conventional memory used while the game runs: this stub's 15 KB.
 *
 * Copyright (C) 2026 the ARM-DOS project. GPL-2 or later.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <unistd.h>
#include <armdos.h>

extern const char *_armdos_progpath;
extern struct psp *_armdos_psp;

static void *xms_entry;

static unsigned xms(unsigned ah, unsigned dx, struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r0 = ah << 8;
    r->r3 = dx;
    return _armdos_farcall(xms_entry, r) & 0xFFFF;
}

static int fail(const char *msg)
{
    printf("%s\n", msg);
    return 1;
}

static int readat(int fd, long off, void *buf, unsigned long n)
{
    unsigned char *p = buf;

    if (lseek(fd, off, SEEK_SET) != off)
        return -1;
    while (n)
    {
        unsigned chunk = n > 32768 ? 32768 : (unsigned)n;
        int got = read(fd, p, chunk);
        if (got <= 0)
            return -1;
        p += got;
        n -= got;
    }
    return 0;
}

/* The AR1 header of the EXE that starts at file offset `at`. */
static int ar1_header(int fd, long at, struct armexe *h, long *ar1_at)
{
    unsigned char mz[64];
    uint32_t lfanew;

    if (readat(fd, at, mz, sizeof mz) || mz[0] != 'M' || mz[1] != 'Z')
        return -1;
    memcpy(&lfanew, mz + 0x3C, 4);
    *ar1_at = at + lfanew;
    if (readat(fd, *ar1_at, h, sizeof *h) || memcmp(h->sig, "AR1", 4))
        return -1;
    return 0;
}

int main(int argc, char **argv)
{
    struct armexe self, game;
    struct armregs r;
    long self_ar1, game_at, game_ar1, end;
    unsigned long need, kb, i;
    uint32_t addr, base, stacktop;
    uint16_t handle;
    static uint32_t relocs[1024];     /* static: the stub's heap never needs to grow */
    int fd;

    fd = open(_armdos_progpath, O_RDONLY | O_BINARY);
    if (fd < 0)
        return fail("Can't open H2.EXE.");
    if (ar1_header(fd, 0, &self, &self_ar1))
        return fail("Bad game image.");
    end = self.image_off + self.image_size;
    if (self.reloc_off + self.reloc_count * 4 > (uint32_t)end)
        end = self.reloc_off + self.reloc_count * 4;
    game_at = (end + 15) & ~15L;
    if (ar1_header(fd, game_at, &game, &game_ar1))
        return fail("Bad game image.");

    /* image + bss + stack, 16-byte aligned, in one XMS block */
    need = game.image_size + game.bss_size + game.stack_size + 64;
    kb = (need + 1023) / 1024;
    xms_entry = armdos_xms_entry();
    if (!xms_entry)
        return fail("This program requires an extended memory manager (HIMEM.SYS).");
    xms(0x08, 0, &r);                     /* AX = largest free block, KB */
    if ((r.r0 & 0xFFFF) < kb)
    {
        printf("Not enough extended memory: %lu KB needed, %lu KB free.\n",
               kb, (unsigned long)(r.r0 & 0xFFFF));
        return 1;
    }
    if (xms(0x09, kb, &r) != 1)
        return fail("Extended memory error.");
    handle = r.r3 & 0xFFFF;
    if (xms(0x0C, handle, &r) != 1)
    {
        xms(0x0A, handle, &r);
        return fail("Extended memory error.");
    }
    addr = ((r.r3 & 0xFFFF) << 16) | (r.r1 & 0xFFFF);
    base = (addr + 15) & ~15u;

    /* load, relocate, clear the bss */
    if ( readat(fd, game_at + game.image_off, (void *)base, game.image_size))
        goto ioerr;
    for (i = 0; i < game.reloc_count; )
    {
        unsigned long n = game.reloc_count - i, k;
        if (n > 1024) n = 1024;
        if (readat(fd, game_at + game.reloc_off + i * 4, relocs, n * 4))
            goto ioerr;
        for (k = 0; k < n; k++)
            if (relocs[k] + 4 <= game.image_size)
                *(uint32_t *)(base + relocs[k]) += base;
        i += n;
    }
    close(fd);
    memset((void *)(base + game.image_size), 0, game.bss_size);
    stacktop = (base + game.image_size + game.bss_size + game.stack_size) & ~7u;

    /* our PSP's reserved bytes 58h-5Bh: "DX" + the handle, for the game */
    {
        uint8_t *p = (uint8_t *)_armdos_psp;
        p[0x58] = 'D';
        p[0x59] = 'X';
        p[0x5A] = handle & 0xFF;
        p[0x5B] = handle >> 8;
    }

    /* for the debugger / profiler (port E9h, not on the screen) */
    {
        char msg[48];
        sprintf(msg, "H2LOAD: image at %08lX\n", (unsigned long)base);
        armdos_debug(msg);
    }
    fflush(stdout);
    {
        register uint32_t r0 __asm__("r0") = (uint32_t)_armdos_psp;
        register uint32_t r1 __asm__("r1") = base;
        register uint32_t r2 __asm__("r2") = stacktop;
        register uint32_t r3 __asm__("r3") = base + game.entry;
        __asm__ volatile(
            "mov  sp, r2\n\t"
            "mov  lr, #0\n\t"
            "bx   r3\n\t"
            : : "r"(r0), "r"(r1), "r"(r2), "r"(r3) : "memory");
    }
    for (;;) ;

ioerr:
    xms(0x0D, handle, &r);
    xms(0x0A, handle, &r);
    return fail("Error reading the game image.");
}
