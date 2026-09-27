/*
 * defrag.h - ARM Disk Optimizer (DEFRAG.EXE), shared declarations.
 *
 *   engine.c  the disk: FAT and directory tree (INT 25h), analysis, the
 *             crash-safe cluster-run move, Full Optimization / Unfragment
 *             Files Only, directory sorting (INT 26h)
 *   ui.c      the screen: cluster map, status and legend boxes, menus,
 *             dialogs, keyboard and INT 33h mouse, pacing
 *   main.c    command line and the program's flow
 */
#ifndef DEFRAG_H
#define DEFRAG_H

#include "dosutil.h"

#define VERSION "1.1"

/* ---- engine ------------------------------------------------------------ */
enum { M_FULL = 0, M_UNFRAG = 1, M_NONE = 2 };
enum { S_NONE = 0, S_NAME, S_EXT, S_DATE, S_SIZE };

struct fil {
    uint32_t entoff;            /* byte offset of the entry in the parent directory */
    uint32_t nclus;
    uint32_t size;
    uint16_t first;             /* first cluster (0: none) */
    uint16_t parent;            /* index of the parent directory, ROOT = the root */
    uint16_t date, time;
    uint16_t frags;             /* 1 = contiguous */
    uint8_t  attr, fl;
    char     name[11];
};
#define ROOT   0xFFFF
#define F_DIR    0x01
#define F_FIXED  0x02           /* unmovable */
#define F_DONE   0x04           /* placed by Full Optimization */
#define F_OPEN   0x08           /* open in an SFT */
#define F_NOSORT 0x10           /* directory: an open file's entry lives here */

/* owner[] codes */
#define OW_FREE 0
#define OW_BAD  0xFFFF
#define OW_RES  0xFFFE          /* reserved / unknown (treated as unmovable) */

struct stats {
    uint32_t files, dirs, fixedfiles, used, freec, bad, fixedclus;
    uint32_t fragfiles, fragclus, frags, gaps;
    unsigned notfrag_pct;       /* % of the used space in contiguous files */
    int recommend;              /* M_FULL / M_UNFRAG / M_NONE */
};

extern int dr;                  /* drive, 0 = A: */
extern unsigned spc, csize, maxc, nclus;
extern int fat16, floppy;
extern uint16_t *owner;
extern struct fil *fils;
extern unsigned nfils;
extern struct stats st;
extern int opt_sort, opt_desc, opt_hidden;
extern int stop_req;            /* the user stopped the run */
extern uint32_t moved_clusters;

/* 0 ok; else an error message (a static string) */
const char *eng_open(int drive);
const char *eng_analyze(void);
void eng_close(void);
/* 0 finished; 1 stopped by the user; else error message */
const char *eng_optimize(int method);
int  fixed_cluster(unsigned c);

/* ---- ui ---------------------------------------------------------------- */
enum { CS_FREE, CS_USED, CS_DONE, CS_FIXED, CS_BAD, CS_READ, CS_WRITE };
extern int fast, autorun, bw;

void ui_init(void);
void ui_exit(void);
void ui_frame(void);                         /* the whole screen, no map */
void ui_map_setup(void);                     /* after eng_open: geometry of the map */
void ui_map_all(int reveal);                 /* draw every cell (reveal: animated sweep) */
void ui_cells(unsigned c, unsigned k, int state);   /* transient r / W */
void ui_refresh(unsigned c, unsigned k);     /* recompute those cells */
void ui_status(unsigned cluster, unsigned pct, const char *method);
void ui_bar(const char *text);               /* bottom status line */
void ui_flush(void);
void ui_pace(uint32_t sectors, unsigned ops);   /* the "disk" time for this work */
int  ui_poll_stop(void);                     /* Esc pressed during a run: ask; 1 = stop */
void ui_tick(void);                          /* elapsed-time refresh */
void ui_timer_start(void);
uint32_t ui_elapsed(void);                   /* seconds */

int  ui_dialog(const char *title, const char *const *lines, int nlines,
               const char *const *radios, int nradios, int *radsel,
               const char *const *buttons, int nbuttons, int defbtn);
void ui_message(const char *title, const char *const *lines, int n);
int  ui_menu(void);                          /* -1 or an item */
int  ui_wait_event(int *key);                /* 1 key, 2 click on the menu bar */
enum { MI_BEGIN, MI_DRIVE, MI_METHOD, MI_SORT, MI_LEGEND, MI_FAST, MI_ABOUT, MI_EXIT };

/* formatting */
char *commas(char *p, uint32_t v);           /* "1,234" */

#endif
