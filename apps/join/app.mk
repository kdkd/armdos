# apps/join/app.mk - JOIN.EXE: Microsoft's MS-DOS 4.0 JOIN (CMD/JOIN/JOIN.C with
# INC/CDS.C DPB.C ERRTST.C SYSVAR.C), MIT-licensed source compiled for ARM.
JOIN_DIR := $(here)
include apps/mslib/mslib.mk

$(call mslib_exe,JOIN,$(JOIN_DIR),src/join.c src/cds.c src/dpb.c src/errtst.c src/sysvar.c src/armdos.c,$(MSLIB_MSC_CFLAGS) -Dpascal= -fcommon,src/JOIN.SKL,,)

.PHONY: join-test
join-test: $(BUILD)/JOIN.EXE $(BUILD)/COMMAND.COM $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(JOIN_DIR)tests/run.mjs
test: join-test
