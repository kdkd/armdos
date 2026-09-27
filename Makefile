# ARM-DOS top-level Makefile (see ARCH.md section 12).
#
# Each component contributes a makefile fragment; missing fragments are
# skipped, so the tree builds whatever exists. Fragments add their outputs as
# prerequisites of "all" (and their checks to "test"); all outputs go to build/.
#
#   sdk/sdk.mk           crt0, libdos, the armdos_exe/armdos_com program rules
#   bios/bios.mk         build/rom.bin
#   kernel/kernel.mk     build/IO.SYS, build/ARMDOS.SYS, boot sector
#   command/command.mk   build/COMMAND.COM
#   apps/*/app.mk        one per program:  $(call armdos_exe,NAME,sources...)
#   disk/disk.mk         build/hd.img, build/floppy.img
#   emu/emu.mk          make emu-test (the emulator's own suites; emu-test-full adds QEMU diffs)
#   web/web.mk (optional)

BUILD := build
export BUILD

.PHONY: all test clean distclean images
all:
test:

include sdk/sdk.mk
include emu/emu.mk
-include bios/bios.mk
-include kernel/kernel.mk
-include command/command.mk
-include $(sort $(wildcard apps/*/app.mk))
-include disk/disk.mk
-include web/web.mk

# clean: the build outputs (build/). distclean: everything the build and
# tools/fetch-3rdparty.sh create - build/, public_html/, the downloaded and unpacked
# third-party files (3rdparty/ except the tracked manifest.json) and Python caches -
# which leaves the tree as it was checked out.
clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf public_html node_modules
	find 3rdparty -mindepth 1 -maxdepth 1 ! -name manifest.json -exec rm -rf {} +
	find . -name __pycache__ -type d -prune -exec rm -rf {} +
	find . -name '*.pyc' -type f -delete
