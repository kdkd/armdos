/*
 * res.h - the interface between COMMAND's resident part (res.c, a few KB
 * that stay in memory) and its transient part (everything else, placed at
 * the top of memory and reloaded from COMSPEC when a program overwrote it),
 * as in MS-DOS 4.0 (COMMAND1.ASM/COMMAND2.ASM "RESGROUP" vs "TRANGROUP").
 *
 * All state that must survive a program run lives here, in the resident
 * part; the transient keeps only scratch data (its .bss is zeroed every
 * time it is entered, and it must have no initialised writable data, so the
 * checksum of its image tells whether a program overwrote it).
 */
#ifndef RES_H
#define RES_H

#include <stdint.h>

#define RES_MAGIC   0x434D4452u         /* "RDMC" */

/* why the transient is entered */
enum { T_INIT = 1, T_LODCOM = 2, T_EXEC = 3 };

struct res {
    uint32_t magic;

    /* ---- services of the resident part */
    void (*exec)(void);                 /* run execpath (block below); comes back through T_EXEC */
    void (*exit)(int code);             /* $EXIT: back to the parent (never returns) */
    void *h22, *h23, *h24;              /* our INT 22h/23h/24h handlers */

    /* ---- the transient (managed by the resident part) */
    uint16_t tseg;                      /* its memory block */
    uint32_t tsize;                     /* bytes allocated */
    uint32_t timage;                    /* image bytes (checksummed) */
    uint32_t tentry;                    /* entry, offset in the image */
    uint32_t tsum;                      /* checksum of the relocated image */
    uint32_t trawsum;                   /* checksum of the image as stored in COMMAND.COM */
    uint32_t tfileoff;                  /* offset of the transient's AR1 header in COMMAND.COM */

    /* ---- process */
    struct psp *mypsp;
    uint16_t parent_psp;
    uint32_t old_term;
    uint16_t envseg;
    uint8_t  permcom, ffail, in_init, init_special, loading, load_failed;

    /* ---- command processor state (the transient's "resident variables") */
    uint8_t  echoflag;                  /* bit0 = echo on; shifted while piping */
    uint8_t  forflag, ifflag, pipeflag, pipefiles, nullflag;
    uint8_t  call_flag, call_batch_flag, extcom, restdir, curdrv;
    uint8_t  in_batch, batch_abort, re_out_app, ctrlc_hit;
    uint8_t  io_save[2];
    uint16_t singlecom;                 /* 0, 1 = /C pending, 0FFFFh done, 0FFF0h/0FF00h/0F000h */
    uint16_t retcode;                   /* INT 21h 4Dh result */
    uint16_t tpa_seg;                   /* COPY's buffer */
    int32_t  verval;                    /* VERIFY to restore, -1 none */
    void    *batch, *next_batch, *forptr;
    int32_t  nest;
    char    *pipeptr, *inpipeptr, *outpipeptr;

    char     comspec[80];
    char     userdir1[80];
    char     re_instr[64];
    char     re_outstr[64];
    char     pipe1[16], pipe2[16];
    char     pipestr[132];
    char     single_buf[132];           /* COMMAND /C string */
    uint8_t  ucombuf[132];              /* the keyboard line: DOS's template */

    /* ---- what EXEC needs (copied here: the transient is freed first) */
    char     execpath[128];
    uint8_t  exectail[128];
    uint8_t  execfcb1[40], execfcb2[40];
    uint8_t  execblock[16];
};

#endif
