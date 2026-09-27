/* keyb.h - KEYB.COM: the resident part's interface (the "shared data area") */
#ifndef KEYB_H
#define KEYB_H

#include <stdint.h>
#include <armdos.h>

/* As in MODE.COM (apps/mode/mode.h): the resident part (kbres.c, up to
   kbend.S's keyb_res_end) sits in .text.unlikely.* sections, which the SDK
   link script places right after crt0, and the tables KEYB builds from
   KEYBOARD.SYS are copied behind it before INT 21h AH=31h - as DOS 4's KEYB
   copies its TEMP_SHARED_DATA into the resident SHARED_DATA area. */
#define RES   __attribute__((section(".text.unlikely.keyb_data,\"ax\",%progbits @")))
#define RESFN __attribute__((section(".text.unlikely.keyb_code"), noinline))

/* keyboard types (KEYBSHAR.INC) */
#define G_KB    0x1000

/* The shared data area (KEYBSHAR.INC SHARED_DATA_STR, ARM-sized). Pointers are
   flat. INT 2Fh AX=AD80h returns AX=FFFFh, BX=0100h (version 1.00) and DI (r5)
   = a pointer to this structure. */
struct keybsd {
    char     sig[4];            /* "KEYB" */
    armdos_vect_t old15, old2f;
    uint16_t keyb_type;         /* G_KB */
    uint16_t system_flag;
    volatile uint8_t table_ok;  /* 0 while KEYB rebuilds the tables */
    volatile uint8_t country;   /* 0FFh = the national layout, 0 = US (Ctrl+Alt+F1) */
    uint8_t  nls1, nls2;        /* dead-key state (NLS_FLAG_1/2) */
    /* table copy begins here (a later KEYB replaces everything from here on) */
    char     language[2];       /* "GR"; 0,0 when the layout was chosen by ID */
    uint16_t invoked_cp;        /* the code page of the active translate section */
    uint16_t invoked_id;        /* the /ID: given (or the first-parameter ID), 0 = none */
    uint16_t special;           /* special features of the state logic */
    uint8_t  hot_on, hot_off;   /* scan codes: Ctrl+Alt+F1 = US, Ctrl+Alt+F2 = national */
    uint8_t  layout_id;         /* published on system board port F6h (ARCH.md 4.6) */
    uint8_t  nsect;             /* specific translate sections loaded */
    const uint8_t *logic;       /* the state logic */
    const uint8_t *common;      /* the common translate section */
    const uint8_t *active;      /* the specific section of invoked_cp, or 0 */
    const uint8_t *sect[8];     /* the specific sections (their code page is at +2) */
    uint32_t used;              /* bytes of table data behind the area */
    /* not copied */
    uint32_t capacity;          /* room for table data (fixed at installation) */
    uint8_t *tables;            /* = keyb_res_end, where the table data lives */
};
#define SD_COPY_START offsetof(struct keybsd, language)
#define SD_COPY_END   offsetof(struct keybsd, capacity)

extern struct keybsd keyb_sd;
void keyb_int15(struct armregs *f);
void keyb_int2f(struct armregs *f);
void keyb_publish(void);
void keyb_stay(const void *src, uint32_t len, uint32_t paras) __attribute__((noreturn));
extern char keyb_res_end[];

/* the layout ids published on port F6h (bits 0-4; bits 5-7 = code page index) */
extern const char keyb_layout_codes[];      /* "US" "GR" ... in id order */
int keyb_cp_index(unsigned cp);             /* 437 850 860 863 865 -> 0-4, else 7 */

#endif
