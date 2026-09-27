/*
 * dos86.h - the DOS personality: x86 processes, memory arena, INT 21h.
 */
#ifndef DOS86_H
#define DOS86_H

#include <armdos.h>
#include "x86.h"

/* layout of the x86 world */
#define DOSDATA_SEG   0x0060u     /* 0600h-0BFFh: DOS data (LoL, InDOS, tables) */
#define ARENA_FIRST   0x00C0u     /* first MCB */
#define ARENA_END     arena_end   /* 640 KB unless /MEM:n */
extern uint16_t arena_end;

#define DD_CRITERR    0x000u      /* offsets in DOSDATA_SEG */
#define DD_INDOS      0x001u
#define DD_MEDIA      0x010u
#define DD_DBCS       0x020u
#define DD_LOL        0x040u      /* LoL-2 = first MCB */
#define DD_SFT        0x0C0u
#define DD_DPB        0x100u
#define DD_UPCASE     0x180u
#define DD_FUPCASE    0x210u
#define DD_FTERM      0x2A0u
#define DD_COLLATE    0x2C0u
#define DD_DBCS65     0x3D0u
#define DD_EXTINFO    0x3E0u

/* F000 "ROM" */
#define ROM_SENTINEL  0xE400u     /* 0F FE 00: end of a nested call */
#define ROM_CALL5     0xE404u     /* 0F FE 01, RETF: CP/M-style CALL 5 */
#define ROM_CASEMAP   0xE408u     /* 0F FE 02, RETF: country case map */
#define ROM_RESET     0xE40Cu     /* 0F FE 03: jumped to via FFFF:0000 */
#define ROM_MOUSECB   0xE410u     /* 0F FE 04: end of a mouse callback */
#define ROM_CONFIG    0xE6F5u     /* INT 15h C0h table */
#define ROM_DISKPARM  0xEFC7u     /* INT 1Eh */
#define ROM_FONT16    0xA000u
#define ROM_FONT8     0xB000u
#define ROM_FONT14    0xB800u
#define ROM_VIDPARM   0xF0A4u     /* INT 1Dh (dummy) */

struct xproc {
    uint16_t psp;
    uint16_t arm_psp;           /* shadow ARM PSP (0 = X86.EXE's own) */
    struct xproc *parent;
    X86 saved;                  /* the parent's CPU state at EXEC */
    int saved_direct;           /* EXEC came in as a direct INT (flags live) */
    uint32_t int22;             /* what EXEC put at PSP:0Ah */
    uint32_t saved_dta;
    char name[13];
};

extern struct xproc *cur_proc;
extern uint16_t cur_psp;        /* INT 21h 50h/51h/62h */
extern uint32_t dta_far;        /* seg << 16 | off */
extern int exit_code;           /* for the top-level process: X86.EXE's exit code */
extern int x86_exited;
extern volatile int ctrlc_hit, crit_abort;
extern uint8_t pit_touched, speaker_touched;
extern volatile int last_port60_value;

void world_init(void);
int  dos_int21(void);
void x86_terminate(int code, int type);
int  x86_load_top(const char *path, const char *tail);
uint32_t dos86_crit_x86(struct armregs *f);
void dos86_mouse_event(void);
int  x86_nested_call(uint16_t cs, uint16_t ip, int as_int);
int  arm21(struct armregs *r);
void set_dta(uint16_t seg, uint16_t off);
int  is_arm_image(const uint8_t *hdr, int n, uint32_t fsize, int fd);
void jft_sync(void);
int  bios_int(int n);
void x86_regs_to_arm(struct armregs *r);
void x86_note_hle_write(uint32_t lin, uint32_t len);   /* ARM wrote into x86 memory: drop translations */
int  hle_in_direct(void);
void cpu_stop_run(int why);
int  x86_defer_exit(int code, int type);
void x86_run_deferred(void);
void x86_keep(uint32_t paras, int code);

/* arena */
int  arena_alloc(uint32_t paras, uint16_t *seg, uint16_t *largest);
int  arena_free(uint16_t seg);
int  arena_resize(uint16_t seg, uint32_t paras, uint16_t *largest);
void arena_free_owner(uint16_t owner);
extern int arena_strategy;

#define STOP_EXIT    1
#define STOP_NESTED  2

#endif
