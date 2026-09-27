# apps/paint/app.mk - PAINT.EXE ("ARM Paint") and BANNER.EXE (see README.md).
# PAINT.EXE goes to C:\PAINT\ (apps/paint/hd.json) with C:\DOS\PAINT.BAT;
# BANNER.EXE goes to C:\DOS by the catch-all rule.
PAINT_DIR := $(here)
$(call armdos_exe,PAINT,paint.c gfx.c file.c print.c,-std=gnu99 -Wno-array-bounds -Wno-format-truncation,,--stack 16384)
$(call armdos_exe,BANNER,banner.c,-std=gnu99)

.PHONY: paint-test
paint-test: $(BUILD)/PAINT.EXE $(BUILD)/BANNER.EXE $(BUILD)/COMMAND.COM $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(PAINT_DIR)tests/run.mjs
test: paint-test

# the page's printer (web/js/printer.js as an FX-80) in Chromium: stages build/paint-web/site;
# uses build/paint-test/print-letter.prn from paint-test as the picture
.PHONY: paint-web-test
paint-web-test: paint-test
	$(or $(PYTHON),python3) $(PAINT_DIR)tests/test_printer.py
