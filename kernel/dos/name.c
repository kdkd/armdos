/*
 * name.c - file names: canonical paths (TRUENAME, DOS/PATH.ASM TransPath),
 * 8.3 components in FCB form, wildcard matching (MetaCompare) and the
 * AH=29h filename parser (FCB.ASM MakeFcb).
 */
#include "dos.h"

/* the file-name upper-case table is the country's (misc.c nls.ucase: DOS 4's
   UCASE and FILE_UCASE tables are the same for every country and code page) */
uint8_t dos_upcase(uint8_t c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0x80) return nls.ucase[2 + c - 0x80];
    return c;
}

/* characters that may not appear in a file name component */
int valid_fchar(int c)
{
    if (c <= 0x20) return 0;
    switch (c) {
    case '.': case '"': case '/': case '\\': case '[': case ']': case ':':
    case '|': case '<': case '>': case '+': case '=': case ';': case ',':
        return 0;
    }
    return 1;
}

static int is_sep(int c) { return c == '\\' || c == '/'; }

/* one path component ("NAME.EXT", '*' allowed if wild) -> FCB form.
   Returns 0, 1 if it has wildcards, -1 if invalid */
int name_to_fcb(const char *s, char *n11)
{
    int i = 0, wild = 0;
    memset(n11, ' ', 11);
    const char *p = s;
    if (*p == '.') return -1;
    while (*p && *p != '.') {
        uint8_t c = dos_upcase(*p++);
        if (c == '*') { while (i < 8) n11[i++] = '?'; wild = 1; continue; }
        if (c == '?') wild = 1;
        else if (!valid_fchar(c) && c != ' ') return -1;   /* 4.00 packs blanks into the name */
        if (i < 8) n11[i++] = c;
    }
    if (*p == '.') {
        p++;
        i = 8;
        while (*p) {
            uint8_t c = dos_upcase(*p++);
            if (c == '*') { while (i < 11) n11[i++] = '?'; wild = 1; continue; }
            if (c == '?') wild = 1;
            else if (!valid_fchar(c) && c != ' ') return -1;
            if (i < 11) n11[i++] = c;
        }
    }
    if (n11[0] == ' ') return -1;
    if (n11[0] == (char)0xE5) n11[0] = 0x05;
    return wild;
}

void fcb_to_name(const char *n11, char *out)
{
    int k = 0, nl = 8, el = 11;
    while (nl > 0 && n11[nl - 1] == ' ') nl--;      /* only trailing blanks are padding */
    while (el > 8 && n11[el - 1] == ' ') el--;
    for (int i = 0; i < nl; i++) out[k++] = (i == 0 && n11[0] == 0x05) ? (char)0xE5 : n11[i];
    if (el > 8) {
        out[k++] = '.';
        for (int i = 8; i < el; i++) out[k++] = n11[i];
    }
    out[k] = 0;
}

int has_wild(const char *n11)
{
    for (int i = 0; i < 11; i++) if (n11[i] == '?') return 1;
    return 0;
}

int name_match(const char *pat, const char *name)
{
    for (int i = 0; i < 11; i++) {
        if (pat[i] == '?') continue;
        if (pat[i] != name[i]) return 0;
    }
    return 1;
}

/* length of the root part of a CDS text: "C:\" for a drive, the SUBST
   target ("C:\WORK", cbEnd = 7) for a substituted one */
int cds_rootlen(const struct cds *c)
{
    return c->bsoffset <= 2 ? 3 : c->bsoffset;
}

/* a drive letter a program may name (valid, and not hidden by JOIN) */
int drive_usable(int drive)
{
    struct cds *c = get_cds(drive);
    return c && (c->flags & CDS_VALID) && !(c->flags & CDS_JOIN);
}

char canon_prejoin[84];         /* the last canon_path: after SUBST, before JOIN */
int canon_ldrive;               /* ... and the drive letter the program named */

/*
 * Canonical form of a path: "C:\DIR\NAME.EXT", upper case, "." and ".."
 * resolved, components cut to 8.3.  Wildcards only in the last component
 * (allow_wild).  Returns 0, or -E_BADDRIVE / -E_NOPATH / -E_NOFILE.
 * A trailing separator is dropped.
 *
 * SUBST and JOIN as DOS 4 (TransPath): the path is built on the drive the
 * program named, from that drive's current directory (the CDS text after
 * cbEnd); a SUBST drive then maps to its target's text; finally a path
 * inside a JOIN directory moves to the joined drive's root.  *pdrive and
 * out are the physical result.
 */
int canon_path(const char *in, char *out, int *pdrive, int allow_wild)
{
    const uint8_t *p = (const uint8_t *)in;
    int drive = cur_drive;
    if (p[0] && p[1] == ':') {
        uint8_t c = dos_upcase(p[0]);
        if (c < 'A' || c > 'Z') return -E_BADDRIVE;
        drive = c - 'A';
        p += 2;
    }
    struct cds *cds = get_cds(drive);
    if (!cds || !(cds->flags & CDS_VALID) || (cds->flags & CDS_JOIN)) return -E_BADDRIVE;

    /* the logical path "X:\..." */
    char buf[140];
    int len;
    buf[0] = 'A' + drive; buf[1] = ':'; buf[2] = '\\'; buf[3] = 0;
    len = 3;
    if (is_sep(*p)) {
        while (is_sep(*p)) p++;
    } else {
        const char *rest = cds->path + cds_rootlen(cds);
        while (*rest == '\\') rest++;
        len += strlcpy(buf + 3, rest, sizeof buf - 3);
        if (len > 3 && buf[len - 1] == '\\') buf[--len] = 0;
    }

    while (*p) {
        /* one component */
        char comp[64];
        int n = 0;
        while (*p && !is_sep(*p)) { if (n < 63) comp[n++] = *p; p++; }
        comp[n] = 0;
        int last = 1;
        const uint8_t *q = p;
        while (is_sep(*q)) q++;
        if (*q) last = 0;
        p = q;
        if (!n) continue;
        if (!strcmp(comp, ".")) continue;
        if (!strcmp(comp, "..")) {
            if (len <= 3) return -E_NOPATH;
            while (len > 3 && buf[len - 1] != '\\') len--;
            if (len > 3) len--;
            buf[len] = 0;
            continue;
        }
        char n11[11];
        int w = name_to_fcb(comp, n11);
        if (w < 0) return last ? -E_NOFILE : -E_NOPATH;
        if (w && (!allow_wild || !last)) return last ? -E_NOFILE : -E_NOPATH;
        char nm[13];
        if (n11[0] == 0x05) n11[0] = 0xE5;
        fcb_to_name(n11, nm);
        if (n11[0] == (char)0xE5) nm[0] = 0xE5;
        int nl = strlen(nm);
        if (len + (len > 3) + nl > 66 + 12) return -E_NOPATH;
        if (len > 3) buf[len++] = '\\';
        memcpy(buf + len, nm, nl + 1);
        len += nl;
    }

    /* SUBST: the drive's root is its target */
    char phys[140];
    int pdrv = drive;
    if (cds->flags & CDS_SUBST) {
        int rl = cds_rootlen(cds);
        memcpy(phys, cds->path, rl);
        int k = rl;
        if (buf[3]) {
            if (phys[k - 1] != '\\') phys[k++] = '\\';
            strlcpy(phys + k, buf + 3, sizeof phys - k);
        } else phys[k] = 0;
        if (k < 3) { phys[2] = '\\'; phys[3] = 0; }
        pdrv = (uint8_t)phys[0] - 'A';
    } else strcpy(phys, buf);
    if (strlen(phys) > 67 + 12) return -E_NOPATH;
    strcpy(canon_prejoin, phys);
    canon_ldrive = drive;

    /* JOIN: a path at or below a join directory belongs to the joined drive */
    for (int i = 0; i < n_cds; i++) {
        struct cds *j = &cds_tab[i];
        if ((j->flags & (CDS_VALID | CDS_JOIN)) != (CDS_VALID | CDS_JOIN)) continue;
        int jl = strlen(j->path);
        if (jl < 3 || strncmp(phys, j->path, jl) || (phys[jl] && phys[jl] != '\\')) continue;
        char t[140];
        t[0] = 'A' + i; t[1] = ':'; t[2] = '\\';
        strlcpy(t + 3, phys[jl] ? phys + jl + 1 : "", sizeof t - 3);
        strcpy(phys, t);
        pdrv = i;
        break;
    }
    if (pdrive) *pdrive = pdrv;
    strcpy(out, phys);
    return 0;
}

/* ------------------------------------------------------ AH=29h */

static int fcb_term(int c)
{
    if (c < ' ') return 1;
    switch (c) {
    case ' ': case '\t': case '.': case '"': case '/': case '\\': case '[': case ']':
    case ':': case '|': case '<': case '>': case '+': case '=': case ';': case ',':
        return 1;
    }
    return 0;
}

int parse_fcb_name(struct armregs *f)
{
    const uint8_t *p = (const uint8_t *)f->r4;
    uint8_t *fcb = (uint8_t *)f->r5;
    int flags = AL(f), ret = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (flags & 1) {
        if (*p == ':' || *p == '.' || *p == ';' || *p == ',' || *p == '=' || *p == '+') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
        }
    }
    if (p[0] && p[1] == ':' && dos_upcase(p[0]) >= 'A' && dos_upcase(p[0]) <= 'Z') {
        int d = dos_upcase(p[0]) - 'A';
        struct cds *c = get_cds(d);
        if (!c || !(c->flags & CDS_VALID)) ret = 0xFF;
        fcb[0] = d + 1;
        p += 2;
    } else if (!(flags & 2)) fcb[0] = 0;

    /* name */
    int i = 0, got = 0;
    char nm[8];
    memset(nm, ' ', 8);
    while (!fcb_term(*p)) {
        uint8_t c = dos_upcase(*p);
        if (c == '*') { while (i < 8) nm[i++] = '?'; p++; got = 1; continue; }
        if (i < 8) nm[i++] = c;
        got = 1;
        p++;
    }
    if (got || !(flags & 4)) memcpy(fcb + 1, nm, 8);
    /* extension */
    char ex[3];
    memset(ex, ' ', 3);
    got = 0;
    if (*p == '.') {
        p++;
        i = 0;
        while (!fcb_term(*p)) {
            uint8_t c = dos_upcase(*p);
            if (c == '*') { while (i < 3) ex[i++] = '?'; p++; got = 1; continue; }
            if (i < 3) ex[i++] = c;
            got = 1;
            p++;
        }
        got = 1;
    }
    if (got || !(flags & 8)) memcpy(fcb + 9, ex, 3);
    if (ret != 0xFF)
        for (i = 1; i < 12; i++) if (fcb[i] == '?') ret = 1;
    f->r4 = (uint32_t)p;
    return ret;
}
