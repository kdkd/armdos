# apps/advent/app.mk - ADVENT.EXE: Open Adventure 1.22 (BSD-2-Clause), the
# Crowther/Woods "Adventure 2.5". src/dungeon.[ch] were generated on the host
# from adventure.yaml by make_dungeon.py (see README.md) and are vendored.
ADVENT_SRCS := src/main.c src/init.c src/actions.c src/score.c src/misc.c \
               src/saveresume.c src/dungeon.c dosline.c
ADVENT_CFLAGS := -I$(here)compat -I$(here)src -D_DEFAULT_SOURCE -DVERSION='"1.22"' \
                 -Wno-sign-compare -Wno-missing-field-initializers -Wno-char-subscripts -Wno-format
$(call armdos_exe,ADVENT,$(ADVENT_SRCS),$(ADVENT_CFLAGS),,--stack 32768)
