/*
 * acm.h - ACMAIN.EXE (the ARM Commander) internals.
 */
#ifndef ACM_H
#define ACM_H
#include <stdint.h>
#include <string.h>
#include "armdos.h"
#include "shared.h"

#define SCR_W 80
#define SCR_H 25
#define CMDROW 23
#define KEYROW 24

/* ---- keys (INT 16h AH=10h codes, E0 prefixes normalised) ---- */
#define K_ENTER   0x1C0D
#define K_CENTER  0x1C0A        /* Ctrl-Enter */
#define K_ESC     0x011B
#define K_TAB     0x0F09
#define K_STAB    0x0F00
#define K_BS      0x0E08
#define K_UP      0x4800
#define K_DOWN    0x5000
#define K_LEFT    0x4B00
#define K_RIGHT   0x4D00
#define K_HOME    0x4700
#define K_END     0x4F00
#define K_PGUP    0x4900
#define K_PGDN    0x5100
#define K_INS     0x5200
#define K_DEL     0x5300
#define K_CPGUP   0x8400
#define K_CPGDN   0x7600
#define K_CHOME   0x7700
#define K_CEND    0x7500
#define K_CLEFT   0x7300
#define K_CRIGHT  0x7400
#define K_F(n)    ((0x3A + (n)) << 8)       /* F1..F10 */
#define K_SF(n)   ((0x53 + (n)) << 8)
#define K_CF(n)   ((0x5D + (n)) << 8)
#define K_AF(n)   ((0x67 + (n)) << 8)
#define K_GPLUS   0x4E2B
#define K_GMINUS  0x4A2D
#define K_GSTAR   0x372A
#define K_CTRL(c) (((c) - 'A' + 1))           /* compare low byte only */
#define K_MOUSE   0x10000

#define ISKEY(k, c)  ((k) == (c))

/* mouse event (K_MOUSE) */
extern int ms_x, ms_y, ms_btn, ms_dbl;   /* cell, 1 = left 2 = right, double click */
extern int mouse_ok;

/* ---- attributes ---- */
#define A_PANEL   0x1B      /* light cyan on blue: frames and file names */
#define A_HEAD    0x1E      /* yellow on blue: column headers */
#define A_SEL     0x1E      /* selected file */
#define A_CURSOR  0x30      /* black on cyan: cursor bar */
#define A_CURSEL  0x3E      /* selected file under the cursor */
#define A_CMD     0x07
#define A_KEYNUM  0x07
#define A_KEYLBL  0x30
#define A_MENU    0x30
#define A_MENUHOT 0x3E
#define A_MENUSEL 0x0F
#define A_MENUSELHOT 0x0E
#define A_SHADOW  0x07

/* ---- screen (scr.c) ---- */
extern uint16_t sb[SCR_W * SCR_H];      /* what is being composed */
extern uint16_t usr[SCR_W * SCR_H];     /* the user (DOS) screen behind the panels */
void scr_init(void);
void flush(void);
void cursor_at(int x, int y);           /* y < 0: hide */
void put(int x, int y, const char *s, int attr);
void putn(int x, int y, const char *s, int n, int attr);  /* pad/truncate to n */
void putc_(int x, int y, int ch, int attr);
void fill(int x, int y, int w, int h, int ch, int attr);
void setattr(int x, int y, int w, int attr);
void box(int x, int y, int w, int h, int attr, int dbl);
void shadow(int x, int y, int w, int h);
void puthot(int x, int y, const char *s, int attr, int hot);   /* '&' marks the hot letter */
int  strwidth_hot(const char *s);
void save_scr(uint16_t *to);
void restore_scr(const uint16_t *from);

/* keyboard / mouse */
int  getkey(void);
int  key_ready(void);
int  shift_state(void);
extern void (*idle_hook)(void);
void mouse_show(int on);
int  wait_release(void);

/* dialogs */
typedef struct {
    uint8_t text, border, title, input, button, fbutton;
} pal_t;
extern const pal_t PAL_GREY, PAL_CYAN, PAL_RED;

enum { DI_TEXT, DI_CTEXT, DI_INPUT, DI_CHECK, DI_BUTTON, DI_HLINE };
typedef struct {
    uint8_t type;
    int8_t  x, y;           /* relative to the box; x < 0 for DI_BUTTON = auto-centre the row */
    uint8_t w;              /* input width */
    const char *text;
    char   *buf;            /* input text */
    int     max;            /* input capacity (incl. NUL) */
    int    *val;            /* checkbox */
} ditem;
int dialog(const char *title, int w, int h, const pal_t *pal, ditem *it, int n, int focus);
void dlg_frame(int bx, int by, int w, int h, const char *title, const pal_t *pal);
void msgbox_draw(const char *title, const char **lines, int nl, const pal_t *pal, int *bx, int *by, int *bw);
int  message(const char *title, const char *l1, const char *l2, const pal_t *pal, const char *const *buttons, int nb);
void error_box(const char *l1, const char *l2);
int  edit_line(char *buf, int max, int *pos, int key);
int  listbox(const char *title, const char *const *lines, int n, int w, int h, int *sel, const pal_t *pal, const char *foot);
void keybar_draw(const char *const *labels);

/* ---- DOS (dos.c) ---- */
int  d_open(const char *p, int mode);
int  d_creat(const char *p, int attr);
int  d_read(int h, void *b, int n);
int  d_write(int h, const void *b, int n);
void d_close(int h);
int  d_getattr(const char *p);
int  d_setattr(const char *p, int a);
int  d_unlink(const char *p);
int  d_rmdir(const char *p);
int  d_mkdir(const char *p);
int  d_rename(const char *a, const char *b);
int  d_chdir(const char *p);
int  d_getdrive(void);                  /* 0 = A */
void d_setdrive(int d);
int  d_getcwd(int drive, char *out);    /* "C:\DOS"; 0 ok */
int  d_diskfree(int drive, uint32_t *total, uint32_t *freeb);
int  d_getftime(int h, uint16_t *t, uint16_t *d);
int  d_setftime(int h, uint16_t t, uint16_t d);
int  d_findfirst(const char *spec, int attr, void *dta);
int  d_findnext(void *dta);
int  d_drive_valid(int d);
uint32_t d_filesize(int h);
int  d_seek(int h, uint32_t pos);
extern int doserr;                      /* last DOS error code */
extern int crit_err;                    /* set by our INT 24h handler */
const char *dos_errmsg(int e);
void dos_hooks(void);
uint32_t mem_free_for_child(void);

struct dta {
    uint8_t  res[21];
    uint8_t  attr;
    uint16_t time, date;
    uint32_t size;
    char     name[13];
} __attribute__((packed));

/* ---- helpers (dos.c) ---- */
void commas(char *out, uint32_t v);
int  wildmatch(const char *pat, const char *name);
void path_join(char *out, const char *dir, const char *name);
void str_upper(char *s);
void str_lower(char *s);
const char *basename_(const char *p);
int  is_exec(const char *name);
void fmt_date(char *o, uint16_t d);
void fmt_time(char *o, uint16_t t);
void u2s(char *o, uint32_t v);
void u2s_pad(char *o, uint32_t v, int w);

/* ---- panels (panel.c) ---- */
typedef struct {
    char     name[13];
    uint8_t  attr;
    uint8_t  sel;
    uint8_t  pad;
    uint16_t time, date;
    uint32_t size;
} fent;

typedef struct {
    char    name[13];
    uint8_t depth;
    uint8_t last;           /* last child of its parent */
    uint8_t pad;
    int16_t parent;
} tent;

enum { M_BRIEF, M_FULL, M_INFO, M_TREE };
enum { S_NAME, S_EXT, S_TIME, S_SIZE, S_UNSORTED };

typedef struct {
    struct acpanel *st;     /* mode, sort, visible, path, filter, cur, top */
    int     x;              /* 0 or 40 */
    fent   *f;
    int     n, cap;
    int     nsel;
    uint32_t selbytes;
    uint32_t totbytes;
    int     nfiles;
    tent   *t;              /* tree */
    int     tn, tcap, tcur, ttop;
    char    treedrive;
} panel;

extern panel P[2];
extern struct acstate *S;
#define ACT (&P[S->active])
#define OTH (&P[!S->active])
int  list_rows(void);
void panel_read(panel *p, const char *keepname);
void panel_reread(panel *p);
void panel_draw(panel *p, int active);
void panel_sort(panel *p);
void panel_fix(panel *p);
int  panel_per_page(panel *p);
void panel_move(panel *p, int key);
void panel_goto_name(panel *p, const char *name);
void panel_setpath(panel *p, const char *path, const char *keepname);
void panel_selcount(panel *p);
fent *panel_curfile(panel *p);
int  panel_hit(panel *p, int x, int y);   /* index under a screen cell or -1 */
void tree_read(panel *p);
void tree_path(panel *p, int i, char *out);
void tree_goto_path(panel *p, const char *path);

/* ---- operations (ops.c) ---- */
void op_copy(int move);
void op_mkdir(void);
void op_delete(void);
void op_select(int how);        /* 1 = grey +, -1 = grey -, 0 = invert */
void op_compare(void);
void op_findfile(void);
void op_history(void);
void op_filter(panel *p);
void op_drive(panel *p);
void op_config(void);
void op_attrib(void);

/* ---- viewer / editor ---- */
void view_file(const char *path);
void edit_file(const char *path);

/* ---- menus (menu.c) ---- */
int  menu_run(int which);        /* returns a command id or 0 */
void help_show(void);
void usermenu(void);

/* ---- main.c ---- */
extern char cmdline[128];
extern int  cmdpos;
extern int  standalone;
extern char progdir[80];
void redraw(void);
void do_command(int id);
void execute(const char *cmd);
void hist_add(const char *c);
void panels_reread_all(void);
void set_active_dir(void);
void save_setup(void);

/* command ids (menus, mouse on the key bar) */
enum {
    C_NONE, C_HELP, C_USERMENU, C_VIEW, C_EDIT, C_COPY, C_RENMOV, C_MKDIR, C_DELETE,
    C_PULLDN, C_QUIT, C_BRIEF_L, C_FULL_L, C_INFO_L, C_TREE_L, C_ONOFF_L,
    C_SNAME_L, C_SEXT_L, C_STIME_L, C_SSIZE_L, C_SUNS_L, C_REREAD_L, C_FILTER_L, C_DRIVE_L,
    C_BRIEF_R, C_FULL_R, C_INFO_R, C_TREE_R, C_ONOFF_R,
    C_SNAME_R, C_SEXT_R, C_STIME_R, C_SSIZE_R, C_SUNS_R, C_REREAD_R, C_FILTER_R, C_DRIVE_R,
    C_SELECT, C_UNSELECT, C_INVERT, C_ATTRIB, C_FIND, C_HISTORY, C_SWAP, C_PANELS,
    C_COMPARE, C_CONFIG, C_KEYBAR, C_MINISTATUS, C_CLOCK, C_SAVESETUP, C_HIDDEN, C_EDITNEW,
};

#endif
