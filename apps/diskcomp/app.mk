# apps/diskcomp/app.mk - DISKCOMP.COM (see README.md); shares apps/format/dosutil.c.
$(call armdos_com,DISKCOMP,diskcomp.c dosutil_inc.c,-I$(here)../format -Wno-array-bounds)
