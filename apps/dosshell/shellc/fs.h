/* fs.h - File System internals shared by fs.c and fsops.c */
#ifndef FS_H
#define FS_H
#include "shell.h"

#define MAXDIRS  512
#define MAXFILES 1500
#define MAXSEL   512

struct dnode {
    char name[13];
    int16_t parent;
    uint8_t depth;
    uint8_t last;           /* last child of its parent */
    uint16_t nfiles;
    uint32_t bytes;
};

struct fent {
    char name[13];
    uint8_t attr;
    uint16_t date, time;
    uint32_t size;
    uint16_t dir;           /* index in the tree (system file list) */
    uint16_t order;         /* disk order */
};

struct pane {
    int drive;              /* 0 = A: */
    struct dnode *dirs;
    int ndirs, cur, tcur, ttop;     /* cur: the directory listed (►); tcur: the tree cursor */
    struct fent *files;
    int nfiles, fcur, ftop;
    int loaded;
    uint32_t disk_files, disk_bytes;
};

enum { AR_SINGLE, AR_MULTIPLE, AR_SYSTEM };
enum { SORT_NAME, SORT_EXT, SORT_DATE, SORT_SIZE, SORT_DISK };

struct fsopts {
    int arrange;
    int sort;
    char mask[13];
    int confirm_delete, confirm_replace, across;
};

extern struct pane panes[2];
extern int active;          /* the pane with the focus (multiple file list) */
extern int focus;           /* 0 drives, 1 tree, 2 files */
extern struct fsopts fso;
extern const uint8_t fs_colors[4][3];
#define FSC(i) (fs_colors[scheme][i])

/* selection: full paths */
extern char (*selset)[80];
extern int nsel;
int  sel_has(const char *path);
void sel_toggle(const char *path);
void sel_clear(void);
void file_path(struct pane *p, int i, char *out);  /* full path of files[i] */
void dir_path(struct pane *p, int d, char *out);    /* "C:\DOS" */

void pane_load(struct pane *p, int drive);
void pane_files(struct pane *p);
int  pane_find_dir(struct pane *p, const char *path);
void fs_refresh(void);          /* re-read after a change */

/* fsops.c */
int  fs_file_action(int item);  /* the File menu; 1 = the screen must be re-read */
void fs_view(struct pane *p, int i);
void fs_open(struct pane *p, int i);
int  fs_display_options(void);
void fs_file_options(void);
void fs_show_info(void);
void fs_info_block(int r, int c, int attr, int ncol, int vcol);
int  fs_selected_count(uint32_t *bytes);
extern int crit_error;
const char *dos_errtext(int err);

#endif
