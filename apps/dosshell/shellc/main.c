/*
 * main.c - SHELLC start-up: the switches DOSSHELL.BAT passes, the files,
 * SHELLB, the Ctrl-C and critical error handlers, and the screen loop.
 *
 *   SHELLC /TRAN/COLOR/DOS/MENU/MUL/SND/MEU:SHELL.MEU/CLR:SHELL.CLR/PROMPT/MAINT/EXIT/SWAP/DATE
 */
#include <stdio.h>
#include <stdlib.h>
#include "shell.h"

struct opts opt;
struct shellblk *sblk;
struct savestate st;
int returning;

extern void mouse_init(void);
extern void mouse_done(void);

static void say(const char *s)
{
    struct armregs r = {0};
    r.r0 = 0x4000;
    r.r1 = 1;
    r.r2 = strlen(s);
    r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

int upc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
void strupr_(char *s) { for (; *s; s++) *s = upc(*s); }

char *fmt_num(char *out, uint32_t v, int commas)
{
    char t[16];
    int n = 0, k = 0;
    do {
        if (commas && k && k % 3 == 0) t[n++] = ',';
        t[n++] = '0' + v % 10;
        v /= 10;
        k++;
    } while (v);
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    out[n] = 0;
    return out;
}

int dos_curdrive(void)
{
    struct armregs r = {0};
    r.r0 = 0x1900;
    _armdos_int21(&r);
    return r.r0 & 0xFF;
}

void dos_setdrive(int d)
{
    struct armregs r = {0};
    r.r0 = 0x0E00;
    r.r3 = d;
    _armdos_int21(&r);
}

int dos_getcwd(int drive, char *buf)
{
    struct armregs r = {0};
    buf[0] = 'A' + drive;
    buf[1] = ':';
    buf[2] = '\\';
    r.r0 = 0x4700;
    r.r3 = drive + 1;
    r.r4 = (uint32_t)(buf + 3);
    if (_armdos_int21(&r)) { buf[3] = 0; return -1; }
    return 0;
}

int dos_chdir(const char *p)
{
    struct armregs r = {0};
    r.r0 = 0x3B00;
    r.r3 = (uint32_t)p;
    return _armdos_int21(&r) ? -1 : 0;
}

int file_read(const char *name, void *buf, int max)
{
    struct armregs r = {0};
    r.r0 = 0x3D00;
    r.r3 = (uint32_t)name;
    if (_armdos_int21(&r)) return -1;
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3F00;
    r.r1 = h;
    r.r2 = max;
    r.r3 = (uint32_t)buf;
    int n = _armdos_int21(&r) ? -1 : (int)(r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00;
    r.r1 = h;
    _armdos_int21(&r);
    return n;
}

int file_write(const char *name, const void *buf, int len)
{
    struct armregs r = {0};
    r.r0 = 0x3C00;
    r.r2 = 0;
    r.r3 = (uint32_t)name;
    if (_armdos_int21(&r)) return -1;
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4000;
    r.r1 = h;
    r.r2 = len;
    r.r3 = (uint32_t)buf;
    int ok = !_armdos_int21(&r) && (int)(r.r0 & 0xFFFF) == len;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00;
    r.r1 = h;
    _armdos_int21(&r);
    return ok ? 0 : -1;
}

void home_path(char *out, const char *name)
{
    if (name[0] && name[1] == ':') { strcpy(out, name); return; }
    if (name[0] == '\\') { out[0] = opt.home[0]; out[1] = ':'; strcpy(out + 2, name); return; }
    strcpy(out, opt.home);
    int n = strlen(out);
    if (out[n - 1] != '\\') out[n++] = '\\';
    strcpy(out + n, name);
}

/* ------------------------------------------------ Ctrl-C, critical errors */
int crit_error = -1;                /* last critical error code (DI), or -1 */

static void int23(struct armregs *f)
{
    f->cpsr &= ~ARM_CPSR_C;         /* ignore Ctrl-C / Ctrl-Break */
}

static void int24(struct armregs *f)
{
    crit_error = f->r5 & 0xFF;
    f->r0 = (f->r0 & ~0xFFu) | 3;   /* fail the call; the Shell shows its own message */
}

void set_handlers(void)
{
    struct armregs r = {0};
    r.r0 = 0x2523;
    r.r3 = (uint32_t)int23;
    _armdos_int21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x2524;
    r.r3 = (uint32_t)int24;
    _armdos_int21(&r);
    memset(&r, 0, sizeof r);        /* BREAK OFF while the Shell runs (DOSSHELL.BAT does it too) */
    r.r0 = 0x3301;
    r.r3 = 0;
    _armdos_int21(&r);
}

/* ----------------------------------------------------------- switches ---- */
static int parse_switches(const char *t)
{
    strcpy(opt.meu, "SHELL.MEU");
    strcpy(opt.clr, "SHELL.CLR");
    strcpy(opt.asc, "SHELL.ASC");
    while (*t) {
        while (*t == ' ' || *t == '\t') t++;
        if (!*t) break;
        if (*t != '/') return -1;
        t++;
        char sw[16];
        int n = 0;
        while (*t && *t != '/' && *t != ':' && *t != ' ' && n < 15) sw[n++] = upc(*t++);
        sw[n] = 0;
        char val[80];
        val[0] = 0;
        if (*t == ':') {
            t++;
            n = 0;
            while (*t && *t != '/' && *t != ' ' && n < 79) val[n++] = upc(*t++);
            val[n] = 0;
        }
        static const char *const ignored[] = {
            "TEXT", "SWAP", "CO1", "CO2", "CO3", "COM2", "LF", "MOS", "B", "PRE", "REF", "NROOT",
            "DBCS", "ENH", "PRO", "CONFIRMDELETEON", "CONFIRMDELETEOFF", "CONFIRMREPLACEON",
            "CONFIRMREPLACEOFF", "ALLOWSELECTON", "ALLOWSELECTOFF", "SORTBYNAME", "SORTBYEXT",
            "SORTBYSIZE", "SORTBYDATE", "SORTBYDISK", 0 };
        if (!strcmp(sw, "MENU")) opt.menu = 1;
        else if (!strcmp(sw, "DOS")) opt.dos = 1;
        else if (!strcmp(sw, "COLOR")) opt.color = 1;
        else if (!strcmp(sw, "EXIT")) opt.exit = 1;
        else if (!strcmp(sw, "PROMPT")) opt.prompt = 1;
        else if (!strcmp(sw, "MAINT")) opt.maint = 1;
        else if (!strcmp(sw, "MUL")) opt.mul = 1;
        else if (!strcmp(sw, "DATE")) opt.date = 1;
        else if (!strcmp(sw, "TRAN")) opt.tran = 1;
        else if (!strcmp(sw, "SND")) opt.snd = 1;
        else if (!strcmp(sw, "MEU")) { if (val[0]) strcpy(opt.meu, val); }
        else if (!strcmp(sw, "CLR")) { if (val[0]) strcpy(opt.clr, val); }
        else if (!strcmp(sw, "ASC")) { if (val[0]) strcpy(opt.asc, val); }
        else {
            int ok = 0;
            for (int i = 0; ignored[i]; i++) if (!strcmp(sw, ignored[i])) ok = 1;
            if (!ok) return -1;
        }
    }
    return 0;
}

/* -------------------------------------------------------------- state ---- */
_Static_assert(sizeof(struct savestate) <= SB_STATESIZE, "the saved state must fit SHELLB's block");

void state_save(void)
{
    if (!sblk) return;
    memcpy(sblk->state, &st, sizeof st < SB_STATESIZE ? sizeof st : SB_STATESIZE);
    sblk->statelen = sizeof st;
}

static void find_shellb(void)
{
    struct armregs r = {0};
    r.r0 = SHELLB_MUX << 8;
    _armdos_int2f(&r);
    if ((r.r0 & 0xFF) != 0xFF) return;
    struct shellblk *b = (struct shellblk *)r.r1;
    if (!b || b->magic != SHELLB_MAGIC || b->version != SHELLB_VERSION) return;
    sblk = b;
}

void wait_enter_prompt(void)
{
    /* the program's output stays; row 24 asks for Enter */
    static const char msg[] = " Press Enter (<\xC4\xC4\xD9) to return to File System.";
    volatile uint16_t *v = S_VRAM;
    struct armregs r = {0};
    r.r0 = 0x0601;                  /* scroll the screen up one line first if row 24 is used */
    for (int c = 0; c < 80; c++)
        if ((v[24 * 80 + c] & 0xFF) != ' ') {
            r.r1 = 0x0700;
            r.r2 = 0;
            r.r3 = (24 << 8) | 79;
            _armdos_int10(&r);
            break;
        }
    for (int c = 0; c < 80; c++) v[24 * 80 + c] = 0x0720;
    for (int c = 0; msg[c]; c++) v[24 * 80 + c] = 0x0700 | (uint8_t)msg[c];
    s_cursor(-1, 0);
    for (;;) {
        struct ev e;
        ev_get(&e);
        if (e.type == EV_KEY && e.key == K_ENTER) break;
        if ((e.type == EV_DOWN || e.type == EV_DBL) && e.row == 24 && e.col < 46) break;
    }
}

int main(int argc, char **argv)
{
    extern struct psp *_armdos_psp;
    const uint8_t *t = (const uint8_t *)_armdos_psp + 0x80;
    char tail[128];
    (void)argc; (void)argv;
    memcpy(tail, t + 1, t[0]);
    tail[t[0]] = 0;
    if (parse_switches(tail) < 0) {
        say("Incorrect startup option.\r\n");
        return 1;
    }
    if (!opt.menu && !opt.dos) {
        say("Start Programs and File System not active.\r\n");
        return 1;
    }
    if (!getenv_dos("COMSPEC")) {
        say("COMSPEC missing in DOS Environment.\r\n");
        return 1;
    }
    /* the Shell's files are where SHELLC.EXE is (DOSSHELL.BAT's CD C:\DOS) */
    {
        extern const char *_armdos_progpath;
        const char *pp = _armdos_progpath;
        const char *bs = pp ? strrchr(pp, '\\') : 0;
        if (bs && pp[1] == ':' && bs - pp < 78) {
            memcpy(opt.home, pp, bs - pp);
            opt.home[bs - pp] = 0;
            if (bs - pp == 2) strcat(opt.home, "\\");
            strupr_(opt.home);
        } else
            dos_getcwd(dos_curdrive(), opt.home);
    }
    find_shellb();
    if (!opt.tran) sblk = 0;        /* resident mode: we run the programs ourselves */
    if (colors_load() < 0) {
        say("Color file missing or unreadable.\r\n");
        return 1;
    }
    set_handlers();
    if (sblk && sblk->restarts && sblk->statelen == sizeof st) {
        memcpy(&st, sblk->state, sizeof st);
        returning = 1;
    }
    mouse_init();
    s_init();
    if (returning && st.press_enter) wait_enter_prompt();
    st.press_enter = 0;
    if (returning && st.cwd[0]) {   /* back where we were: the program may have changed directory */
        dos_setdrive(st.cwd[0] - 'A');
        dos_chdir(st.cwd);
    }
    if (!opt.menu) st.screen = 1;
    for (;;) {
        if (st.screen == 1) {
            int r = filesys();
            if (r == 1 || !opt.menu) break;
            st.screen = 0;
            continue;
        }
        sp_run();
        break;
    }
    shell_exit();
    return 0;
}
