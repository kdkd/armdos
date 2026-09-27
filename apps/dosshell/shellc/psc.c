/*
 * psc.c - program startup commands (the Commands field of a program item).
 *
 * Lines are separated by the F4 marker (BAh). A "[...]" in a line pops a
 * dialog and is replaced by what the user types:
 *   /t"title"  /i"instruction"  /p"prompt" (default "Parameters . .  ")
 *   /d"default"  /r (typing replaces the default)  /l"n" (max length)
 *   /f"..." (the answer must name an existing file)  /m /c /# /@ (accepted)
 * "[]" gives the plain "Program Parameters" dialog. %1..%9 insert the answer
 * of an earlier bracket again.
 */
#include "shell.h"

struct pscdlg {
    char title[41], instr[41], prompt[21], def[128];
    int remove, maxlen, mustexist;
};

static const char *qstr(const char *p, char *out, int max, int *err)
{
    int n = 0;
    if (*p != '"') { *err = 1; return p; }
    p++;
    while (*p && *p != '"') {
        if (n < max) out[n++] = *p;
        else *err = 2;
        p++;
    }
    out[n] = 0;
    if (*p != '"') { *err = 1; return p; }
    return p + 1;
}

static int ask(struct pscdlg *pd, char *answer, int item_help)
{
    struct dfield f[3];
    int w = 44, h = 10;
    const char *prompt = pd->prompt[0] ? pd->prompt : "Parameters . .  ";
    int width = 16;
    int fl = strlen(prompt) + width + 2;
    memset(f, 0, sizeof f);
    f[0].type = DF_TEXT;
    f[0].row = 3;
    f[0].label = pd->instr;
    f[0].col = 1 + (w - 2 - (int)strlen(pd->instr) + 1) / 2;
    f[1].type = DF_INPUT;
    if (pd->maxlen && pd->maxlen <= width) {    /* /l"n": a field of that size */
        f[1].type = DF_FIXED;
        width = pd->maxlen;
        fl = strlen(prompt) + width + 2;
    }
    f[1].row = 5;
    f[1].col = 1 + (w - 2 - fl) / 2;
    f[1].label = prompt;
    f[1].buf = answer;
    f[1].max = pd->maxlen ? pd->maxlen : 127;
    f[1].width = width;
    f[1].value = pd->remove;
    f[1].help = H_PARAMS;
    strcpy(answer, pd->def);
    struct dialog d = { pd->title, 12, 27, h, w, C_DLG, f, 2, BTN_ENTER, item_help, 1, 0, 0 };
    for (;;) {
        if (dialog_run(&d) != K_ENTER) return 0;
        if (pd->mustexist) {
            struct armregs r = {0};
            r.r0 = 0x4300;
            r.r3 = (uint32_t)answer;
            if (_armdos_int21(&r)) { message("File not found.", H_MESSAGE); continue; }
        }
        return 1;
    }
}

/* returns 1 with out = NUL separated lines ending in an empty one */
int psc_expand(const char *title, const char *psc, char *out, int outsize)
{
    char answers[9][128];
    int nans = 0, o = 0;
    (void)title;
    memset(answers, 0, sizeof answers);
    const char *p = psc;
    while (*p) {
        char line[260];
        int l = 0;
        while (*p && (uint8_t)*p != 0xBA) {
            if (*p == '[') {
                struct pscdlg pd;
                int err = 0;
                memset(&pd, 0, sizeof pd);
                p++;
                if (*p == ']') {
                    strcpy(pd.title, "Program Parameters");
                    strcpy(pd.instr, "Type the parameters, then press Enter.");
                }
                while (*p && *p != ']' && !err) {
                    if (*p == ' ') { p++; continue; }
                    if (*p != '/') { err = 3; break; }
                    int c = p[1];
                    p += 2;
                    char tmp[128];
                    switch (c) {
                    case 't': case 'T': p = qstr(p, pd.title, 40, &err); if (err == 2) err = 4; break;
                    case 'i': case 'I': p = qstr(p, pd.instr, 40, &err); if (err == 2) err = 5; break;
                    case 'p': case 'P': p = qstr(p, pd.prompt, 20, &err); if (err == 2) err = 6; break;
                    case 'd': case 'D': p = qstr(p, pd.def, 127, &err); break;
                    case 'l': case 'L': p = qstr(p, tmp, 4, &err); pd.maxlen = atoi(tmp); break;
                    case 'f': case 'F': p = qstr(p, tmp, 127, &err); pd.mustexist = 1; break;
                    case 'm': case 'M': case 'c': case 'C': p = qstr(p, tmp, 127, &err); break;
                    case 'r': case 'R': pd.remove = 1; break;
                    case '#': case '@': break;
                    default: err = 3;
                    }
                }
                if (err || *p != ']') {
                    message(err == 1 ? "Quotation mark missing in Program Startup Command." :
                            err == 2 ? "Default value in Program Startup Command too long." :
                            err == 4 ? "Title in Program Startup Command too long." :
                            err == 5 ? "Instruction in Program Startup Command too long." :
                            err == 6 ? "Prompt in Program Startup Command too long." :
                            *p != ']' ? "Brackets missing in Program Startup Command." :
                            "Character invalid in Program Startup Command.", H_MESSAGE);
                    return 0;
                }
                p++;
                if (!pd.title[0]) strcpy(pd.title, "Program Parameters");
                if (nans >= 9) nans = 8;
                if (!ask(&pd, answers[nans], H_PARAMS)) return 0;
                for (const char *a = answers[nans]; *a && l < 250; ) line[l++] = *a++;
                nans++;
                continue;
            }
            if (*p == '%' && p[1] >= '1' && p[1] <= '9') {
                int k = p[1] - '1';
                if (k >= nans) { message("Parameter uninitialized in Program Startup Commmand.", H_MESSAGE); return 0; }
                for (const char *a = answers[k]; *a && l < 250; ) line[l++] = *a++;
                p += 2;
                continue;
            }
            if (l < 250) line[l++] = *p;
            p++;
        }
        if ((uint8_t)*p == 0xBA) p++;
        line[l] = 0;
        while (l && line[l - 1] == ' ') line[--l] = 0;
        if (!l) continue;
        if (o + l + 2 >= outsize) {
            message("Program Startup Command exceeds buffer size.", H_MESSAGE);
            return 0;
        }
        memcpy(out + o, line, l + 1);
        o += l + 1;
    }
    out[o] = 0;
    return 1;
}
