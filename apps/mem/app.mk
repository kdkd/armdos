# apps/mem/app.mk - MEM.EXE: Microsoft's MS-DOS 4.0 MEM (CMD/MEM/MEM.C),
# MIT-licensed source compiled for ARM.  See README.md.
MEM_DIR := $(here)
include apps/mslib/mslib.mk

$(call mslib_exe,MEM,$(MEM_DIR),src/mem.c,$(MSLIB_MSC_CFLAGS),src/MEM.SKL,--subst MSDOS=ARMDOS,)

.PHONY: mem-test
mem-test: $(BUILD)/MEM.EXE $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(MEM_DIR)tests/run.mjs
test: mem-test
