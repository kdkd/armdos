# apps/more/app.mk - MORE.COM (MS-DOS 4.00 MORE re-created in C, see README.md)
MORE_DIR := $(here)
$(call armdos_com,MORE,more.c u4lib.c u4int.S,-I$(MORE_DIR)../dosutil)

# "make more-test": MORE.COM vs the real 4.00 MORE (apps/more/tests/run.mjs)
.PHONY: more-test
more-test: $(BUILD)/MORE.COM $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(MORE_DIR)tests/run.mjs
test: more-test
