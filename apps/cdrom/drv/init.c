/*
 * init.c - ARMCD.SYS INIT: parse the DEVICE= line, find the drive, say hello.
 * Linked after armcd_res_end and discarded after INIT.
 *
 *   DEVICE=[d:][path]ARMCD.SYS [/D:devname] [/Q]
 *
 *   /D:devname  the character device name the CD-ROM extensions are told
 *               about (ARMCDEX /D:devname); default ARMCD001
 *   /Q          no banner
 */
#include "armcd.h"
#include <armdos.h>

int int21(struct armregs *r);

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void say(const char *s)
{
    struct armregs r;
    for (unsigned i = 0; i < sizeof r / 4; i++) ((uint32_t *)&r)[i] = 0;
    r.r0 = 0x4000; r.r1 = 1; r.r2 = slen(s); r.r3 = (uint32_t)s;
    int21(&r);
}

/* IDENTIFY PACKET DEVICE (ATA command A1h): words 23-26 firmware, 27-46 model */
static int identify(char *model, char *fw)
{
    IOP(P_DH) = 0xA0;
    if (atapi_wait(S_BSY, 0, 18)) return -1;
    IOP(P_CMD) = 0xA1;
    if (atapi_wait(S_BSY | S_DRQ, S_DRQ, 18)) return -1;
    for (int i = 0; i < 256; i++) {
        uint16_t w = IOW(P_DATA);
        if (i >= 23 && i < 27) { fw[(i - 23) * 2] = w >> 8; fw[(i - 23) * 2 + 1] = w; }
        if (i >= 27 && i < 47) { model[(i - 27) * 2] = w >> 8; model[(i - 27) * 2 + 1] = w; }
    }
    fw[8] = 0; model[40] = 0;
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
    for (int i = 7; i >= 0 && fw[i] == ' '; i--) fw[i] = 0;
    return 0;
}

void armcd_init(struct cdreq_init *q)
{
    const char *p = (const char *)q->arg;
    int quiet = 0;
    while (*p && *p != ' ' && *p != '/' && *p != '\r' && *p != '\n') p++;   /* the path */
    for (; *p && *p != '\r' && *p != '\n'; p++) {
        if (p[0] != '/') continue;
        char c = p[1] & ~0x20;
        if (c == 'D' && p[2] == ':') {
            int i = 0;
            p += 3;
            while (*p && *p > ' ' && *p != '/' && i < 8) armcd_header.name[i++] = *p++;
            while (i < 8) armcd_header.name[i++] = ' ';
            p--;
        } else if (c == 'Q') quiet = 1;
    }

    /* the drive: secondary channel, master. Polled: nIEN set. */
    IOP(P_CTL) = 0x02;
    IOP(P_DH) = 0xA0;
    int found = 0;
    char model[41], fw[9];
    if (IOP(P_CMD) != 0xFF) {
        IOP(P_CMD) = 0x08;                        /* DEVICE RESET: the ATAPI signature */
        atapi_wait(S_BSY, 0, 18);
        if (IOP(P_BCL) == 0x14 && IOP(P_BCH) == 0xEB && identify(model, fw) == 0) found = 1;
    }
    if (!quiet || !found) {
        say("\r\nARM-PC CD-ROM Device Driver  Version 1.10\r\n"
            "Copyright (C) Europa Micro Systems 1993.  All rights reserved.\r\n");
    }
    if (!found) {
        say("  No CD-ROM drive found on the secondary IDE channel.\r\n"
            "  ARMCD.SYS not installed.\r\n\r\n");
        q->units = 0;
        q->brk = (uint32_t)&armcd_header;
        q->h.status = ST_DONE | ST_ERROR | E_GENERAL;
        return;
    }
    /* INQUIRY, and a TEST UNIT READY that takes the power-on unit attention */
    uint8_t c[12], inq[36];
    for (int i = 0; i < 12; i++) c[i] = 0;
    c[0] = 0x12; c[4] = 36;
    atapi_packet(c, inq, 36, 0);
    c[0] = 0x00; c[4] = 0;
    atapi_packet(c, 0, 0, 0);
    if (!quiet) {
        char line[80], nm[9];
        int k = 0;
        const char *a = "  Unit 0: ";
        while (*a) line[k++] = *a++;
        for (int i = 0; model[i]; i++) line[k++] = model[i];
        line[k++] = ' '; line[k++] = ' ';
        for (int i = 0; fw[i]; i++) line[k++] = fw[i];
        a = "  (secondary IDE, master)\r\n";
        while (*a) line[k++] = *a++;
        line[k] = 0;
        say(line);
        for (int i = 0; i < 8; i++) nm[i] = armcd_header.name[i];
        nm[8] = 0;
        for (int i = 7; i >= 0 && nm[i] == ' '; i--) nm[i] = 0;
        say("  Device name: "); say(nm); say("\r\n\r\n");
    }
    q->units = 0;
    q->brk = (uint32_t)armcd_res_end;
    q->h.status = ST_DONE;
}
