/*
 * colors.c - the four colour schemes of the text-mode Shell (read from the
 * real SHELLC's screens, cell by cell) and the Change Colors screen.
 */
#include "shell.h"

int scheme;

/* C_TITLE C_TITLEBOX C_BAR C_BARMN C_BARSEL C_BARSELMN
   C_WORK C_INSTR C_ITEM C_ITEMSEL C_FKEY
   C_PULL C_PULLMN C_PULLSEL C_PULLSELMN
   C_DLG C_DLGSEL C_DLGMN C_HELP C_HELPSEL C_ARROW
   C_WARN C_WARNSEL C_SAMPLE C_PROMPT */
const uint8_t schemes[4][C_COUNT] = {
    { 0x1F, 0x0F, 0x30, 0x3F, 0x07, 0x0F,
      0x70, 0x71, 0x70, 0x07, 0x30,
      0x30, 0x3F, 0x07, 0x0F,
      0x70, 0x07, 0x7F, 0x71, 0x07, 0x71,
      0x47, 0x74, 0x71, 0x70 },
    { 0x1F, 0x0F, 0x70, 0x7C, 0x0F, 0x0C,
      0x0F, 0x0F, 0x0F, 0x70, 0x70,
      0x70, 0x7C, 0x0F, 0x0C,
      0x70, 0x0F, 0x7C, 0x30, 0x0F, 0x0A,
      0x4F, 0x74, 0x1F, 0x70 },
    { 0x30, 0x0F, 0x70, 0x7F, 0x0F, 0x09,
      0x0F, 0x0F, 0x0F, 0x70, 0x70,
      0x70, 0x7F, 0x0F, 0x09,
      0x70, 0x0F, 0x7F, 0x70, 0x0F, 0x0F,
      0x7F, 0x70, 0x60, 0x70 },
    { 0x30, 0x0F, 0x70, 0x7C, 0x0F, 0x0C,
      0x1F, 0x1F, 0x1F, 0x70, 0x70,
      0x70, 0x7C, 0x0F, 0x0C,
      0x70, 0x0F, 0x7C, 0x30, 0x0F, 0x1F,
      0x4F, 0x74, 0x70, 0x70 },
};

/* the File System's own colours: frame+headings, names, the cursor bar */
const uint8_t fs_colors[4][3] = {
    { 0x70, 0x70, 0x07 },
    { 0x0F, 0x0A, 0x70 },
    { 0x0F, 0x0F, 0x70 },
    { 0x1F, 0x1F, 0x70 },
};

/* SHELL.CLR: "ADSHCLR", 0, scheme 1-4 (ARM-DOS's own small format) */
static uint8_t clrbuf[16];

int colors_load(void)
{
    char path[96];
    home_path(path, opt.clr);
    int n = file_read(path, clrbuf, sizeof clrbuf);
    if (n < 9) return -1;
    if (!memcmp(clrbuf, "ADSHCLR", 7) && clrbuf[8] >= 1 && clrbuf[8] <= 4) scheme = clrbuf[8] - 1;
    else if (clrbuf[0] == 0x34 && clrbuf[1] == 0x12) scheme = 0;    /* the real Shell's file */
    else return -1;
    return 0;
}

void colors_save(void)
{
    char path[96];
    home_path(path, opt.clr);
    memset(clrbuf, 0, sizeof clrbuf);
    memcpy(clrbuf, "ADSHCLR", 7);
    clrbuf[8] = scheme + 1;
    if (file_write(path, clrbuf, sizeof clrbuf) < 0) message("Access denied.", H_MESSAGE);
}

/* ------------------------------------------------------ Change Colors ---- */
static void panel(int r, int c, int h, int w, int attr, int divider, const char *buttons)
{
    s_fill(r, c, h, w, ' ', attr);
    s_box(r, c, h, w, attr);
    if (divider) {
        s_hdiv(r + h - 3, c, w, attr);
        s_put(r + h - 2, c + 1, buttons, attr);
    }
}

static void cc_draw(void *p)
{
    (void)p;
    int w = CLR(C_WORK), in = CLR(C_INSTR);
    draw_title("Start Programs");
    s_fill(1, 0, 1, COLS, ' ', CLR(C_BAR));
    s_ch(1, 66, B_V, CLR(C_BAR));
    s_put(1, 69, "F1=Help", CLR(C_BAR));
    s_fill(2, 0, 22, COLS, ' ', w);
    s_put(4, 33, "Change Colors", in);
    s_ch(4, 64, '1' + scheme, in);
    s_put(6, 14, "To change colors use  \x1B  and   \x1A.  Press Enter to", in);
    s_put(7, 14, "save current color selections or Esc to Cancel.", in);
    panel(9, 10, 11, 49, CLR(C_HELP), 1, "  Esc=Cancel   F1=Help   F11=Index   F9=Keys");
    panel(12, 27, 10, 44, CLR(C_DLG), 1, BTN_ENTER);
    panel(13, 15, 8, 58, CLR(C_WARN), 1, BTN_ENTER);
    panel(10, 23, 7, 33, CLR(C_SAMPLE), 0, 0);
    s_put(13, 29, "Sample color panels", CLR(C_SAMPLE));
    draw_fkeys("  <\xC4\xD9=Enter  Esc=Cancel");
}

void change_colors(void)
{
    int old = scheme;
    push_layer(cc_draw, 0);
    for (;;) {
        redraw();
        struct ev e;
        ev_get(&e);
        int k = 0;
        if (e.type == EV_KEY) k = e.key;
        else if (e.type == EV_DOWN || e.type == EV_DBL) {
            if (e.row == 24) k = fkey_hit("  <\xC4\xD9=Enter  Esc=Cancel", e.col);
            else if (e.row == 6 && e.col == 36) k = K_LEFT;
            else if (e.row == 6 && e.col == 45) k = K_RIGHT;
            else if (e.row == 1 && e.col >= 69 && e.col <= 75) k = K_F1;
        }
        if (k == K_LEFT) scheme = (scheme + 3) % 4;
        else if (k == K_RIGHT) scheme = (scheme + 1) % 4;
        else if (k == K_ENTER) { colors_save(); break; }
        else if (k == K_ESC) { scheme = old; break; }
        else if (k == K_F1) help_show(H_COLORS, 0, 0);
    }
    pop_layer();
}
