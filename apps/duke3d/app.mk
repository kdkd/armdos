# apps/duke3d/app.mk - DUKE3D.EXE: Duke Nukem 3D (3D Realms' GPL-2 source,
# via Chocolate Duke3D) on Ken Silverman's Build engine (BUILDLIC.TXT) with
# the Apogee Sound System (Jim Dose, GPL-2) and an ARM-DOS platform layer
# (src/armdos/). See README.md.
#
# Integer code: soft-float (the SDK default). The renderer (engine, draw)
# and the mixer are -O2, the rest -Os: the program image has to fit in
# conventional memory.

DUKE3D_DIR    := $(here)
DUKE3D_SRCS   := $(patsubst $(DUKE3D_DIR)%,%,$(wildcard $(DUKE3D_DIR)src/engine/*.c) \
                   $(wildcard $(DUKE3D_DIR)src/game/*.c) $(wildcard $(DUKE3D_DIR)src/audiolib/*.c) \
                   $(wildcard $(DUKE3D_DIR)src/armdos/*.c) $(wildcard $(DUKE3D_DIR)src/armdos/nocards/*.c))
DUKE3D_CFLAGS := -std=gnu99 -fno-strict-aliasing -fcommon -fno-short-enums \
                 -include inttypes.h -DPLATFORM_DOS=1 -DARMDOS -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ '-D__UINT32_TYPE__=unsigned int' \
                 -I$(DUKE3D_DIR)src/armdos -I$(DUKE3D_DIR)src/armdos/nocards -I$(DUKE3D_DIR)src/engine -I$(DUKE3D_DIR)src/game \
                 -I$(DUKE3D_DIR)src \
                 -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
                 -Wno-unused-but-set-variable -Wno-missing-field-initializers \
                 -Wno-implicit-fallthrough -Wno-pointer-sign -Wno-char-subscripts \
                 -Wno-parentheses -Wno-type-limits -Wno-empty-body \
                 -Wno-misleading-indentation -Wno-array-bounds -Wno-dangling-pointer \
                 -Wno-stringop-truncation -Wno-format-overflow -Wno-format-truncation \
                 -Wno-old-style-declaration -Wno-enum-conversion -Wno-unused-value \
                 -Wno-address -Wno-format -Wno-unused-label -Wno-maybe-uninitialized \
                 -Wno-stringop-overflow -Wno-restrict -Wno-shift-negative-value \
                 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -Wno-clobbered \
                 -Wno-unused-parameter -Wno-sizeof-pointer-memaccess -Wno-overflow \
                 -Wno-cast-function-type -Wno-absolute-value -Wno-int-in-bool-context \
                 -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-implicit-function-declaration \
                 -Wno-implicit-int -Wno-return-mismatch -Wno-return-type -Wno-builtin-declaration-mismatch

DUKE3D_LIBS   := -Wl,--no-enum-size-warning

DUKE3D_HOT    := engine/engine engine/draw engine/cache engine/tiles engine/fixedPoint_math \
                 audiolib/mv_mix audiolib/multivoc armdos/display_armdos

# The game (2.3 MB with its data) runs in extended memory, as DUKE3D.EXE did
# under DOS/4GW: build/duke3d/D3DGAME.EXE is the game image, D3DLOAD.EXE the
# extender stub (loader/d3dload.c) that loads it into an XMS block, and
# DUKE3D.EXE is the two bound together (stub first, game image appended).
$(call armdos_exe_to,D3DGAME,$(DUKE3D_SRCS),$(DUKE3D_CFLAGS),$(DUKE3D_LIBS),--stack 262144,$(BUILD)/duke3d)
$(call armdos_exe_to,D3DLOAD,loader/d3dload.c,,,--stack 4096,$(BUILD)/duke3d)

$(patsubst %,$(BUILD)/obj/D3DGAME/src/%.c.o,$(DUKE3D_HOT)): ARMDOS_CFLAGS += -O2

# char: the game and the engine as Chocolate Duke has them (GCC on x86:
# signed); Jim Dose's sound library as DUKE3D.EXE had it (Watcom C: unsigned,
# the ARM default) - its pan table and MIDI code depend on it.
$(patsubst %.c,$(BUILD)/obj/D3DGAME/%.c.o,$(filter-out src/audiolib/%,$(DUKE3D_SRCS))): ARMDOS_CFLAGS += -fsigned-char

$(BUILD)/DUKE3D.EXE: $(BUILD)/duke3d/D3DLOAD.EXE $(BUILD)/duke3d/D3DGAME.EXE
	$(NODE) -e 'const fs=require("fs");const a=fs.readFileSync(process.argv[1]),b=fs.readFileSync(process.argv[2]);const pad=(16-a.length%16)%16;fs.writeFileSync(process.argv[3],Buffer.concat([a,Buffer.alloc(pad),b]))' $^ $@

.PHONY: DUKE3D
DUKE3D: $(BUILD)/DUKE3D.EXE
all: $(BUILD)/DUKE3D.EXE

# the disk image carries DUKE3D.EXE and the shareware data (apps/duke3d/hd.json)
DISK_DEPS += $(BUILD)/DUKE3D.EXE $(wildcard 3rdparty/duke3d/*)

# "make duke3d-test": boot ARM-DOS headless and play (apps/duke3d/tests/run.mjs):
# start-up, the 3D Realms logo and title with OPL3 music and SB16 effects,
# E1L1, fps at 100 MHz, C:\>GAMES\DUKE3D\DUKE3D, no HIMEM.SYS.
.PHONY: duke3d-test
test: duke3d-test
duke3d-test: $(BUILD)/DUKE3D.EXE $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS $(BUILD)/MOUSE.COM \
             $(BUILD)/MEM.EXE $(BUILD)/SBMIX.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DUKE3D_DIR)tests/run.mjs play fps fromroot nomem

# "make duke3d-joystick-test": a joystick on the game port (apps/duke3d/tests/joystick.mjs):
# DUKE3D.CFG ControllerType = 2, the start-up calibration, walk/turn/fire with the stick.
.PHONY: duke3d-joystick-test
test: duke3d-joystick-test
duke3d-joystick-test: $(BUILD)/DUKE3D.EXE $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS $(BUILD)/MOUSE.COM \
             $(BUILD)/MEM.EXE $(BUILD)/SBMIX.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DUKE3D_DIR)tests/joystick.mjs
