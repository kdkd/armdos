/*
 * bootrec.c - build an ARM-DOS boot record: the embedded boot sector
 * (build/bootsect.bin: ARM branch, OEM name, code from 0x40) with a DOS 4
 * extended BPB, as FORMAT's WriteBootSector and SYS's Write_Boot_Record do
 * with INC/BOOT.INC.
 */
#include "dosutil.h"
#include "bootrec.h"

extern const uint8_t bootsect_bin[512];

void make_bootrec(uint8_t *s, const struct bpb *b, int fixed, uint32_t serial,
                  const char *label11, int fat16)
{
    memcpy(s, bootsect_bin, 512);
    memcpy(s + 0x0B, b, sizeof *b);             /* 25 bytes: 0x0B-0x23 */
    s[0x24] = fixed ? 0x80 : 0x00;              /* physical drive */
    s[0x25] = 0;
    s[0x26] = 0x29;                             /* extended boot signature */
    s[0x27] = serial; s[0x28] = serial >> 8; s[0x29] = serial >> 16; s[0x2A] = serial >> 24;
    memcpy(s + 0x2B, label11 ? label11 : "NO NAME    ", 11);
    memcpy(s + 0x36, fat16 ? "FAT16   " : "FAT12   ", 8);
    s[0x3E] = s[0x3F] = 0;
    s[0x1FE] = 0x55; s[0x1FF] = 0xAA;
}

/* FORMAT's Create_Serial_ID (CMD/FORMAT/MSFOR.ASM, KERNEL.md 1.4) */
uint32_t format_serial(void)
{
    unsigned y, mo, d, h, mi, s, hs;
    get_date(&y, &mo, &d);
    get_time(&h, &mi, &s, &hs);
    unsigned hi = (((mo << 8) | d) + ((s << 8) | hs)) & 0xFFFF;
    unsigned lo = (y + ((h << 8) | mi)) & 0xFFFF;
    return ((uint32_t)hi << 16) | lo;
}
