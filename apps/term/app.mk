# apps/term/app.mk - TERM.EXE, a Procomm/Qmodem-style terminal program (see README.md).
# lib/ (comm, scr, vt, zmodem, xmodem) is shared with apps/bbs.
TERM_DIR := $(here)
TERM_LIB := lib/comm.c lib/scr.c lib/vt.c lib/zmodem.c lib/xmodem.c
# speech (ESC P speak;... ESC \\) uses DRARM's text-to-speech engine (apps/drarm, VFP floats)
TERM_TTS := ../drarm/ttsnrl.c ../drarm/ttstext.c ../drarm/ttssyn.c ../drarm/speak.c
$(call armdos_exe,TERM,term.c script.c speech.c $(TERM_LIB) $(TERM_TTS),-Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds $(ARMDOS_VFP) -Iapps/drarm -DTTS_MAXSEG=360,-lm,--stack 16384)

.PHONY: term-test term-zmhost-test
# ZMODEM/XMODEM/YMODEM code built for the host and checked against lrzsz (if
# lsz/lrz are installed or LRZSZ=dir points at a build of lrzsz 0.12.20)
term-zmhost-test:
	@mkdir -p $(BUILD)/term-test
	cc -O2 -Wall -DZM_HOST -I$(TERM_DIR)lib -o $(BUILD)/term-test/zmhost $(TERM_DIR)tests/zmhost/host.c $(TERM_DIR)lib/zmodem.c $(TERM_DIR)lib/xmodem.c
	@LSZ=$${LRZSZ:+$$LRZSZ/src/lsz}; LRZ=$${LRZSZ:+$$LRZSZ/src/lrz}; LSZ=$${LSZ:-$$(command -v lsz || command -v sz)}; LRZ=$${LRZ:-$$(command -v lrz || command -v rz)}; \
	if [ -n "$$LSZ" ] && [ -n "$$LRZ" ]; then python3 $(TERM_DIR)tests/zmhost/run.py $(BUILD)/term-test/zmhost $$LSZ $$LRZ $(BUILD)/term-test/zmwork; \
	else echo "term-zmhost-test: lrzsz not found (brew install lrzsz / apt install lrzsz, or LRZSZ=path/to/lrzsz-0.12.20) - skipped"; fi
term-test: $(BUILD)/TERM.EXE term-zmhost-test $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(TERM_DIR)tests/run.mjs
test: term-test

# TERM /S:GETKEEN.SCR against The ARM Pit BBS (two machines, real 14400 pacing);
# `node apps/term/tests/script.mjs 2400` measures the same call at 2400 bps
.PHONY: term-script-test
term-script-test: $(BUILD)/TERM.EXE $(BUILD)/bbs.img $(BUILD)/rom.bin
	$(NODE) $(TERM_DIR)tests/script.mjs
test: term-script-test

# TERM and the talking numbers: WOPR spoken through the Sound Blaster, Alt-Z/Alt-D menus that run
# the Alt-command you press, Ctrl-Alt-Del mid-call (emu/phonelines.mjs, docs/MODEM.md)
.PHONY: term-wopr-test
term-wopr-test: $(BUILD)/TERM.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(TERM_DIR)tests/wopr.mjs
test: term-wopr-test
