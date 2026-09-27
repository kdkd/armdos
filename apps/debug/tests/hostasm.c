/* hostasm.c - host build of DEBUG's disassembler + assembler for differential tests.
 *   hostasm d ARM   : stdin lines "addr word"  -> disassembly
 *   hostasm t       : stdin lines "addr hw next" -> Thumb disassembly
 *   hostasm a       : stdin lines "addr<TAB>text" -> "word" or "ERR col"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../debug.h"

int asm_address(const char **pp, uint32_t *v)
{
    const char *p = *pp; uint32_t x = 0; int d = 0;
    for (;; p++, d++) {
        int c = *p;
        if (c >= '0' && c <= '9') x = x * 16 + c - '0';
        else if ((c | 32) >= 'a' && (c | 32) <= 'f') x = x * 16 + (c | 32) - 'a' + 10;
        else break;
    }
    if (!d) return 0;
    *pp = p; *v = x; return 1;
}

int main(int argc, char **argv)
{
    char line[512], out[256];
    char mode = argc > 1 ? argv[1][0] : 'd';
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (mode == 'd') {
            unsigned a, w; sscanf(line, "%x %x", &a, &w);
            disasm_arm(w, a, out); printf("%s\n", out);
        } else if (mode == 't') {
            unsigned a, h; int n; sscanf(line, "%x %x %x", &a, &h, (unsigned *)&n);
            int sz = disasm_thumb(h, n, a, out); printf("%d %s\n", sz, out);
        } else {
            char *tab = strchr(line, '\t'); if (!tab) { puts("BAD"); continue; }
            *tab = 0; unsigned a = strtoul(line, 0, 16);
            uint8_t b[64]; const char *e;
            int n = assemble(tab + 1, a, b, sizeof b, &e);
            if (!n) printf("ERR %d\n", (int)(e - (tab + 1)));
            else if (n == 4) printf("%08x\n", b[0] | b[1] << 8 | b[2] << 16 | (unsigned)b[3] << 24);
            else { for (int i = 0; i < n; i++) printf("%02x", b[i]); printf("\n"); }
        }
        fflush(stdout);
    }
    return 0;
}
