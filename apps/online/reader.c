/* reader.c - the document reader: scrolling text as it arrives, highlighted links you
 * follow with Tab/Enter, a link number or the mouse, Back through the history. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "online.h"

#define TOP 2
#define ROWS 21
#define MAXHIST 12

static struct doc *hist[MAXHIST];
static int nhist;
static char numbuf[6];

void history_clear(void)
{
    for (int i = 0; i < nhist; i++) if (hist[i] != net.incoming) doc_free(hist[i]);
    nhist = 0;
}

static void push(struct doc *d)
{
    if (nhist == MAXHIST) {
        if (hist[0] != net.incoming) doc_free(hist[0]);
        memmove(hist, hist + 1, (MAXHIST - 1) * sizeof hist[0]);
        nhist--;
    }
    hist[nhist++] = d;
}

static struct doc *cur(void) { return nhist ? hist[nhist - 1] : NULL; }

static uint8_t style_attr(int s)
{
    switch (s) {
    case 'h': case 't': case 'y': return 0x1E;
    case 'H': case 'g': return 0x1A;
    case 's': case 'b': case 'w': return 0x1F;
    case 'i': case 'c': case 'k': return 0x13;
    case 'r': return 0x1C;
    case 'm': case 'p': return 0x1D;
    case 'l': return 0x1B;
    default: return 0x17;
    }
}

static int selnum(struct doc *d) { return d && d->sel >= 0 && d->sel < d->nlk ? d->lk[d->sel].num : 0; }

static void draw_line(struct doc *d, int row, int i)
{
    int y = TOP + row;
    scr_fill(0, y, 79, 1, ' ', 0x17);
    if (!d || i >= d->nlines) {
        if (d && !d->complete && i == d->nlines) scr_puts(2, y, 0x13, "\xFA \xFA \xFA  receiving  \xFA \xFA \xFA");
        return;
    }
    int len;
    const char *s = doc_line(d, i, &len);
    int col = 0, st = 'n', ln = 0, sel = selnum(d);
    for (int k = 0; k < len; k++) {
        uint8_t c = (uint8_t)s[k];
        if (c == 1 && k + 2 < len) { ln = ((uint8_t)s[k + 1] - 32) * 96 + ((uint8_t)s[k + 2] - 32); k += 2; continue; }
        if (c == 2) { ln = 0; continue; }
        if (c == 3 && k + 1 < len) { st = (uint8_t)s[++k]; continue; }
        uint8_t a = style_attr(st);
        if (ln && ln == sel) a = ATTR_SEL;
        scr_putc(2 + col, y, c, a);
        col++;
    }
}

static void draw_scrollbar(struct doc *d)
{
    for (int r = 0; r < ROWS; r++) scr_putc(79, TOP + r, 0xB0, 0x13);
    if (!d || d->nlines <= ROWS) return;
    int pos = (int)((long)d->top * (ROWS - 1) / (d->nlines - ROWS > 0 ? d->nlines - ROWS : 1));
    if (pos > ROWS - 1) pos = ROWS - 1;
    scr_putc(79, TOP + pos, 0xDB, 0x1B);
}

static void draw_header(struct doc *d)
{
    char b[81];
    scr_fill(0, 1, 80, 1, ' ', ATTR_TITLE);
    if (!d) { scr_puts(1, 1, ATTR_TITLE, "Please wait . . ."); return; }
    scr_printf(1, 1, ATTR_TITLE, "%.56s", d->title);
    if (d->complete) snprintf(b, sizeof b, "Line %d of %d", d->top + 1, d->nlines);
    else snprintf(b, sizeof b, "Line %d of %d+", d->top + 1, d->nlines);
    scr_puts(79 - (int)strlen(b), 1, ATTR_TITLE, b);
}

static void draw_help(void)
{
    if (numbuf[0]) {
        char b[80];
        snprintf(b, sizeof b, "Go to link number: %s_    (Enter=Go  Esc=Cancel)", numbuf);
        ui_help(b);
        return;
    }
    ui_help("Tab=Next link  Enter=Go  Esc=Back  F2=Search  P=Print  F10=Menu  Alt-X=Sign off");
}

static void draw_all(void)
{
    struct doc *d = cur();
    mouse_show(0);
    ui_titlebar(d ? d->channel : NULL);
    draw_header(d);
    for (int r = 0; r < ROWS; r++) draw_line(d, r, d ? d->top + r : 0);
    draw_scrollbar(d);
    draw_help();
    ui_statusbar();
    mouse_show(1);
}

static void clamp(struct doc *d)
{
    int max = d->nlines - ROWS;
    if (d->top > max) d->top = max;
    if (d->top < 0) d->top = 0;
}

/* Tab / Shift-Tab: the next link start in the text; scrolls to it */
static void tab(struct doc *d, int dir)
{
    if (!d->nlk) return;
    int i = d->sel;
    if (i < 0 || d->lk[i].line < d->top || d->lk[i].line >= d->top + ROWS) {
        /* start from the screen */
        i = dir > 0 ? -1 : d->nlk;
        for (int k = 0; k < d->nlk; k++) if (d->lk[k].line >= d->top) { i = dir > 0 ? k - 1 : k; break; }
    }
    for (;;) {
        i += dir;
        if (i < 0 || i >= d->nlk) return;
        if (!d->lk[i].cont) break;
    }
    d->sel = i;
    int l = d->lk[i].line;
    if (l < d->top) d->top = l > 3 ? l - 3 : 0;
    else if (l >= d->top + ROWS) d->top = l - ROWS + 4;
    clamp(d);
}

static void follow(struct doc *d, int num)
{
    if (!d || num <= 0) return;
    if (net.incoming) net_cancel();     /* the rest of this one is not needed now */
    net_follow(d->id, (uint16_t)num);
}

void reader_open(char type, const char *arg)
{
    if (net.incoming) net_cancel();
    net_request(type, arg);
}

static void help(void)
{
    ui_message("Reading", "Up/Down, PgUp/PgDn, Home/End scroll the text; it keeps arriving while you read.\n"
        "Tab and Shift-Tab move between the highlighted links; Enter follows one. Or type a link's number and Enter, or click it with the mouse.\n"
        "Pictures open in the picture viewer (VGA): you watch them arrive, S saves one.\n"
        "Esc or Backspace goes back. F2 searches the Encyclopedia. P prints to PRN. F10 is the main menu, Alt-X signs off.", 0x70);
}

/* 0 = main menu, 1 = sign off, 2 = the line went dead */
int reader_run(void)
{
    int lines_shown = -1, complete_shown = -1;
    struct doc *shown = NULL;
    numbuf[0] = 0;
    draw_all();
    for (;;) {
        int k = ui_idle_key();
        struct doc *d = cur();
        if (net.newdoc) {
            push(net.newdoc);
            net.newdoc = NULL;
            d = cur();
            d->top = 0; d->sel = -1;
            lines_shown = -1;
        }
        if (viewer_pending) {
            mouse_show(0);
            viewer_run();
            draw_all();
            continue;
        }
        if (net.msg_pending) {
            net.msg_pending = 0;
            draw_all();
            if (ui_message(net.msg_class == 'I' ? "ARM-DOS Online" : "Sorry", net.msg, net.msg_class == 'I' ? 0x70 : 0x4F) == KEY_LOST) return 2;
            if (!cur()) return 0;
            draw_all();
            continue;
        }
        if (net.lost || !net.carrier) return 2;
        if (d != shown || (d && (d->nlines != lines_shown || d->complete != complete_shown))) {
            /* new lines arrived: redraw what is on screen of them */
            if (d != shown) draw_all();
            else {
                mouse_show(0);
                for (int r = 0; r < ROWS; r++) {
                    int i = d->top + r;
                    if (i >= lines_shown - 1 && i <= d->nlines) draw_line(d, r, i);
                }
                draw_header(d); draw_scrollbar(d);
                mouse_show(1);
            }
            shown = d;
            lines_shown = d ? d->nlines : -1;
            complete_shown = d ? d->complete : -1;
        }
        int mx, my;
        if (!k && mouse_click(&mx, &my) && d) {
            if (my >= TOP && my < TOP + ROWS && mx >= 2) {
                int l = d->top + my - TOP;
                for (int i = 0; i < d->nlk; i++)
                    if (d->lk[i].line == l && mx - 2 >= d->lk[i].col && mx - 2 < d->lk[i].col + d->lk[i].len) { d->sel = i; draw_all(); follow(d, d->lk[i].num); break; }
            } else if (mx == 79 && my >= TOP && my < TOP + ROWS) {
                d->top += my < TOP + ROWS / 2 ? -ROWS : ROWS; clamp(d); draw_all();
            } else if (my == 23) k = K_ESC;
        }
        if (!k) { idle(); continue; }
        if (k == K_ALT(ALT_X)) return 1;
        if (k == K_F(10)) { if (net.incoming || net.waiting) net_cancel(); return 0; }
        if (k == K_F(1)) { help(); draw_all(); continue; }
        if (k >= '0' && k <= '9' && d) {
            int n = (int)strlen(numbuf);
            if (n < 4) { numbuf[n] = (char)k; numbuf[n + 1] = 0; }
            draw_help();
            continue;
        }
        if (numbuf[0]) {
            if (k == K_ENTER) { int n = atoi(numbuf); numbuf[0] = 0; draw_help(); follow(d, n); continue; }
            if (k == K_BS) { numbuf[strlen(numbuf) - 1] = 0; draw_help(); continue; }
            if (k == K_ESC) { numbuf[0] = 0; draw_help(); continue; }
        }
        if (k == K_ESC || k == K_BS) {
            if (net.waiting) { net_cancel(); if (!d) return 0; ui_statusbar(); continue; }
            if (d && !d->complete) net_cancel();
            if (nhist) { if (hist[nhist - 1] != net.incoming) doc_free(hist[nhist - 1]); nhist--; }
            if (!nhist) return 0;
            if (cur()->partial) {
                /* it was cut short when we left it: fetch it again */
                struct doc *p = cur();
                net_resend(p);
                doc_free(p); nhist--;
                if (!nhist) { shown = NULL; draw_all(); continue; }
            }
            shown = NULL;
            continue;
        }
        if (k == K_F(2) || k == '/') {
            char q[64] = "";
            if (ui_prompt("Encyclopedia", "Search the Encyclopedia for:", q, 60)) reader_open('S', q);
            draw_all();
            continue;
        }
        if (!d) continue;
        int old = d->top;
        switch (k) {
        case K_UP: d->top--; break;
        case K_DOWN: d->top++; break;
        case K_PGUP: d->top -= ROWS - 1; break;
        case K_PGDN: case ' ': d->top += ROWS - 1; break;
        case K_HOME: d->top = 0; break;
        case K_END: d->top = d->nlines; break;
        case 9: tab(d, 1); draw_all(); continue;
        case KEY_EXT | 0x0F: tab(d, -1); draw_all(); continue;
        case K_ENTER: if (d->sel >= 0) follow(d, d->lk[d->sel].num); continue;
        case 'p': case 'P': print_doc(d); draw_all(); continue;
        default: continue;
        }
        clamp(d);
        if (d->top != old) {
            /* scrolling moves the selection along if it went off the screen */
            if (d->sel >= 0 && (d->lk[d->sel].line < d->top || d->lk[d->sel].line >= d->top + ROWS)) d->sel = -1;
            mouse_show(0);
            for (int r = 0; r < ROWS; r++) draw_line(d, r, d->top + r);
            draw_header(d); draw_scrollbar(d);
            mouse_show(1);
        }
    }
}
