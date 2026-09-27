/* bbs.h - The ARM Pit BBS (apps/bbs): shared declarations. */
#ifndef BBS_H
#define BBS_H
#include <stdint.h>
#include "sio.h"

#define BBS_VERSION "1.0"

/* ---- configuration (BBS.CFG, AREAS.CFG) ---- */
struct cfg {
    char name[40];          /* "The ARM Pit" */
    char sysop[36];
    char location[40];
    int  port;
    long baud;
    int  timelimit;         /* minutes per call */
    int  newsec;            /* security level of new users */
    char init[48];          /* modem init string (after ATZ) */
    int  node;
};
extern struct cfg cfg;

#define MAXAREAS 8
struct area { int num; char name[40]; char path[48]; char extra[48]; char desc[64]; };
extern struct area msgareas[MAXAREAS], fileareas[MAXAREAS], doors[MAXAREAS];
extern int nmsgareas, nfileareas, ndoors;

/* ---- users (USERS.DAT, fixed 256-byte records) ---- */
struct user {
    char name[36];
    char city[32];
    char phone[16];
    char birth[10];
    char password[16];
    char computer[32];
    uint16_t sec, lines, calls, msgs, ups, downs;
    uint32_t upk, downk;
    uint8_t ansi, expert, deleted, hotkeys;
    char first[10];         /* first call MM-DD-YY */
    char last[16];          /* last call MM-DD-YY HH:MM */
    uint16_t lastread[MAXAREAS];
    uint16_t mins_today;
    char today[10];
};
union userrec { struct user u; char pad[256]; };
extern struct user user;    /* the caller */
extern int usernum;         /* record number, -1 = none */

int  user_find(const char *name, struct user *u);
int  user_count(void);
int  user_get(int n, struct user *u);
int  user_save(int n, const struct user *u);   /* n = -1: append, returns record */
void user_seed(void);

/* ---- helpers ---- */
void today_str(char *b);    /* MM-DD-YY */
void now_str(char *b);      /* MM-DD-YY HH:MM */
void time_str(char *b);     /* HH:MM:SS */
void commas(char *b, long v);
void strupr_s(char *s);
void sysop_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void local_status(void);

/* ---- sections ---- */
void msg_menu(void);
int  msg_count(int area);
int  msg_post(int area, const char *from, const char *to, const char *subj, char lines[][80], int n);
void comment_to_sysop(void);
void file_menu(void);
int  file_count(void);
void door_menu(void);
void bulletins(int at_logon);
void whos_online(void);
void userlog(void);
void settings(void);
void last_callers(void);
void lastcall_add(void);
void sysop_note(void);

/* file transfer glue (xfer.c) */
int  xfer_send(int proto, char *const *paths, int n);
int  xfer_recv(int proto, const char *dir, char *gotname, int gotmax);
int  pick_proto(void);
extern long session_baud;
#endif
