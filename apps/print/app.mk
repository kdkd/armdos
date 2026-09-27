# apps/print/app.mk - PRINT.COM (MS-DOS 4.00 PRINT spooler re-created in C, see README.md)
# res.c/resasm.S/resend.c must come first: the resident part is what the
# linker puts ahead of the transient code (see res.c).
PRINT_DIR := $(here)
$(call armdos_com,PRINT,res.c resasm.S resend.c print.c u4lib.c u4int.S,-I$(PRINT_DIR)../dosutil,,--stack 3072)
$(BUILD)/obj/PRINT/res.c.o: ARMDOS_CFLAGS += -fno-builtin -fno-tree-loop-distribute-patterns -fno-jump-tables \
                                            -fno-toplevel-reorder -fno-ipa-cp -fno-ipa-sra

# SPIN.EXE: a test helper (busy for N ticks without DOS calls)
$(call armdos_exe_to,SPIN,tests/spin.c tests/u4lib.c tests/u4int.S,-I$(PRINT_DIR)../dosutil,,,$(BUILD)/printtest)

# "make print-test": PRINT.COM vs the real 4.00 PRINT (apps/print/tests/run.mjs)
.PHONY: print-test
print-test: $(BUILD)/PRINT.COM $(BUILD)/printtest/SPIN.EXE $(BUILD)/FIND.EXE $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE \
            $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(PRINT_DIR)tests/run.mjs
test: print-test
