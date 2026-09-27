# apps/tree/app.mk - TREE.COM (MS-DOS 4.00 TREE re-created in C, see README.md)
TREE_DIR := $(here)
$(call armdos_com,TREE,tree.c u4lib.c u4int.S,-I$(TREE_DIR)../dosutil)

# "make tree-test": TREE.COM vs the real 4.00 TREE (apps/tree/tests/run.mjs)
.PHONY: tree-test
tree-test: $(BUILD)/TREE.COM $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(TREE_DIR)tests/run.mjs
test: tree-test
