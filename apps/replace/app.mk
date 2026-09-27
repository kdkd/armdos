# apps/replace/app.mk - REPLACE.EXE (MS-DOS 4.00 REPLACE re-created in C, see README.md)
REPLACE_DIR := $(here)
$(call armdos_exe,REPLACE,replace.c u4lib.c u4int.S,-I$(REPLACE_DIR)../dosutil)

# "make replace-test": REPLACE.EXE vs the real 4.00 REPLACE (apps/replace/tests/run.mjs)
.PHONY: replace-test
replace-test: $(BUILD)/REPLACE.EXE $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
              $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(REPLACE_DIR)tests/run.mjs
test: replace-test
