/* k_map.c - print the memory map (MCB chain, system sub-blocks) like MEM /DEBUG */
#include "t.h"

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

int main(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x5200;
    _armdos_int21(&r);
    const uint8_t *lol = (const uint8_t *)r.r1;
    uint16_t seg = rd16(lol - 2);
    unsigned long freeb = 0, largest = 0;
    t_log("T:MAP LoL at %05lX, first MCB %04X\n", (unsigned long)r.r1, seg);
    for (int guard = 0; guard < 200; guard++) {
        const uint8_t *m = (const uint8_t *)((uint32_t)seg << 4);
        uint16_t owner = rd16(m + 1), size = rd16(m + 3);
        char name[9];
        memcpy(name, m + 8, 8);
        name[8] = 0;
        for (int i = 0; i < 8; i++) if ((uint8_t)name[i] < ' ') name[i] = 0;
        t_log("T:MAP %c %05lX %6lu owner %04X %s\n", m[0], (unsigned long)(seg + 1) << 4, (unsigned long)size * 16, owner,
              owner == 8 ? "(system)" : owner == 0 ? "(free)" : name);
        if (owner == 8) {
            const uint8_t *s = m + 16, *end = m + 16 + size * 16;
            while (s < end) {
                char n2[9];
                memcpy(n2, s + 8, 8);
                n2[8] = 0;
                t_log("T:MAP     %c %05lX %6lu %s\n", s[0], (unsigned long)(s - (const uint8_t *)0) + 16, (unsigned long)rd16(s + 3) * 16, s[0] == 'D' ? n2 : "");
                s += 16 + rd16(s + 3) * 16;
            }
        }
        if (!owner) { freeb += size * 16UL; if (size * 16UL > largest) largest = size * 16UL; }
        if (m[0] == 'Z') break;
        seg += size + 1;
    }
    t_log("T:MAP free %lu largest %lu\n", freeb, largest);
    return 0;
}
