# apps/label/app.mk - LABEL.COM (MS-DOS 4.00 LABEL re-created in C, see README.md)
LABEL_DIR := $(here)
$(call armdos_com,LABEL,label.c u4lib.c u4int.S,-I$(LABEL_DIR)../dosutil)

# "make label-test": LABEL.COM vs the real 4.00 LABEL (apps/label/tests/run.mjs)
.PHONY: label-test
label-test: $(BUILD)/LABEL.COM $(BUILD)/u4test/CLS.EXE $(BUILD)/u4test/REDIR.EXE $(BUILD)/ktest/TSHELL.EXE \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(LABEL_DIR)tests/run.mjs
test: label-test
