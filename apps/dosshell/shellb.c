/*
 * SHELLB.COM - the Shell's small resident loader.
 *
 *   SHELLB batchname      (DOSSHELL.BAT: "@SHELLB DOSSHELL")
 *
 * Reads the batch file, takes the SHELLC command line from it (the line after
 * ":COMMON"), and runs SHELLC. When SHELLC wants a program run in transient
 * mode it leaves the command lines in our block (shared.h) and exits, so
 * only this loader stays in memory while the program runs; then SHELLC is
 * started again. Ends with errorlevel 255, so the batch file goes to :END
 * (the real SHELLB returned 255 when it could not install, and SHELLC ended
 * the session with a GOTO END; the batch file text is the same).
 */
#include <string.h>
#include "armdos.h"
#include "shared.h"

static struct shellblk blk;
static armdos_vect_t old2f;
#define batch blk.cmds          /* the batch file is read into the (still unused) command area */
static char prog[144];
static char tail[128];

struct psp *_armdos_psp;

/* our own start-up (instead of libdos's, which brings the heap and argv):
   keep only PSP..stack top, run main, exit with its code */
void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    extern int main(void);
    struct armregs r = {0};
    _armdos_psp = psp;
    r.r0 = 0x4A00;
    r.r1 = ((uint32_t)(stacktop - (uint8_t *)psp) + 15) >> 4;
    r.r8 = (uint32_t)psp >> 4;
    _armdos_int21(&r);
    int rc = main();
    memset(&r, 0, sizeof r);
    r.r0 = 0x4C00 | (rc & 0xFF);
    _armdos_int21(&r);
    for (;;) ;
}

static armdos_vect_t getvect(int n)
{
    struct armregs r = {0};
    r.r0 = 0x3500 | n;
    _armdos_int21(&r);
    return (armdos_vect_t)r.r1;
}

static void setvect(int n, armdos_vect_t h)
{
    struct armregs r = {0};
    r.r0 = 0x2500 | n;
    r.r3 = (uint32_t)h;
    _armdos_int21(&r);
}

static void say(const char *s)
{
    struct armregs r = {0};
    r.r0 = 0x4000;
    r.r1 = 2;
    r.r2 = strlen(s);
    r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

static void int2f(struct armregs *f)
{
    if (AH(f) == SHELLB_MUX) {
        if (AL(f) == 0) {
            f->r0 = (f->r0 & ~0xFFu) | 0xFF;
            f->r1 = (uint32_t)&blk;
        }
        return;
    }
    armdos_callold(old2f, f);
}

static int upc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int load_batch(const char *name)
{
    char word[80], full[144];
    int i = 0, dot = 0;
    while (*name && *name != ' ' && *name != '\t' && *name != '/' && i < 70) {
        word[i] = upc(*name++);
        if (word[i] == '.') dot = 1;
        if (word[i] == '\\' || word[i] == ':') dot = 0;
        i++;
    }
    word[i] = 0;
    if (!i) return -1;
    if (!dot) strcpy(word + i, ".BAT");
    if (!find_program(word, full)) return -1;
    struct armregs r = {0};
    r.r0 = 0x3D00;
    r.r3 = (uint32_t)full;
    if (_armdos_int21(&r)) return -1;
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3F00;
    r.r1 = h;
    r.r2 = sizeof batch - 1;
    r.r3 = (uint32_t)batch;
    int n = _armdos_int21(&r) ? 0 : (int)(r.r0 & 0xFFFF);
    batch[n] = 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00;
    r.r1 = h;
    _armdos_int21(&r);
    return n;
}

/* find "SHELLC ..." (after :COMMON if there is one) -> prog, tail */
static int find_shellc(void)
{
    char *p = batch, *best = 0;
    int common = 0;
    while (*p) {
        char *e = p;
        while (*e && *e != '\r' && *e != '\n') e++;
        char c = *e;
        *e = 0;
        char *q = p;
        while (*q == ' ' || *q == '\t' || *q == '@') q++;
        if (*q == ':') {
            char lab[8];
            int k = 0;
            q++;
            while (k < 7 && q[k] && q[k] != ' ') { lab[k] = upc(q[k]); k++; }
            lab[k] = 0;
            if (!strcmp(lab, "COMMON")) common = 1;
        } else {
            char w[80];
            int k = 0;
            while (k < 78 && *q && !strchr(" \t/", *q)) w[k++] = upc(*q++);
            w[k] = 0;
            char *b = strrchr(w, '\\');
            b = b ? b + 1 : (strchr(w, ':') ? strchr(w, ':') + 1 : w);
            if ((!strcmp(b, "SHELLC") || !strcmp(b, "SHELLC.EXE")) && (common || !best)) {
                if (find_program(w, prog) != 1) return -1;
                make_absolute(prog);        /* the programs it runs may change directory */
                strncpy(tail, q, sizeof tail - 1);
                best = p;
                if (common) { *e = c; break; }
            }
        }
        *e = c;
        p = e;
        while (*p == '\r' || *p == '\n' || *p == 0x1A) p++;
    }
    return best ? 0 : -1;
}

int main(void)
{
    const uint8_t *t = (const uint8_t *)_armdos_psp + 0x80;
    char arg[128];
    int n = t[0];
    memcpy(arg, t + 1, n);
    arg[n] = 0;
    char *a = arg;
    while (*a == ' ' || *a == '\t') a++;
    if (load_batch(a) < 0 || find_shellc() < 0) {
        say("Shell batch filename missing or incorrect\r\n");
        return 255;
    }
    blk.magic = SHELLB_MAGIC;
    blk.version = SHELLB_VERSION;
    old2f = getvect(0x2F);
    setvect(0x2F, int2f);
    for (;;) {
        blk.action = SB_EXIT;
        int rc = dos_exec(prog, tail);
        if (rc < 0 || (rc >> 8) != 0) break;       /* not loaded, or ^C / critical error abort */
        if (blk.action == SB_RUN) {
            for (char *c = blk.cmds; *c; c += strlen(c) + 1)
                run_line(c);
        } else if (blk.action == SB_PROMPT) {
            run_comspec("");
        } else
            break;
        if (blk.restarts < 255) blk.restarts++;
    }
    setvect(0x2F, old2f);
    return 255;
}
