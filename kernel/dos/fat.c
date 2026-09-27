/*
 * fat.c - the file allocation table (DOS/FAT.ASM): 12- and 16-bit entries,
 * cluster allocation, freeing chains and counting free space.
 */
#include "dos.h"

int dpb_fat16(const struct dpb *d) { return d->max_cluster >= 4086; }

int fat_eof(const struct dpb *d, unsigned v)
{
    if (dpb_fat16(d)) return v >= 0xFFF8;
    return v >= 0xFF8;
}

unsigned fat_eofval(const struct dpb *d) { return dpb_fat16(d) ? 0xFFFF : 0xFFF; }

uint32_t clus2sec(const struct dpb *d, unsigned cl)
{
    return d->first_data + ((uint32_t)(cl - 2) << d->cluster_shift);
}

static int fat_byte(struct dpb *d, uint32_t off, uint8_t **pp, struct buf **pb)
{
    struct buf *b = getbuf(d, d->first_fat + off / 512, AREA_FAT, 0);
    if (!b) return -1;
    *pb = b;
    *pp = &b->data[off % 512];
    return 0;
}

uint32_t fat_get(struct dpb *d, unsigned cl)
{
    struct buf *b;
    uint8_t *p;
    if (cl < 2 || cl > d->max_cluster) return fat_eofval(d);
    if (dpb_fat16(d)) {
        uint32_t off = cl * 2;
        if (fat_byte(d, off, &p, &b)) return 0xFFFFFFFFu;
        return p[0] | (p[1] << 8);
    }
    uint32_t off = cl + cl / 2;
    unsigned lo, hi;
    if (fat_byte(d, off, &p, &b)) return 0xFFFFFFFFu;
    lo = *p;
    if (fat_byte(d, off + 1, &p, &b)) return 0xFFFFFFFFu;
    hi = *p;
    unsigned v = lo | (hi << 8);
    return (cl & 1) ? v >> 4 : v & 0xFFF;
}

int fat_set(struct dpb *d, unsigned cl, unsigned v)
{
    struct buf *b;
    uint8_t *p;
    if (cl < 2 || cl > d->max_cluster) return -E_BADDATA;
    if (dpb_fat16(d)) {
        if (fat_byte(d, cl * 2, &p, &b)) return -E_GENFAIL;
        p[0] = v; p[1] = v >> 8;
        buf_dirty(b);
        return 0;
    }
    uint32_t off = cl + cl / 2;
    v &= 0xFFF;
    if (fat_byte(d, off, &p, &b)) return -E_GENFAIL;
    if (cl & 1) *p = (*p & 0x0F) | ((v & 0x0F) << 4);
    else *p = v & 0xFF;
    buf_dirty(b);
    if (fat_byte(d, off + 1, &p, &b)) return -E_GENFAIL;
    if (cl & 1) *p = v >> 4;
    else *p = (*p & 0xF0) | (v >> 8);
    buf_dirty(b);
    return 0;
}

/* allocate a free cluster, chain it after prev (0 = start a chain);
   returns the cluster, 0 if the disk is full, or -err */
int fat_alloc(struct dpb *d, unsigned prev)
{
    unsigned max = d->max_cluster;
    unsigned start = d->next_free;
    if (start < 2 || start > max) start = 2;
    unsigned cl = start;
    for (unsigned n = 0; n < max - 1; n++) {
        uint32_t v = fat_get(d, cl);
        if (v == 0xFFFFFFFFu) return -E_GENFAIL;
        if (v == 0) {
            int e = fat_set(d, cl, fat_eofval(d));
            if (e) return e;
            if (prev && (e = fat_set(d, prev, cl))) return e;
            d->next_free = cl;
            if (d->free_count != 0xFFFF && d->free_count) d->free_count--;
            return cl;
        }
        if (++cl > max) cl = 2;
    }
    d->free_count = 0;
    return 0;
}

void dir_cache_reset(void);

int fat_free_chain(struct dpb *d, unsigned cl)
{
    dir_cache_reset();
    int guard = d->max_cluster;
    while (cl >= 2 && cl <= d->max_cluster && guard-- > 0) {
        uint32_t next = fat_get(d, cl);
        if (next == 0xFFFFFFFFu) return -E_GENFAIL;
        int e = fat_set(d, cl, 0);
        if (e) return e;
        if (d->free_count != 0xFFFF) d->free_count++;
        if (fat_eof(d, next) || next == 0) break;
        cl = next;
    }
    return 0;
}

int fat_count_free(struct dpb *d)
{
    if (d->free_count != 0xFFFF) return d->free_count;
    unsigned n = 0;
    for (unsigned cl = 2; cl <= d->max_cluster; cl++) {
        uint32_t v = fat_get(d, cl);
        if (v == 0xFFFFFFFFu) return -E_GENFAIL;
        if (v == 0) n++;
    }
    d->free_count = n;
    return n;
}

/* follow a chain n links from start; *out = the cluster (0 if the chain is shorter) */
int fat_walk(struct dpb *d, unsigned start, unsigned n, unsigned *out)
{
    unsigned cl = start;
    while (n--) {
        uint32_t v = fat_get(d, cl);
        if (v == 0xFFFFFFFFu) return -E_GENFAIL;
        if (fat_eof(d, v) || v < 2) { *out = 0; return 0; }
        cl = v;
    }
    *out = cl;
    return 0;
}
