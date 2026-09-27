# apps/joytest/app.mk - JOYTEST.EXE: joystick test and calibration for the game
# adapter at 201h (C:\DOS). See README.md. JOYBIOS.EXE (tests/joybios.c) prints
# the BIOS's INT 11h / INT 15h AH=84h answers for the test; it stays in build/joytest-test.
JOYTEST_DIR := $(here)
$(call armdos_exe,JOYTEST,joytest.c,-Wno-array-bounds -Wno-format-truncation)
$(call armdos_exe_to,JOYBIOS,tests/joybios.c,,,,$(BUILD)/joytest-test)

.PHONY: joytest-test
joytest-test: $(BUILD)/JOYTEST.EXE $(JOYBIOS_OUT) $(BUILD)/COMMAND.COM $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/rom.bin
	$(NODE) $(JOYTEST_DIR)tests/run.mjs
test: joytest-test

# "make joy-web-test": the page's joystick (Gamepad API, the pad's JOY mode, lamps, the open
# case's game port) in Chromium with a scripted controller (web/tests/test_joystick.py; stages
# its own site in build/joy-web around the image joytest-test builds).
.PHONY: joy-web-test
joy-web-test: joytest-test
	$(WEB_PY) web/tests/test_joystick.py
