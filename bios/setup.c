/* ARM/AT BIOS — the CMOS SETUP utility (DEL during POST) */
#include "bios.h"

/* colours: VGA, or (on a mono card, where blue/cyan backgrounds would all be
   plain black) normal/bright/reverse video */
static int C_BG = 0x1F, C_LABEL = 0x1E, C_VALUE = 0x1F, C_SEL = 0x70, C_TITLE = 0x3F, C_HELP = 0x1B, C_BOX = 0x4F, C_BOXQ = 0x4E;

static void pick_colours(void)
{
    if (video_mono()) { C_BG = 0x07; C_LABEL = 0x07; C_VALUE = 0x0F; C_SEL = 0x70; C_TITLE = 0x70; C_HELP = 0x07; C_BOX = 0x70; C_BOXQ = 0x70; }
    else { C_BG = 0x1F; C_LABEL = 0x1E; C_VALUE = 0x1F; C_SEL = 0x70; C_TITLE = 0x3F; C_HELP = 0x1B; C_BOX = 0x4F; C_BOXQ = 0x4E; }
}

enum { IT_DATE, IT_TIME, IT_FDA, IT_FDB, IT_HD, IT_BOOT, IT_QUICK, IT_BEEP, IT_NUM, IT_COUNT };

static const char *const labels[IT_COUNT] = {
    "Date (mm/dd/yyyy)", "Time (hh:mm:ss)", "Floppy Drive A:", "Floppy Drive B:", "Hard Disk C:",
    "Boot Sequence", "Quick Power On Self Test", "POST Beep", "Boot Up NumLock Status",
};
static const int item_row[IT_COUNT] = { 3, 4, 6, 7, 8, 10, 11, 12, 13 };

static int boot_seq, quick, beep_on, num_on;
static int yr, mo, dy, hh, mi, ss;
static int sub;         /* which part of the date or time is being edited */

static const char *const dow[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

static int day_of_week(int y, int m, int d)
{
    static const int t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static int days_in(int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0)) return 29;
    return d[m - 1];
}

static void read_clock(void)
{
    ss = bcd2bin(cmos_read(0)); mi = bcd2bin(cmos_read(2)); hh = bcd2bin(cmos_read(4));
    dy = bcd2bin(cmos_read(7)); mo = bcd2bin(cmos_read(8));
    yr = bcd2bin(cmos_read(0x32)) * 100 + bcd2bin(cmos_read(9));
}

static void write_clock(void)
{
    cmos_write(0, bin2bcd(ss)); cmos_write(2, bin2bcd(mi)); cmos_write(4, bin2bcd(hh));
    cmos_write(7, bin2bcd(dy)); cmos_write(8, bin2bcd(mo));
    cmos_write(9, bin2bcd(yr % 100)); cmos_write(0x32, bin2bcd(yr / 100));
}

static void value_text(int it, char *b, int n)
{
    static const char *const bootn[3] = { "A:, C:", "C:, A:", "C: Only" };
    switch (it) {
    case IT_DATE: snprintf(b, n, "%s %02d/%02d/%04d", dow[day_of_week(yr, mo, dy)], mo, dy, yr); break;
    case IT_TIME: snprintf(b, n, "%02d:%02d:%02d", hh, mi, ss); break;
    case IT_FDA: snprintf(b, n, "1.44 MB  3\xAB\""); break;
    case IT_FDB: snprintf(b, n, "Not Installed"); break;
    case IT_HD:
        if (hd_sectors()) snprintf(b, n, "Auto (%u MB, LBA)", hd_sectors() / 2048);
        else snprintf(b, n, "Not Installed");
        break;
    case IT_BOOT: snprintf(b, n, "%s", bootn[boot_seq]); break;
    case IT_QUICK: snprintf(b, n, "%s", quick ? "Enabled" : "Disabled"); break;
    case IT_BEEP: snprintf(b, n, "%s", beep_on ? "Enabled" : "Disabled"); break;
    case IT_NUM: snprintf(b, n, "%s", num_on ? "On" : "Off"); break;
    }
}

static void draw_frame(void)
{
    video_fill(0, 0, 80 * 25, ' ', C_BG);
    video_fill(0, 0, 80, ' ', C_TITLE);
    video_write_at(0, 17, "ARM/AT BIOS SETUP  (C) 1988 Europa Micro Systems", C_TITLE);
    video_fill(1, 0, 80, 0xC4, C_BG);
    video_fill(15, 0, 80, 0xC4, C_BG);
    char b[80];
    snprintf(b, sizeof b, "CPU                     : ARM926EJ-S (ARMv5TE)  %d MHz", inb(0xF1) == 0xFF ? 100 : inb(0xF1));
    video_write_at(16, 3, b, C_HELP);
    snprintf(b, sizeof b, "Base / Extended Memory  : 640K / %uK", (ram_end - EXT_MEM_START) / 1024);
    video_write_at(17, 3, b, C_HELP);
    video_write_at(18, 3, video_card == VID_HERCULES ? "Video Adapter           : Hercules Graphics Card (64K)" :
                          video_card == VID_MDA ? "Video Adapter           : Monochrome Display Adapter" :
                          "Video Adapter           : ARM-PC VGA (256K)", C_HELP);
    video_fill(22, 0, 80, 0xC4, C_BG);
    video_write_at(23, 2, "\x18\x19 Select Item   \x1B\x1A Field   PgUp/PgDn/+/- Modify   F10 Save & Exit   Esc Exit", C_HELP);
}

static void draw_items(int cur)
{
    char b[48];
    for (int i = 0; i < IT_COUNT; i++) {
        int r = item_row[i];
        video_fill(r, 1, 78, ' ', C_BG);
        video_write_at(r, 3, labels[i], C_LABEL);
        video_write_at(r, 28, ":", C_LABEL);
        value_text(i, b, sizeof b);
        int editable = !(i == IT_FDA || i == IT_FDB || i == IT_HD);
        video_write_at(r, 30, b, (i == cur && editable) ? C_VALUE : C_VALUE);
        if (i == cur) {
            if (i == IT_DATE) {
                int off[3] = { 4, 7, 10 }, len[3] = { 2, 2, 4 };
                for (int k = 0; k < len[sub]; k++) video_fill(r, 30 + off[sub] + k, 1, b[off[sub] + k], C_SEL);
            } else if (i == IT_TIME) {
                for (int k = 0; k < 2; k++) video_fill(r, 30 + sub * 3 + k, 1, b[sub * 3 + k], C_SEL);
            } else {
                video_write_at(r, 30, b, C_SEL);
            }
        }
    }
}

static void modify(int it, int d)
{
    switch (it) {
    case IT_DATE:
        if (sub == 0) mo = (mo + d + 11) % 12 + 1;
        else if (sub == 1) dy = (dy - 1 + d + days_in(yr, mo)) % days_in(yr, mo) + 1;
        else { yr += d; if (yr < 1980) yr = 2099; if (yr > 2099) yr = 1980; }
        if (dy > days_in(yr, mo)) dy = days_in(yr, mo);
        write_clock();
        break;
    case IT_TIME:
        if (sub == 0) hh = (hh + d + 24) % 24;
        else if (sub == 1) mi = (mi + d + 60) % 60;
        else ss = (ss + d + 60) % 60;
        write_clock();
        break;
    case IT_BOOT: boot_seq = (boot_seq + d + 3) % 3; break;
    case IT_QUICK: quick ^= 1; break;
    case IT_BEEP: beep_on ^= 1; break;
    case IT_NUM: num_on ^= 1; break;
    }
}

static int confirm(const char *q)
{
    int w = strlen(q) + 10;
    int c = 40 - w / 2;
    video_fill(10, c, w, ' ', C_BOX);
    video_fill(11, c, w, ' ', C_BOX);
    video_fill(12, c, w, ' ', C_BOX);
    video_write_at(11, c + 2, q, C_BOX);
    video_write_at(11, c + 2 + strlen(q) + 1, "(Y/N)?", C_BOXQ);
    for (;;) {
        int k = kbd_get() & 0xFF;
        if (k == 'y' || k == 'Y' || k == '\r') return 1;
        if (k == 'n' || k == 'N' || k == 27) return 0;
    }
}

void setup_utility(void)
{
    pick_colours();
    video_set_mode(video_default_mode());
    video_cursor_shape(0x20, 0);
    boot_seq = cmos_read(CMOS_BOOT) % 3;
    int fl = cmos_read(CMOS_FLAGS);
    quick = fl & 1;
    beep_on = !(fl & 2);
    num_on = !(fl & 4);
    int cur = IT_DATE;
    sub = 0;
    draw_frame();
    uint32_t last = 0;
    for (;;) {
        read_clock();
        draw_items(cur);
        int k;
        for (;;) {
            k = kbd_peek();
            if (k >= 0) { kbd_get(); break; }
            if (ticks() - last >= 9) { last = ticks(); read_clock(); draw_items(cur); }
            wfi();
        }
        int sc = k >> 8, ch = k & 0xFF;
        if (ch == 0xE0) ch = 0;
        if (sc == 0x48 && !ch) { do cur = (cur + IT_COUNT - 1) % IT_COUNT; while (cur == IT_FDA || cur == IT_FDB || cur == IT_HD); sub = 0; }
        else if (sc == 0x50 && !ch) { do cur = (cur + 1) % IT_COUNT; while (cur == IT_FDA || cur == IT_FDB || cur == IT_HD); sub = 0; }
        else if (sc == 0x4B && !ch) { if (sub > 0) sub--; }
        else if (sc == 0x4D && !ch) { if ((cur == IT_DATE || cur == IT_TIME) && sub < 2) sub++; }
        else if ((sc == 0x49 && !ch) || ch == '+') modify(cur, 1);
        else if ((sc == 0x51 && !ch) || ch == '-') modify(cur, -1);
        else if (sc == 0x44 && !ch) {                   /* F10 */
            if (confirm("SAVE to CMOS and EXIT")) {
                cmos_write(CMOS_BOOT, boot_seq);
                cmos_write(CMOS_FLAGS, (quick ? 1 : 0) | (beep_on ? 0 : 2) | (num_on ? 0 : 4));
                break;
            }
            draw_frame();
        } else if (ch == 27) {
            if (confirm("Quit without saving")) break;
            draw_frame();
        }
    }
    video_set_mode(video_default_mode());
}
