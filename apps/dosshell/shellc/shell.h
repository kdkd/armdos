/*
 * shell.h - SHELLC.EXE, the ARM-DOS re-creation of the MS-DOS 4.00 Shell
 * (text mode). See ../README.md.
 */
#ifndef SHELL_H
#define SHELL_H
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "armdos.h"
#include "shared.h"

/* ------------------------------------------------------------ screen ---- */
#define ROWS 25
#define COLS 80
extern uint16_t sb[ROWS * COLS];            /* what the next flush shows */

void s_fill(int r, int c, int h, int w, int ch, int attr);
void s_put(int r, int c, const char *s, int attr);     /* clipped at col 80 */
void s_putn(int r, int c, const char *s, int n, int attr); /* exactly n cells */
void s_ch(int r, int c, int ch, int attr);
void s_attr(int r, int c, int w, int attr);
int  s_getch(int r, int c);
void s_box(int r, int c, int h, int w, int attr);    /* single frame, interior untouched */
void s_hdiv(int r, int c, int w, int attr);          /* ├───┤ */
void s_center(int r, int c, int w, const char *s, int attr);
void s_flush(void);
/* mode 7 (the Hercules/MDA card): the text page is at B0000h and colours are
   shown as normal / bright / reverse video (scr.c; see README) */
#define S_VRAM ((volatile uint16_t *)(*(volatile uint8_t *)0x449 == 7 ? 0xB0000 : 0xB8000))
int s_mono(void);
uint16_t s_cell(uint16_t v);
void s_cursor(int r, int c);        /* r < 0: hide */
void s_init(void);
void s_done(void);
void s_raw_clear(int attr);         /* clear VRAM itself (before running a program) */

/* box characters (CP437) */
#define B_H   0xC4
#define B_V   0xB3
#define B_TL  0xDA
#define B_TR  0xBF
#define B_BL  0xC0
#define B_BR  0xD9
#define B_LT  0xC3
#define B_RT  0xB4
#define B_TT  0xC2
#define B_BT  0xC1
#define G_TRI 0x10          /* ► */
#define G_UP  0x18
#define G_DN  0x19
#define G_LEFT 0x1B
#define G_RIGHT 0x1A

/* ------------------------------------------------------------ colours --- */
enum {
    C_TITLE, C_TITLEBOX, C_BAR, C_BARMN, C_BARSEL, C_BARSELMN,
    C_WORK, C_INSTR, C_ITEM, C_ITEMSEL, C_FKEY,
    C_PULL, C_PULLMN, C_PULLSEL, C_PULLSELMN,
    C_DLG, C_DLGSEL, C_DLGMN, C_HELP, C_HELPSEL, C_ARROW,
    C_WARN, C_WARNSEL, C_SAMPLE, C_PROMPT,
    C_COUNT
};
extern int scheme;                  /* 0..3 = Change Colors 1..4 */
extern const uint8_t schemes[4][C_COUNT];
#define CLR(x) (schemes[scheme][x])
int  colors_load(void);
void colors_save(void);
void change_colors(void);

/* ------------------------------------------------------------- input ---- */
enum { EV_KEY = 1, EV_DOWN, EV_UP, EV_DBL };
struct ev { int type, key, row, col; };
/* keys: BIOS scan<<8 | ascii, E0 prefix folded to 00 */
#define K_ENTER  0x1C0D
#define K_ESC    0x011B
#define K_TAB    0x0F09
#define K_STAB   0x0F00
#define K_BS     0x0E08
#define K_SPACE  0x3920
#define K_UP     0x4800
#define K_DOWN   0x5000
#define K_LEFT   0x4B00
#define K_RIGHT  0x4D00
#define K_HOME   0x4700
#define K_END    0x4F00
#define K_PGUP   0x4900
#define K_PGDN   0x5100
#define K_INS    0x5200
#define K_DEL    0x5300
#define K_CHOME  0x7700
#define K_CEND   0x7500
#define K_F1     0x3B00
#define K_F2     0x3C00
#define K_F3     0x3D00
#define K_F4     0x3E00
#define K_F9     0x4300
#define K_F10    0x4400
#define K_F11    0x8500
#define K_AF1    0x6800
#define K_SF9    0x5C00
#define KASCII(k) ((k) & 0xFF)
#define KSCAN(k)  (((k) >> 8) & 0xFF)
int  ev_get(struct ev *e);          /* waits; updates the clock while idle */
int  key_pending(void);
extern int mouse_ok;
void mouse_show(int on);
extern void (*idle_hook)(void);     /* called about every tick while waiting */

/* --------------------------------------------------------- framework ---- */
typedef void (*drawfn)(void *ctx);
void push_layer(drawfn fn, void *ctx);
void pop_layer(void);
void redraw(void);
extern int title_mode;              /* 0 Start Programs, 1 File System */
void draw_title(const char *title);
void draw_fkeys(const char *text);  /* row 24 */
int  fkey_hit(const char *text, int col);   /* key for a click on the row-24 labels */
void draw_clock(void);

/* action bar and pulldowns */
struct mitem {
    const char *text;       /* NULL = blank separator line */
    int mn;                 /* index of the mnemonic letter in text */
    const char *right;      /* e.g. "F3" */
};
struct menu {
    const char *name;
    int mn;                 /* mnemonic index in name */
    int col;                /* column of the name on row 1 */
    int width;              /* pulldown box width */
    const struct mitem *items;
    int n;
};
typedef int (*availfn)(int menu, int item);
void draw_actionbar(const struct menu *m, int n, int active, int openmenu);
int  run_actionbar(const struct menu *m, int n, availfn avail, int start, int opened, int *helpid);
    /* -> menu<<8 | item, or -1 */

/* dialogs */
enum { DF_TEXT, DF_INPUT, DF_FIXED, DF_RADIO, DF_CHECK, DF_CHOICE, DF_PASSWORD };
struct dfield {
    int type;
    int row, col;           /* relative to the box */
    const char *label;      /* text before the field / the entries for lists ('\n' separated) */
    char *buf;              /* input buffer */
    int max;                /* max length (input) */
    int width;              /* visible width (input) */
    int value;              /* radio: index; check: bit mask; choice: index */
    int help;               /* help topic for F1 */
    int barw;               /* choice/check highlight bar width (0 = text only) */
};
struct dialog {
    const char *title;
    int row, col, h, w;     /* box; row < 0 = centred */
    int color;              /* C_DLG or C_WARN or C_HELP */
    struct dfield *f;
    int nf;
    const char *buttons;    /* the text after the divider, e.g. "  <─┘=Enter   Esc=Cancel   F1=Help" */
    int help;               /* topic when no field has one */
    int focus;              /* field with the cursor */
    int f2;                 /* F2 saves (Add/Change dialogs) */
    int noenter;            /* Enter moves to the next field instead of finishing */
    int tcol;               /* title column in the box (0 = centred) */
};
int  dialog_run(struct dialog *d);  /* K_ENTER, K_ESC or K_F2 */
void dialog_draw(void *d);
int  message(const char *text, int help);          /* error/warning box, returns key */
int  choose2(const char *title, const char *text, const char *c1, const char *c2, int help);
    /* numbered choice box: 1, 2 or 0 (Esc) */
void status_box(const char *title, const char *line1, const char *line2);   /* progress text */
int  warn_choice(const char *text, const char *c1, const char *c2, int help);   /* 1, 2 or 0 */
extern const char ENTERGLYPH[];     /* "<─┘" */
extern const char BTN_ENTER[];      /* "  <─┘=Enter   Esc=Cancel   F1=Help" */

/* help */
void help_show(int topic, const char *title, const char *text);  /* topic >= 0: SHELL.HLP */
void help_item(const char *title, const char *text);
enum {                      /* topics in SHELL.HLP (data/help.txt, same order) */
    H_KEYS, H_HELPHELP, H_SPINSTR, H_FSINSTR, H_MOUSE, H_KBD, H_PROGRAM, H_GROUP, H_SPEXIT,
    H_START, H_ADDPROG, H_CHGPROG, H_DELPROG, H_COPYPROG, H_ADDGRP, H_CHGGRP, H_DELGRP,
    H_REORDER, H_EXITSHELL, H_RESUMESP, H_FILE, H_OPTIONS, H_ARRANGE, H_FSEXIT, H_OPEN,
    H_PRINT, H_ASSOC, H_MOVE, H_COPY, H_DELETE, H_RENAME, H_ATTR, H_VIEW, H_MKDIR,
    H_SELALL, H_DESELALL, H_DISPOPT, H_FILEOPT, H_SHOWINFO, H_SINGLE, H_MULTIPLE, H_SYSTEM,
    H_EXITFS, H_RESUMEFS, H_TITLE, H_PASSWORD, H_HELPTEXT, H_COMMANDS, H_GRPFILE,
    H_COLORS, H_DELITEM, H_NODELITEM, H_CONFDEL, H_CONFREP, H_SELACROSS, H_NAME,
    H_SORTNAME, H_SORTEXT, H_SORTDATE, H_SORTSIZE, H_SORTDISK, H_HIDDEN, H_READONLY,
    H_ARCHIVE, H_ONEATATIME, H_ALLATONCE, H_NODELDIR, H_DELDIR, H_SKIP, H_REPLACE,
    H_SKIPDEL, H_DELFILE, H_DRIVE, H_RENDIR, H_COPYTO, H_MOVETO, H_NEWNAME, H_OPENOPT,
    H_NEWDIR, H_TREE, H_FILELIST, H_SELECTING, H_ASSOCEXT, H_VIEWHELP, H_DELFILES,
    H_MOVEFROM, H_COPYFROM, H_MESSAGE, H_INTRO, H_PARAMS, H_ITEMS,
    H_COUNT
};

/* ------------------------------------------------------------- state ---- */
struct opts {
    unsigned menu:1, dos:1, color:1, exit:1, prompt:1, maint:1, mul:1, date:1, tran:1, snd:1;
    char meu[80];           /* /MEU: main group file */
    char clr[80];           /* /CLR: colour file */
    char asc[80];           /* /ASC: association file */
    char home[80];          /* directory SHELLC was started in (files are found there) */
};
extern struct opts opt;
extern struct shellblk *sblk;       /* SHELLB's block, or NULL (SHELLC stays resident) */

/* start programs */
void startprog(void);               /* the Start Programs screen (main loop) */
int  sp_run(void);                  /* returns when the Shell should exit */

/* file system: returns 0 = back to Start Programs, 1 = exit the Shell */
int  filesys(void);

/* running programs */
void launch(const char *cmds, int from_fs);  /* NUL separated lines, "" end */
void command_prompt(void);
void shell_exit(void);              /* leave the Shell (clears the screen) */
extern int returning;               /* restarted by SHELLB after a program */

/* persistent state across SHELLB restarts */
struct savestate {
    uint8_t screen;         /* 0 Start Programs, 1 File System, 2 Change Colors */
    uint8_t group;          /* 0 main, 1 = in the sub-group below */
    uint8_t mainsel, subsel;
    uint8_t fs_from_sp;     /* the File System was entered from Start Programs */
    uint8_t press_enter;    /* show "Press Enter (<─┘) to return to File System." */
    char    subfile[13];
    char    subtitle[41];
    char    cwd[68];        /* current directory when the program was started */
    uint8_t fs[200];        /* File System state (fs.c) */
};
extern struct savestate st;
void state_save(void);

/* utilities */
int  dos_getcwd(int drive, char *buf);  /* "C:\DIR" */
int  dos_curdrive(void);
void dos_setdrive(int d);
int  dos_chdir(const char *p);
int  file_read(const char *name, void *buf, int max);
int  file_write(const char *name, const void *buf, int len);
void home_path(char *out, const char *name);   /* home dir + name */
void beep(void);
char *fmt_num(char *out, uint32_t v, int commas); /* returns out */
int  upc(int c);
void strupr_(char *s);

/* .MEU files (meu.c) */
#define MEU_MAX     16
#define MEU_TITLE   40
#define MEU_HELP    478
#define MEU_CMD     500
struct meuitem {
    char title[MEU_TITLE + 1];
    uint8_t isprog;         /* 1 program, 0 group */
    char password[9];
    char cmd[MEU_CMD + 1];  /* program startup command, or the group's .MEU file name */
    char help[MEU_HELP + 1];
};
struct meugroup {
    int n;
    struct meuitem it[MEU_MAX];
};
int  meu_load(const char *file, struct meugroup *g);
int  meu_save(const char *file, const struct meugroup *g);

/* program startup commands (psc.c): builds the command lines; 0 = cancelled */
int  psc_expand(const char *title, const char *psc, char *out, int outsize);

/* help text wrapping */
int  wrap_text(const char *text, int width, const char **lines, int *lens, int max);

#endif
