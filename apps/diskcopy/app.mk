# apps/diskcopy/app.mk - DISKCOPY.COM (see README.md); shares apps/format/dosutil.c.
$(call armdos_com,DISKCOPY,diskcopy.c dosutil_inc.c,-I$(here)../format -Wno-array-bounds)
