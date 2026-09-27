# apps/find/app.mk - FIND.EXE (MS-DOS 4.00 FIND re-created in C, see README.md)
FIND_DIR := $(here)
$(call armdos_exe,FIND,find.c u4lib.c u4int.S,-I$(FIND_DIR)../dosutil)

# "make find-test": FIND.EXE vs the real 4.00 FIND (apps/find/tests/run.mjs)
.PHONY: find-test
find-test: $(BUILD)/FIND.EXE $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin \
           $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(FIND_DIR)tests/run.mjs
test: find-test
