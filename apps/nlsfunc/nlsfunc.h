/* nlsfunc.h - NLSFUNC.EXE: the resident part's interface */
#ifndef NLSFUNC_H
#define NLSFUNC_H

#include <stdint.h>
#include <armdos.h>

/* As in MODE.COM (apps/mode/mode.h): the resident part (nlsres.c, nlsint.S up
   to nls_res_end) sits in .text.unlikely.* sections right after crt0. */
#define RES   __attribute__((section(".text.unlikely.nls_data,\"ax\",%progbits @")))
#define RESFN __attribute__((section(".text.unlikely.nls_code"), noinline))

/* the kernel's national language data (kernel/inc/kabi.h) */
struct nls_state {
    uint32_t magic;
    char     path[64];
    uint16_t country, cp, syscp, pad;
    uint8_t  info[34];
    uint8_t  ucase[130];
    uint8_t  fchar[24];
    uint8_t  collate[258];
};
#define NLS_MAGIC 0x31534C4Eu   /* "NLS1" */

struct nlsres {
    char sig[4];                /* "NLSF" */
    armdos_vect_t old2f;
    char path[64];              /* NLSFUNC's own COUNTRY.SYS, "" = the kernel's */
    char bootdrive;             /* 'C' */
};

extern struct nlsres nls_res;
void nls_int2f(struct armregs *f);
int nls_int21(struct armregs *r);
extern char nls_res_end[];

#endif
