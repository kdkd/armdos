# apps/subst/app.mk - SUBST.EXE: Microsoft's MS-DOS 4.0 SUBST (CMD/SUBST/SUBST.C with
# INC/CDS.C DPB.C ERRTST.C SYSVAR.C), MIT-licensed source compiled for ARM.
SUBST_DIR := $(here)
include apps/mslib/mslib.mk

$(call mslib_exe,SUBST,$(SUBST_DIR),src/subst.c src/cds.c src/dpb.c src/errtst.c src/sysvar.c src/armdos.c,$(MSLIB_MSC_CFLAGS) -Dpascal= -fcommon,src/SUBST.SKL,,)

.PHONY: subst-test
subst-test: $(BUILD)/SUBST.EXE $(BUILD)/COMMAND.COM $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(SUBST_DIR)tests/run.mjs
test: subst-test
