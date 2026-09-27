/*
 * xload.c - the extended-memory loader stub of TC.EXE and QB.EXE.
 *
 * Borland's and Microsoft's big IDEs of 1990 swapped or overlaid themselves
 * to leave room for the program being developed. On the ARM-PC the natural
 * way is the one DUKE3D.EXE uses: the IDE is an ordinary ARM-DOS EXE image
 * appended to this small stub,
 *
 *     TC.EXE = [xload stub, an ARM-DOS EXE, ~8 KB] + [the IDE's EXE image]
 *
 * and the stub loads the image into an XMS block (HIMEM.SYS) and runs it
 * there. Only the stub stays in conventional memory, so a program compiled
 * and run from the IDE gets nearly all of the 640 KB.
 *
 * The stub finds the appended image after its own relocation table,
 * allocates one XMS block for image + bss + stack, reads and relocates the
 * image exactly as the DOS loader does (ARCH.md 8: add the load base to
 * every listed word, clear the bss) and enters it with the EXE entry
 * registers: r0 = our PSP (command tail, environment and handles are the
 * program's), r1 = load base, r2 = block end = stack top, sp = stack top.
 * The image's C runtime then finds no DOS block to grow and takes its heap
 * from XMS. The XMS handle of the image block is left in the PSP's reserved
 * bytes 58h-5Bh ("DX" + handle; the convention DUKE3D.EXE's loader
 * started), and ximage.c in the image frees it at exit.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <unistd.h>
#include <armdos.h>

extern const char *_armdos_progpath;
extern struct psp *_armdos_psp;

static void *xms_entry;
static uint32_t relocs[256];

static unsigned xms(unsigned ah, unsigned dx, struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r0 = ah << 8;
    r->r3 = dx;
    return _armdos_farcall(xms_entry, r) & 0xFFFF;
}

static int say(const char *msg)
{
    write(1, msg, strlen(msg));
    write(1, "\r\n", 2);
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
static int ar1_header(int fd, long at, struct armexe *h)
{
    unsigned char mz[64];
    uint32_t lfanew;

    if (readat(fd, at, mz, sizeof mz) || mz[0] != 'M' || mz[1] != 'Z')
        return -1;
    memcpy(&lfanew, mz + 0x3C, 4);
    if (readat(fd, at + lfanew, h, sizeof *h) || memcmp(h->sig, "AR1", 4))
        return -1;
    return 0;
}

static void utoa10(unsigned long v, char *out)
{
    char t[12];
    int n = 0;
    do t[n++] = '0' + v % 10; while (v /= 10);
    while (n) *out++ = t[--n];
    *out = 0;
}

int main(void)
{
    struct armexe self, img;
    struct armregs r;
    long img_at, end;
    unsigned long need, kb, i;
    uint32_t addr, base, stacktop;
    uint16_t handle;
    int fd;

    fd = open(_armdos_progpath, O_RDONLY | O_BINARY);
    if (fd < 0 || ar1_header(fd, 0, &self))
        return say("Cannot read the program file.");
    end = self.image_off + self.image_size;
    if (self.reloc_off + self.reloc_count * 4 > (uint32_t)end)
        end = self.reloc_off + self.reloc_count * 4;
    img_at = (end + 15) & ~15L;
    if (ar1_header(fd, img_at, &img))
        return say("Cannot read the program file.");

    need = img.image_size + img.bss_size + img.stack_size + 64;
    kb = (need + 1023) / 1024;
    xms_entry = armdos_xms_entry();
    if (!xms_entry)
        return say("This program requires an extended memory manager (HIMEM.SYS).");
    xms(0x08, 0, &r);                     /* AX = largest free block, KB */
    if ((r.r0 & 0xFFFF) < kb)
    {
        char msg[64], num[12];
        strcpy(msg, "Not enough extended memory: ");
        utoa10(kb, num);
        strcat(msg, num);
        strcat(msg, " KB needed.");
        return say(msg);
    }
    if (xms(0x09, kb, &r) != 1)
        return say("Extended memory error.");
    handle = r.r3 & 0xFFFF;
    if (xms(0x0C, handle, &r) != 1)
    {
        xms(0x0A, handle, &r);
        return say("Extended memory error.");
    }
    addr = ((r.r3 & 0xFFFF) << 16) | (r.r1 & 0xFFFF);
    base = (addr + 15) & ~15u;

    if (readat(fd, img_at + img.image_off, (void *)base, img.image_size))
        goto ioerr;
    for (i = 0; i < img.reloc_count; )
    {
        unsigned long n = img.reloc_count - i, k;
        if (n > 256) n = 256;
        if (readat(fd, img_at + img.reloc_off + i * 4, relocs, n * 4))
            goto ioerr;
        for (k = 0; k < n; k++)
            if (relocs[k] + 4 <= img.image_size)
                *(uint32_t *)(base + relocs[k]) += base;
        i += n;
    }
    close(fd);
    memset((void *)(base + img.image_size), 0, img.bss_size);
    stacktop = (base + img.image_size + img.bss_size + img.stack_size) & ~7u;

    {
        uint8_t *p = (uint8_t *)_armdos_psp;
        p[0x58] = 'D';
        p[0x59] = 'X';
        p[0x5A] = handle & 0xFF;
        p[0x5B] = handle >> 8;
    }
    {
        register uint32_t r0 __asm__("r0") = (uint32_t)_armdos_psp;
        register uint32_t r1 __asm__("r1") = base;
        register uint32_t r2 __asm__("r2") = stacktop;
        register uint32_t r3 __asm__("r3") = base + img.entry;
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
    return say("Error reading the program file.");
}
