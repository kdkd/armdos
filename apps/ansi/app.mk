# apps/ansi/app.mk - ANSI.SYS, a DEVICE= console driver (ARCH.md 14.4).
#
# A freestanding AR1 image (no crt0, no libc) whose image begins with its
# device header (the SDK link script puts .devhdr first). The resident part
# (ints.o, ansi.o) is linked before mark.o's ansi_res_end, the INIT code
# (init.o) after it, and INIT returns ansi_res_end as the break address.
ANSI_DIR  := $(here)
ANSI_OBJ  := $(BUILD)/obj/ANSI
ANSI_CFLAGS := $(ARMDOS_ARCH) -Os -g -Wall -Wextra -Wno-unused-parameter -Wno-array-bounds -ffreestanding -fno-delete-null-pointer-checks \
               -fno-builtin -fno-jump-tables -fno-tree-loop-distribute-patterns -fno-reorder-functions \
               -ffunction-sections -fno-common -isystem $(ARMDOS_SDK)/include
ANSI_OBJS := $(ANSI_OBJ)/ints.o $(ANSI_OBJ)/ansi.o $(ANSI_OBJ)/mark.o $(ANSI_OBJ)/init.o

$(ANSI_OBJ)/%.o: $(ANSI_DIR)%.c $(ANSI_DIR)ansi.h
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ANSI_CFLAGS) -c $< -o $@
$(ANSI_OBJ)/%.o: $(ANSI_DIR)%.S
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_ASFLAGS) -c $< -o $@

$(BUILD)/ANSI.SYS: $(ANSI_OBJS) $(ARMDOS_SDK)/link.ld $(ARMDOS_SDK)/elf2exe.mjs
	$(ARMDOS_CC) $(ARMDOS_ARCH) -nostdlib -nostartfiles -T $(ARMDOS_SDK)/link.ld -Wl,--entry=0 \
	    -Wl,-q -Wl,--gc-sections -Wl,--no-warn-rwx-segments -Wl,-Map=$(ANSI_OBJ)/ANSI.map \
	    -o $(ANSI_OBJ)/ANSI.elf $(ANSI_OBJS) -lgcc
	$(ARMDOS_ELF2EXE) --ar1 --stack 0 $(ANSI_OBJ)/ANSI.elf -o $@

.PHONY: ANSI
ANSI: $(BUILD)/ANSI.SYS
all: $(BUILD)/ANSI.SYS
DISK_DEPS += $(BUILD)/ANSI.SYS

# tests (apps/ansi/tests/run.mjs, "make ansi-test"): helper programs go to
# build/ansi-test/, not to C:\DOS
$(call armdos_exe_to,ACAT,tests/acat.c,,,,$(BUILD)/ansi-test)
$(call armdos_exe_to,ATEST,tests/atest.c,-Wno-array-bounds,,,$(BUILD)/ansi-test)
.PHONY: ansi-test
ansi-test: $(BUILD)/ANSI.SYS $(ACAT_OUT) $(ATEST_OUT) $(BUILD)/ktest/TSHELL.EXE \
           $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
	$(NODE) $(ANSI_DIR)tests/run.mjs
test: ansi-test
