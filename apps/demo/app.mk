# apps/demo/app.mk - DEMO.EXE, "ARM-DOS 4.00: the demo" (see README.md).
# Goes to C:\DEMO\ (apps/demo/hd.json), not C:\DOS.
DEMO_DIR := $(here)
$(call armdos_exe,DEMO,demo.c music.c fx.S,-std=gnu99,,--stack 16384)

.PHONY: demo-test
demo-test: $(BUILD)/DEMO.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DEMO_DIR)tests/run.mjs
test: demo-test
