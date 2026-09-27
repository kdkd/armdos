# apps/fc/app.mk - FC.EXE: Microsoft's MS-DOS 4.0 FC (CMD/FC, the old MS
# "tools" file compare), MIT-licensed source compiled for ARM.  See README.md.
FC_DIR := $(here)
include apps/mslib/mslib.mk

FC_SRCS := src/fc.c src/error.c src/fgetl.c src/ntoi.c src/update.c src/kstring.c src/fcasm.c
$(call mslib_exe,FC,$(FC_DIR),$(FC_SRCS),$(MSLIB_MSC_CFLAGS) -DMSDOS -fno-builtin,,,)

.PHONY: fc-test
fc-test: $(BUILD)/FC.EXE $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(FC_DIR)tests/run.mjs
test: fc-test
