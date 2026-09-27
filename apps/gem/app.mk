# apps/gem/app.mk - GEM for ARM-DOS (see README.md).
#
#   GEMVDI.EXE   the VDI: GDOS entry + the screen driver (CGA / Hercules / VGA)
#
GEM_DIR := $(here)
# make GEM_DEBUG=1: debug text on port E9h (-DGEMDEBUG -DGEMTRACE)
GEM_DEBUG_FLAGS := $(if $(GEM_DEBUG),-DGEMDEBUG -DGEMTRACE)
GEM_PORT_CFLAGS := $(GEM_DEBUG_FLAGS) -std=gnu89 -Wno-return-mismatch -fno-strict-aliasing -I$(GEM_DIR)include \
	-Wno-implicit-fallthrough -Wno-sign-compare -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-parentheses -Wno-return-type -Wno-missing-field-initializers -Wno-unused-function

GEMVDI_SRCS := vdi/gdos.c vdi/tramp.S vdi/jmptbl.c vdi/monobj.c vdi/monout.c vdi/opttext.c \
	vdi/isin.c vdi/dummy.c vdi/draw.c vdi/raster.c vdi/text.c vdi/mouse.c vdi/dev.c \
	vdi/fonts.c vdi/patdata.c
$(call armdos_exe,GEMVDI,$(GEMVDI_SRCS),$(GEM_PORT_CFLAGS) -I$(GEM_DIR)vdi,,--stack 8192)

# test programs (build/gem-test/)
$(call armdos_exe_to,VDITEST,tests/vditest.c,,,,$(BUILD)/gem-test)
$(call armdos_exe_to,XMSTEST,tests/xmstest.c,,,,$(BUILD)/gem-test)
$(call armdos_exe_to,MODETEST,tests/modetest.c,,,,$(BUILD)/gem-test)

#   GEM.EXE      the AES (GEM/3 AES sources ported to ARM + gemarm.c/gemasm.S)
GEMAES_SRCS := $(patsubst $(GEM_DIR)%,%,$(wildcard $(GEM_DIR)aes/gem*.c)) aes/optimize.c aes/gemasm.S common/gemrt.c
$(call armdos_exe,GEM,$(GEMAES_SRCS),$(GEM_PORT_CFLAGS) -fno-builtin -I$(GEM_DIR)aes,,--stack 16384)

#   DESKTOP.APP  the GEM Desktop (GEM/3 Desktop sources ported to ARM + deskarm.c)
GEMDESK_SRCS := $(patsubst $(GEM_DIR)%,%,$(wildcard $(GEM_DIR)desk/*.c)) common/gemrt.c
$(call armdos_exe,DESKTOP,$(GEMDESK_SRCS),$(GEM_PORT_CFLAGS) -fno-builtin -I$(GEM_DIR)desk,,--stack 16384)

#   CALCLOCK.ACC the Calculator, Clock and Print Spooler desk accessory
GEMACC_SRCS := acc/ccsmain.c acc/calc.c acc/clok.c acc/spol.c acc/calcif.c acc/accarm.c acc/fld.c
$(call armdos_exe,CALCLOCK,$(GEMACC_SRCS),$(GEM_PORT_CFLAGS) -fno-builtin -I$(GEM_DIR)acc,,--stack 4096)

#   DEMO.APP     the GEM Programmer's Toolkit sample drawing program (DRI, non-copyrighted)
GEMDEMO_SRCS := demo/demo.c demo/gembind.c demo/vdibind.c demo/demoarm.c
$(call armdos_exe,GEMDEMO,$(GEMDEMO_SRCS),$(GEM_PORT_CFLAGS) -fno-builtin -I$(GEM_DIR)demo,,--stack 8192)

DISK_DEPS += $(BUILD)/GEMVDI.EXE $(BUILD)/GEM.EXE $(BUILD)/DESKTOP.EXE $(BUILD)/CALCLOCK.EXE $(BUILD)/GEMDEMO.EXE $(GEM_DIR)demo/demo.rsc \
	$(GEM_DIR)aes/gem.rsc $(GEM_DIR)desk/resource/desktop.rsc $(GEM_DIR)desk/icons/desklo.icn \
	$(GEM_DIR)desk/icons/deskhi.icn $(GEM_DIR)dist/DESKTOP.INF $(GEM_DIR)dist/GEM.BAT

# "make gem-test": boot ARM-DOS headless, start GEM and drive the Desktop
# with the mouse (tests/run.mjs; screenshots in build/gem-test/)
.PHONY: gem-test
gem-test: $(BUILD)/GEMVDI.EXE $(BUILD)/GEM.EXE $(BUILD)/DESKTOP.EXE $(BUILD)/CALCLOCK.EXE $(BUILD)/GEMDEMO.EXE \
          $(BUILD)/gem-test/VDITEST.EXE $(BUILD)/gem-test/XMSTEST.EXE $(BUILD)/gem-test/MODETEST.EXE \
          $(BUILD)/EDIT.EXE $(BUILD)/MEM.EXE $(BUILD)/FORMAT.COM $(BUILD)/COMMAND.COM $(BUILD)/MOUSE.COM $(BUILD)/HIMEM.SYS \
          $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(GEM_DIR)tests/run.mjs
test: gem-test
