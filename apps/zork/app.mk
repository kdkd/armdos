# apps/zork/app.mk - ZORK1/2/3.EXE: MojoZork (zlib) + an Infocom-style DOS front end.
# One source, three programs; each opens its own story (ZORKn.DAT) from its
# own directory. See README.md.
ZORK_CFLAGS := -Wno-sign-compare -Wno-unused-function -Wno-implicit-fallthrough -Wno-array-bounds -Wno-format-truncation
$(call armdos_exe,ZORK1,zorkdos.c,$(ZORK_CFLAGS) -DSTORYNAME='"ZORK1"',,--stack 16384)
$(call armdos_exe,ZORK2,zorkdos.c,$(ZORK_CFLAGS) -DSTORYNAME='"ZORK2"',,--stack 16384)
$(call armdos_exe,ZORK3,zorkdos.c,$(ZORK_CFLAGS) -DSTORYNAME='"ZORK3"',,--stack 16384)
