/* wadcheck.c - read DOOM1.WAD through stdio the way DOOM does (fseek + fread
 * of each lump) and compare with a straight sequential read: finds file-system
 * bugs without DOOM in the way. Prints "WADCHECK ok|bad ..." */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
{
    c = ~c;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
    return ~c;
}

unsigned _armdos_raw_extmem = 1;

int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "DOOM1.WAD";
    FILE *f = fopen(name, "rb");
    if (!f) { printf("WADCHECK bad open %s\n", name); return 1; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *all = malloc(size);
    if (!all) { printf("WADCHECK bad malloc %ld\n", size); return 1; }
    size_t got = fread(all, 1, size, f);
    printf("buf %p\n", all);
    printf("size %ld read %u crc %08lx\n", size, (unsigned)got, (unsigned long)crc32(0, all, got));
    uint32_t nl = *(uint32_t *)(all + 4), dir = *(uint32_t *)(all + 8);
    printf("numlumps %lu dir %lu\n", (unsigned long)nl, (unsigned long)dir);
    int bad = 0;
    uint8_t *buf = malloc(200000);
    for (uint32_t i = 0; i < nl; i++) {
        uint32_t pos = *(uint32_t *)(all + dir + 16 * i), len = *(uint32_t *)(all + dir + 16 * i + 4);
        if (len > 200000) continue;
        fseek(f, pos, SEEK_SET);
        size_t n = fread(buf, 1, len, f);
        if (n != len || memcmp(buf, all + pos, len)) {
            if (bad++ < 10) printf("lump %lu %.8s pos %lu len %lu: got %u, first diff at %d\n",
                (unsigned long)i, (char *)all + dir + 16 * i + 8, (unsigned long)pos, (unsigned long)len, (unsigned)n,
                (int)({ int k = 0; while (k < (int)n && buf[k] == all[pos + k]) k++; k; }));
        }
    }
    printf("WADCHECK %s %d bad lumps\n", bad ? "bad" : "ok", bad);
    return 0;
}
