/*
 * find.c - INT 21h AH=4Eh/4Fh (DOS/SEARCH.ASM).  All the search state lives
 * in the 21 reserved bytes of the DTA (INC/FIND.INC), so find-next needs no
 * kernel memory and any number of searches can be interleaved.
 */
#include "dos.h"

static void fill_found(struct find_dta *t, const struct dirent *de)
{
    t->attr = de->attr;
    t->time = de->time;
    t->date = de->date;
    t->size = de->size;
    char n[13];
    fcb_to_name(de->name, n);
    memset(t->name, 0, sizeof t->name);
    strcpy(t->name, n);
}

static int search(struct find_dta *t)
{
    int err;
    if (t->index == 0xFFFF) return -E_NOMORE;
    struct dpb *d = drive_dpb((t->drive & 0x1F) - 1, &err);
    if (!d) return -err;
    struct dirent de;
    int r = dir_find(d, t->dircluster, t->pattern, t->sattr, t->index, &de, 0, 0);
    if (r == -E_NOFILE) { t->index = 0xFFFF; return -E_NOMORE; }
    if (r < 0) return r;
    t->index = r + 1;
    fill_found(t, &de);
    return 0;
}

void find_first(struct armregs *f)
{
    struct find_dta *t = (struct find_dta *)cur_dta;
    struct pathinfo pi;
    const char *path = (const char *)f->r3;
    if (path[0] && path[1] == ':' && !path[2]) {
        /* a bare "d:" names nothing: fails before the drive is touched (4.00) */
        sys_err(f, E_NOPATH);
        return;
    }
    int e = resolve(path, &pi, 1);
    if (e < 0) { sys_err(f, -e); return; }
    memset(t, 0, 21);
    if (pi.dev) {
        /* a device name "finds" the device */
        t->drive = pi.drive + 1;
        memcpy(t->pattern, pi.name11, 11);
        t->index = 0xFFFF;
        struct dirent de;
        memset(&de, 0, sizeof de);
        memcpy(de.name, pi.name11, 8);
        memset(de.name + 8, ' ', 3);
        de.attr = 0x40;
        STAMP_NOW(&de);
        fill_found(t, &de);
        t->attr = 0x40;
        return;
    }
    if (pi.isroot) {
        /* a bare root ("C:\\") names no entry: 4.00 fails it (JOIN relies on this) */
        sys_err(f, E_NOPATH);
        return;
    }
    if (pi.remote) {                    /* 111Bh fills the whole DTA */
        e = redir_find(&pi, CL(f), (uint8_t *)t);
        if (e == -E_NOFILE) e = -E_NOMORE;      /* no match: 12h, as for a local drive */
        if (e < 0) sys_err(f, -e);
        return;
    }
    t->drive = pi.drive + 1;
    memcpy(t->pattern, pi.name11, 11);
    t->sattr = CL(f);
    t->index = 0;
    t->dircluster = pi.dircl;
    e = search(t);
    if (e < 0) sys_err(f, -e);
}

void find_next(struct armregs *f)
{
    struct find_dta *t = (struct find_dta *)cur_dta;
    if (t->drive & 0x80) {              /* a redirector's search (111Ch) */
        int e = redir_find(0, 0, (uint8_t *)t);
        if (e < 0) sys_err(f, -e);
        return;
    }
    if (!(t->drive & 0x1F) || (t->drive & 0x1F) > 26) { sys_err(f, E_NOMORE); return; }
    int e = search(t);
    if (e < 0) sys_err(f, -e);
}
