/* dbg.h - DEBUG internals shared by debug.c (commands) and run.c (execution) */
#ifndef DBG_H
#define DBG_H
#include <stdint.h>

#include "armdos.h"
#include "debug.h"

/* the program's registers: r0-r12, sp, lr, pc, cpsr (= struct armregs order) */
enum { R_SP = 13, R_LR = 14, R_PC = 15, R_CPSR = 16, NREGS = 17 };
extern uint32_t ureg[NREGS];

#define CPSR_T      0x20u
#define CPSR_MODE   0x1Fu
#define MODE_USR    0x10u
#define MODE_SVC    0x13u
#define MODE_SYS    0x1Fu

/* why dbg_run() returned */
enum { STOP_BP = 0, STOP_TERM = 1 };

/* trap.S */
uint32_t dbg_run(void);
extern char dbg_trap[], dbg_term22[];
extern uint32_t dbg_ret_sp;
int dbg_absdisk(int write, struct armregs *r);


/* debug.c */
extern JMPBUF cmdjb;
extern volatile int ctrlc_hit, abort24;
extern uint16_t my_psp, user_psp;
extern uint32_t ram_end;
int dos(struct armregs *r);
void outc(int c);
void outs(const char *s);
void outhex(uint32_t v, int digits);
void crlf(void);
void flush(void);
void set_psp(uint16_t seg);
uint8_t peek8(uint32_t a);
uint32_t peek16(uint32_t a);
uint32_t peek32(uint32_t a);
int ram_ok(uint32_t a, uint32_t n);

/* run.c */
extern volatile int running;
void run_install(void);
void run_remove(void);
int next_pc(uint32_t *npc);                 /* -> 1 if computable (bit 0 = Thumb) */
int insn_size(uint32_t pc, int thumb);
int is_call(uint32_t pc, int thumb, uint32_t *after);
int plant(uint32_t addr_t);                 /* address | thumb bit; 0 = cannot */
void unplant_all(void);
uint32_t go(void);                          /* run with the planted breakpoints */

#endif
