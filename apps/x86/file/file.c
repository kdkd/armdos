/*
 * file.c - FILE.EXE, "what is this file?" for ARM-DOS (after Unix file(1)).
 *
 *   FILE [/?] filespec [filespec ...]      (wildcards allowed)
 *
 * Looks at a file's contents (and, where contents cannot tell, its name) and
 * says what it is: ARM-DOS and x86 DOS executables (and whether ELBOW can run
 * them), device drivers, disk images, archives, game data, pictures, sound,
 * text.  Written for ARM-DOS by Europa Micro Systems; part of apps/x86.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>

#define HDR 4096
static unsigned char h[HDR];
static long fsize;
static int nh;
static FILE *fp;

static unsigned u16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned long u32(const unsigned char *p) { return p[0] | (p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | (p[2] << 8) | p[3]; }

static int readat(long off, void *buf, int n)
{
    if (off < 0 || off >= fsize) return 0;
    fseek(fp, off, SEEK_SET);
    return (int)fread(buf, 1, n, fp);
}

static const char *commas(unsigned long v)
{
    static char b[4][20];
    static int k;
    char t[20], *o = b[k = (k + 1) & 3];
    int n = sprintf(t, "%lu", v), j = 0;
    for (int i = 0; i < n; i++) {
        o[j++] = t[i];
        if ((n - i - 1) % 3 == 0 && i != n - 1) o[j++] = ',';
    }
    o[j] = 0;
    return o;
}

static const char *ext_of(const char *name)
{
    const char *d = strrchr(name, '.');
    return d ? d + 1 : "";
}

/* ---- executables */

/* the SDK's self-relocating .COM prologue (sdk/comstub.S): adr r12, stub; ldr r4, ... */
static int is_comstub(void) { return nh >= 8 && u32(h) == 0xE24FC008u && (u32(h + 4) & 0xFFFF0000u) == 0xE59F0000u; }

static int arm_word(unsigned long w) { return (w >> 28) == 0xE; }

static void arm_image(const unsigned char *a, char *out, const char *what)
{
    unsigned flags = u16(a + 6);
    unsigned long img = u32(a + 12), bss = u32(a + 16), stk = u32(a + 20), entry = u32(a + 24), nrel = u32(a + 32);
    (void)stk;
    sprintf(out, "%s, ARMv5TE%s, %s bytes + %s bss, %s relocs, entry +%lXh%s",
            what, (flags & 1) || (entry & 1) ? " Thumb" : "", commas(img), commas(bss), commas(nrel),
            entry & ~1ul, (flags & 2) ? ", XMS" : "");
}

static int ar1_device(const unsigned char *a, char *out)
{
    unsigned char dh[24];
    if (readat(u32(a + 8), dh, 24) != 24) return 0;
    if (u32(dh) != 0xFFFFFFFFu && u32(dh) > 0x100000) return 0;
    unsigned attr = u16(dh + 4);
    char name[9];
    memcpy(name, dh + 16, 8); name[8] = 0;
    for (int i = 7; i >= 0 && name[i] == ' '; i--) name[i] = 0;
    if (attr & 0x8000) {
        for (int i = 0; name[i]; i++) if (!isprint((unsigned char)name[i])) return 0;
        sprintf(out, "ARM-DOS character device driver (AR1), device %s%s", name,
                (attr & 3) ? (attr & 1 ? " (console input: a CON replacement)" : " (console output)") : "");
    } else sprintf(out, "ARM-DOS block device driver (AR1), %u unit%s", dh[16], dh[16] == 1 ? "" : "s");
    return 1;
}

static int x86_features(long off, long len)
{
    /* rough: count 386 (66h/67h/0Fh xx) opcode bytes */
    static unsigned char b[2048];
    int n = readat(off, b, len < 2048 ? (int)len : 2048), c386 = 0;
    for (int i = 0; i + 1 < n; i++) {
        if (b[i] == 0x66 && (b[i + 1] == 0xB8 || b[i + 1] == 0x8B || b[i + 1] == 0x89 || b[i + 1] == 0x50 || b[i + 1] == 0xC1)) c386++;
        if (b[i] == 0x0F && (b[i + 1] == 0xB6 || b[i + 1] == 0xB7 || b[i + 1] == 0xBE || b[i + 1] == 0xBF || b[i + 1] == 0xA0 || b[i + 1] == 0xA8)) c386++;
    }
    return c386;
}

static int exe(const char *name, char *out)
{
    if (nh < 2 || !((h[0] == 'M' && h[1] == 'Z') || (h[0] == 'Z' && h[1] == 'M'))) return 0;
    if (nh < 0x40) { strcpy(out, "DOS executable (MZ), truncated"); return 1; }
    unsigned long lfa = u32(h + 0x3C);
    unsigned char s[64];
    int ns = (lfa >= 0x40 && lfa < (unsigned long)fsize) ? readat(lfa, s, 64) : 0;
    if (ns >= 64 && !memcmp(s, "AR1", 4)) {
        arm_image(s, out, "ARM-DOS .EXE (MZ/AR1)");
        return 1;
    }
    if (ns >= 2 && s[0] == 'N' && s[1] == 'E') { strcpy(out, "Windows or OS/2 executable (NE) - not for DOS or ELBOW"); return 1; }
    if (ns >= 4 && !memcmp(s, "PE\0\0", 4)) { strcpy(out, "Windows executable (PE) - not for DOS or ELBOW"); return 1; }
    if (ns >= 2 && s[0] == 'L' && (s[1] == 'E' || s[1] == 'X')) { strcpy(out, "DOS-extended or OS/2 executable (LE/LX) - needs protected mode"); return 1; }
    unsigned cp = u16(h + 4), cblp = u16(h + 2), hdr = u16(h + 8), nrel = u16(h + 6);
    long img = (long)cp * 512 - (cblp ? 512 - cblp : 0) - hdr * 16L;
    const char *pack = "";
    if (!memcmp(h + 0x1C, "LZ09", 4) || !memcmp(h + 0x1C, "LZ91", 4)) pack = ", LZEXE-packed";
    else if (memmem(h, nh, "PKLITE", 6)) pack = ", PKLITE-packed";
    else if (memmem(h, nh, "Borland", 7)) pack = "";
    char extra[80] = "";
    if (memmem(h, nh, "DOS/4G", 6) || memmem(h, nh, "PMODE", 5) || memmem(h, nh, "DOS32", 5))
        strcpy(extra, " - a DOS extender (protected mode): ELBOW cannot run it");
    else {
        int c = x86_features(hdr * 16L + (long)u16(h + 0x16) * 16 + u16(h + 0x14), 2048);
        sprintf(extra, ", %s (runs under ELBOW)", c > 3 ? "80386" : "8086");
    }
    sprintf(out, "x86 DOS .EXE (MZ), %s bytes, %u reloc%s%s%s", commas(img > 0 ? img : 0), nrel, nrel == 1 ? "" : "s", pack, extra);
    (void)name;
    return 1;
}

static int com(const char *name, char *out)
{
    if (strcasecmp(ext_of(name), "COM")) return 0;
    if (is_comstub()) { sprintf(out, "ARM-DOS .COM (self-relocating, SDK), %s bytes", commas(fsize)); return 1; }
    int words = nh / 4 > 4 ? 4 : nh / 4, al = 0;
    for (int i = 0; i < words; i++) if (arm_word(u32(h + i * 4))) al++;
    if (words && (h[3] & 0xF0) == 0xE0 && (h[3] & 0x0C) != 0x0C && (words == 4 ? al >= 3 : 1)) {
        sprintf(out, "ARM-DOS .COM (ARM code), %s bytes", commas(fsize));
        return 1;
    }
    sprintf(out, "x86 DOS .COM (by its code), %s bytes%s (runs under ELBOW)", commas(fsize),
            x86_features(0, fsize) > 3 ? ", 80386" : "");
    return 1;
}

/* ---- disks */

static int bpb_desc(const unsigned char *b, char *out, const char *what)
{
    unsigned bps = u16(b + 11), spc = b[13], nf = b[16];
    unsigned long tot = u16(b + 19) ? u16(b + 19) : u32(b + 32);
    if (bps != 512 || !spc || (spc & (spc - 1)) || nf < 1 || nf > 2 || !tot) return 0;
    char label[12] = "";
    if (b[38] == 0x29) { memcpy(label, b + 43, 11); label[11] = 0; for (int i = 10; i >= 0 && label[i] == ' '; i--) label[i] = 0; }
    unsigned long clusters = tot / spc;
    sprintf(out, "%s, FAT%s, %s KB%s%s%s", what, clusters < 4085 ? "12" : "16", commas(tot / 2),
            label[0] ? ", label \"" : "", label, label[0] ? "\"" : "");
    return 1;
}

static int disk(char *out)
{
    if (nh < 512 || h[510] != 0x55 || h[511] != 0xAA) return 0;
    static const struct { long size; const char *what; } fl[] = {
        { 368640, "360 KB" }, { 737280, "720 KB" }, { 1228800, "1.2 MB" }, { 1474560, "1.44 MB" }, { 2949120, "2.88 MB" } };
    for (unsigned i = 0; i < sizeof fl / sizeof fl[0]; i++)
        if (fsize == fl[i].size) {
            char w[40];
            sprintf(w, "%s floppy disk image", fl[i].what);
            if (bpb_desc(h, out, w)) {
                if (h[3] == 0xEA) strcat(out, " (ARM-DOS boot sector)");
                return 1;
            }
        }
    /* an MBR with one partition */
    const unsigned char *pe = h + 0x1BE;
    for (int i = 0; i < 4; i++, pe += 16) {
        if (!pe[4]) continue;
        unsigned char vbr[512];
        unsigned long lba = u32(pe + 8);
        if (readat(lba * 512, vbr, 512) == 512 && bpb_desc(vbr, out, "hard disk image")) {
            char t[40];
            sprintf(t, ", partition type %02Xh", pe[4]);
            strcat(out, t);
            return 1;
        }
    }
    if (bpb_desc(h, out, "FAT boot sector")) return 1;
    return 0;
}

/* ---- archives and data */

static int zip(char *out)
{
    if (nh < 4 || memcmp(h, "PK\3\4", 4)) return 0;
    unsigned char e[22];
    long pos = fsize - 22;
    unsigned files = 0;
    for (long back = 0; back < 65557 && pos - back >= 0; back++)
        if (readat(pos - back, e, 22) == 22 && !memcmp(e, "PK\5\6", 4)) { files = u16(e + 10); break; }
    if (files) sprintf(out, "ZIP archive, %u file%s", files, files == 1 ? "" : "s");
    else strcpy(out, "ZIP archive");
    return 1;
}

static int wad_pak(char *out)
{
    if (nh >= 12 && (!memcmp(h, "IWAD", 4) || !memcmp(h, "PWAD", 4))) {
        unsigned long n = u32(h + 4);
        const char *game = "";
        unsigned char d[16 * 64];
        int k = readat(u32(h + 8), d, sizeof d);
        for (int i = 0; i + 16 <= k; i += 16) {
            if (!memcmp(d + i + 8, "E1M1", 4)) { game = " (DOOM)"; break; }
            if (!memcmp(d + i + 8, "MAP01", 5)) { game = " (DOOM II / Final DOOM)"; break; }
        }
        sprintf(out, "DOOM %s%s, %s lumps", h[0] == 'I' ? "IWAD (main game data)" : "PWAD (add-on)", game, commas(n));
        return 1;
    }
    if (nh >= 12 && !memcmp(h, "PACK", 4)) { sprintf(out, "Quake PAK archive, %s files", commas(u32(h + 8) / 64)); return 1; }
    return 0;
}

static int zmachine(const char *name, char *out)
{
    const char *e = ext_of(name);
    if (nh < 64 || h[0] < 1 || h[0] > 8) return 0;
    int ok = !strncasecmp(e, "Z", 1) || !strcasecmp(e, "DAT");
    if (!ok) return 0;
    char serial[7];
    memcpy(serial, h + 0x12, 6); serial[6] = 0;
    for (int i = 0; i < 6; i++) if (!isdigit((unsigned char)serial[i])) return 0;
    unsigned rel = be16(h + 2);
    static const struct { unsigned rel; const char *serial, *title; } known[] = {
        { 88, "840726", "Zork I" }, { 119, "880429", "Zork I" }, { 48, "840904", "Zork II" }, { 63, "860811", "Zork II" },
        { 17, "840727", "Zork III" }, { 25, "860811", "Zork III" }, { 59, "851108", "The Hitchhiker's Guide to the Galaxy" } };
    const char *title = "";
    for (unsigned i = 0; i < sizeof known / sizeof known[0]; i++)
        if (known[i].rel == rel && !strcmp(known[i].serial, serial)) title = known[i].title;
    sprintf(out, "Z-machine story file v%u, release %u, serial %s%s%s%s", h[0], rel, serial,
            *title ? " (" : "", title, *title ? ")" : "");
    return 1;
}

static int wolf(const char *name, char *out)
{
    const char *e = ext_of(name);
    if (strcasecmp(e, "WL1") && strcasecmp(e, "WL6") && strcasecmp(e, "SOD") && strcasecmp(e, "SDM")) return 0;
    const char *what = "data";
    if (!strncasecmp(name, "VSWAP", 5)) what = "graphics and sound pages (VSWAP)";
    else if (!strncasecmp(name, "GAMEMAPS", 8) || (nh > 8 && !memcmp(h, "TED5", 4))) what = "maps (GAMEMAPS)";
    else if (!strncasecmp(name, "MAPHEAD", 7)) what = "map header";
    else if (!strncasecmp(name, "AUDIO", 5)) what = "sound and music";
    else if (!strncasecmp(name, "VGAGRAPH", 8)) what = "VGA graphics";
    else if (!strncasecmp(name, "CONFIG", 6)) what = "configuration";
    sprintf(out, "Wolfenstein 3D %s, %s bytes", what, commas(fsize));
    return 1;
}

static int picture(char *out)
{
    if (nh >= 24 && !memcmp(h, "\x89PNG\r\n\x1a\n", 8)) { sprintf(out, "PNG image, %lu x %lu", be32(h + 16), be32(h + 20)); return 1; }
    if (nh >= 10 && (!memcmp(h, "GIF87a", 6) || !memcmp(h, "GIF89a", 6))) { sprintf(out, "GIF image (%.6s), %u x %u", h, u16(h + 6), u16(h + 8)); return 1; }
    if (nh >= 30 && h[0] == 'B' && h[1] == 'M' && u32(h + 2) <= (unsigned long)fsize + 16) {
        sprintf(out, "BMP image, %lu x %lu, %u bits per pixel", u32(h + 18), u32(h + 22), u16(h + 28)); return 1;
    }
    if (nh >= 128 && h[0] == 0x0A && h[1] <= 5 && h[2] == 1) {
        sprintf(out, "PCX image, %u x %u, %u bits per pixel", u16(h + 8) - u16(h + 4) + 1, u16(h + 10) - u16(h + 6) + 1, h[3] * h[65]); return 1;
    }
    if (nh >= 4 && h[0] == 0xFF && h[1] == 0xD8 && h[2] == 0xFF) { strcpy(out, "JPEG image"); return 1; }
    return 0;
}

static int sound(char *out)
{
    if (nh >= 36 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WAVE", 4)) {
        sprintf(out, "WAV sound, %lu Hz, %u bit, %s", u32(h + 24), u16(h + 34), u16(h + 22) == 2 ? "stereo" : "mono"); return 1;
    }
    if (nh >= 26 && !memcmp(h, "Creative Voice File\x1a", 20)) { sprintf(out, "Creative Voice (VOC) sound, version %u.%02u", h[23], h[22]); return 1; }
    if (nh >= 14 && !memcmp(h, "MThd", 4)) { sprintf(out, "MIDI music, format %u, %u track%s", be16(h + 8), be16(h + 10), be16(h + 10) == 1 ? "" : "s"); return 1; }
    if (fsize > 1084) {
        unsigned char m[4];
        if (readat(1080, m, 4) == 4 && (!memcmp(m, "M.K.", 4) || !memcmp(m, "4CHN", 4) || !memcmp(m, "8CHN", 4))) {
            char t[21]; memcpy(t, h, 20); t[20] = 0;
            sprintf(out, "Amiga/ProTracker module (MOD) \"%s\"", t); return 1;
        }
    }
    return 0;
}

static int misc(const char *name, char *out)
{
    if (nh >= 4 && !memcmp(h, "\x7f" "ELF", 4)) { sprintf(out, "ELF %s-bit %s object", h[4] == 2 ? "64" : "32", u16(h + 18) == 40 ? "ARM" : u16(h + 18) == 3 ? "i386" : "other"); return 1; }
    if (nh >= 2 && h[0] == 0x1F && h[1] == 0x8B) { strcpy(out, "gzip compressed data"); return 1; }
    if (nh >= 7 && h[2] == '-' && h[3] == 'l' && h[4] == 'h' && h[6] == '-') { sprintf(out, "LHarc archive (-lh%c-)", h[5]); return 1; }
    if (nh >= 2 && h[0] == 0x60 && h[1] == 0xEA) { strcpy(out, "ARJ archive"); return 1; }
    if (nh >= 2 && h[0] == 0x1A && h[1] >= 1 && h[1] <= 11 && !strcasecmp(ext_of(name), "ARC")) { strcpy(out, "ARC archive"); return 1; }
    if (nh >= 8 && h[0] == 0xFF && !memcmp(h + 1, "FONT   ", 7)) { strcpy(out, "DOS code page information file (CPI font)"); return 1; }
    if (nh >= 1 && h[0] == 0xFF && !strcasecmp(ext_of(name), "BAS")) { strcpy(out, "GW-BASIC program (tokenized)"); return 1; }
    if (nh >= 1 && h[0] == 0xFE && !strcasecmp(ext_of(name), "BAS")) { strcpy(out, "GW-BASIC program (protected)"); return 1; }
    if (nh >= 64 && !memcmp(h, "AR1", 4)) {
        if (ar1_device(h, out)) return 1;
        arm_image(h, out, "ARM-DOS AR1 image (loaded by the kernel)");
        return 1;
    }
    return 0;
}

static int text(const char *name, char *out)
{
    long lines = 0, crlf = 0, lf = 0, esc = 0, bin = 0, high = 0;
    static unsigned char b[8192];
    fseek(fp, 0, SEEK_SET);
    int n, prev = 0;
    long total = 0;
    while ((n = (int)fread(b, 1, sizeof b, fp)) > 0 && total < 262144) {
        for (int i = 0; i < n; i++) {
            unsigned c = b[i];
            if (c == '\n') { lines++; if (prev == '\r') crlf++; else lf++; }
            else if (c == 27) esc++;
            else if (c < 32 && c != '\r' && c != '\t' && c != 12 && c != 26 && c != 8 && c != 7) bin++;
            else if (c >= 128) high++;
            prev = c;
        }
        total += n;
    }
    if (bin * 50 > total) return 0;
    const char *e = ext_of(name);
    const char *kind = "text";
    if (!strcasecmp(e, "BAT")) kind = "batch file";
    else if (!strcasecmp(name, "CONFIG.SYS")) kind = "CONFIG.SYS text";
    else if (esc > 20 && esc * 30 > lines) kind = "ANSI art (text with escape sequences)";
    else if (!strcasecmp(e, "ASM")) kind = "assembly language source, text";
    else if (!strcasecmp(e, "C") || !strcasecmp(e, "H")) kind = "C source, text";
    else if (!strcasecmp(e, "BAS")) kind = "BASIC program, text";
    else if (high * 10 > total) kind = "text (IBM PC character set)";
    sprintf(out, "%s, %s line endings, %s line%s", kind, crlf >= lf ? "CR LF" : "LF (Unix)", commas(lines), lines == 1 ? "" : "s");
    return 1;
}

static void identify(const char *path, const char *name, int attr)
{
    char out[256];
    printf("%s: ", name);
    if (attr & 0x10) { puts("directory"); return; }
    if (attr & 0x08) { puts("volume label"); return; }
    fp = fopen(path, "rb");
    if (!fp) { puts("cannot open"); return; }
    fseek(fp, 0, SEEK_END);
    fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    nh = (int)fread(h, 1, HDR, fp);
    if (fsize == 0) strcpy(out, "empty");
    else if (!(exe(name, out) || com(name, out) || misc(name, out) || disk(out) || zip(out) || wad_pak(out) ||
               zmachine(name, out) || wolf(name, out) || picture(out) || sound(out) || text(name, out)))
        sprintf(out, "data, %s bytes", commas(fsize));
    fclose(fp);
    puts(out);
}

int main(int argc, char **argv)
{
    int any = 0;
    if (argc < 2) { fputs("Required parameter missing\n", stdout); return 1; }
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "/?")) {
            puts("Identifies what kind of file a file is.\n\n"
                 "FILE [drive:][path]filename [...]\n\n"
                 "  Wildcards (* and ?) may be used.  FILE tells ARM programs from x86\n"
                 "  programs (which run under ELBOW), device drivers, disk images,\n"
                 "  archives, game data, pictures, sound and text.");
            return 0;
        }
        struct find_t ft;
        char dir[128];
        strcpy(dir, argv[i]);
        char *s = strrchr(dir, '\\'), *c = strrchr(dir, ':');
        if (c && (!s || c > s)) s = c;
        if (s) s[1] = 0; else dir[0] = 0;
        if (_dos_findfirst(argv[i], 0x16, &ft)) {
            /* a device (CON, NUL...) or nothing */
            FILE *t = fopen(argv[i], "rb");
            if (t && isatty(fileno(t))) { printf("%s: character device\n", argv[i]); fclose(t); any = 1; continue; }
            if (t) fclose(t);
            printf("%s: File not found\n", argv[i]);
            continue;
        }
        do {
            if (!strcmp(ft.name, ".") || !strcmp(ft.name, "..")) continue;
            char path[160];
            snprintf(path, sizeof path, "%s%s", dir, ft.name);
            identify(path, ft.name, ft.attrib);
            any = 1;
        } while (!_dos_findnext(&ft));
    }
    return any ? 0 : 1;
}
