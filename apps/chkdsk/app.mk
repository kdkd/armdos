# apps/chkdsk/app.mk - CHKDSK.COM (see README.md); shares apps/format/dosutil.c.
$(call armdos_com,CHKDSK,chkdsk.c dosutil_inc.c,-I$(here)../format -Wno-array-bounds)
