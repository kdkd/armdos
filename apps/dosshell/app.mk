# apps/dosshell/app.mk - the DOS 4.00 Shell for ARM-DOS (see README.md):
#   SHELLB.COM   the small resident loader DOSSHELL.BAT starts first
#   SHELLC.EXE   the Shell (Start Programs, File System, Change Colors)
#   DOSSHELL.BAT, SHELL.MEU, DOSUTIL.MEU, GAMES.MEU, SHELL.HLP, SHELL.CLR -> C:\DOS
DOSSHELL_DIR := $(here)
DOSSHELL_OUT := $(BUILD)/dosshell

$(call armdos_com,SHELLB,shellb.c runcmd.c tinystr.c,-fno-builtin,,--stack 2048)

SHELLC_SRCS := shellc/main.c shellc/scr.c shellc/ui.c shellc/menu.c shellc/meu.c \
               shellc/psc.c shellc/colors.c shellc/fs.c shellc/fsops.c shellc/help.c \
               shellc/launch.c runcmd.c
$(call armdos_exe,SHELLC,$(SHELLC_SRCS),-I$(DOSSHELL_DIR) -Wno-array-bounds -Wno-missing-field-initializers -Wno-format-truncation,,--stack 32768)

# the data files (built from text sources by tools/*.mjs)
DOSSHELL_DATA := $(DOSSHELL_OUT)/SHELL.MEU $(DOSSHELL_OUT)/DOSUTIL.MEU $(DOSSHELL_OUT)/GAMES.MEU \
                 $(DOSSHELL_OUT)/SHELL.HLP $(DOSSHELL_OUT)/SHELL.CLR

# an item whose program is not built is left out (--have: every program make builds)
$(DOSSHELL_OUT)/SHELL.MEU: $(DOSSHELL_DIR)data/menus.json $(DOSSHELL_DIR)tools/mkmeu.mjs dosshell-menus-check
	@mkdir -p $(DOSSHELL_OUT)
	$(NODE) $(DOSSHELL_DIR)tools/mkmeu.mjs $< $(DOSSHELL_OUT) --have "$(notdir $(ARMDOS_PROGRAMS))"
$(DOSSHELL_OUT)/DOSUTIL.MEU $(DOSSHELL_OUT)/GAMES.MEU: $(DOSSHELL_OUT)/SHELL.MEU
.PHONY: dosshell-menus-check
dosshell-menus-check:

$(DOSSHELL_OUT)/SHELL.HLP: $(DOSSHELL_DIR)data/help.txt $(DOSSHELL_DIR)tools/mkhlp.mjs $(DOSSHELL_DIR)shellc/shell.h
	@mkdir -p $(DOSSHELL_OUT)
	$(NODE) $(DOSSHELL_DIR)tools/mkhlp.mjs $< $@

$(DOSSHELL_OUT)/SHELL.CLR: $(DOSSHELL_DIR)tools/mkmeu.mjs
	@mkdir -p $(DOSSHELL_OUT)
	$(NODE) $(DOSSHELL_DIR)tools/mkmeu.mjs --clr $@

all: $(DOSSHELL_DATA)
DISK_DEPS += $(DOSSHELL_DATA) $(BUILD)/SHELLB.COM $(BUILD)/SHELLC.EXE

.PHONY: dosshell-test
dosshell-test: $(BUILD)/SHELLB.COM $(BUILD)/SHELLC.EXE $(DOSSHELL_DATA) $(BUILD)/COMMAND.COM \
               $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(DOSSHELL_DIR)tests/run.mjs
test: dosshell-test
