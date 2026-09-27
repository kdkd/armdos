# apps/sbtest/app.mk - SBTEST.EXE: detect the Sound Blaster 16 and its OPL3,
# play a PCM chime and an FM chord (the sb.h SDK API). See README.md.
SBTEST_DIR := $(here)
$(call armdos_exe,SBTEST,sbtest.c,,-lm)

.PHONY: sbtest-test
sbtest-test: $(BUILD)/SBTEST.EXE $(BUILD)/SBMIX.EXE $(BUILD)/COMMAND.COM $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/rom.bin
	$(NODE) $(SBTEST_DIR)tests/run.mjs
test: sbtest-test

# "make sb-web-test": the page's Sound Blaster audio path in Chromium
# (web/tests/test_sb_audio.py; stages its own site in build/sb-web).
.PHONY: sb-web-test
sb-web-test: sbtest-test
	$(WEB_PY) web/tests/test_sb_audio.py
