# apps/midi/app.mk - General MIDI on the ARM-PC: PLAYMIDI.EXE (C:\DOS), the
# C:\MIDI\ songs (hd.json) and the tests of the MPU-401 + GM synthesizer
# (emu/dev/mpu401.mjs, emu/dev/gmsynth.mjs, the sound set sf/ARMGS.SFA).
# See README.md.
MIDI_DIR := $(here)
$(call armdos_exe,PLAYMIDI,playmidi.c,-Wno-array-bounds)

DISK_DEPS += $(wildcard $(MIDI_DIR)data/*.MID) $(MIDI_DIR)data/README.TXT

# "make midi-test": the MPU-401 registers, the synth (pitch, envelope, drums,
# controllers) and PLAYMIDI playing a file on the booted machine (WAV analysis).
.PHONY: midi-test
midi-test: $(BUILD)/PLAYMIDI.EXE $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(MIDI_DIR)tests/synth.mjs
	$(NODE) $(MIDI_DIR)tests/playmidi.mjs
test: midi-test

# DOOM and Duke Nukem 3D with General MIDI music on the MPU-401 (apps/doom/tests/gm.mjs,
# apps/duke3d/tests/gm.mjs; the games' own tests keep the OPL3 by running without the MPU).
.PHONY: doom-gm-test duke3d-gm-test
doom-gm-test: $(BUILD)/DOOM.EXE $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) apps/doom/tests/gm.mjs
duke3d-gm-test: $(BUILD)/DUKE3D.EXE $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) apps/duke3d/tests/gm.mjs
test: doom-gm-test duke3d-gm-test

# "make gm-web-test": the page's MPU-401/GM path in Chromium (web/tests/test_gm_audio.py;
# stages its own site in build/gm-web around build/midi-test/playmidi.img).
.PHONY: gm-web-test
gm-web-test: midi-test
	$(WEB_PY) web/tests/test_gm_audio.py
