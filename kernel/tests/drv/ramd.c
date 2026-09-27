/* ramd.c - K_RAMD.SYS, a test DEVICE= block driver: a 64 KB RAM disk
   (FAT12, 128 sectors), formatted at INIT with the label RAMDISK. */
#include "klib.h"

#define SECTORS 128
static void strategy(struct reqhdr *r);
static void interrupt(void);
__attribute__((section(".devhdr"), used))
struct devhdr ramd_header = {
    DEV_END, DEVA_32BIT, 0, strategy, interrupt, "\1RAMDSK"
};

static struct bpb bpb = { 512, 1, 1, 1, 32, SECTORS, 0xF8, 1, 1, 1, 0, 0 };
static struct bpb *bpbs[1] = { &bpb };
static uint8_t disk[SECTORS * 512] __attribute__((aligned(4)));
static struct reqhdr *req;

static void strategy(struct reqhdr *r) { req = r; }
extern char __bss_end__[];

static void interrupt(void)
{
    struct reqhdr *r = req;
    r->status = RS_DONE;
    switch (r->cmd) {
    case CMD_INIT: {
        struct req_init *q = (struct req_init *)r;
        memset(disk, 0, sizeof disk);
        disk[0] = 0x0E; disk[1] = 0; disk[2] = 0; disk[3] = 0xEA;      /* b 0x40 */
        memcpy(disk + 3, "\xEA" "RAMDISK", 8);
        memcpy(disk + 0x0B, &bpb, sizeof bpb);
        disk[0x26] = 0x29;
        memcpy(disk + 0x2B, "RAMDISK    FAT12   ", 19);
        disk[510] = 0x55; disk[511] = 0xAA;
        disk[512] = 0xF8; disk[513] = 0xFF; disk[514] = 0xFF;          /* FAT */
        memcpy(disk + 1024, "RAMDISK    \x08", 12);                   /* root: label */
        q->units = 1;
        q->arg = (uint32_t)bpbs;
        q->brk = (uint32_t)__bss_end__;
        static const char msg[] = "RAM disk installed as drive ?:\r\n";
        char m[40];
        memcpy(m, msg, sizeof msg);
        *strchr(m, '?') = 'A' + q->drive;
        struct armregs a = { 0 };
        a.r0 = 0x4000; a.r1 = 1; a.r2 = sizeof msg - 1; a.r3 = (uint32_t)m;
        kint(0x21, &a);
        break;
    }
    case CMD_MEDIA:
        ((struct req_media *)r)->changed = 1;
        break;
    case CMD_BPB:
        ((struct req_bpb *)r)->bpb = (uint32_t)&bpb;
        break;
    case CMD_READ: case CMD_WRITE: case CMD_WRITEV: {
        struct req_rw *q = (struct req_rw *)r;
        uint32_t s = q->start == 0xFFFF ? q->start32 : q->start;
        if (s + q->count > SECTORS) { q->count = 0; r->status |= RS_ERROR | DE_NOTFOUND; break; }
        if (r->cmd == CMD_READ) memcpy((void *)q->addr, disk + s * 512, q->count * 512);
        else memcpy(disk + s * 512, (void *)q->addr, q->count * 512);
        break;
    }
    default:
        r->status |= RS_ERROR | DE_BADCMD;
    }
}
