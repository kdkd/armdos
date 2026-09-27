# emu/emu.mk - the emulator's own test suites (emu/tests/run-all.mjs): the CPU and JIT,
# the disassembler, every device, the modem and phone exchange, ZMODEM, the test ROM.
#
#   make emu-test        the fast set (a minute or two): part of "make test"
#   make emu-test-full   plus the differential checks against QEMU (qemu-system-arm):
#                        random ARM and VFP instructions and C programs, run on both
#                        (several minutes; skipped with a note if QEMU is missing)
.PHONY: emu-test emu-test-full
emu-test: $(BUILD)/rom.bin $(BUILD)/hd.img
	$(NODE) emu/tests/run-all.mjs --quick
test: emu-test

emu-test-full: $(BUILD)/rom.bin $(BUILD)/hd.img
	$(NODE) emu/tests/run-all.mjs
