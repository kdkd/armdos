# apps/hexen2/app.mk - H2.EXE: Raven's Hexen II engine as uHexen2 (Hammer of
# Thyrion, GPL-2) has it, software renderer, with an ARM-DOS platform layer
# (src/*_armdos.c). See README.md. No game data is built or shipped: the
# player brings the demo's pak0.pak (README.TXT, INSTALL.BAT).
#
# Like QUAKE.EXE it is built for the ARM926's VFP ($(ARMDOS_VFP), softfp);
# no x86 assembly (id386 is 0). The hot renderer files are -O2, the rest -Os.

H2_DIR    := $(here)
H2_SRCS   := $(patsubst $(H2_DIR)%,%,$(filter-out %/bgmnull_none.c %/bgmnull_midi.c,$(wildcard $(H2_DIR)src/*.c $(H2_DIR)src/hexen2/*.c \
                                               $(H2_DIR)src/h2shared/*.c $(H2_DIR)src/common/*.c)))
H2_CFLAGS := $(ARMDOS_VFP) -std=gnu99 -DARMDOS -D__MSDOS__ -DNO_PCI_AUDIO -D_NO_CDAUDIO -D_NO_MIDIDRV \
             -I$(H2_DIR)src -I$(H2_DIR)src/hexen2 -I$(H2_DIR)src/h2shared -I$(H2_DIR)src/common \
             -fsigned-char -fno-strict-aliasing -fno-short-enums -fcommon \
             -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
             -Wno-unused-but-set-variable -Wno-missing-field-initializers \
             -Wno-implicit-fallthrough -Wno-char-subscripts -Wno-parentheses -Wno-type-limits \
             -Wno-empty-body -Wno-misleading-indentation -Wno-array-bounds \
             -Wno-stringop-truncation -Wno-format-overflow -Wno-format-truncation \
             -Wno-unused-parameter -Wno-unused-label -Wno-clobbered -Wno-cast-function-type

H2_HOT    := d_edge d_polyse d_scan d_sky d_sprite d_surf d_part d_zpoint \
             r_aclip r_bsp r_draw r_edge r_light r_sky r_sprite r_surf mathlib \
             snd_mix model pr_exec
H2_HOT2   := r_alias r_main r_misc r_part sv_phys world

H2_LIBS   := $(ARMDOS_PRINTF_FLOAT) -lm -Wl,--no-enum-size-warning

# The game (530 KB of code, 2.2 MB of bss, a 1 MB stack) runs in extended
# memory, as H2DOS.EXE did under CWSDPMI: build/hexen2/H2GAME.EXE is the game
# image, H2LOAD.EXE the extender stub (loader/h2load.c, after apps/duke3d's)
# that loads it into an XMS block, and H2.EXE the two bound together (stub
# first, game image appended at the next 16-byte boundary).
$(call armdos_exe_to,H2GAME,$(H2_SRCS),$(H2_CFLAGS),$(H2_LIBS),--stack 16384,$(BUILD)/hexen2)
$(call armdos_exe_to,H2LOAD,loader/h2load.c,,,--stack 4096,$(BUILD)/hexen2)

$(patsubst %,$(BUILD)/obj/H2GAME/src/h2shared/%.c.o,$(H2_HOT)) \
$(patsubst %,$(BUILD)/obj/H2GAME/src/hexen2/%.c.o,$(H2_HOT2)): ARMDOS_CFLAGS += -O2

$(BUILD)/H2.EXE: $(BUILD)/hexen2/H2LOAD.EXE $(BUILD)/hexen2/H2GAME.EXE
	$(NODE) -e 'const fs=require("fs");const a=fs.readFileSync(process.argv[1]),b=fs.readFileSync(process.argv[2]);const pad=(16-a.length%16)%16;fs.writeFileSync(process.argv[3],Buffer.concat([a,Buffer.alloc(pad),b]))' $^ $@

.PHONY: H2
H2: $(BUILD)/H2.EXE
all: $(BUILD)/H2.EXE
DISK_DEPS += $(BUILD)/H2.EXE $(BUILD)/H2CHECK.EXE $(wildcard $(H2_DIR)data/*.* $(H2_DIR)data/DATA1/*)

# H2CHECK.EXE: INSTALL.BAT's check of DATA1\PAK0.PAK (size, directory, CRC-32)
$(call armdos_exe,H2CHECK,check/h2check.c,,,)

# "make hexen2-test": boots ARM-DOS headless. Without the demo's pak0.pak
# (not part of ARM-DOS; HEXEN2_PAK=file for local testing) only the no-data
# checks run, the rest is skipped. tests/hostlink.mjs (GETPAK over the Host
# Link, ~1 minute) is run by hand.
.PHONY: hexen2-test
hexen2-test: $(BUILD)/H2.EXE $(BUILD)/H2CHECK.EXE $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS $(BUILD)/MOUSE.COM \
             $(BUILD)/MORE.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(H2_DIR)tests/run.mjs nodata play timedemo files
test: hexen2-test
