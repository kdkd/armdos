# apps/mslib/mslib.mk - the shared library for Microsoft's MS-DOS 4.0 C
# utilities on ARM-DOS (message retriever, SysParse, MS C bits) and the rule
# that builds such a utility.  Included by every app.mk that uses it (the
# guard makes that harmless):
#
#     FOO_DIR := $(here)                  # before the include ($(here) moves)
#     include apps/mslib/mslib.mk
#     $(call mslib_exe,FOO,$(FOO_DIR),src/foo.c ...,CFLAGS,src/FOO.SKL,MSGTAB-OPTS,ELF2EXE-OPTS)
#
# builds build/FOO.EXE from the sources plus a message table generated from
# the utility's .SKL skeleton and apps/mslib/msg/USA-MS.MSG
# (tools/msgtab.py, the stand-in for Microsoft's BUILDMSG.EXE).

ifndef MSLIB_MK
MSLIB_MK := 1

MSLIB_DIR    := apps/mslib/
MSLIB_OUT    := $(BUILD)/obj/mslib
MSLIB_LIB    := $(MSLIB_OUT)/libmslib.a
MSLIB_CFLAGS := -I$(MSLIB_DIR)include -DARMDOS
MSLIB_PY     ?= python3
MSLIB_MSGTAB := $(MSLIB_PY) $(MSLIB_DIR)tools/msgtab.py
MSLIB_MSGDEP := $(MSLIB_DIR)tools/msgtab.py $(MSLIB_DIR)msg/USA-MS.MSG

# Microsoft's 1987 C: K&R definitions, implicit int, 16-bit int assumptions
# in places (the ports fix those that matter under #ifdef ARMDOS)
MSLIB_MSC_CFLAGS := -Dfar= -Dnear= -Dhuge= -std=gnu89 -fpermissive -fno-strict-aliasing -fsigned-char \
    -Wno-implicit-int -Wno-implicit-function-declaration -Wno-return-type \
    -Wno-int-conversion -Wno-incompatible-pointer-types -Wno-pointer-sign \
    -Wno-sign-compare -Wno-parentheses -Wno-unused-variable -Wno-unused-but-set-variable \
    -Wno-unused-function -Wno-missing-field-initializers -Wno-char-subscripts \
    -Wno-builtin-declaration-mismatch -Wno-old-style-declaration -Wno-format \
    -Wno-comment -Wno-empty-body -Wno-unused-value -Wno-int-to-pointer-cast \
    -Wno-pointer-to-int-cast -Wno-address -Wno-dangling-else -Wno-misleading-indentation \
    -Wno-implicit-fallthrough -Wno-unused-label -Wno-type-limits -Wno-array-bounds \
    -Wno-overflow -Wno-multichar -Wno-endif-labels -Wno-maybe-uninitialized \
    -Wno-stringop-overflow -Wno-format-overflow -Wno-restrict -Wno-cast-function-type \
    -Wno-shift-count-overflow -Wno-uninitialized -Wno-discarded-qualifiers -Wno-main -Wno-unused-but-set-parameter -Wno-missing-braces -Wno-missing-parameter-type -Wno-return-mismatch

MSLIB_SRCS := $(wildcard $(MSLIB_DIR)src/*.c)
MSLIB_OBJS := $(patsubst $(MSLIB_DIR)src/%.c,$(MSLIB_OUT)/%.o,$(MSLIB_SRCS)) $(MSLIB_OUT)/common_msg.o

$(MSLIB_OUT)/%.o: $(MSLIB_DIR)src/%.c $(wildcard $(MSLIB_DIR)include/*.h) $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) $(MSLIB_CFLAGS) -c $< -o $@

$(MSLIB_OUT)/common_msg.c: $(MSLIB_MSGDEP)
	@mkdir -p $(dir $@)
	$(MSLIB_MSGTAB) --common -o $@

$(MSLIB_OUT)/common_msg.o: $(MSLIB_OUT)/common_msg.c $(MSLIB_DIR)include/mslib_msg.h
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) $(MSLIB_CFLAGS) -c $< -o $@

$(MSLIB_LIB): $(MSLIB_OBJS)
	@rm -f $@
	$(ARMDOS_AR) rcs $@ $^

.PHONY: mslib
mslib: $(MSLIB_LIB)

# $(1) NAME  $(2) app directory  $(3) sources (relative to it)  $(4) CFLAGS
# $(5) .SKL (relative to the app directory, empty = no message table)
# $(6) msgtab.py options  $(7) elf2exe options  $(8) extra objects to link
# (generated sources the app.mk compiles itself)
define _mslib_msgtab
$(BUILD)/obj/$(1)/msgtab.c: $(2)$(5) $(MSLIB_MSGDEP)
	@mkdir -p $$(dir $$@)
	$(MSLIB_MSGTAB) $(2)$(5) -o $$@ $(6)

$(BUILD)/obj/$(1)/msgtab.o: $(BUILD)/obj/$(1)/msgtab.c $(MSLIB_DIR)include/mslib_msg.h
	$$(ARMDOS_CC) $$(ARMDOS_CFLAGS) $(MSLIB_CFLAGS) -c $$< -o $$@
endef

mslib_exe = $(if $(strip $(5)),$(eval $(call _mslib_msgtab,$(strip $(1)),$(2),,,$(strip $(5)),$(6))))$(eval \
  $(call _armdos_program,$(strip $(1)),$(strip $(3)),$(MSLIB_CFLAGS) $(4),$(if $(strip $(5)),$(BUILD)/obj/$(strip $(1))/msgtab.o) $(8) $(MSLIB_LIB),$(7),EXE,$(2),$(BUILD)))$(eval \
  all: $(BUILD)/$(strip $(1)).EXE)$(eval \
  $$($(strip $(1))_ELF): $(if $(strip $(5)),$(BUILD)/obj/$(strip $(1))/msgtab.o) $(8) $(MSLIB_LIB))

endif
