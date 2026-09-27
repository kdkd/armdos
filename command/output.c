/*
 * output.c - console/handle output and the message retriever's number, date
 * and time formatting rules (as DOS 4.00's message retriever).
 */
#include "cmd.h"

uint8_t country[34];

void load_country(void)
{
    uint8_t buf[34];
    if (dos_country(buf) == 0) memcpy(country, buf, sizeof buf);
    if (!country[0x0B]) country[0x0B] = '-';
    if (!country[0x0D]) country[0x0D] = ':';
    if (!country[0x09]) country[0x09] = '.';
}

void outn(int h, const char *s, unsigned n)
{
    if (n) dos_write(h, s, n);
}

void out(int h, const char *s)
{
    outn(h, s, strlen(s));
}

void outc(int h, int c)
{
    char ch = (char)c;
    dos_write(h, &ch, 1);
}

void crlf(int h)
{
    outn(h, "\r\n", 2);
}

void crlf2(void)
{
    crlf(1);
}

/* unsigned decimal, right-aligned in width with pad (width 0 = no padding) */
char *fmt_uint(char *buf, unsigned long v, int width, int pad)
{
    char tmp[16];
    int n = 0;
    do {
        tmp[n++] = '0' + v % 10;
        v /= 10;
    } while (v);
    int i = 0;
    while (width > n) {
        buf[i++] = pad;
        width--;
    }
    while (n) buf[i++] = tmp[--n];
    buf[i] = 0;
    return buf;
}

char *fmt_hex4(char *buf, unsigned v)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < 4; i++) buf[i] = hx[(v >> (12 - 4 * i)) & 15];
    buf[4] = 0;
    return buf;
}

static char *two(char *p, int v)
{
    *p++ = '0' + (v / 10) % 10;
    *p++ = '0' + v % 10;
    return p;
}

/* $M_DATE_REPLACE: every component zero padded to 2 digits, year 2 or 4 */
void fmt_date(char *buf, int y, int m, int d, int year4)
{
    int fmt = country[0] | (country[1] << 8);
    int sep = country[0x0B];
    char ys[8];
    char *p;
    if (year4) {
        fmt_uint(ys, y, 4, '0');
    } else {
        ys[0] = '0' + (y / 10) % 10;
        ys[1] = '0' + y % 10;
        ys[2] = 0;
    }
    p = buf;
    if (fmt == 2) {                         /* Japan y-m-d */
        strcpy(p, ys);
        p += strlen(ys);
        *p++ = sep;
        p = two(p, m);
        *p++ = sep;
        p = two(p, d);
    } else if (fmt == 1) {                  /* Europe d-m-y */
        p = two(p, d);
        *p++ = sep;
        p = two(p, m);
        *p++ = sep;
        strcpy(p, ys);
        p += strlen(ys);
    } else {                                /* USA m-d-y */
        p = two(p, m);
        *p++ = sep;
        p = two(p, d);
        *p++ = sep;
        strcpy(p, ys);
        p += strlen(ys);
    }
    *p = 0;
}

/* $M_TIME_REPLACE: hour not padded; minutes, seconds, hundredths are.
 * fields: 2 = hh:mm, 3 = hh:mm:ss, 4 = hh:mm:ss.hh.  h12: use the country's
 * 12/24 hour setting (the _Cty formats); 0 = always 24 hour. */
void fmt_time(char *buf, int h, int m, int s, int hs, int fields, int h12)
{
    char *p = buf;
    int ampm = 0;
    if (h12 && !(country[0x11] & 1)) {
        ampm = h >= 12 ? 'p' : 'a';
        if (h == 0) h = 12;
        else if (h > 12) h -= 12;
    }
    if (h >= 10) *p++ = '0' + h / 10;
    *p++ = '0' + h % 10;
    *p++ = country[0x0D];
    p = two(p, m);
    if (fields >= 3) {
        *p++ = country[0x0D];
        p = two(p, s);
    }
    if (fields >= 4) {
        *p++ = country[0x09];
        p = two(p, hs);
    }
    if (ampm) *p++ = ampm;
    *p = 0;
}
