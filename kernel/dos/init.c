/*
 * init.c - ARMDOS.SYS initialisation (DOS/MSINIT.ASM "DOSINIT") and the
 * services SYSINIT uses to finish building the system: installable device
 * drivers, the FILES/FCBS/BUFFERS/LASTDRIVE tables, the memory arena.
 */
#include "dos.h"

struct lol LOL __attribute__((aligned(16), section(".lol"))) = { .share_retry = 3 };
struct dosvars DV;
struct dosinit *dinit;
uint32_t dos_image_start, dos_image_end;
uint16_t init_psp_seg;
struct cds *cds_tab;
int n_cds;

#define MAX_DPB     16
static struct dpb dpb_pool[MAX_DPB];
static int n_dpb;

#define INIT_SFTS   5
static struct { struct sftblock h; struct sft e[INIT_SFTS]; } PACKED sft_first;
static struct cds cds_init[5];
static struct buf buf_init_pool[2] __attribute__((aligned(4)));
static uint8_t init_psp[256] __attribute__((aligned(16)));

/* ------------------------------------------------------------- NUL */

static struct reqhdr *nul_req;
static void nul_strategy(struct reqhdr *r) { nul_req = r; }
static void nul_interrupt(void)
{
    struct reqhdr *r = nul_req;
    r->status = RS_DONE;
    if (r->cmd == CMD_READ) ((struct req_rw *)r)->count = 0;       /* always at end of file */
    else if (r->cmd == CMD_NDREAD) r->status |= RS_BUSY;
}

/* ------------------------------------------------------------- DPBs */

void build_dpb(struct dpb *d, const struct bpb *b)
{
    d->sector_size = b->bytes_per_sec;
    d->cluster_mask = b->sec_per_clus - 1;
    int shift = 0;
    while ((1 << shift) < b->sec_per_clus) shift++;
    d->cluster_shift = shift;
    d->first_fat = b->reserved;
    d->nfats = b->nfats;
    d->root_ents = b->root_ents;
    d->fat_size = b->fat_secs;
    d->dir_sector = b->reserved + b->nfats * b->fat_secs;
    d->first_data = d->dir_sector + (b->root_ents * 32 + 511) / 512;
    uint32_t total = b->total16 ? b->total16 : b->total32;
    d->max_cluster = (total - d->first_data) / b->sec_per_clus + 1;
    d->media = b->media;
    d->next_free = 0;
    d->free_count = 0xFFFF;
}

struct cds *get_cds(int drive)
{
    if (drive < 0 || drive >= n_cds) return 0;
    return &cds_tab[drive];
}

static void init_cds(struct cds *c, int drive, struct dpb *d)
{
    memset(c, 0, sizeof *c);
    c->path[0] = 'A' + drive;
    c->path[1] = ':';
    c->path[2] = '\\';
    c->flags = d ? CDS_VALID : 0;
    c->dpb = d;
    c->cluster = d ? 0 : 0xFFFF;
    c->bsoffset = 2;
}

static int add_units(struct devhdr *dev, int units, struct bpb **bpbs, int *toomany)
{
    int first = LOL.nblock;
    if (toomany) *toomany = 0;
    if (first + units > MAX_DPB || first + units > 26) { if (toomany) *toomany = 1; return -1; }
    for (int u = 0; u < units; u++) {
        struct dpb *d = &dpb_pool[n_dpb++];
        memset(d, 0, sizeof *d);
        d->drive = first + u;
        d->unit = u;
        d->driver = dev;
        if (bpbs && bpbs[u]) build_dpb(d, bpbs[u]);
        d->first_access = 0xFF;             /* build the BPB on first use */
        d->next = (struct dpb *)0xFFFFFFFFu;
        if (d->drive > 0) dpb_pool[d->drive - 1].next = d;
        else LOL.dpb_head = d;
        if (d->drive < n_cds) init_cds(&cds_tab[d->drive], d->drive, d);
    }
    LOL.nblock = first + units;
    return first;
}

/* ------------------------------------------------------ the API */

static void api_add_chardev(struct devhdr *d)
{
    d->next = LOL.nul.next;
    LOL.nul.next = d;
    if (d->attr & DEVA_STDIN) LOL.con = d;
    if (d->attr & DEVA_CLOCK) LOL.clock = d;
}

static int api_add_blockdev(struct devhdr *d, int units, struct bpb **bpbs, int *toomany)
{
    int first = add_units(d, units, bpbs, toomany);
    if (first < 0) return -1;
    /* link the driver into the device chain after NUL */
    d->next = LOL.nul.next;
    LOL.nul.next = d;
    return first;
}

static void devmark(uint32_t at, char id, uint32_t bytes, const char *name)
{
    struct devmark *m = (struct devmark *)at;
    memset(m, 0, sizeof *m);
    m->id = id;
    m->seg = (at >> 4) + 1;
    m->size = (bytes + 15) >> 4;
    if (name) memcpy(m->name, name, 8);
}

static uint32_t api_build_tables(const struct dosconfig *c, uint32_t at)
{
    at = (at + 15) & ~15u;
    /* FILES: the first 5 SFT entries are in the DOS; the rest in a second block */
    if (c->files > INIT_SFTS) {
        unsigned n = c->files - INIT_SFTS;
        uint32_t bytes = 6 + n * sizeof(struct sft);
        devmark(at, 'F', bytes, 0);
        struct sftblock *b = (struct sftblock *)(at + 16);
        memset(b, 0, bytes);
        b->next = (struct sftblock *)0xFFFFFFFFu;
        b->count = n;
        sft_first.h.next = b;
        at = (at + 16 + bytes + 15) & ~15u;
    }
    /* FCBS */
    {
        unsigned n = c->fcbs ? c->fcbs : 4;
        uint32_t bytes = 6 + n * sizeof(struct sft);
        devmark(at, 'X', bytes, 0);
        struct sftblock *b = (struct sftblock *)(at + 16);
        memset(b, 0, bytes);
        b->next = (struct sftblock *)0xFFFFFFFFu;
        b->count = n;
        LOL.fcb_sft = b;
        LOL.fcb_keep = c->fcbs_keep;
        at = (at + 16 + bytes + 15) & ~15u;
    }
    /* BUFFERS (the two DOS uses during start-up count as two of them) */
    {
        unsigned n = c->buffers ? c->buffers : 15;
        if (n > 2) {
            uint32_t bytes = (n - 2) * sizeof(struct buf);
            devmark(at, 'B', bytes, 0);
            buf_add_pool((struct buf *)(at + 16), n - 2);
            at = (at + 16 + bytes + 15) & ~15u;
        }
        LOL.buffers = n;
        LOL.lookahead = c->lookahead;
    }
    /* LASTDRIVE: the CDS array */
    {
        unsigned n = c->lastdrive;
        if (n < LOL.nblock) n = LOL.nblock;
        if (n > 26) n = 26;
        uint32_t bytes = n * sizeof(struct cds);
        devmark(at, 'L', bytes, 0);
        struct cds *t = (struct cds *)(at + 16);
        for (unsigned i = 0; i < n; i++) {
            if ((int)i < n_cds) t[i] = cds_tab[i];
            else init_cds(&t[i], i, 0);
        }
        cds_tab = t;
        n_cds = n;
        LOL.cds = t;
        LOL.lastdrive = n;
        at = (at + 16 + bytes + 15) & ~15u;
    }
    return at;
}

static void api_make_arena(uint16_t first, uint16_t sysparas, uint16_t end)
{
    mem_make_arena(first, sysparas, end);
    LOL.first_mcb = first;
}

static void api_extend_arena(uint16_t end) { mem_extend(end); }
static void api_set_break(int on) { break_on = on ? 1 : 0; }

static const struct dosapi api = {
    DOSINIT_MAGIC, &LOL, api_add_chardev, api_add_blockdev, api_build_tables,
    api_make_arena, api_extend_arena, set_shell, api_set_break, &nls,
};

/* ------------------------------------------------------------ entry */

extern char __image_end[], __bss_end__[];

void dos_entry(struct dosinit *di) __attribute__((section(".text.dos_entry")));
void dos_entry(struct dosinit *di)
{
    dinit = di;
    dos_image_start = di->dos_start;
    dos_image_end = di->dos_end;

    LOL.share_retry = 3;
    LOL.share_delay = 1;
    LOL.first_mcb = 0;
    LOL.nul.next = di->devchain;
    LOL.nul.attr = DEVA_CHAR | DEVA_NUL;
    LOL.nul.strategy = nul_strategy;
    LOL.nul.interrupt = nul_interrupt;
    memcpy(LOL.nul.name, "NUL     ", 8);
    LOL.clock = di->clock;
    LOL.con = di->con;
    LOL.max_sector = 512;
    LOL.sft_head = &sft_first.h;
    sft_first.h.next = (struct sftblock *)0xFFFFFFFFu;
    sft_first.h.count = INIT_SFTS;
    LOL.bootdrive = di->bootdrive + 1;
    {
        struct armregs r = { 0 };
        r.r0 = 0x8800;
        kint(0x15, &r);
        LOL.extmem_kb = r.r0 & 0xFFFF;
    }

    cds_tab = cds_init;
    n_cds = 5;
    for (int i = 0; i < 5; i++) init_cds(&cds_tab[i], i, 0);
    LOL.cds = cds_tab;
    LOL.lastdrive = 5;
    LOL.dpb_head = (struct dpb *)0xFFFFFFFFu;
    add_units(di->block, di->nunits, di->bpbs, 0);
    /* the fixed disk's BPB is already known */
    for (int i = 0; i < n_dpb; i++) {
        struct req_media q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q; q.h.unit = dpb_pool[i].unit; q.h.cmd = CMD_REMOVABLE;
        devcall(dpb_pool[i].driver, &q);
        if ((q.h.status & RS_BUSY) && di->bpbs[i] && di->bpbs[i]->bytes_per_sec == 512)
            dpb_pool[i].first_access = 0;
    }

    buf_init(buf_init_pool, 2);
    LOL.buffers = 2;

    /* the process that SYSINIT runs as */
    init_psp_seg = SEG(init_psp);
    cur_psp = init_psp_seg;
    new_psp(init_psp_seg, 0xA000, 0);
    struct psp *p = PSP(init_psp_seg);
    p->parent = init_psp_seg;
    cur_dta = (uint32_t)init_psp + 0x80;
    cur_drive = di->bootdrive;
    switchar = '/';

    /* the DOS interrupts */
    IVT[0x20] = (uint32_t)int20_handler;
    IVT[0x21] = (uint32_t)int21_handler;
    IVT[0x22] = (uint32_t)int22_default;
    IVT[0x23] = (uint32_t)int23_default;
    IVT[0x24] = (uint32_t)int24_default;
    IVT[0x25] = (uint32_t)int25_handler;
    IVT[0x26] = (uint32_t)int26_handler;
    IVT[0x27] = (uint32_t)int27_handler;
    IVT[0x28] = (uint32_t)int28_handler;
    IVT[0x2F] = (uint32_t)int2f_handler;
    p->int22 = IVT[0x22];
    p->int23 = IVT[0x23];
    p->int24 = IVT[0x24];
    fault_install();

    di->api = &api;
}
