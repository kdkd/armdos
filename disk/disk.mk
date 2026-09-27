# disk/disk.mk - disk images (ARCH.md section 11). Included last by the
# top-level Makefile, so other fragments can add image inputs first:
#     DISK_DEPS += $(BUILD)/IO.SYS $(BUILD)/ARMDOS.SYS $(BUILD)/bootsect.bin
# The manifests (disk/hd.json, disk/floppy.json) say what goes where; files
# they name that do not exist yet are skipped with a note.

MKIMAGE    := $(NODE) disk/mkimage.mjs
DISK_TREES := $(shell find disk/c disk/a -type f 2>/dev/null)
DISK_IN     = $(ARMDOS_PROGRAMS) $(DISK_DEPS) $(DISK_TREES) disk/mkimage.mjs \
              $(wildcard $(BUILD)/*.SYS $(BUILD)/*.COM $(BUILD)/*.EXE $(BUILD)/bootsect.bin)

# per-app fragments (apps/*/hd.json, apps/*/floppy-games.json, ...) are merged in
DISK_FRAGS := $(wildcard apps/*/*.json)
APP_TREES  := $(shell find apps/*/disk -type f 2>/dev/null)

$(BUILD)/manifests/%.json: disk/%.json disk/merge.mjs $(DISK_FRAGS)
	@$(NODE) disk/merge.mjs $< $@

$(BUILD)/hd.img: $(BUILD)/manifests/hd.json $(DISK_IN) $(APP_TREES)
	@mkdir -p $(dir $@)
	$(MKIMAGE) build $< -o $@

$(BUILD)/floppy-boot.img: $(BUILD)/manifests/floppy.json $(DISK_IN) $(APP_TREES)
	@mkdir -p $(dir $@)
	$(MKIMAGE) build $< -o $@

$(BUILD)/floppy-%.img: $(BUILD)/manifests/floppy-%.json $(DISK_IN) $(APP_TREES)
	@mkdir -p $(dir $@)
	$(MKIMAGE) build $< -o $@

.PHONY: images disk-test setup-floppy-test
# D:, the drive that is the user's to keep (web/js/keepdisk.js): formatted, with only a README
$(BUILD)/d.img: disk/d.json disk/d/README.TXT disk/mkimage.mjs
	@mkdir -p $(dir $@)
	$(MKIMAGE) build disk/d.json -o $@

images: $(BUILD)/hd.img $(BUILD)/d.img $(BUILD)/floppy-boot.img $(BUILD)/floppy-games.img $(BUILD)/floppy-utils.img
all: images

disk-test: $(ARMDOS_TEST_BINS)
	$(NODE) disk/test-mkimage.mjs
test: disk-test

# the Startup diskette as a setup disk: boot, FORMAT C: /S, INSTALL, boot from C: (disk/test-setup-floppy.mjs)
.PHONY: setup-floppy-test
setup-floppy-test: $(BUILD)/floppy-boot.img $(BUILD)/hd.img $(BUILD)/rom.bin
	$(NODE) disk/test-setup-floppy.mjs
test: setup-floppy-test
.SECONDARY: $(BUILD)/manifests/hd.json $(BUILD)/manifests/floppy.json $(BUILD)/manifests/floppy-games.json $(BUILD)/manifests/floppy-utils.json
