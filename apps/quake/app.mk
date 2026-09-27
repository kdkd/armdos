# apps/quake/app.mk - QUAKE.EXE: id Software's WinQuake software renderer
# (GPL-2) with an ARM-DOS platform layer (src/*_armdos.c). See README.md.
#
# Quake is float-heavy (view setup, clipping, alias models, particles): it is
# built for the ARM926's VFP (softfp, $(ARMDOS_VFP)); the x86 assembly is not
# used (id386 is 0 on ARM: the C versions of the span/edge/poly code).
# The hot renderer files are -O2, the rest -Os.

QUAKE_DIR    := $(here)
QUAKE_SRCS   := $(patsubst $(QUAKE_DIR)%,%,$(wildcard $(QUAKE_DIR)src/*.c))
QUAKE_CFLAGS := $(ARMDOS_VFP) -std=gnu89 -fsigned-char -fno-strict-aliasing -fno-short-enums -fcommon \
                -Wno-sign-compare -Wno-unused-variable -Wno-unused-function \
                -Wno-unused-but-set-variable -Wno-missing-field-initializers \
                -Wno-implicit-fallthrough -Wno-pointer-sign -Wno-char-subscripts \
                -Wno-parentheses -Wno-type-limits -Wno-empty-body \
                -Wno-misleading-indentation -Wno-array-bounds -Wno-dangling-pointer \
                -Wno-stringop-truncation -Wno-format-overflow -Wno-format-truncation \
                -Wno-old-style-declaration -Wno-enum-conversion -Wno-return-type \
                -Wno-implicit-int -Wno-maybe-uninitialized -Wno-unused-value \
                -Wno-address -Wno-cast-function-type -Wno-format -Wno-sizeof-pointer-memaccess \
                -Wno-stringop-overflow -Wno-restrict -Wno-shift-negative-value \
                -Wno-absolute-value -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast \
                -Wno-incompatible-pointer-types -Wno-int-conversion \
                -Wno-implicit-function-declaration -Wno-builtin-declaration-mismatch \
                -Wno-clobbered -Wno-unused-parameter -Wno-unused-label

QUAKE_HOT    := d_edge d_polyse d_scan d_sky d_sprite d_surf d_part d_zpoint \
                r_aclip r_alias r_bsp r_draw r_edge r_light r_main r_misc r_sky \
                r_sprite r_surf r_part mathlib snd_mix world sv_phys pr_exec model

QUAKE_LIBS   := $(ARMDOS_PRINTF_FLOAT) -lm -Wl,--no-enum-size-warning

$(call armdos_exe,QUAKE,$(QUAKE_SRCS),$(QUAKE_CFLAGS),$(QUAKE_LIBS),--stack 16384)

$(patsubst %,$(BUILD)/obj/QUAKE/src/%.c.o,$(QUAKE_HOT)): ARMDOS_CFLAGS += -O2

# the disk image carries the shareware data (apps/quake/hd.json)
DISK_DEPS += 3rdparty/quake/ID1/PAK0.PAK

# "make quake-test": boots ARM-DOS headless (COMMAND.COM, HIMEM.SYS,
# MOUSE.COM, SET BLASTER) and plays; timedemo reports fps at 100 MHz.
.PHONY: quake-test
quake-test: $(BUILD)/QUAKE.EXE $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS $(BUILD)/MOUSE.COM \
            $(BUILD)/MEM.EXE $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(QUAKE_DIR)tests/run.mjs play timedemo fromroot nomem
test: quake-test

# "make quake-joystick-test": the joystick on the game port (in_dos.c's code, tests/joystick.mjs)
.PHONY: quake-joystick-test
quake-joystick-test: $(BUILD)/QUAKE.EXE $(BUILD)/COMMAND.COM $(BUILD)/HIMEM.SYS \
            $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(QUAKE_DIR)tests/joystick.mjs
test: quake-joystick-test
