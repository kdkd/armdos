# apps/tvlib/tvlib.mk - shared build pieces of the Turbo Vision IDEs (TC.EXE,
# QB.EXE); included by apps/tc/app.mk and apps/qbasic/app.mk (not an app.mk
# of its own, so it is read once, before they use it).
#
#   $(TVLIB_TV)      Turbo Vision (EDIT's libtv.a: build/obj/EDIT/libtv.a)
#   $(TVLIB_OBJS)    EDIT's platform layer (apps/edit/armdos, unchanged) +
#                    extras (plat.cpp), edits_hl.o (TEditor::formatLine with a
#                    colouring hook), ximage.o (the XMS image side)
#   $(TVLIB_CXXFLAGS) flags for code that includes <tvision/tv.h>
#   $(TVLIB_STUB)    build/tvlib/XLOAD.EXE, the extended-memory loader stub
#   $(call tvlib_bind,OUT,IMAGE): OUT = stub + IMAGE (see xload.c)
ifndef TVLIB_MK
TVLIB_MK := 1

TVLIB_DIR := $(here)
TVLIB_OUT := $(BUILD)/obj/TVLIB
TVLIB_TV  := $(BUILD)/obj/EDIT/libtv.a
TVLIB_EDIT := $(TVLIB_DIR)../edit/

TVLIB_CXXFLAGS := -std=gnu++17 -D__ARMDOS__ \
                  -I$(TVLIB_EDIT)tvision/include -I$(TVLIB_EDIT)tvision/include/tvision \
                  -I$(TVLIB_EDIT)tvision/include/tvision/compat/borland \
                  -I$(TVLIB_EDIT)tvision/include/tvision/compat/windows \
                  -I$(TVLIB_EDIT)armdos -I$(TVLIB_DIR) \
                  -Wno-deprecated-declarations -Wno-unused-variable -Wno-sign-compare \
                  -Wno-missing-field-initializers -Wno-implicit-fallthrough -Wno-parentheses \
                  -Wno-class-memaccess -Wno-unused-but-set-variable

TVLIB_CPP  := plat plat_text plat_dir plat_rt edits_hl tvhelp
TVLIB_OBJS := $(patsubst %,$(TVLIB_OUT)/%.o,$(TVLIB_CPP)) $(TVLIB_OUT)/ximage.o

$(TVLIB_OUT)/%.o: $(TVLIB_DIR)%.cpp $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CXX) $(ARMDOS_CXXFLAGS) $(TVLIB_CXXFLAGS) -MMD -MP -c $< -o $@

$(TVLIB_OUT)/ximage.o: $(TVLIB_DIR)ximage.c $(ARMDOS_SDK_HEADERS)
	@mkdir -p $(dir $@)
	$(ARMDOS_CC) $(ARMDOS_CFLAGS) -I$(TVLIB_DIR) -MMD -MP -c $< -o $@

-include $(TVLIB_OBJS:.o=.d)

# the loader stub: small stack, no stdio
$(call armdos_exe_to,XLOAD,xload.c,,,--stack 2048,$(BUILD)/tvlib)
TVLIB_STUB := $(BUILD)/tvlib/XLOAD.EXE

# $(call tvlib_bind,OUT,IMAGE): the stub, padded to 16 bytes, then the image
define tvlib_bind
$(1): $(TVLIB_STUB) $(2)
	$$(NODE) -e 'const fs=require("fs");const a=fs.readFileSync(process.argv[1]),b=fs.readFileSync(process.argv[2]);const pad=(16-a.length%16)%16;fs.writeFileSync(process.argv[3],Buffer.concat([a,Buffer.alloc(pad),b]))' $(TVLIB_STUB) $(2) $$@
endef

endif
