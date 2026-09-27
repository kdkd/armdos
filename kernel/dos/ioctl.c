/* ioctl.c - INT 21h AH=44h (DOS/IOCTL.ASM) */
#include "dos.h"

static struct dpb *ioctl_dpb(int bl, int *err)
{
    int drive = bl ? bl - 1 : cur_drive;
    struct cds *c = get_cds(drive);
    if (c && (c->flags & CDS_VALID) && (c->flags & CDS_NET)) { *err = E_INVFN; return 0; }   /* redirected */
    if (!c || !(c->flags & CDS_VALID) || !c->dpb) { *err = E_BADDRIVE; return 0; }
    return c->dpb;
}

/* a device error from generic IOCTL: DOS 4 returns the driver's code + 13h */
static void deverr(struct armregs *f, int st)
{
    int code = (st & 0xFF) + 0x13;
    set_exterr(code, 0, 0, 0);
    exterr.volptr = 0;
    f->r0 = code;
    f->cpsr |= CPSR_C;
}

static int gen_call(struct devhdr *d, int unit, int cat, int minor, uint32_t data, struct armregs *f)
{
    struct req_gioctl q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.unit = unit;
    q.h.cmd = CMD_GENIOCTL;
    q.category = cat;
    q.minor = minor;
    q.si = f->r4;
    q.di = f->r5;
    q.data = data;
    return devcall(d, &q);
}

void ioctl_fn(struct armregs *f)
{
    struct sft *s;
    struct dpb *d;
    int err, st;
    switch (AL(f)) {
    case 0x00:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); return; }
        if (s->flags & SF_DEVICE) {
            struct devhdr *dv = (struct devhdr *)s->devptr;
            f->r3 = (s->flags & 0xFF) | (dv->attr & 0x4000) | (s->flags & SF_REMOTE);
        } else f->r3 = s->flags & 0xFFFF & ~SF_NODATE;
        f->r0 = f->r3;
        return;
    case 0x01:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); return; }
        if (DH(f)) { sys_err(f, E_BADDATA); return; }
        if (!(s->flags & SF_DEVICE)) { sys_err(f, E_INVFN); return; }
        s->flags = (s->flags & 0xFF00) | (DL(f) & 0xFF) | SF_DEVICE;
        return;
    case 0x02: case 0x03: {
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); return; }
        if (!(s->flags & SF_DEVICE)) { sys_err(f, E_INVFN); return; }
        struct devhdr *dv = (struct devhdr *)s->devptr;
        if (!(dv->attr & DEVA_IOCTL)) { sys_err(f, E_INVFN); return; }
        struct req_rw q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.cmd = AL(f) == 2 ? CMD_IOCTL_IN : CMD_IOCTL_OUT;
        q.addr = f->r3;
        q.count = CX(f);
        st = devcall(dv, &q);
        if (st & RS_ERROR) { sys_err(f, E_ACCESS); return; }
        f->r0 = q.count;
        return;
    }
    case 0x04: case 0x05: {
        d = ioctl_dpb(BL(f), &err);
        if (!d) { sys_err(f, err); return; }
        if (!(d->driver->attr & DEVA_IOCTL)) { sys_err(f, E_INVFN); return; }
        struct req_rw q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.unit = d->unit;
        q.h.cmd = AL(f) == 4 ? CMD_IOCTL_IN : CMD_IOCTL_OUT;
        q.addr = f->r3;
        q.count = CX(f);
        st = devcall(d->driver, &q);
        if (st & RS_ERROR) { sys_err(f, E_ACCESS); return; }
        f->r0 = q.count;
        return;
    }
    case 0x06: case 0x07:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); return; }
        if (!(s->flags & SF_DEVICE)) {
            if (AL(f) == 6) set_al(f, s->position < s->size ? 0xFF : 0x00);
            else set_al(f, 0xFF);
            return;
        } else {
            struct devhdr *dv = (struct devhdr *)s->devptr;
            struct req_ndread q;
            memset(&q, 0, sizeof q);
            q.h.len = sizeof q;
            q.h.cmd = AL(f) == 6 ? CMD_NDREAD : CMD_OUTSTAT;
            st = devcall(dv, &q);
            set_al(f, (st & RS_BUSY) ? 0x00 : 0xFF);
        }
        return;
    case 0x08:
        d = ioctl_dpb(BL(f), &err);
        if (!d) { sys_err(f, err); return; }
        if (!(d->driver->attr & DEVA_OPENCLOSE)) { sys_err(f, E_INVFN); return; }
        {
            struct reqhdr q;
            memset(&q, 0, sizeof q);
            q.len = 13; q.unit = d->unit; q.cmd = CMD_REMOVABLE;
            st = devcall(d->driver, &q);
            f->r0 = (st & RS_BUSY) ? 1 : 0;
        }
        return;
    case 0x09:
        if (drive_remote(BL(f) ? BL(f) - 1 : cur_drive)) { f->r3 = 0x1000; return; }   /* remote */
        d = ioctl_dpb(BL(f), &err);
        if (!d) { sys_err(f, err); return; }
        f->r3 = d->driver->attr & ~0x1000;
        {
            struct cds *c = get_cds(BL(f) ? BL(f) - 1 : cur_drive);
            if (c && (c->flags & CDS_SUBST)) f->r3 |= 0x8000;   /* a SUBSTed drive */
        }
        return;
    case 0x0A:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); return; }
        f->r3 = s->flags;
        return;
    case 0x0B:
        return;
    case 0x0C: {
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); return; }
        if (!(s->flags & SF_DEVICE)) { sys_err(f, E_INVFN); return; }
        struct devhdr *dv = (struct devhdr *)s->devptr;
        if (!(dv->attr & DEVA_GENIOCTL)) { sys_err(f, E_INVFN); return; }
        st = gen_call(dv, 0, CH(f), CL(f), f->r3, f);
        if (st & RS_ERROR) deverr(f, st);
        return;
    }
    case 0x0D:
        d = ioctl_dpb(BL(f), &err);
        if (!d) { sys_err(f, err); return; }
        if (!(d->driver->attr & DEVA_GENIOCTL)) { sys_err(f, E_INVFN); return; }
        st = gen_call(d->driver, d->unit, CH(f), CL(f), f->r3, f);
        if (st & RS_ERROR) deverr(f, st);
        else if (CL(f) == 0x40 || CL(f) == 0x46) {
            d->first_access = 0xFF;             /* the parameters changed: rebuild */
        }
        return;
    case 0x0E: case 0x0F: {
        d = ioctl_dpb(BL(f), &err);
        if (!d) { sys_err(f, err); return; }
        if (!(d->driver->attr & DEVA_GENIOCTL)) { set_al(f, 0); return; }
        struct reqhdr q;
        memset(&q, 0, sizeof q);
        q.len = 13; q.unit = d->unit; q.cmd = AL(f) == 0x0E ? CMD_GETLOG : CMD_SETLOG;
        st = devcall(d->driver, &q);
        if (st & RS_ERROR) { sys_err(f, E_INVFN); return; }
        set_al(f, q.unit);
        if (AL(f) && AL(f) - 1 != d->drive) {
            /* the physical drive now answers to another letter: forget its buffers */
            invalidate_bufs(d->drive);
            d->first_access = 0xFF;
        }
        return;
    }
    default:
        sys_err(f, E_INVFN);
    }
}
