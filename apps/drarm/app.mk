# apps/drarm/app.mk - Dr. ARMitso (DOCTOR.EXE), the talking psychologist, and
# SAY.EXE, both on DRARM's own text-to-speech engine (tts*.c). See README.md.
DRARM_DIR := $(here)
DRARM_TTS := ttsnrl.c ttstext.c ttssyn.c speak.c
$(call armdos_exe,SAY,say.c $(DRARM_TTS),$(ARMDOS_VFP),-lm)
$(call armdos_exe,DOCTOR,doctor.c eliza.c $(DRARM_TTS),$(ARMDOS_VFP),-lm)

.PHONY: drarm-test
drarm-test: $(BUILD)/SAY.EXE $(BUILD)/DOCTOR.EXE $(BUILD)/SBMIX.EXE $(BUILD)/COMMAND.COM $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/rom.bin
	$(NODE) $(DRARM_DIR)tests/run.mjs
test: drarm-test
