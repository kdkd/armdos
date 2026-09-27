/*
 * h2check.c - H2CHECK.EXE: is DATA1\PAK0.PAK the Hexen II data H2.EXE needs?
 *
 *   H2CHECK [path\PAK0.PAK] [/Q]
 *
 * Checks the file INSTALL.BAT names (default DATA1\PAK0.PAK next to
 * H2CHECK.EXE): its size, the pack directory (file count and the 16-bit
 * CRC of the directory, the same test the engine makes in quakefs.c) and,
 * for the demo, a CRC-32 of the whole file, so a file damaged on its way
 * in (a broken download, an interrupted transfer) is caught before the
 * game trips over it. /Q skips the CRC-32 (a few seconds).
 *
 * Exit codes (for IF ERRORLEVEL): 0 = good, 1 = no such file,
 * 2 = not a known Hexen II pak0.pak, 3 = damaged (CRC-32 mismatch).
 *
 * Copyright (C) 2026 the ARM-DOS project. GPL-2 or later (see
 * ../src/COPYING).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <io.h>

extern const char *_armdos_progpath;

/* the pak0.pak files the engine knows (quakefs.c pakdata[]) */
struct known {
    long size;
    int files;
    unsigned dircrc;        /* CRC-16 (CCITT, init FFFF) of the directory */
    unsigned long crc32;    /* 0 = not checked */
    const char *what;
    int needs_pak1;
};

static const struct known known[] = {
    { 27750257L, 797, 22780, 0xBB755DBCUL,
      "the Hexen II demo, version 1.11 (November 1997)", 0 },
    { 22704056L, 696, 34289, 0,
      "Hexen II, the full game, version 1.11", 1 },
    { 0 }
};

static unsigned char buf[32768];
static unsigned long crctab[256];

static void mkcrctab(void)
{
    unsigned long c;
    int n, k;

    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
        crctab[n] = c;
    }
}

/* quakefs.c: CRC_Init (0xffff), CRC_ProcessByte (CCITT, polynomial 1021h) */
static unsigned crc16(unsigned crc, const unsigned char *p, long n)
{
    int k;

    while (n-- > 0) {
        crc ^= (unsigned)*p++ << 8;
        for (k = 0; k < 8; k++)
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
    }
    return crc;
}

static long rd32(const unsigned char *p)
{
    return (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) |
                  ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24));
}

static void commas(char *out, long v)
{
    char tmp[16];
    int n, i, j = 0;

    sprintf(tmp, "%ld", v);
    n = strlen(tmp);
    for (i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0)
            out[j++] = ',';
        out[j++] = tmp[i];
    }
    out[j] = 0;
}

static int readall(int fd, void *p, long n)
{
    unsigned char *q = p;

    while (n > 0) {
        int got = read(fd, q, n > (long)sizeof buf ? (int)sizeof buf : (int)n);
        if (got <= 0)
            return -1;
        q += got;
        n -= got;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char path[128], num[16];
    const char *name = 0;
    const struct known *k;
    unsigned char hdr[12], *dir;
    long size, dirofs, dirlen, done;
    unsigned long crc;
    unsigned dcrc;
    int fd, i, quick = 0, files;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "/Q") || !strcmp(argv[i], "/q"))
            quick = 1;
        else if (!strcmp(argv[i], "/?")) {
            printf("Checks the Hexen II data file for H2.EXE.\n\n"
                   "H2CHECK [[drive:][path]PAK0.PAK] [/Q]\n\n"
                   "  /Q  Quick check: no CRC-32 of the whole file.\n");
            return 0;
        } else
            name = argv[i];
    }
    if (!name) {
        /* DATA1\PAK0.PAK next to H2CHECK.EXE */
        char *slash;
        strncpy(path, _armdos_progpath, sizeof path - 20);
        path[sizeof path - 20] = 0;
        slash = strrchr(path, '\\');
        if (slash)
            slash[1] = 0;
        else
            path[0] = 0;
        strcat(path, "DATA1\\PAK0.PAK");
        name = path;
    }

    printf("Checking %s\n", name);
    fd = open(name, O_RDONLY | O_BINARY);
    if (fd < 0) {
        printf("  File not found.\n");
        return 1;
    }
    size = lseek(fd, 0, SEEK_END);
    commas(num, size);
    printf("  Size       %14s bytes\n", num);

    lseek(fd, 0, SEEK_SET);
    if (size < 12 || readall(fd, hdr, 12) || memcmp(hdr, "PACK", 4)) {
        close(fd);
        printf("\nThis is not a pak file.\n");
        return 2;
    }
    dirofs = rd32(hdr + 4);
    dirlen = rd32(hdr + 8);
    if (dirofs < 12 || dirlen < 64 || dirlen % 64 || dirofs + dirlen > size
        || dirlen > 4096L * 64 || !(dir = malloc(dirlen))) {
        close(fd);
        printf("\nThe pak file's directory is damaged.\n");
        return 2;
    }
    lseek(fd, dirofs, SEEK_SET);
    if (readall(fd, dir, dirlen)) {
        close(fd);
        printf("\nError reading the file.\n");
        return 2;
    }
    files = (int)(dirlen / 64);
    dcrc = crc16(0xFFFF, dir, dirlen);
    free(dir);
    printf("  Directory  %8d files, CRC %u\n", files, dcrc);

    for (k = known; k->size; k++)
        if (k->files == files && k->dircrc == dcrc)
            break;
    if (!k->size || k->size != size) {
        close(fd);
        printf("\nThis is not a Hexen II PAK0.PAK that H2.EXE knows.\n"
               "H2.EXE needs PAK0.PAK from the Hexen II demo version 1.11\n"
               "(27,750,257 bytes) - see README.TXT.\n");
        return 2;
    }

    if (k->crc32 && !quick) {
        mkcrctab();
        crc = 0xFFFFFFFFUL;
        lseek(fd, 0, SEEK_SET);
        for (done = 0; done < size; ) {
            int got = read(fd, buf, sizeof buf);
            unsigned char *p = buf, *e;
            if (got <= 0)
                break;
            for (e = buf + got; p < e; p++)
                crc = crctab[(crc ^ *p) & 0xFF] ^ (crc >> 8);
            done += got;
            if ((done & 0xFFFFF) < (long)sizeof buf || done == size)
                printf("\r  CRC-32     %3d%%", (int)(done / (size / 100 + 1)));
        }
        crc ^= 0xFFFFFFFFUL;
        printf("\r  CRC-32           %08lX%s\n", crc,
               crc == k->crc32 ? "" : "  (should be BB755DBC)");
        if (done != size || crc != k->crc32) {
            close(fd);
            printf("\nPAK0.PAK is damaged: copy it again.\n");
            return 3;
        }
    }
    close(fd);

    printf("\nPAK0.PAK is %s.\n", k->what);
    if (k->needs_pak1) {
        char p1[140], *slash;
        strcpy(p1, name);
        slash = strrchr(p1, '\\');
        strcpy(slash ? slash + 1 : p1, "PAK1.PAK");
        if (access(p1, 0) != 0) {
            printf("The full game also needs PAK1.PAK in the same directory.\n");
            return 2;
        }
    }
    return 0;
}
