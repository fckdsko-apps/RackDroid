# Desktop VCV Rack build of the RackDroid Drums pack (`make dist` produces
# dist/RackDroidDrums-<version>-<os>-<arch>.vcvplugin, installable in Rack 2).
# The Android build compiles the same sources via native/CMakeLists.txt.
RACK_DIR ?= ../third_party/Rack

# The vendored Rack tree has no dep/include (that's populated by upstream's
# `make dep`); point straight at the dep sources instead, exactly like the
# Android CMake build does. jansson headers come from any configured CMake
# build of this repo (JANSSON_INC to override).
FLAGS += \
	-I../native/compat \
	-I$(RACK_DIR)/dep/nanovg/src \
	-I$(RACK_DIR)/dep/nanosvg/src \
	-I$(RACK_DIR)/dep/oui-blendish \
	-I$(RACK_DIR)/dep/osdialog \
	-I$(RACK_DIR)/dep/filesystem/include \
	-I$(RACK_DIR)/dep/fuzzysearchdatabase/src \
	-I$(RACK_DIR)/dep/glfw/include \
	-I$(RACK_DIR)/dep/simde \
	-I$(RACK_DIR)/dep/speexdsp/include \
	-I$(RACK_DIR)/dep/pffft \
	-I$(RACK_DIR)/dep/tinyexpr

JANSSON_INC ?= $(firstword $(wildcard \
	../build-hostui/_deps/jansson-build/include \
	../app/.cxx/RelWithDebInfo/*/arm64-v8a/_deps/jansson-build/include))
ifneq ($(JANSSON_INC),)
	FLAGS += -I$(JANSSON_INC)
endif

# speexdsp_config_types.h is autoconf/CMake-generated; emit the standard
# stdint version (same content the CMake build configures).
SPEEX_STUB := build/dep_include
$(shell mkdir -p $(SPEEX_STUB) && printf '%s\n' \
	'#ifndef __SPEEX_TYPES_H__' \
	'#define __SPEEX_TYPES_H__' \
	'#include <stdint.h>' \
	'typedef int16_t spx_int16_t;' \
	'typedef uint16_t spx_uint16_t;' \
	'typedef int32_t spx_int32_t;' \
	'typedef uint32_t spx_uint32_t;' \
	'#endif' > $(SPEEX_STUB)/speexdsp_config_types.h)
FLAGS += -I$(SPEEX_STUB)

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)

include $(RACK_DIR)/plugin.mk
