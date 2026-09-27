/* debug.h - DEBUG for ARM-DOS: shared declarations */
#ifndef DEBUG_H
#define DEBUG_H
#include <stdint.h>

/* setjmp/longjmp: our own on ARM (newlib's drags in the C++ unwinder, 5 KB) */
#if defined(__arm__)
typedef uint32_t JMPBUF[10];
int dbg_setjmp(JMPBUF b) __attribute__((returns_twice));
void dbg_longjmp(JMPBUF b, int v) __attribute__((noreturn));
#define SETJMP(b) dbg_setjmp(b)
#define LONGJMP(b, v) dbg_longjmp(b, v)
#else
#include <setjmp.h>
#define JMPBUF jmp_buf
#define SETJMP(b) setjmp(b)
#define LONGJMP(b, v) longjmp(b, v)
#endif

/* disasm.c: objdump text
 ("mnemonic\toperands\t@ comment") */
void disasm_arm(uint32_t w, uint32_t pc, char *out);
int disasm_thumb(unsigned hw, int next, uint32_t pc, char *out);     /* -> size 2 or 4 */

/* asm.c: one line -> bytes (0 = error, *errpos = the offending character) */
int assemble(const char *line, uint32_t addr, uint8_t *out, int max, const char **errpos);
/* provided by the host program: parse a DEBUG address at *pp (advancing it) */
int asm_address(const char **pp, uint32_t *v);

#endif
