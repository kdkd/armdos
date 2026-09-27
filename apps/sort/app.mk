# apps/sort/app.mk - SORT.EXE (MS-DOS 4.00 SORT re-created in C, see README.md)
SORT_DIR := $(here)
$(call armdos_exe,SORT,sort.c u4lib.c u4int.S,-I$(SORT_DIR)../dosutil)

# "make sort-test": SORT.EXE vs the real 4.00 SORT (apps/sort/tests/run.mjs)
.PHONY: sort-test
sort-test: $(BUILD)/SORT.EXE $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(SORT_DIR)tests/run.mjs
test: sort-test
