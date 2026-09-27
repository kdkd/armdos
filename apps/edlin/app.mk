# apps/edlin/app.mk - EDLIN.COM, the MS-DOS 4.00 line editor ported to ARM
# (a C port of Microsoft's MIT-licensed CMD/EDLIN sources; see README.md).
# build/EDLIN.COM goes to C:\DOS through disk/hd.json's build/*.COM rule.

EDLIN_DIR := $(here)
$(call armdos_com,EDLIN,edlin.c)

# "make edlin-test": boot ARM-DOS headless and drive EDLIN (tests/run.mjs).
# EDREDIR.EXE (a test helper, not on the disks) goes to build/edlin-test/.
$(call armdos_exe_to,EDREDIR,tests/redir.c,,,,$(BUILD)/edlin-test)

.PHONY: edlin-test
edlin-test: $(BUILD)/EDLIN.COM $(EDREDIR_OUT) $(BUILD)/ktest/TSHELL.EXE \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(EDLIN_DIR)tests/run.mjs
test: edlin-test
