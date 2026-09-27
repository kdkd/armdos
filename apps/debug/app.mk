# apps/debug/app.mk - DEBUG.COM: DOS 4.00's DEBUG for the ARM PC (see README.md).
# A self-relocating .COM (elf2exe --com --selfreloc), as DEBUG was a .COM in 4.00.

DEBUG_DIR := $(here)

$(call armdos_com,DEBUG,debug.c run.c disasm.c asm.c trap.S)

# "make debug-test": the disassembler/assembler differential tests (host build
# against emu/disasm.mjs, arm-none-eabi-objdump and arm-none-eabi-as), then
# DEBUG itself running headless on ARM-DOS (tests/run.mjs).
.PHONY: debug-test
debug-test: $(BUILD)/DEBUG.COM $(BUILD)/rom.bin $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin \
            $(BUILD)/ktest/TSHELL.EXE
	$(NODE) $(DEBUG_DIR)tests/asmtest.mjs
	$(NODE) $(DEBUG_DIR)tests/run.mjs

test: debug-test
