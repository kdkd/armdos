/*
 * shared.h - what AC.EXE (the small resident loader) and ACMAIN.EXE (the
 * Commander itself) share.
 *
 * Like the Norton Commander of 1989 (NC.EXE + NCMAIN.EXE), the ARM Commander
 * is split in two so that a program started from it gets almost all of the
 * memory: AC.EXE stays resident (a few KB), runs ACMAIN.EXE, and when ACMAIN
 * wants a command executed it leaves the command line in this block and
 * exits; AC runs the command and starts ACMAIN again, which restores its
 * panels from the state kept here.
 *
 * ACMAIN finds the block through its command tail: "/$XXXXXXXX" = the flat
 * address of the block in hex. Without it ACMAIN runs stand-alone and
 * executes commands itself (staying in memory).
 */
#ifndef AC_SHARED_H
#define AC_SHARED_H
#include <stdint.h>

#define AC_MAGIC    0x31434D41u     /* "AMC1" */
#define AC_VERSION  1

#define ACT_QUIT    0       /* F10: the loader ends too */
#define ACT_RUN     1       /* run cmd[], then restart ACMAIN */

#define AC_HIST     16      /* command history entries */
#define AC_HISTLEN  128

struct acpanel {            /* one panel's persistent state */
    uint8_t  mode;          /* 0 brief, 1 full, 2 info, 3 tree */
    uint8_t  sort;          /* 0 name, 1 extension, 2 time, 3 size, 4 unsorted */
    uint8_t  visible;
    uint8_t  pad;
    int16_t  cur, top;      /* cursor and first line shown */
    char     path[80];      /* "C:\DOS" */
    char     curname[13];   /* the file under the cursor */
    char     filter[13];    /* "*.*" */
    uint8_t  pad2[2];
};

struct acstate {
    uint32_t magic;         /* AC_MAGIC when valid */
    uint8_t  active;        /* 0 left, 1 right */
    uint8_t  panels_on;     /* Ctrl-O */
    uint8_t  keybar;        /* Ctrl-B */
    uint8_t  hidden;        /* show hidden/system files */
    uint8_t  ministatus;
    uint8_t  clock;
    uint8_t  confirm_del;
    uint8_t  lastmenu;      /* the pull-down menu used last */
    struct acpanel p[2];
    int16_t  nhist;
    int16_t  pad;
    char     hist[AC_HIST][AC_HISTLEN];
};

struct acblk {
    uint32_t magic;
    uint16_t version;
    uint8_t  action;        /* ACT_* set by ACMAIN before it exits */
    uint8_t  restarts;      /* how often ACMAIN has been restarted (0 = first start) */
    uint16_t lastrc;        /* AX of 4Dh after the last command: type<<8 | code */
    uint16_t pad;
    char     cmd[132];      /* the command line to run */
    struct acstate st;
};

#endif
