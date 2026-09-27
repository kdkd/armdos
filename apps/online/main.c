/* ARM-DOS Online - the client (ONLINE.EXE). An online service in the manner of the
 * early 1990s: sign on with the modem, pick a channel, read articles that come down the
 * line, watch pictures arrive. The other end is emu/online (555-0199), which fetches from
 * the real Wikipedia, Wiktionary, Open-Meteo and Hacker News. See README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <dos.h>
#include "online.h"

struct config cfg = { "Guest", "555-0199", "AT&C1&D2", 2, 115200L };
static char cfgpath[80] = "C:\\ONLINE\\ONLINE.CFG";

/* ------------------------------------------------------------ configuration */
static void load_config(const char *argv0)
{
    /* next to the program: ONLINE.CFG */
    if (argv0 && argv0[0]) {
        const char *s = strrchr(argv0, '\\');
        if (s && s - argv0 < 60) { memcpy(cfgpath, argv0, s - argv0 + 1); strcpy(cfgpath + (s - argv0 + 1), "ONLINE.CFG"); }
    }
    FILE *f = fopen(cfgpath, "r");
    if (!f) return;
    char line[100];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq++ = 0;
        if (!strcmp(line, "NAME")) snprintf(cfg.name, sizeof cfg.name, "%s", eq);
        else if (!strcmp(line, "NUMBER")) snprintf(cfg.number, sizeof cfg.number, "%s", eq);
        else if (!strcmp(line, "INIT")) snprintf(cfg.init, sizeof cfg.init, "%s", eq);
        else if (!strcmp(line, "PORT")) cfg.port = atoi(eq);
        else if (!strcmp(line, "BAUD")) cfg.baud = atol(eq);
    }
    fclose(f);
    if (cfg.port < 1 || cfg.port > 4) cfg.port = 2;
    if (cfg.baud < 300) cfg.baud = 115200L;
}

void save_config(void)
{
    FILE *f = fopen(cfgpath, "w");
    if (!f) return;
    fprintf(f, "NAME=%s\nNUMBER=%s\nINIT=%s\nPORT=%d\nBAUD=%ld\n", cfg.name, cfg.number, cfg.init, cfg.port, cfg.baud);
    fclose(f);
}

/* ------------------------------------------------------------ printing */
void print_doc(struct doc *d)
{
    static uint16_t save[80 * 25];
    char line[100];
    if (!d) return;
    mouse_show(0);
    scr_save(save);
    scr_box(24, 10, 32, 5, 0x70, 1, "Print");
    scr_puts(27, 12, 0x70, "Printing on PRN . . .");
    scr_shadow(24, 10, 32, 5);
    int fd = open("PRN", O_WRONLY | O_BINARY);
    int ok = fd >= 0;
    if (ok) {
        struct dosdate_t dt;
        _dos_getdate(&dt);
        int n = snprintf(line, sizeof line, "ARM-DOS Online - %s - %02d/%02d/%04d\r\n", d->channel, dt.month, dt.day, dt.year);
        ok = write(fd, line, n) == n;
        n = snprintf(line, sizeof line, "%.76s\r\n\r\n", d->title);
        write(fd, line, n);
        for (int i = 0; ok && i < d->nlines; i++) {
            n = doc_plain(d, i, line, 90);
            line[n++] = '\r'; line[n++] = '\n';
            ok = write(fd, line, n) == n;
        }
        write(fd, "\f", 1);
        close(fd);
    }
    scr_restore(save);
    mouse_show(1);
    if (!ok) ui_message("Print", "The printer is not ready.", 0x4F);
}

/* ------------------------------------------------------------ the sign-on screen */
#define DESK 0x1F
static const char *const pic_phone[5] = {
    "   _________   ", "  / _______ \\  ", " |_/ o o o \\_| ", "   | o o o |   ", "   |_o_o_o_|   " };
static const char *const pic_link[5] = {
    "               ", " \xDA\xC4\xC4\xBF     \xDA\xC4\xC4\xBF ", " \xB3\xFE\xFE\xB3~~~~~\xB3\xFE\xFE\xB3 ", " \xC0\xC4\xC4\xD9     \xC0\xC4\xC4\xD9 ", "               " };
static const char *const pic_lock[5] = {
    "     .---.     ", "     |   |     ", "   \xDA\xC4\xC1\xC4\xC4\xC4\xC1\xC4\xBF   ", "   \xB3  \x0F  \xB3   ", "   \xC0\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xD9   " };
static const char *const *const pics[3] = { pic_phone, pic_link, pic_lock };
static const char *const pic_caption[3] = { "Dialing", "Connecting", "Verifying password" };
static int step_state[3];     /* 0 waiting, 1 active, 2 done, 3 failed */
static int anim;

static void draw_step(int i)
{
    int x = 4 + i * 26, y = 15;
    static const uint8_t fg[4] = { 0x18, 0x1E, 0x1A, 0x1C };
    uint8_t a = fg[step_state[i]];
    scr_fill(x, y, 22, 7, ' ', DESK & 0xF0);
    for (int r = 0; r < 5; r++) scr_puts(x + 3, y + r, (DESK & 0xF0) | (a & 0x0F), pics[i][r]);
    char b[40];
    if (step_state[i] == 1) snprintf(b, sizeof b, "%s%.*s", pic_caption[i], anim % 4, "...");
    else if (step_state[i] == 2) snprintf(b, sizeof b, "%s \xFB", pic_caption[i]);
    else snprintf(b, sizeof b, "%s", pic_caption[i]);
    scr_fill(x, y + 6, 22, 1, ' ', DESK & 0xF0);
    scr_puts(x + (22 - (int)strlen(b)) / 2, y + 6, (DESK & 0xF0) | (a & 0x0F), b);
    if (i < 2) scr_puts(x + 21, y + 2, (DESK & 0xF0) | (step_state[i] == 2 ? 0x0A : 0x08), "\x1A\x1A");
}

static void draw_steps(void) { for (int i = 0; i < 3; i++) draw_step(i); }

static void signon_screen(const char *note, int note_attr)
{
    mouse_show(0);
    scr_fill(0, 0, 80, 25, ' ', DESK);
    ui_titlebar("Sign On");
    ui_logo(2, DESK);
    scr_center(5, 0x1B, "The information service for your ARM-DOS computer");
    scr_box(16, 7, 48, 7, 0x70, 1, "Sign On");
    scr_puts(19, 9, 0x70, "Screen Name:");
    scr_puts(19, 10, 0x70, "Password:");
    scr_printf(19, 11, 0x78, "Access number %s via COM%d", cfg.number, cfg.port);
    scr_fill(32, 9, 18, 1, ' ', ATTR_FIELD);
    scr_puts(32, 9, ATTR_FIELD, cfg.name);
    scr_fill(32, 10, 18, 1, ' ', ATTR_FIELD);
    scr_shadow(16, 7, 48, 7);
    draw_steps();
    if (note) scr_center(22, note_attr, note);
    ui_help("Enter=Sign On  Tab=Next field  Esc=Exit to DOS");
    ui_statusbar();
    mouse_show(1);
}

static void progress(int step, const char *text)
{
    if (step == 0) {                     /* animate while the modem works */
        static uint32_t t;
        if (TICKS() - t >= 5) {
            t = TICKS(); anim++;
            for (int i = 0; i < 3; i++) if (step_state[i] == 1) draw_step(i);
            ui_titlebar(NULL);
        }
        return;
    }
    if (step == 1) { step_state[0] = 1; draw_steps(); scr_fill(0, 22, 80, 2, ' ', DESK); scr_center(23, 0x17, text); }
    if (step == 2) { step_state[0] = 2; step_state[1] = 2; step_state[2] = 1; draw_steps(); scr_fill(0, 22, 80, 2, ' ', DESK); scr_center(23, 0x1A, text); ui_statusbar(); }
}

/* returns 1 = sign on, 0 = exit */
static int signon_form(char *pass)
{
    int field = 0;
    for (;;) {
        char buf[17];
        if (field == 0) {
            snprintf(buf, sizeof buf, "%s", cfg.name);
            scr_cursor_on(1);
            if (!scr_input(32, 9, 18, ATTR_FIELD, buf, 16)) { scr_cursor_on(0); return 0; }
            if (buf[0]) snprintf(cfg.name, sizeof cfg.name, "%s", buf);
            field = 1;
            continue;
        }
        buf[0] = 0;
        /* the password shows as stars */
        int n = 0;
        scr_cursor_on(1);
        for (;;) {
            scr_fill(32, 10, 18, 1, ' ', ATTR_FIELD);
            for (int i = 0; i < n; i++) scr_putc(32 + i, 10, '*', ATTR_FIELD);
            scr_cursor(32 + n, 10);
            int k = key_get();
            if (k == K_ENTER) { pass[n] = 0; scr_cursor_on(0); return 1; }
            if (k == K_ESC) { scr_cursor_on(0); return 0; }
            if (k == 9 || k == (KEY_EXT | 0x0F) || k == K_UP) { field = 0; break; }
            if (k == K_BS) { if (n) n--; continue; }
            if (k >= 32 && k < 127 && n < 16) pass[n++] = (char)k;
        }
    }
}

/* ------------------------------------------------------------ the main menu */
struct channel { const char *name, *desc; uint8_t attr; char req; };
static const struct channel chans[8] = {
    { "Encyclopedia", "Look anything up, A to Z", 0x1F, 'S' },
    { "Dictionary", "Meanings and usage of words", 0x2F, 'D' },
    { "Weather", "Forecasts for cities worldwide", 0x3F, 'W' },
    { "Technology News", "Top stories in computing", 0x4F, 'N' },
    { "Today in History", "What happened on this date", 0x5F, 'T' },
    { "Random Article", "Feeling lucky? Try one!", 0x6F, 'R' },
    { "About ARM-DOS Online", "Where all this comes from", 0x70, 'A' },
    { "Sign Off", "Hang up and return to DOS", 0x0C, 'Q' },
};

static void draw_button(int i, int sel)
{
    int x = i % 2 ? 41 : 3, y = 6 + (i / 2) * 4, w = 36;
    const struct channel *c = &chans[i];
    scr_fill(x, y, w, 3, ' ', c->attr);
    if (sel) {
        scr_box(x, y, w, 3, (c->attr & 0xF0) | 0x0F, 1, NULL);
    }
    scr_printf(x + 2, y + 1, (c->attr & 0xF0) | 0x0E, "%d", i + 1);
    scr_puts(x + 4, y + 1, c->attr | (sel ? 0x0F : 0), c->name);
    if (!sel) scr_puts(x + 4, y + 2, (c->attr & 0xF0) | ((c->attr & 0xF0) == 0x70 ? 0x08 : 0x07), c->desc);
    /* shadow */
    for (int j = 1; j <= 3; j++) scr_putc(x + w, y + j, j == 3 ? 0xDF : 0xDB, 0x08);
    for (int k = 1; k < w; k++) scr_putc(x + k, y + 3, 0xDF, 0x08);
}

static void menu_screen(int sel)
{
    mouse_show(0);
    scr_fill(0, 0, 80, 25, 0xB0, 0x08);
    ui_titlebar("Main Menu");
    scr_fill(0, 1, 80, 4, ' ', 0x1F);
    scr_printf(2, 2, 0x1E, "Welcome, %s!", net.member[0] ? net.member : cfg.name);
    const char *nl = strchr(net.welcome_text, '\n');
    scr_printf(2, 3, 0x17, "%s", nl ? nl + 1 : "");
    struct dosdate_t dt; _dos_getdate(&dt);
    static const char *const mon[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    char b[40];
    snprintf(b, sizeof b, "%s %d, %d", mon[(dt.month + 11) % 12], dt.day, dt.year);
    scr_puts(78 - (int)strlen(b), 2, 0x1B, b);
    for (int i = 0; i < 8; i++) draw_button(i, i == sel);
    ui_help("1-8 or arrows + Enter=Open a channel   F1=Help   Alt-X=Sign Off");
    ui_statusbar();
    mouse_show(1);
}

static void about(void)
{
    ui_message("About ARM-DOS Online",
        "ARM-DOS Online " ONLINE_VERSION " - an online service for ARM-DOS computers, reached with the modem on COM2 at 555-0199.\n"
        "The Encyclopedia and Today in History come from Wikipedia and the Dictionary from Wiktionary (text CC BY-SA). "
        "Weather data by Open-Meteo.com (CC BY 4.0). Technology News from Hacker News. Pictures from Wikimedia Commons (see each file's page).\n"
        "(C) Europa Micro Systems 1988.", 0x70);
}

/* 0 = sign off, 2 = line lost */
static int main_menu(void)
{
    static int sel;
    history_clear();
    menu_screen(sel);
    for (;;) {
        int k = ui_idle_key();
        int mx, my, pick = -1;
        if (net.lost || !net.carrier) return 2;
        if (!k && mouse_click(&mx, &my)) {
            for (int i = 0; i < 8; i++) {
                int x = i % 2 ? 41 : 3, y = 6 + (i / 2) * 4;
                if (mx >= x && mx < x + 36 && my >= y && my < y + 3) pick = i;
            }
        }
        if (!k && pick < 0) { idle(); continue; }
        int old = sel;
        if (k == K_UP && sel >= 2) sel -= 2;
        else if (k == K_DOWN && sel < 6) sel += 2;
        else if (k == K_LEFT && sel % 2) sel--;
        else if (k == K_RIGHT && !(sel % 2)) sel++;
        else if (k == 9) sel = (sel + 1) % 8;
        else if (k >= '1' && k <= '8') pick = k - '1';
        else if (k == K_ENTER) pick = sel;
        else if (k == K_ALT(ALT_X) || k == K_ESC) pick = 7;
        else if (k == K_F(1)) { ui_message("Main Menu", "Choose a channel with the arrow keys and Enter, its number, or a click. In any channel, Esc goes back and F10 returns here. Alt-X signs off.", 0x70); continue; }
        if (sel != old) { mouse_show(0); draw_button(old, 0); draw_button(sel, 1); mouse_show(1); }
        if (pick < 0) continue;
        sel = pick;
        char q[64] = "";
        int ok = 1;
        switch (chans[pick].req) {
        case 'S': ok = ui_prompt("Encyclopedia", "Search the Encyclopedia for:", q, 60); break;
        case 'D': ok = ui_prompt("Dictionary", "Look up the word:", q, 40); break;
        case 'W': ok = ui_prompt("Weather", "City (for example: Berlin, or Paris, FR):", q, 50); break;
        case 'T': { struct dosdate_t dt; _dos_getdate(&dt); snprintf(q, sizeof q, "%02d%02d", dt.month, dt.day); break; }
        case 'A': about(); continue;
        case 'Q': return 0;
        }
        if (!ok) { menu_screen(sel); continue; }
        reader_open(chans[pick].req, q);
        int r = reader_run();
        history_clear();
        if (r == 1) return 0;
        if (r == 2) return 2;
        menu_screen(sel);
    }
}

/* ------------------------------------------------------------ sign off */
static void sign_off(void)
{
    mouse_show(0);
    scr_box(20, 10, 40, 5, 0x70, 1, "Sign Off");
    scr_puts(23, 12, 0x70, "Signing off . . .");
    scr_shadow(20, 10, 40, 5);
    net_send('Q', NULL, 0);
    uint32_t t = TICKS();
    /* the host says goodbye and hangs up: DCD drops, the modem says NO CARRIER */
    while (TICKS() - t < ms2ticks(8000)) {
        net_poll();
        if (net.goodbye && !net.carrier && net.nocarrier) break;
        idle();
    }
    if (net.carrier || com_carrier()) modem_hangup();
    delay_ms(200);
    net_poll();
}

int main(int argc, char **argv)
{
    (void)argc;
    load_config(argv[0]);
    scr_init();
    scr_cursor_on(0);
    mouse_init();
    if (modem_open() < 0) {
        printf("ONLINE: no modem on COM%d.\n", cfg.port);
        return 1;
    }
    /* the first time: where the modem's speed is set (web/js/modem-panel.js), before the call */
    const char *note = "Set the speed switch on the modem (below the PC) first: 2400 bps to 56K."; char notebuf[80]; int nattr = 0x17;
    for (;;) {
        char pass[20];
        memset(step_state, 0, sizeof step_state);
        signon_screen(note, nattr);
        if (!signon_form(pass)) break;
        save_config();
        signon_screen(NULL, 0);
        scr_puts(32, 10, ATTR_FIELD, "********");
        if (modem_dial(progress) < 0) {
            step_state[0] = 3; draw_steps();
            snprintf(notebuf, sizeof notebuf, "The call did not go through: %s", net.result);
            if (!strcmp(net.result, "BUSY")) snprintf(notebuf, sizeof notebuf, "The access number is busy (BUSY). Please try again.");
            else if (!strcmp(net.result, "CANCELLED")) snprintf(notebuf, sizeof notebuf, "Cancelled.");
            note = notebuf; nattr = 0x1C;
            continue;
        }
        if (!net_signon(cfg.name, pass)) {
            step_state[2] = 3; draw_steps();
            modem_hangup();
            note = "The host did not accept the sign-on. Please try again."; nattr = 0x1C;
            continue;
        }
        step_state[2] = 2; draw_steps();
        delay_ms(700);
        int r = main_menu();
        uint32_t s = online_seconds();
        if (r == 2) {
            modem_hangup();
            snprintf(notebuf, sizeof notebuf, "You have been disconnected (%s) after %02lu:%02lu:%02lu online.", net.nocarrier ? "NO CARRIER" : "carrier lost",
                     (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
            nattr = 0x1C;
        } else {
            sign_off();
            snprintf(notebuf, sizeof notebuf, "Signed off after %02lu:%02lu:%02lu online. Modem: %s", (unsigned long)(s / 3600),
                     (unsigned long)(s / 60 % 60), (unsigned long)(s % 60), net.nocarrier ? "NO CARRIER" : "hung up");
            nattr = 0x1A;
        }
        note = notebuf;
        history_clear();
    }
    com_close(0);
    mouse_show(0);
    scr_fill(0, 0, 80, 25, ' ', 0x07);
    scr_cursor(0, 0);
    scr_cursor_on(1);
    printf("Thank you for using ARM-DOS Online.\n");
    return 0;
}
