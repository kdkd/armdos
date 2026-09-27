/*
 * mouse.c - MOUSE.COM, the installer (transient part).
 *
 *   MOUSE          install the driver (INT 33h) and stay resident
 *   MOUSE OFF      remove it again (if nothing has hooked INT 33h/10h since)
 *
 * The driver uses the BIOS pointing-device services (INT 15h AX=C2xxh);
 * only the resident part (mres.S, mres.c, up to mouse_res_end) stays in
 * memory: INT 21h AH=31h keeps the PSP up to there, the environment is freed.
 */
#include <string.h>
#include <ctype.h>
#include "mouse.h"

void mouse_setpsp(uint16_t seg);

static void say(const char *s)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = 1; r.r2 = strlen(s); r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

static int bios15(int ax, int bx, uint32_t ptr)
{
    struct armregs r = { 0 };
    r.r0 = ax; r.r1 = ptr ? ptr : (uint32_t)bx;
    int cf = _armdos_intr(0x15, &r);
    return cf || ((r.r0 >> 8) & 0xFF) != 0;
}

/* the resident copy, if one is installed: its PSP segment, and the distance
   between its addresses and ours (same program, other load address) */
static int find_resident(uint16_t *psp, int32_t *delta)
{
    struct armregs r = { 0 };
    if (!ARMDOS_IVT[0x33]) return 0;
    r.r0 = 0x6D6D;
    _armdos_int33(&r);
    if ((r.r0 & 0xFFFF) != 0x4D4D) return 0;
    if (memcmp((const void *)r.r4, mouse_sig, 8)) return 0;
    *psp = r.r1 & 0xFFFF;
    *delta = (int32_t)(r.r4 - (uint32_t)mouse_sig);
    return 1;
}

static int remove_driver(void)
{
    uint16_t psp;
    int32_t d;
    if (!find_resident(&psp, &d)) {
        say("Mouse driver not installed\r\n");
        return 1;
    }
    uint32_t their33 = (uint32_t)mouse_int33 + d, their10 = (uint32_t)mouse_int10 + d;
    if ((uint32_t)ARMDOS_IVT[0x33] != their33 || (uint32_t)ARMDOS_IVT[0x10] != their10) {
        say("Unable to remove mouse driver\r\n");
        return 1;
    }
    armdos_vect_t old33 = *(armdos_vect_t *)((uint32_t)&mouse_old33 + d);
    armdos_vect_t old10 = *(armdos_vect_t *)((uint32_t)&mouse_old10 + d);
    bios15(0xC200, 0x0000, 0);                  /* disable the pointing device */
    bios15(0xC207, 0, 0);                       /* no handler */
    armdos_setvect(0x33, old33);
    armdos_setvect(0x10, old10);
    struct armregs r = { 0 };
    r.r0 = 0x4900; r.r8 = psp;
    _armdos_int21(&r);
    say("Mouse driver removed\r\n");
    return 0;
}

int main(int argc, char **argv)
{
    say("ARM-DOS Mouse Driver Version 1.00\r\n"
        "Copyright (C) Europa Micro Systems 1988.  All rights reserved.\r\n");
    if (argc > 1) {
        char a[8];
        strncpy(a, argv[1], 7);
        a[7] = 0;
        for (char *p = a; *p; p++) *p = toupper((unsigned char)*p);
        if (!strcmp(a, "OFF")) return remove_driver();
        say("Invalid parameter\r\n");
        return 1;
    }
    uint16_t psp;
    int32_t d;
    if (find_resident(&psp, &d)) {
        say("Mouse driver already installed\r\n");
        return 0;
    }
    /* the BIOS found a PS/2 mouse at POST (equipment word bit 2)? */
    int present = (*(volatile uint16_t *)0x410 & 4) != 0;
    if (present) present = !bios15(0xC205, 0x0300, 0) && !bios15(0xC201, 0, 0);
    if (!present) {
        say("Mouse not found\r\nDriver not installed\r\n");
        return 1;
    }
    mouse_reset();
    mouse_setpsp((uint32_t)_armdos_psp >> 4);
    mouse_old33 = armdos_getvect(0x33);
    mouse_old10 = armdos_getvect(0x10);
    bios15(0xC207, 0, (uint32_t)mouse_event);
    bios15(0xC200, 0x0100, 0);                  /* enable */
    armdos_setvect(0x33, mouse_int33);
    armdos_setvect(0x10, mouse_int10);
    say("Mouse driver installed\r\n");

    /* free the environment, keep PSP .. mouse_res_end */
    struct armregs r = { 0 };
    /* close the standard handles: a resident program keeps nothing open
       (and a redirected output file is complete when the shell closes it) */
    for (int h = 0; h < 5; h++) {
        r.r0 = 0x3E00; r.r1 = h;
        _armdos_int21(&r);
        memset(&r, 0, sizeof r);
    }
    r.r0 = 0x4900; r.r8 = _armdos_psp->envseg;
    _armdos_int21(&r);
    _armdos_psp->envseg = 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3100;
    r.r3 = ((uint32_t)mouse_res_end - (uint32_t)_armdos_psp + 15) >> 4;
    _armdos_int21(&r);
    return 0;
}
