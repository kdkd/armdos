/*
 * help.c - the help panels (F1), the help index (F11 / Alt+F1) and the key
 * assignments (F9). Topics come from SHELL.HLP (built from data/help.txt by
 * tools/mkhlp.mjs): "ADSHHLP", 0, u16 count, then count x {u16 offset,
 * u16 length} of "Title\0text" records, '&' = new line in the text.
 */
#include "shell.h"

#define HROW 13
#define HW 49
#define HH 11
#define TEXTW 43
#define TEXTROWS 5

static char htext[2600];
static char htitle[48];
static int hloaded = -1;

static int load_topic(int topic)
{
    static uint8_t hdr[10 + 4 * (H_COUNT + 8)];
    char path[96];
    if (hloaded == topic) return 0;
    home_path(path, "SHELL.HLP");
    int n = file_read(path, hdr, sizeof hdr);
    if (n < 10 || memcmp(hdr, "ADSHHLP", 7)) return -1;
    int cnt = hdr[8] | (hdr[9] << 8);
    if (topic < 0 || topic >= cnt || 10 + topic * 4 + 4 > n) return -1;
    unsigned off = hdr[10 + topic * 4] | (hdr[11 + topic * 4] << 8);
    unsigned len = hdr[12 + topic * 4] | (hdr[13 + topic * 4] << 8);
    static char rec[2700];
    if (len > sizeof rec - 1) len = sizeof rec - 1;
    /* read the record: open, seek, read */
    struct armregs r = {0};
    r.r0 = 0x3D00;
    r.r3 = (uint32_t)path;
    if (_armdos_int21(&r)) return -1;
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4200;
    r.r1 = h;
    r.r2 = 0;
    r.r3 = off;
    _armdos_int21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x3F00;
    r.r1 = h;
    r.r2 = len;
    r.r3 = (uint32_t)rec;
    int got = _armdos_int21(&r) ? 0 : (int)(r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00;
    r.r1 = h;
    _armdos_int21(&r);
    rec[got] = 0;
    strncpy(htitle, rec, sizeof htitle - 1);
    const char *t = rec + strlen(rec) + 1;
    if (t > rec + got) t = "";
    strncpy(htext, t, sizeof htext - 1);
    hloaded = topic;
    return 0;
}

/* word-wrap: '&' forces a line break; returns the number of lines */
int wrap_text(const char *text, int width, const char **lines, int *lens, int max)
{
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        const char *s = p;
        int len = 0, lastsp = -1;
        while (s[len] && s[len] != '&' && len < width + 1) {
            if (s[len] == ' ') lastsp = len;
            len++;
        }
        if (!s[len] || s[len] == '&') {
            lines[n] = s;
            lens[n++] = len;
            p = s + len;
            if (*p == '&') p++;
            continue;
        }
        /* too long: break at the last blank */
        int cut = lastsp > 0 ? lastsp : width;
        lines[n] = s;
        lens[n] = cut;
        while (lens[n] > 0 && s[lens[n] - 1] == ' ') lens[n]--;
        n++;
        p = s + cut;
        while (*p == ' ') p++;
    }
    return n;
}

struct hctx {
    const char *title;
    const char *lines[160];
    int lens[160];
    int n, top;
    int index;              /* the index: lines are topics, sel = highlighted */
    int sel;
    int col;
};

/* the index: the six instruction topics, then all the others by title */
static const int index_first[] = { H_INTRO, H_SELECTING, H_SPINSTR, H_FSINSTR, H_KBD, H_MOUSE };
#define NFIRST 6
#define NINDEX (H_COUNT - 2)            /* all but H_MESSAGE and H_ITEMS */
static int index_topics[NINDEX];
static char index_titles[NINDEX][41];
static int index_built;

static void help_draw(void *p)
{
    struct hctx *h = p;
    int at = CLR(C_HELP), c = h->col;
    s_fill(HROW, c, HH, HW, ' ', at);
    s_box(HROW, c, HH, HW, at);
    int tl = strlen(h->title);
    s_put(HROW + 1, c + 1 + (HW - 3 - tl) / 2, h->title, at);
    int more_up = h->top > 0, more_dn = h->top + TEXTROWS < h->n;
    if (h->index) {
        more_up = h->sel > 0;
        more_dn = h->sel < h->n - 1;
    }
    s_put(HROW + 2, c + 37, "More:", at);
    if (more_up) s_ch(HROW + 2, c + 42, G_UP, at);
    if (more_dn) s_ch(HROW + 2, c + 44, G_DN, at);
    for (int i = 0; i < TEXTROWS; i++) {
        int k = h->top + i;
        if (k >= h->n) break;
        int x = c + (h->index ? 3 : 2);
        int sel = h->index && k == h->sel;
        if (sel) s_fill(HROW + 3 + i, c + 2, 1, HW - 4, ' ', CLR(C_HELPSEL));
        for (int j = 0; j < h->lens[k]; j++)
            s_ch(HROW + 3 + i, x + j, (uint8_t)h->lines[k][j], sel ? CLR(C_HELPSEL) : at);
    }
    s_hdiv(HROW + 8, c, HW, at);
    s_put(HROW + 9, c + 1, "  Esc=Cancel   F1=Help   F11=Index   F9=Keys   ", at);
}

static int help_col(void) { return 15; }

/* The help panels form a small stack, as in the real Shell: the context help
   is at the bottom; F9 puts the key assignments on top of it; F1 (Help on
   Help) and F11 (the index) replace the panel on top, and so does a topic
   chosen in the index; Esc takes the top panel away (so Esc on a topic
   reached through F11 closes the help, Esc on Help on Help reached through
   F9 goes back to the context help). */
enum { HP_TEXT, HP_TOPIC, HP_INDEX };
struct hpanel { int kind, topic; const char *title, *text; };
static struct hpanel hstack[4];
static int hdepth;
static struct hctx hc;
static char ptitle[48], ptext[2600];

static int build_index(void)
{
    if (index_built) return 0;
    int n = 0;
    for (int i = 0; i < NFIRST; i++) index_topics[n++] = index_first[i];
    for (int t = 0; t < H_COUNT; t++) {
        int skip = t == H_MESSAGE || t == H_ITEMS;
        for (int i = 0; i < NFIRST; i++) if (index_first[i] == t) skip = 1;
        if (!skip && n < NINDEX) index_topics[n++] = t;
    }
    for (int i = 0; i < n; i++) {
        if (load_topic(index_topics[i]) < 0) return -1;
        strncpy(index_titles[i], htitle, 40);
    }
    for (int i = NFIRST; i < n; i++)            /* the rest by title */
        for (int j = i; j > NFIRST && strcmp(index_titles[j - 1], index_titles[j]) > 0; j--) {
            char tt[41];
            int ti = index_topics[j];
            memcpy(tt, index_titles[j], 41);
            memcpy(index_titles[j], index_titles[j - 1], 41);
            memcpy(index_titles[j - 1], tt, 41);
            index_topics[j] = index_topics[j - 1];
            index_topics[j - 1] = ti;
        }
    index_built = 1;
    return 0;
}

/* make hc show the panel on top of the stack; -1 = the help file is missing */
static int enter_panel(void)
{
    struct hpanel *p = &hstack[hdepth - 1];
    memset(&hc, 0, sizeof hc);
    hc.col = help_col();
    if (p->kind == HP_INDEX) {
        if (build_index() < 0) return -1;
        hc.title = "Indexed Help Selections";
        hc.index = 1;
        for (int i = 0; i < NINDEX; i++) {
            hc.lines[i] = index_titles[i];
            hc.lens[i] = strlen(index_titles[i]);
        }
        hc.n = NINDEX;
        return 0;
    }
    if (p->kind == HP_TOPIC) {
        if (load_topic(p->topic) < 0) return -1;
        strcpy(ptitle, htitle);
        strcpy(ptext, htext);
        hc.title = ptitle;
        hc.n = wrap_text(ptext, TEXTW, hc.lines, hc.lens, 160);
        return 0;
    }
    hc.title = p->title;
    hc.n = wrap_text(p->text, TEXTW, hc.lines, hc.lens, 160);
    return 0;
}

static void help_run(void)
{
    push_layer(help_draw, &hc);
    while (hdepth > 0) {
        if (enter_panel() < 0) {
            message("The help file is missing or unreadable. Insert the Shell disk, then press F1.", H_MESSAGE);
            break;
        }
        int next = 0;                   /* 1 = the stack changed */
        while (!next) {
            redraw();
            struct ev e;
            ev_get(&e);
            int k = 0;
            if (e.type == EV_KEY) k = e.key;
            else if (e.type == EV_DOWN || e.type == EV_DBL) {
                int c = hc.col;
                if (e.row == HROW + 9)
                    k = fkey_hit("  Esc=Cancel   F1=Help   F11=Index   F9=Keys   ", e.col - c - 1);
                else if (e.row == HROW + 2 && e.col == c + 42) k = hc.index ? K_UP : K_PGUP;
                else if (e.row == HROW + 2 && e.col == c + 44) k = hc.index ? K_DOWN : K_PGDN;
                else if (hc.index && e.row >= HROW + 3 && e.row < HROW + 3 + TEXTROWS &&
                         e.col > c && e.col < c + HW - 1) {
                    int s = hc.top + e.row - HROW - 3;
                    if (s < hc.n) {
                        hc.sel = s;
                        if (e.type == EV_DBL) k = K_ENTER;
                    }
                } else if (e.row < HROW || e.row >= HROW + HH || e.col < c || e.col >= c + HW)
                    k = K_ESC;
            }
            if (!k) continue;
            struct hpanel *top = &hstack[hdepth - 1];
            if (k == K_ESC) { hdepth--; next = 1; continue; }
            if (k == K_F1) { top->kind = HP_TOPIC; top->topic = H_HELPHELP; next = 1; continue; }
            if (k == K_F11 || k == K_AF1) { top->kind = HP_INDEX; next = 1; continue; }
            if (k == K_F9) {
                if (top->kind == HP_TOPIC && top->topic == H_KEYS) continue;
                if (hdepth < 4) {
                    hstack[hdepth].kind = HP_TOPIC;
                    hstack[hdepth].topic = H_KEYS;
                    hdepth++;
                    next = 1;
                }
                continue;
            }
            if (hc.index) {
                if (k == K_UP && hc.sel > 0) hc.sel--;
                else if (k == K_DOWN && hc.sel < hc.n - 1) hc.sel++;
                else if (k == K_PGUP) { hc.sel -= TEXTROWS; if (hc.sel < 0) hc.sel = 0; }
                else if (k == K_PGDN) { hc.sel += TEXTROWS; if (hc.sel > hc.n - 1) hc.sel = hc.n - 1; }
                else if (k == K_CHOME || k == K_HOME) hc.sel = 0;
                else if (k == K_CEND || k == K_END) hc.sel = hc.n - 1;
                else if (k == K_ENTER) { top->kind = HP_TOPIC; top->topic = index_topics[hc.sel]; next = 1; continue; }
                if (hc.sel < hc.top) hc.top = hc.sel;
                if (hc.sel >= hc.top + TEXTROWS) hc.top = hc.sel - TEXTROWS + 1;
                continue;
            }
            if (k == K_PGDN || k == K_DOWN) {
                if (hc.top + TEXTROWS < hc.n) hc.top += k == K_PGDN ? TEXTROWS : 1;
                if (hc.top + TEXTROWS > hc.n && hc.n > TEXTROWS) hc.top = hc.n - TEXTROWS;
            } else if (k == K_PGUP || k == K_UP) {
                hc.top -= k == K_PGUP ? TEXTROWS : 1;
                if (hc.top < 0) hc.top = 0;
            } else if (k == K_CHOME) hc.top = 0;
            else if (k == K_CEND) hc.top = hc.n > TEXTROWS ? hc.n - TEXTROWS : 0;
        }
    }
    hdepth = 0;
    pop_layer();
}

/* help for a topic in SHELL.HLP; or for an item (title + text from the .MEU) */
void help_show(int topic, const char *title, const char *text)
{
    s_cursor(-1, 0);
    hdepth = 1;
    if (text) {
        hstack[0].kind = HP_TEXT;
        hstack[0].title = title;
        hstack[0].text = text;
    } else {
        hstack[0].kind = HP_TOPIC;
        hstack[0].topic = topic;
    }
    help_run();
}

void help_item(const char *title, const char *text)
{
    help_show(-1, title, text);
}
