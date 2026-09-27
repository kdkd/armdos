/*
 * shared.h - what SHELLB.COM (the small resident loader) and SHELLC.EXE (the
 * Shell) share.
 *
 * DOS 4.00's DOSSHELL.BAT runs "SHELLB DOSSHELL" and then "SHELLC /TRAN ...".
 * The real SHELLB stays resident and talks to SHELLC through INT 2Fh with
 * multiplex id 19h; in transient mode (/TRAN) SHELLC leaves memory whenever
 * it starts a program, so only SHELLB (3.9 KB) stays resident while the
 * program runs (the real one does that by feeding the program's command line
 * and a "GOTO COMMON" back into the running DOSSHELL.BAT through COMMAND.COM's
 * INT 2Fh AEh hook).
 *
 * ARM-DOS's SHELLB does the same job as a small parent process: it reads the
 * SHELLC line out of the batch file it was given, runs SHELLC, runs whatever
 * SHELLC asked for once SHELLC has left memory, and starts SHELLC again. The
 * block below is how they talk; SHELLC finds it with INT 2Fh AX=1900h
 * (AL = FFh installed, BX = flat pointer to the block), the real SHELLB's
 * multiplex id.
 */
#ifndef DOSSHELL_SHARED_H
#define DOSSHELL_SHARED_H
#include <stdint.h>

#define SHELLB_MUX      0x19
#define SHELLB_MAGIC    0x424C4853u     /* "SHLB" */
#define SHELLB_VERSION  1

/* action: what SHELLC wants SHELLB to do after it exits */
#define SB_EXIT     0       /* the Shell was left: SHELLB ends too */
#define SB_RUN      1       /* run the command lines in cmds, then restart SHELLC */
#define SB_PROMPT   2       /* run COMMAND.COM (the Shell command prompt), then restart */

#define SB_CMDSIZE   1024
#define SB_STATESIZE 384

struct shellblk {
    uint32_t magic;
    uint16_t version;
    uint8_t  action;        /* SB_* set by SHELLC before it exits */
    uint8_t  restarts;      /* SHELLB: how often SHELLC has been restarted (0 = first start) */
    uint16_t statelen;      /* bytes of state[] in use (SHELLC's own format) */
    uint16_t pad;
    char     cmds[SB_CMDSIZE];      /* NUL-separated command lines, empty line ends */
    uint8_t  state[SB_STATESIZE];   /* SHELLC's saved screen state */
};

/* runcmd.c (linked into both) */
int  run_line(const char *line);        /* one command line, as COMMAND would */
int  run_comspec(const char *tail);     /* %COMSPEC% with this tail */
int  find_program(const char *word, char *full);  /* 1 .COM/.EXE, 2 .BAT, 0 */
const char *getenv_dos(const char *name);
int  dos_exec(const char *path, const char *tail);
void make_absolute(char *path);  /* AX = type<<8 | code, or -1 */

#endif
