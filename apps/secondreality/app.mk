# apps/secondreality/app.mk - Future Crew's Second Reality (1993), the x86
# binaries from the public-domain source release, run by ELBOW (apps/x86).
# Nothing to build: the files go to C:\ELBOW\DEMOS\SECOND (hd.json).
SR_DIR := $(here)
DISK_DEPS += $(wildcard $(SR_DIR)demo/*) $(SR_DIR)dist/README.TXT $(SR_DIR)dist/SECOND.BAT

# "make secondreality-test" (part of "make test", ~2 minutes): the setup screen,
# the intro in the unchained VGA mode, Sound Blaster music from EMS, a clean exit
.PHONY: secondreality-test
secondreality-test: $(BUILD)/ELBOW.EXE $(BUILD)/HIMEM.SYS $(BUILD)/ktest/TSHELL.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(SR_DIR)tests/run.mjs
test: secondreality-test
