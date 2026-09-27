/*
 * tcomp.c - TC.EXE's compiler: TinyCC (apps/tcc/src, LGPL-2.1: the ARM-DOS
 * port with its MZ+AR1 .EXE writer) compiled into the IDE as libtcc, the
 * way Turbo C's integrated compiler lived inside TC.EXE. No EXEC: the IDE
 * calls tcc_new / tcc_add_file / tcc_output_file directly, collects the
 * errors and warnings through tcc_set_error_func, and watches the source
 * being read (for the "Lines compiled" counter of the Compiling box)
 * through a hook on read().
 *
 * This file is the only one of TC.EXE that sees TinyCC's internals; it is
 * LGPL-2.1 like TinyCC (the IDE itself is MIT).
 */
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

/* every read() TinyCC does goes through tc_read (source buffers, 8 KB) */
static int tc_read(int fd, void *buf, unsigned n);
#define read(fd, buf, n) tc_read(fd, buf, n)

#define ONE_SOURCE 1
#include "libtcc.c"

#undef read
#undef total_lines     /* TinyCC's TCC_STATE_VAR shorthand: we name the field */
#undef free            /* tcc.h reserves the C allocator for itself */
#undef malloc
#undef realloc
#undef strdup

#include "tcomp.h"

static TcJob *job;

/* ---- what TCC.EXE's main (tcc.c) provides and libtcc needs ---- */

static char tc_libdir[260] = "C:/TC";
const char *tcc_armdos_dir(void)
{
    return tc_libdir;
}

/* "TCC -run" support: TC.EXE runs programs with EXEC, not in memory */
/* (tccrun.c declares the table's type inside a function: define it by its
 * assembler name) */
const void *const tc_no_hostsyms[2] __asm__("tcc_armdos_hostsyms") = { 0, 0 };

/* ---- progress ---- */

static int tc_read(int fd, void *buf, unsigned n)
{
    if (job && job->progress && tcc_state && file)
    {
        BufferedFile *f;
        long total = tcc_state->total_lines, main = 0;
        const char *name = file->filename;
        for (f = file; f; f = f->prev)
        {
            total += f->line_num > 0 ? f->line_num - 1 : 0;
            if (!f->prev)
                main = f->line_num > 0 ? f->line_num - 1 : 0;
        }
        job->progress(job->ctx, name, total, main);
    }
    return read(fd, buf, n);
}

/* ---- messages ---- */

static void tc_error(void *opaque, const char *msg)
{
    TcJob *j = opaque;
    const char *p, *last = msg;
    char fname[260];
    int line = 0, kind = TCM_ERROR;
    const char *text;

    /* "In file included from X:1:\n" lines come first: the last line is it */
    for (p = msg; *p; p++)
        if (*p == '\n' && p[1])
            last = p + 1;
    fname[0] = 0;
    text = last;
    /* FILE:LINE: error: text  |  FILE: error: text  |  tcc: error: text */
    {
        const char *e = strstr(last, ": error: ");
        const char *w = strstr(last, ": warning: ");
        const char *m = e ? e : w;
        if (w && (!e || w < e)) m = w;
        if (m)
        {
            kind = (m == w) ? TCM_WARNING : TCM_ERROR;
            text = m + (kind == TCM_WARNING ? 11 : 9);
            /* the location before it */
            {
                const char *colon = 0, *q;
                int len = (int)(m - last);
                for (q = m - 1; q > last; q--)
                    if (*q == ':')
                    {
                        colon = q;
                        break;
                    }
                if (colon && colon[1] >= '0' && colon[1] <= '9' && colon > last + 1)
                {
                    line = atoi(colon + 1);
                    len = (int)(colon - last);
                }
                if (len > 0 && len < (int)sizeof fname && strncmp(last, "tcc", len))
                {
                    memcpy(fname, last, len);
                    fname[len] = 0;
                }
            }
        }
    }
    if (kind == TCM_WARNING)
        j->warns++;
    else
        j->errors++;
    if (j->message)
        j->message(j->ctx, kind, fname[0] ? fname : 0, line, text);
}

/* ---- the driver ---- */

static char *deps_copy[65];

static void add_paths(TCCState *s, const char *list, int lib)
{
    char buf[260];
    const char *p = list;
    while (p && *p)
    {
        const char *e = strchr(p, ';');
        int n = e ? (int)(e - p) : (int)strlen(p);
        while (n > 0 && p[n - 1] == ' ') n--;
        while (n > 0 && *p == ' ') { p++; n--; }
        if (n > 0 && n < (int)sizeof buf)
        {
            memcpy(buf, p, n);
            buf[n] = 0;
            normalize_slashes(buf);
            if (lib)
                tcc_add_library_path(s, buf);
            else
                tcc_add_sysinclude_path(s, buf);
        }
        p = e ? e + 1 : 0;
    }
}

static void add_defines(TCCState *s, const char *list)
{
    char buf[128];
    const char *p = list;
    while (p && *p)
    {
        const char *e = strchr(p, ';');
        int n = e ? (int)(e - p) : (int)strlen(p);
        while (n > 0 && p[n - 1] == ' ') n--;
        while (n > 0 && *p == ' ') { p++; n--; }
        if (n > 0 && n < (int)sizeof buf)
        {
            memcpy(buf, p, n);
            buf[n] = 0;
            tcc_define_symbol(s, buf, 0);
        }
        p = e ? e + 1 : 0;
    }
}

int tc_compile(TcJob *j)
{
    TCCState *s;
    int i, ret = 0;

    for (i = 0; deps_copy[i]; i++)
    {
        free(deps_copy[i]);
        deps_copy[i] = 0;
    }
    j->errors = j->warns = 0;
    j->lines = 0;
    j->outsize = 0;
    j->deps = deps_copy;
    job = j;

    if (j->tcdir && *j->tcdir)
    {
        strncpy(tc_libdir, j->tcdir, sizeof tc_libdir - 1);
        tc_libdir[sizeof tc_libdir - 1] = 0;
        normalize_slashes(tc_libdir);
        i = strlen(tc_libdir);
        if (i > 1 && tc_libdir[i - 1] == '/')
            tc_libdir[i - 1] = 0;
    }

    s = tcc_new();
    if (!s)
    {
        j->errors = 1;
        if (j->message)
            j->message(j->ctx, TCM_ERROR, 0, 0, "Not enough memory");
        job = 0;
        return 1;
    }
    tcc_set_error_func(s, j, tc_error);
    /* the IDE's include directories replace TinyCC's {B}/include */
    tcc_set_options(s, "-nostdinc");
    if (j->warnings == 0)
        tcc_set_options(s, "-w");
    else if (j->warnings == 2)
        tcc_set_options(s, "-Wall");
    if (j->warnerror)
        tcc_set_options(s, "-Werror");
    if (j->stack)
    {
        char opt[40];
        sprintf(opt, "-Wl,--stack=%u", j->stack);
        tcc_set_options(s, opt);
    }
    s->gen_deps = 1;            /* collect the user's headers (for Make) */
    add_paths(s, j->incdirs, 0);
    add_paths(s, j->libdirs, 1);
    add_defines(s, j->defines);
    if (tcc_set_output_type(s, j->objonly ? TCC_OUTPUT_OBJ : TCC_OUTPUT_EXE) < 0)
        ret = -1;
    /* sources first, then objects, then libraries (DOS linker order) */
    for (i = 0; ret == 0 && i < j->nfiles; i++)
    {
        const char *ext = tcc_fileextension(j->files[i]);
        if (!strcasecmp(ext, ".o") || !strcasecmp(ext, ".a"))
            continue;
        if (tcc_add_file(s, j->files[i]) < 0)
            ret = -1;
    }
    for (i = 0; ret == 0 && i < j->nfiles; i++)
        if (!strcasecmp(tcc_fileextension(j->files[i]), ".o"))
            if (tcc_add_file(s, j->files[i]) < 0)
                ret = -1;
    for (i = 0; ret == 0 && i < j->nfiles; i++)
        if (!strcasecmp(tcc_fileextension(j->files[i]), ".a"))
            if (tcc_add_file(s, j->files[i]) < 0)
                ret = -1;
    j->lines = s->total_lines;
    if (ret == 0 && s->nb_errors == 0)
    {
        if (j->progress)
            j->progress(j->ctx, 0, j->lines, -1);       /* linking */
        if (tcc_output_file(s, j->out) < 0)
            ret = -1;
    }
    if (ret == 0 && s->nb_errors == 0)
    {
        struct stat st;
        if (stat(j->out, &st) == 0)
            j->outsize = st.st_size;
    }
    for (i = 0; i < s->nb_target_deps && i < 64; i++)
        deps_copy[i] = strdup(s->target_deps[i]);
    deps_copy[i] = 0;
    if (s->nb_errors && !j->errors)
        j->errors = s->nb_errors;
    if (ret < 0 && !j->errors)
        j->errors = 1;
    tcc_delete(s);
    job = 0;
    return j->errors;
}
