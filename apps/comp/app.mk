# apps/comp/app.mk - COMP.COM (MS-DOS 4.00 COMP re-created in C, see README.md)
COMP_DIR := $(here)
$(call armdos_com,COMP,comp.c u4lib.c u4int.S,-I$(COMP_DIR)../dosutil)

# "make comp-test": COMP.COM vs the real 4.00 COMP (apps/comp/tests/run.mjs)
.PHONY: comp-test
comp-test: $(BUILD)/COMP.COM $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(COMP_DIR)tests/run.mjs
test: comp-test
