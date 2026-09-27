# apps/sys/app.mk - SYS.COM (see README.md); shares apps/format's runtime and
# the embedded ARM boot sector (build/bootsect.bin).
$(call armdos_com,SYS,sys.c dosutil_inc.c bootrec_inc.c bootsect_inc.S,-I$(here)../format -Wno-array-bounds -DBOOTSECT_BIN='"$(BUILD)/bootsect.bin"')
$(BUILD)/obj/SYS/bootsect_inc.S.o: $(BUILD)/bootsect.bin
