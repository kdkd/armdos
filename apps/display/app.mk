# apps/display/app.mk - DISPLAY.SYS (code page switching for CON, DEVICE=) and
# EGA.CPI (its fonts: code pages 437 850 860 863 865, 8x16 8x14 8x8).
#
# DISPLAY.SYS is a freestanding AR1 image whose image begins with its device
# header, built like ANSI.SYS (apps/ansi/app.mk): the resident part (ints.o,
# display.o) is linked before mark.o's display_res_end, the INIT code (init.o)
# after it; INIT puts the prepared-font slots at display_res_end.
DISP_DIR  := $(here)
DISP_OBJ  := $(BUILD)/obj/DISPLAY
DISP_CFLAGS := $(ARMDOS_ARCH) -Os -g -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds -ffreestanding -fno-delete-null-pointer-checks \
               -fno-builtin -fno-jump-tables -fno-tree-loop-distribute-patterns -fno-reorder-functions \
               -ffunction-sections -fno-common -isystem $(ARMDOS_SDK)/include
DISP_OBJS := $(DISP_OBJ)/ints.o $(DISP_OBJ)/display.o $(DISP_OBJ)/mark.o $(DISP_OBJ)/init.o

$(DISP_OBJ)/%.o: $(DISP_DIR)%.c $(DISP_DIR)display.h
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(DISP_CFLAGS) -c $< -o $@
$(DISP_OBJ)/%.o: $(DISP_DIR)%.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $@

$(BUILD)/DISPLAY.SYS: $(DISP_OBJS) $(ARMDOS_SDK)/link.ld $(ARMDOS_SDK)/elf2exe.mjs
	$(ARMDOS_CC) $(ARMDOS_ARCH) -nostdlib -nostartfiles -T $(ARMDOS_SDK)/link.ld -Wl,--entry=0 \
	    -Wl,-q -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(DISP_OBJ)/DISPLAY.map \
	    -o $(DISP_OBJ)/DISPLAY.elf $(DISP_OBJS) -lgcc
	$(ARMDOS_ELF2EXE) --ar1 --stack 0 $(DISP_OBJ)/DISPLAY.elf -o $@

$(BUILD)/EGA.CPI: $(wildcard $(DISP_DIR)fonts/*.BIN) $(DISP_DIR)tools/mkcpi.mjs
	$(NODE) $(DISP_DIR)tools/mkcpi.mjs $@

.PHONY: DISPLAY
DISPLAY: $(BUILD)/DISPLAY.SYS $(BUILD)/EGA.CPI
all: $(BUILD)/DISPLAY.SYS $(BUILD)/EGA.CPI
DISK_DEPS += $(BUILD)/DISPLAY.SYS $(BUILD)/EGA.CPI

.PHONY: display-test
display-test: $(BUILD)/DISPLAY.SYS $(BUILD)/EGA.CPI $(BUILD)/MODE.COM $(BUILD)/ANSI.SYS $(BUILD)/KEYB.COM $(BUILD)/KEYBOARD.SYS \
              $(BUILD)/NLSFUNC.EXE $(BUILD)/COUNTRY.SYS \
              $(BUILD)/COMMAND.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin $(BUILD)/keyb-test/KEYTEST.EXE
	$(NODE) $(DISP_DIR)tests/run.mjs
test: display-test
