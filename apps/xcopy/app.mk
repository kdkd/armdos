# apps/xcopy/app.mk - XCOPY.EXE (MS-DOS 4.00 XCOPY re-created in C, see README.md)
XCOPY_DIR := $(here)
$(call armdos_exe,XCOPY,xcopy.c u4lib.c u4int.S,-I$(XCOPY_DIR)../dosutil)

# "make xcopy-test": XCOPY.EXE vs the real 4.00 XCOPY (apps/xcopy/tests/run.mjs)
.PHONY: xcopy-test
xcopy-test: $(BUILD)/XCOPY.EXE $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(XCOPY_DIR)tests/run.mjs
test: xcopy-test
