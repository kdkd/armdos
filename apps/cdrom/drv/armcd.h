/* armcd.h - ARMCD.SYS internals (resident part and INIT) */
#ifndef ARMCD_H
#define ARMCD_H

#include "../inc/cdrom.h"

/* Resident variables and constants live in .text.* sections so they are linked
   before armcd_res_end (see apps/ansi/ansi.h for the trick); INIT (init.c) is
   linked after it and discarded. tools/rescheck.mjs verifies the map. */
#define RES   __attribute__((section(".text.armcd_data,\"ax\",%progbits @")))
#define RESRO __attribute__((section(".text.armcd_rodata,\"ax\",%progbits @")))

#define IOP(p)   (*(volatile uint8_t *)(0x10000000u + (p)))
#define IOW(p)   (*(volatile uint16_t *)(0x10000000u + (p)))
#define IOL(p)   (*(volatile uint32_t *)(0x10000000u + (p)))
#define TICKS    (*(volatile uint32_t *)0x46C)

/* secondary IDE channel */
#define P_DATA   0x170
#define P_ERR    0x171
#define P_REASON 0x172
#define P_BCL    0x174
#define P_BCH    0x175
#define P_DH     0x176
#define P_CMD    0x177
#define P_CTL    0x376

#define S_BSY 0x80
#define S_DRQ 0x08
#define S_ERR 0x01

extern struct cddevhdr armcd_header;
extern uint8_t armcd_res_end[];
void armcd_strategy(struct cdreq *r);
void armcd_interrupt(void);
void armcd_init(struct cdreq_init *q);

/* resident helpers INIT uses too */
int  atapi_packet(const uint8_t *cdb, void *buf, unsigned len, int out);
int  atapi_wait(uint8_t mask, uint8_t want, uint32_t ticks);
extern uint32_t armcd_sense;        /* key << 16 | asc << 8 | ascq of the last failure */

#endif
