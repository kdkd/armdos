# web/web.mk - the web front end (web/README.md).
#   make site        stage the complete static site into build/site
#   make web-test    Playwright checks of the staged site (Python with Playwright:
#                    pip install playwright && playwright install chromium firefox;
#                    WEB_PY=/path/to/python selects the interpreter)

SITE      := $(BUILD)/site
WEB_SRC   := $(shell find web/index.html web/disks.json web/sw.js web/manifest.webmanifest web/css web/js web/assets web/fonts web/docs web/tools -type f 2>/dev/null)
EMU_SRC   := $(wildcard emu/*.mjs emu/dev/*.mjs emu/online/*.mjs emu/fonts/*)
WEB_PY    ?= python3

# a freshly formatted, empty 1.44 MB diskette for the disk box
$(BUILD)/floppy-blank.img: disk/mkimage.mjs
	@mkdir -p $(BUILD)/web-empty
	$(NODE) disk/mkimage.mjs build --format fd1440 --dir $(BUILD)/web-empty --label "NO NAME" -o $@

$(SITE)/index.html: $(WEB_SRC) $(EMU_SRC) $(BUILD)/rom.bin $(BUILD)/hd.img $(BUILD)/floppy-boot.img $(BUILD)/floppy-blank.img $(wildcard $(BUILD)/floppy-*.img) $(wildcard $(BUILD)/bbs.img) $(BUILD)/cdrom/sampler93/disc.json
	$(NODE) web/tools/build-site.mjs --out $(SITE)

# the disk box's blank diskette is part of the site (build.sh stages it into public_html/)
all: $(BUILD)/floppy-blank.img

.PHONY: site web-test cdrom-web-test
site: $(SITE)/index.html

web-test: site
	$(NODE) web/tests/test-fat.mjs
	$(WEB_PY) web/tests/test_site.py
	$(WEB_PY) web/tests/test_monitor.py
	$(WEB_PY) web/tests/test_mobile.py
	$(WEB_PY) web/tests/test_stream.py
	$(WEB_PY) web/tests/test_round5.py
	$(WEB_PY) web/tests/test_modem.py
	$(WEB_PY) web/tests/test_openbox.py
	$(WEB_PY) web/tests/test_keyb.py
	$(WEB_PY) web/tests/test_kbdnudge.py
	$(WEB_PY) web/tests/test_persist.py
	$(WEB_PY) web/tests/test_sw_update.py
	$(WEB_PY) web/tests/test_bbs_retry.py

# the CD-ROM drive on the page (web/js/cdrom.js): insert, ARMCDEX, DIR D:, CDPLAY plays track 2
cdrom-web-test: site $(BUILD)/CDPLAY.EXE
	$(WEB_PY) web/tests/test_cdrom.py
