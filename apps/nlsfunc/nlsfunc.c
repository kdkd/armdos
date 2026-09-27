/*
 * nlsfunc.c - NLSFUNC.EXE for ARM-DOS 4.00: the transient part.
 *
 *   NLSFUNC [[d:][path]filename]
 *
 * Installs the resident part (nlsres.c) that gives the kernel COUNTRY.SYS
 * after CONFIG.SYS time - CHCP needs it. Messages as NLSFUNC 4.00: silent when
 * it installs, "NLSFUNC already installed", "File not found" for a COUNTRY.SYS
 * that is not there, the parser's messages for bad parameters.
 */
#include <string.h>
#include <stdlib.h>
#include "nlsfunc.h"

static void out(const char *s)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = 2; r.r2 = strlen(s); r.r3 = (uint32_t)s;     /* NLSFUNC 4.00 writes to STDERR */
    _armdos_int21(&r);
}

int main(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x3000;
    _armdos_int21(&r);
    if ((r.r0 & 0xFFFF) != 4) { out("Incorrect DOS version\r\n"); return 1; }

    /* the command tail: at most one file name */
    const uint8_t *t = _armdos_psp->cmdtail;
    char tail[130];
    int n = t[0] < 127 ? t[0] : 127;
    memcpy(tail, t + 1, n);
    tail[n] = 0;
    char *cr = strchr(tail, '\r');
    if (cr) *cr = 0;
    char *p = tail, *file = 0;
    while (*p == ' ' || *p == '\t' || *p == ';' || *p == ',') p++;
    if (*p) {
        char *s = p;
        if (*s == '/') {
            while (*p && *p != ' ' && *p != '\t') p++;
            *p = 0;
            out("Invalid switch - "); out(s); out("\r\n");
            return 1;
        }
        while (*p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';') p++;
        if (*p) *p++ = 0;
        file = s;
        while (*p == ' ' || *p == '\t' || *p == ';' || *p == ',') p++;
        if (*p) { out("Too many parameters - "); out(p); out("\r\n"); return 1; }
    }

    memset(&r, 0, sizeof r);
    r.r0 = 0x1400;
    _armdos_int2f(&r);
    if ((r.r0 & 0xFF) == 0xFF) { out("NLSFUNC already installed\r\n"); return 1; }

    if (file) {
        for (char *c = file; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3D00; r.r3 = (uint32_t)file;
        if (_armdos_int21(&r)) { out("File not found\r\n"); return 1; }
        int h = r.r0 & 0xFFFF;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3E00; r.r1 = h;
        _armdos_int21(&r);
        strncpy(nls_res.path, file, sizeof nls_res.path - 1);
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3305;
    _armdos_int21(&r);
    nls_res.bootdrive = 'A' - 1 + (r.r3 & 0xFF);

    nls_res.old2f = armdos_getvect(0x2F);
    armdos_setvect(0x2F, nls_int2f);

    /* a resident program keeps nothing open; the environment goes too */
    for (int h = 0; h < 5; h++) { memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = h; _armdos_int21(&r); }
    memset(&r, 0, sizeof r);
    r.r0 = 0x4900; r.r8 = _armdos_psp->envseg;
    _armdos_int21(&r);
    _armdos_psp->envseg = 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3100;
    r.r3 = ((uint32_t)nls_res_end - (uint32_t)_armdos_psp + 15) >> 4;
    _armdos_int21(&r);
    return 0;
}
