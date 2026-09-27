/* doc.c - documents as they arrive from the service: the text (lines with the
 * style/link codes of emu/online/doc.mjs), the line index and the links on each line. */
#include <stdlib.h>
#include <string.h>
#include "online.h"

struct doc *doc_new(uint16_t id, char kind, const char *title, const char *channel)
{
    struct doc *d = calloc(1, sizeof *d);
    if (!d) return NULL;
    d->id = id; d->kind = kind; d->sel = -1;
    strncpy(d->title, title, sizeof d->title - 1);
    strncpy(d->channel, channel, sizeof d->channel - 1);
    d->cap = 4096; d->buf = malloc(d->cap);
    d->capl = 256; d->lines = malloc(d->capl * sizeof *d->lines);
    d->caplk = 64; d->lk = malloc(d->caplk * sizeof *d->lk);
    if (!d->buf || !d->lines || !d->lk) { doc_free(d); return NULL; }
    return d;
}

void doc_free(struct doc *d)
{
    if (!d) return;
    free(d->buf); free(d->lines); free(d->lk); free(d);
}

static void add_link(struct doc *d, int line, int col, int len, int num, int cont, int style)
{
    if (d->nlk == d->caplk) {
        struct linkocc *n = realloc(d->lk, d->caplk * 2 * sizeof *n);
        if (!n) return;
        d->lk = n; d->caplk *= 2;
    }
    struct linkocc *o = &d->lk[d->nlk++];
    o->line = (uint16_t)line; o->col = (uint8_t)col; o->len = (uint8_t)len; o->num = (uint16_t)num; o->cont = (uint8_t)cont; o->style = (uint8_t)style;
}

/* index the line that starts at `start` and ends before `end` (the LF) */
static void index_line(struct doc *d, int start, int end)
{
    if (d->nlines == d->capl) {
        int32_t *n = realloc(d->lines, d->capl * 2 * sizeof *n);
        if (!n) return;
        d->lines = n; d->capl *= 2;
    }
    int line = d->nlines;
    d->lines[d->nlines++] = start;
    int col = 0, num = 0, lstart = 0, style = 'n', lstyle = 'l';
    for (int i = start; i < end; i++) {
        uint8_t c = (uint8_t)d->buf[i];
        if (c == 1 && i + 2 < end) {
            num = ((uint8_t)d->buf[i + 1] - 32) * 96 + ((uint8_t)d->buf[i + 2] - 32);
            lstart = col; lstyle = 'l';
            i += 2;
        } else if (c == 2) {
            if (num) {
                /* a link that was open at the end of the line before continues here */
                int cont = 0;
                if (d->nlk && d->lk[d->nlk - 1].num == num && d->lk[d->nlk - 1].line == line - 1) cont = 1;
                add_link(d, line, lstart, col - lstart, num, cont, lstyle);
            }
            num = 0;
        } else if (c == 3 && i + 1 < end) {
            style = (uint8_t)d->buf[++i];
            if (num && col == lstart) lstyle = style;
        } else col++;
    }
    (void)style;
}

void doc_append(struct doc *d, const uint8_t *p, int n)
{
    if (d->len + n > d->cap) {
        int nc = d->cap;
        while (d->len + n > nc) nc *= 2;
        char *nb = realloc(d->buf, nc);
        if (!nb) return;
        d->buf = nb; d->cap = nc;
    }
    memcpy(d->buf + d->len, p, n);
    d->len += n;
    for (int i = d->scan; i < d->len; i++) {
        if (d->buf[i] == '\n') {
            index_line(d, d->scan, i);
            d->scan = i + 1;
        }
    }
}

const char *doc_line(struct doc *d, int i, int *len)
{
    if (i < 0 || i >= d->nlines) { *len = 0; return ""; }
    int s = d->lines[i];
    int e = s;
    while (e < d->len && d->buf[e] != '\n') e++;
    *len = e - s;
    return d->buf + s;
}

int doc_plain(struct doc *d, int i, char *out, int max)
{
    int len, n = 0;
    const char *s = doc_line(d, i, &len);
    for (int k = 0; k < len && n < max - 1; k++) {
        uint8_t c = (uint8_t)s[k];
        if (c == 1) { k += 2; continue; }
        if (c == 2) continue;
        if (c == 3) { k++; continue; }
        out[n++] = (char)c;
    }
    out[n] = 0;
    return n;
}
