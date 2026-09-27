# apps/attrib/app.mk - ATTRIB.EXE: Microsoft's MS-DOS 4.0 ATTRIB
# (CMD/ATTRIB/ATTRIB.C), MIT-licensed source compiled for ARM.  See README.md.
ATTRIB_DIR := $(here)
include apps/mslib/mslib.mk

$(call mslib_exe,ATTRIB,$(ATTRIB_DIR),src/attrib.c src/attrib_arm.c,$(MSLIB_MSC_CFLAGS),src/ATTRIB.SKL,,)

.PHONY: attrib-test
attrib-test: $(BUILD)/ATTRIB.EXE $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(ATTRIB_DIR)tests/run.mjs
test: attrib-test
