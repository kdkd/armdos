/* online.h - ARM-DOS Online, the client (ONLINE.EXE). See README.md. */
#ifndef ONLINE_H
#define ONLINE_H
#include <stdint.h>
#include "../term/lib/comm.h"
#include "../term/lib/scr.h"

#define ONLINE_VERSION "1.00"

/* ------------------------------------------------------------ configuration (main.c) */
struct config {
    char name[17];
    char number[32];
    char init[48];
    int  port;
    long baud;
};
extern struct config cfg;

/* ------------------------------------------------------------ documents (doc.c) */
struct linkocc {
    uint16_t line;
    uint8_t  col, len;
    uint16_t num;
    uint8_t  cont;          /* continues a link from the line before */
    uint8_t  style;         /* 'l' link, 'p' picture, ... */
};
struct doc {
    uint16_t id;
    char kind;              /* E encyclopedia, L list, D dictionary, W weather, N news, T today */
    char title[72];
    char channel[24];
    char *buf; int len, cap;
    int32_t *lines; int nlines, capl;       /* start offsets of complete lines */
    int scan;
    struct linkocc *lk; int nlk, caplk;
    int complete;
    int partial;            /* cancelled before the end: fetched again on Back */
    char rtype; uint8_t req[80]; int reqn;   /* the request that brought it */
    int top, sel;           /* reader state: first line shown, selected link (index into lk, -1 none) */
};
struct doc *doc_new(uint16_t id, char kind, const char *title, const char *channel);
void doc_free(struct doc *d);
void doc_append(struct doc *d, const uint8_t *p, int n);
const char *doc_line(struct doc *d, int i, int *len);
int  doc_plain(struct doc *d, int i, char *out, int max);   /* a line without codes */

/* ------------------------------------------------------------ the line (link.c) */
struct net {
    int  online;            /* carrier up and signed on */
    int  carrier;
    long rate;              /* from CONNECT */
    uint32_t t0;            /* BIOS ticks at CONNECT */
    int  lost;              /* carrier dropped unexpectedly */
    int  discard;           /* waiting for Z after a cancel */
    int  waiting;           /* a request is out */
    char status[80];        /* the service's 'S' text */
    int  msg_pending; char msg_class; char msg[400];
    int  welcome; char member[20]; char welcome_text[200];
    int  goodbye; char goodbye_text[120];
    struct doc *incoming;   /* document being received */
    struct doc *newdoc;     /* a document header just arrived (the UI takes it) */
    uint32_t rxbytes;
    char result[40];        /* last modem result */
    int  nocarrier;         /* the modem said NO CARRIER */
    char lastreq_type; uint8_t lastreq[80]; int lastreq_n;
};
extern struct net net;

int  modem_open(void);
/* dial: progress(step, text) is called: 1 dialling, 2 connecting (CONNECT seen) */
int  modem_dial(void (*progress)(int step, const char *text));   /* 0 ok, else error text in net.result */
void modem_hangup(void);
void net_reset(void);
void net_poll(void);
void net_send(char type, const void *payload, int n);
void net_request(char type, const char *s);
void net_follow(uint16_t docid, uint16_t link);
void net_cancel(void);
void net_resend(struct doc *d);   /* ask again for what brought d */
int  net_signon(const char *name, const char *pass);            /* 1 ok */
uint32_t online_seconds(void);

/* ------------------------------------------------------------ the picture viewer (viewer.c) */
void viewer_begin(const uint8_t *p, int n);
void viewer_byte(uint8_t b);
void viewer_end(void);
extern int viewer_pending;          /* a 'G' arrived: the UI should run viewer_run() */
void viewer_run(void);              /* until a key; returns in text mode */

/* ------------------------------------------------------------ screen (ui.c) */
#define ATTR_BAR     0x70
#define ATTR_LOGO    0x4F
#define ATTR_DESK    0x17
#define ATTR_TITLE   0x3F
#define ATTR_HELP    0x30
#define ATTR_BOX     0x70
#define ATTR_BOXHI   0x7F
#define ATTR_FIELD   0x1F
#define ATTR_SEL     0x70

void ui_titlebar(const char *channel);
void ui_statusbar(void);
void ui_help(const char *text);
void ui_tick(void);                   /* clock + online time, once in a while */
int  ui_message(const char *title, const char *text, int attr);   /* waits for a key; returns it */
int  ui_prompt(const char *title, const char *label, char *buf, int max);  /* 1 = Enter */
void ui_logo(int y, int bg);
int  ui_wait_key(void);               /* keys while keeping the line alive; -1 on carrier loss */
int  ui_idle_key(void);               /* one pass: polls line + mouse, returns a key or 0 */

/* mouse (ui.c) */
extern int mouse_present;
void mouse_init(void);
void mouse_show(int on);
int  mouse_click(int *x, int *y);     /* 1 = left button went down since last call */

/* reader (reader.c) */
int  reader_run(void);                /* 0 = back to the menu, 1 = sign off, 2 = line lost */
void reader_open(char type, const char *arg);   /* start a request, show the reader */
void history_clear(void);

/* main.c */
void save_config(void);
void print_doc(struct doc *d);
#define KEY_LOST (-1)
#endif
