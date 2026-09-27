# apps/cdrom/disc.mk - the "ARM-DOS Multimedia Sampler '93" disc (included by app.mk).
#
#   make cdrom-disc        -> build/cdrom/sampler93/{disc.json, data.iso, tNN.wav/.opus/.mp3}
#   make cdrom-disc-test   ISO 9660 builder test + the disc's consistency checks
#
# The audio (ffmpeg: loudnorm, Opus, MP3) is only re-made when an original in
# 3rdparty/cdrom-music/ or the build script changes; the ISO is rebuilt when anything it
# contains changes. Programs that are not built yet are left off the disc with a note.
CDROM_DISC_DIR := $(BUILD)/cdrom/sampler93
CDROM_DISC := $(CDROM_DISC_DIR)/disc.json
CDROM_DISC_INPUTS := $(CDROM_DIR)tools/build-disc.mjs $(CDROM_DIR)tools/iso9660.mjs \
    $(shell find $(CDROM_DIR)disc 3rdparty/cdrom-music -type f 2>/dev/null | sed 's/ /\\ /g') \
    $(wildcard $(BUILD)/DEMO.EXE $(BUILD)/ZORK1.EXE $(BUILD)/ZORK2.EXE $(BUILD)/ZORK3.EXE $(BUILD)/ADVENT.EXE $(BUILD)/ARMINFO.EXE) \
    apps/demo/DEMO.NFO apps/ansi/art/ANSI.ART $(wildcard apps/basic/disk/BASIC/*) apps/zork/data/LICENSE.TXT

$(CDROM_DISC): $(CDROM_DISC_INPUTS)
	@mkdir -p $(CDROM_DISC_DIR)
	$(NODE) $(CDROM_DIR)tools/build-disc.mjs --out $(CDROM_DISC_DIR)

.PHONY: cdrom-disc cdrom-disc-test
cdrom-disc: $(CDROM_DISC)
all: $(CDROM_DISC)

cdrom-disc-test: $(CDROM_DISC)
	$(NODE) $(CDROM_DIR)tests/iso9660.mjs
	$(NODE) $(CDROM_DIR)tests/disc.mjs --disc $(CDROM_DISC_DIR)
test: cdrom-disc-test

# CDPLAY.INI (made with the disc) goes to C:\DOS: build the disc before the hard disk image
DISK_DEPS += $(CDROM_DISC)
