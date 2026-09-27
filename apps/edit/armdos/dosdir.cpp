/*
 * armdos/dosdir.cpp - the Borland RTL directory functions Turbo Vision uses
 * (findfirst/findnext, _dos_findfirst/next, fnsplit/fnmerge, getdisk/setdisk,
 * getcurdir) and driveValid(), done with INT 21h. Replaces magiblot's
 * source/platform/dir.cpp + findfrst.cpp (which emulate them on Unix and
 * Windows). Structures are Turbo Vision's (compat/borland/dos.h, dir.h).
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License (as Turbo Vision).
 */
#define Uses_TStringView
#include <tvision/tv.h>

#include <dos.h>        /* tvision/compat/borland/dos.h */
#include <dir.h>
#include <string.h>
#include <armdos.h>

/* ------------------------------------------------------------- find */

/* DOS keeps the search state in the DTA (43 bytes). Turbo Vision's find_t
 * is bigger than DOS's, so each find_t in use gets a DTA of its own here
 * (like Borland's RTL, a find_t address that is used again reuses its slot). */
struct DosDta
{
    uint8_t reserved[21];
    uint8_t attrib;
    uint16_t time, date;
    uint32_t size;
    char name[13];
} __attribute__((packed));

enum { findSlots = 8 };
static struct { const void *owner; DosDta dta; } slots[findSlots];
static unsigned nextSlot;

static DosDta *slotFor(const void *owner, bool create)
{
    for (auto &s : slots)
        if (s.owner == owner)
            return &s.dta;
    if (!create)
        return 0;
    auto &s = slots[nextSlot++ % findSlots];
    s.owner = owner;
    return &s.dta;
}

static void *getDta()
{
    struct armregs r = {};
    r.r0 = 0x2F00;
    _armdos_int21(&r);
    return (void *) r.r1;
}

static void setDta(void *p)
{
    struct armregs r = {};
    r.r0 = 0x1A00;
    r.r3 = (uint32_t) p;
    _armdos_int21(&r);
}

static void fill(struct find_t *f, const DosDta *d)
{
    f->attrib = d->attrib;
    f->wr_time = d->time;
    f->wr_date = d->date;
    f->size = d->size;
    strncpy(f->name, d->name, 13);
    f->name[12] = 0;
}

unsigned _dos_findfirst( const char *pathname, unsigned attrib,
                         struct find_t *fileinfo ) noexcept
{
    DosDta *d = slotFor(fileinfo, true);
    void *old = getDta();
    setDta(d);
    struct armregs r = {};
    r.r0 = 0x4E00;
    r.r2 = attrib & 0xFFFF;
    r.r3 = (uint32_t) pathname;
    int cf = _armdos_int21(&r);
    setDta(old);
    if (cf)
        return r.r0 & 0xFFFF;
    fill(fileinfo, d);
    return 0;
}

unsigned _dos_findnext( struct find_t *fileinfo ) noexcept
{
    DosDta *d = slotFor(fileinfo, false);
    if (!d)
        return 0x12;
    void *old = getDta();
    setDta(d);
    struct armregs r = {};
    r.r0 = 0x4F00;
    int cf = _armdos_int21(&r);
    setDta(old);
    if (cf)
        return r.r0 & 0xFFFF;
    fill(fileinfo, d);
    return 0;
}

int findfirst( const char *pathname, struct ffblk *ffblk, int attrib ) noexcept
{
    return _dos_findfirst(pathname, attrib, (struct find_t *) ffblk) ? -1 : 0;
}

int findnext( struct ffblk *ffblk ) noexcept
{
    return _dos_findnext((struct find_t *) ffblk) ? -1 : 0;
}

/* ------------------------------------------------------------- drives */

int getdisk() noexcept
{
    struct armregs r = {};
    r.r0 = 0x1900;
    _armdos_int21(&r);
    return r.r0 & 0xFF;
}

int setdisk( int drive ) noexcept
{
    struct armregs r = {};
    r.r0 = 0x0E00;
    r.r3 = drive & 0xFF;
    _armdos_int21(&r);
    return r.r0 & 0xFF;         /* number of logical drives */
}

int getcurdir( int drive, char *direc ) noexcept
{
    /* drive: 0 = default, 1 = A:; result without drive or leading '\' */
    char buf[68];
    struct armregs r = {};
    r.r0 = 0x4700;
    r.r3 = drive & 0xFF;
    r.r4 = (uint32_t) buf;
    if (_armdos_int21(&r))
        return -1;
    strnzcpy(direc, buf, MAXDIR);
    return 0;
}

/* A drive is valid if IOCTL "is removable" (AX=4408h) accepts it. */
Boolean armdosDriveValid( char drive ) noexcept
{
    int d = (drive & 0xDF) - 'A';
    if (d < 0 || d > 25)
        return False;
    struct armregs r = {};
    r.r0 = 0x4408;
    r.r1 = d + 1;
    if (_armdos_int21(&r) == 0)
        return True;
    /* fall back: select it and see if DOS took it */
    int cur = getdisk();
    setdisk(d);
    bool ok = getdisk() == d;
    setdisk(cur);
    return Boolean(ok);
}

/* ------------------------------------------------------------- paths */

static size_t movestr(char *dest, TStringView src, size_t size) noexcept
{
    if (size)
    {
        size_t n = src.size();
        if (n > size - 1)
            n = size - 1;
        memmove(dest, src.data(), n);
        dest[n] = '\0';
        return n;
    }
    return 0;
}

void fnmerge( char *pathP, const char *driveP, const char *dirP,
              const char *nameP, const char *extP ) noexcept
{
    /* Note: fexpand() passes components that overlap pathP (a union), so
     * nothing may be written to pathP before its source has been read. */
    size_t n = 0;
    if (driveP && *driveP)
    {
        n += movestr(&pathP[n], driveP, MAXPATH - n);
        if (pathP[n-1] != ':')
            n += movestr(&pathP[n], ":", MAXPATH - n);
    }
    if (dirP && *dirP)
    {
        n += movestr(&pathP[n], dirP, MAXPATH - n);
        if (pathP[n-1] != '\\' && pathP[n-1] != '/')
            n += movestr(&pathP[n], "\\", MAXPATH - n);
    }
    if (nameP && *nameP)
        n += movestr(&pathP[n], nameP, MAXPATH - n);
    if (extP && *extP)
    {
        if (*extP != '.')
            n += movestr(&pathP[n], ".", MAXPATH - n);
        n += movestr(&pathP[n], extP, MAXPATH - n);
    }
    if (n == 0)
        pathP[0] = 0;
}

static bool copyComponent(char *dst, const char *start, const char *end, size_t max) noexcept
{
    if (end != start)
    {
        if (dst)
            strnzcpy(dst, start, min<size_t>(max, end - start + 1));
        return true;
    }
    return false;
}

int fnsplit( const char *pathP, char *driveP, char *dirP, char *nameP, char *extP ) noexcept
{
    int flags = 0;
    if (driveP) memset(driveP, 0, MAXDRIVE);
    if (dirP) memset(dirP, 0, MAXDIR);
    if (nameP) memset(nameP, 0, MAXFILE);
    if (extP) memset(extP, 0, MAXEXT);
    if (pathP && *pathP)
    {
        size_t len = strlen(pathP);
        const char *pathEnd = pathP + len;
        const char *colonP = 0, *slashP = 0, *lastDotP = 0, *firstDotP = 0;
        for (size_t i = len - 1; i < len; --i)
            switch (pathP[i])
            {
                case '?': case '*':
                    if (!slashP) flags |= WILDCARDS;
                    break;
                case '.':
                    if (!slashP)
                    {
                        if (!lastDotP) lastDotP = pathP + i;
                        firstDotP = pathP + i;
                    }
                    break;
                case '\\': case '/':
                    if (!slashP) slashP = pathP + i;
                    break;
                case ':':
                    if (i == 1) { colonP = pathP + i; i = 0; }
                    break;
            }
        const char *driveEnd = colonP ? colonP + 1 : pathP;
        const char *dirEnd = slashP ? slashP + 1 : driveEnd;
        const char *nameEnd = lastDotP ? lastDotP : pathEnd;
        if (lastDotP == pathEnd - 1 && lastDotP - firstDotP < 2 && firstDotP == dirEnd)
            dirEnd = nameEnd = pathEnd;
        if (copyComponent(driveP, pathP, driveEnd, MAXDRIVE)) flags |= DRIVE;
        if (copyComponent(dirP, driveEnd, dirEnd, MAXDIR)) flags |= DIRECTORY;
        if (copyComponent(nameP, dirEnd, nameEnd, MAXFILE)) flags |= FILENAME;
        if (copyComponent(extP, nameEnd, pathEnd, MAXEXT)) flags |= EXTENSION;
    }
    return flags;
}
