# apps/doom/app.mk - DOOM.EXE: doomgeneric (GPL-2) with the ARM-DOS platform
# layer (src/doomgeneric_armdos.c, src/i_pcsound_armdos.c, src/i_sb_armdos.c,
# src/i_oplmus_armdos.c). See README.md.
#
# Size matters: DOS loads the program image into conventional memory (the
# zone, 8 MB, comes from XMS), so most of the game is built -Os (the SDK
# default) and only the hot files (renderer, fixed-point maths, blockmap and
# sight checks, zone allocator) -O2. Soft-float (the SDK's -mfloat-abi=soft):
# DOOM is fixed-point, the few floats are not hot.

DOOM_DIR    := $(here)
DOOM_SRCS   := $(patsubst $(DOOM_DIR)%,%,$(wildcard $(DOOM_DIR)src/*.c))
DOOM_CFLAGS := -std=gnu99 -fno-strict-aliasing \
               -DARMDOS -DCMAP256 -DFEATURE_SOUND \
               -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 \
               -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
               -Wno-unused-but-set-variable -Wno-missing-field-initializers \
               -Wno-implicit-fallthrough -Wno-pointer-sign -Wno-format-truncation \
               -Wno-char-subscripts -Wno-parentheses -Wno-type-limits \
               -Wno-empty-body -Wno-misleading-indentation -Wno-array-bounds \
               -Wno-dangling-pointer -Wno-stringop-truncation -Wno-format-overflow \
               -Wno-old-style-declaration -Wno-enum-conversion

DOOM_HOT    := r_draw r_plane r_segs r_bsp r_main r_things m_fixed \
               p_maputl p_sight p_map z_zone v_video doomgeneric_armdos

$(call armdos_exe,DOOM,$(DOOM_SRCS),$(DOOM_CFLAGS),,--stack 32768)

$(patsubst %,$(BUILD)/obj/DOOM/src/%.c.o,$(DOOM_HOT)): ARMDOS_CFLAGS += -O2

# the disk image carries the WAD (apps/doom/hd.json)
DISK_DEPS += 3rdparty/doom/DOOM1.WAD

# "make doom-test": boot ARM-DOS headless and play (apps/doom/tests/run.mjs).
# WADCHECK.EXE (a file-system read check) goes to build/doom-test/, not C:.
$(call armdos_exe_to,WADCHECK,tests/wadcheck.c,,,,$(BUILD)/doom-test)

.PHONY: doom-test
doom-test: $(BUILD)/DOOM.EXE $(WADCHECK_OUT) $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DOOM_DIR)tests/run.mjs wadcheck play timedemo

# "make doom-sound-test": DOOM with the Sound Blaster 16 (SET BLASTER from
# AUTOEXEC.BAT): OPL music checked note for note against Chocolate Doom's OPL
# player (tests/oplref, built with the host gcc), SFX through DMA, WAVs in
# build/doom-test/sound/ (apps/doom/tests/sound.mjs).
.PHONY: doom-sound-test
doom-sound-test: $(BUILD)/DOOM.EXE $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DOOM_DIR)tests/sound.mjs
test: doom-sound-test

# "make doom-joy-test": the joystick on the game port (use_joystick 1 in DEFAULT.CFG,
# the three calibration prompts, walk/turn/fire with the stick; apps/doom/tests/joystick.mjs)
.PHONY: doom-joy-test
doom-joy-test: $(BUILD)/DOOM.EXE $(BUILD)/ktest/TSHELL.EXE \
               $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DOOM_DIR)tests/joystick.mjs
test: doom-joy-test

# "make doom-mouse-test": the mouse through MOUSE.COM's INT 33h (Mouse: detected,
# turn/fire/forward, mouse_sensitivity, no driver, use_mouse 0; tests/mouse.mjs)
.PHONY: doom-mouse-test
doom-mouse-test: $(BUILD)/DOOM.EXE $(BUILD)/MOUSE.COM $(BUILD)/ktest/TSHELL.EXE \
                 $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DOOM_DIR)tests/mouse.mjs
test: doom-mouse-test
