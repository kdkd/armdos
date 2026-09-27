/*
 * buf.c - the sector buffers (BUFFERS=, DOS/BUF.ASM): a write-back cache
 * kept in most-recently-used order.  FAT sectors are written to every copy
 * of the FAT when they are flushed.
 */
#include "dos.h"

static struct buf *head;                /* MRU first */
static int nbufs;

void buf_init(struct buf *pool, int n)
{
    head = 0;
    nbufs = 0;
    buf_add_pool(pool, n);
}

void buf_add_pool(struct buf *pool, int n)
{
    for (int i = 0; i < n; i++) {
        struct buf *b = &pool[i];
        memset(b, 0, sizeof *b - SECSIZE);
        b->drive = 0xFF;
        /* append at the tail (least recently used) */
        if (!head) { head = b; b->next = b->prev = b; }
        else { b->prev = head->prev; b->next = head; head->prev->next = b; head->prev = b; }
        nbufs++;
    }
}

static void to_front(struct buf *b)
{
    if (b == head) return;
    b->prev->next = b->next;
    b->next->prev = b->prev;
    b->prev = head->prev;
    b->next = head;
    head->prev->next = b;
    head->prev = b;
    head = b;
}

static int write_buf(struct buf *b)
{
    struct dpb *d = b->dpb;
    int e;
    if (b->flags & BF_FAT) {
        /* every FAT copy */
        e = 0;
        for (int i = 0; i < d->nfats; i++) {
            int r = dsk_io(d, 1, b->sector + (uint32_t)i * d->fat_size, 1, b->data, AREA_FAT);
            if (r < 0) e = r;
        }
    } else {
        e = dsk_io(d, 1, b->sector, 1, b->data, (b->flags & BF_DIR) ? AREA_DIR : AREA_DATA);
    }
    b->flags &= ~BF_DIRTY;              /* written or given up on */
    return e;
}

int bufs_dirty(int drive)
{
    struct buf *b = head;
    if (!b) return 0;
    do {
        if (b->drive != 0xFF && (b->flags & BF_DIRTY) && (drive < 0 || b->drive == drive)) return 1;
        b = b->next;
    } while (b != head);
    return 0;
}

int flush_bufs(int drive)
{
    int e = 0;
    struct buf *b = head;
    if (!b) return 0;
    do {
        if (b->drive != 0xFF && (b->flags & BF_DIRTY) && (drive < 0 || b->drive == drive)) {
            int r = write_buf(b);
            if (r < 0) e = r;
        }
        b = b->next;
    } while (b != head);
    return e;
}

void dir_cache_reset(void);

void invalidate_bufs(int drive)
{
    struct buf *b = head;
    dir_cache_reset();
    if (!b) return;
    do {
        if (drive < 0 || b->drive == drive) { b->drive = 0xFF; b->flags = 0; }
        b = b->next;
    } while (b != head);
}

/*
 * The buffer holding `sector` of dpb's drive, read from the disk unless
 * noread (the caller will overwrite all of it).  NULL if the read failed
 * (the INT 24h was answered Fail).
 */
struct buf *getbuf(struct dpb *dpb, uint32_t sector, int area, int noread)
{
    struct buf *b = head;
    do {
        if (b->drive == dpb->drive && b->sector == sector) {
            to_front(b);
            return b;
        }
        b = b->next;
    } while (b != head);

    /* take the least recently used one */
    b = head->prev;
    if (b->drive != 0xFF && (b->flags & BF_DIRTY)) write_buf(b);
    b->drive = 0xFF;
    b->flags = 0;
    if (!noread) {
        if (dsk_io(dpb, 0, sector, 1, b->data, area) < 0) return 0;
    }
    b->drive = dpb->drive;
    b->dpb = dpb;
    b->sector = sector;
    b->flags = area == AREA_FAT ? BF_FAT : area == AREA_DIR ? BF_DIR : BF_DATA;
    to_front(b);
    return b;
}

void buf_dirty(struct buf *b)
{
    b->flags |= BF_DIRTY;
}

/* before a direct transfer of sectors [sector, sector+count): write any dirty
   buffered copies (read) or drop them (write) */
void buf_sync_range(struct dpb *dpb, uint32_t sector, unsigned count, int invalidate)
{
    struct buf *b = head;
    do {
        if (b->drive == dpb->drive && b->sector >= sector && b->sector < sector + count) {
            if (invalidate) { b->drive = 0xFF; b->flags = 0; }
            else if (b->flags & BF_DIRTY) write_buf(b);
        }
        b = b->next;
    } while (b != head);
}
