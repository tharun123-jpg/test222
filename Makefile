# =============================================================================
#  Motion Graphics Toolkit for Adobe After Effects
#
#  Two things can be built from this tree:
#
#    make            -- the pure-C++ core library and its test suite.
#                       Needs nothing but a C++17 compiler. This is the part
#                       you can hack on and verify without owning After Effects.
#
#    make plugins    -- the actual .plugin / .aex bundles. Requires the Adobe
#                       After Effects SDK; point AE_SDK_ROOT at it:
#
#                         make plugins AE_SDK_ROOT=/path/to/AfterEffectsSDK
#
#  CMakeLists.txt drives the same two targets for people who prefer CMake, and
#  is the supported path on Windows.
# =============================================================================

CXX      ?= g++
AR       ?= ar
STD      ?= c++17
OPT      ?= -O2
WARN     ?= -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor
CXXFLAGS ?= -std=$(STD) $(OPT) $(WARN)
INCLUDES := -Iinclude

BUILD    := build

# -----------------------------------------------------------------------------
#  Core library + tests  (no Adobe SDK required)
# -----------------------------------------------------------------------------
CORE_SRC := $(sort $(wildcard src/core/*.cpp))
CORE_OBJ := $(patsubst src/core/%.cpp,$(BUILD)/obj/core/%.o,$(CORE_SRC))

# test_ae_glue.cpp is built separately: it needs the AE headers, and the whole
# point of the core suite is that it needs nothing but a compiler.
TEST_SRC := $(filter-out tests/test_ae_glue.cpp,$(sort $(wildcard tests/*.cpp)))
TEST_OBJ := $(patsubst tests/%.cpp,$(BUILD)/obj/tests/%.o,$(TEST_SRC))

CORE_LIB := $(BUILD)/libmgtk_core.a
TEST_BIN := $(BUILD)/mgtk_tests

# The glue suite runs the real dispatcher and the real effect registry against
# the stand-in host in tests/ae_shim. It is the closest thing to an After
# Effects install that this repository can have, and it is why the plug-in code
# is worth trusting at all before the SDK build.
GLUE_TEST_BIN := $(BUILD)/mgtk_glue_tests
GLUE_INCLUDES := -Isrc/ae -Itests/ae_shim -Isrc/ae/plugins

.PHONY: all test test-glue clean help plugins aegp check-sdk pipl gen-pipl

all: $(TEST_BIN)

$(BUILD)/obj/core/%.o: src/core/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/%.o: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Itests -MMD -MP -c $< -o $@

$(CORE_LIB): $(CORE_OBJ)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(TEST_BIN): $(TEST_OBJ) $(CORE_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(TEST_OBJ) $(CORE_LIB) -o $@

$(GLUE_TEST_BIN): tests/test_ae_glue.cpp tests/ae_shim/host.cpp $(CORE_SRC) $(wildcard src/ae/*.cpp) $(wildcard src/ae/plugins/*.cpp)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Itests $(GLUE_INCLUDES) \
	  $(CORE_SRC) $(wildcard src/ae/*.cpp) tests/test_main.cpp tests/test_ae_glue.cpp \
	  tests/ae_shim/host.cpp -o $@

test: $(TEST_BIN) $(GLUE_TEST_BIN)
	@./$(TEST_BIN)
	@echo ""
	@./$(GLUE_TEST_BIN)

# The glue suite on its own, for when you are editing src/ae.
test-glue: $(GLUE_TEST_BIN)
	@./$(GLUE_TEST_BIN)

# Run one effect's tests:  make test-one F=halftone
test-one: $(TEST_BIN)
	@./$(TEST_BIN) $(F)

-include $(wildcard $(BUILD)/obj/*/*.d)

# -----------------------------------------------------------------------------
#  After Effects plug-ins
#
#  Each effect is its own code fragment with its own PiPL, which is what Adobe
#  recommends: "one PiPL, and one plug-in, per code fragment". They all link
#  against the same static core, so the algorithms are shared and only the thin
#  AE glue is duplicated.
# -----------------------------------------------------------------------------
AE_INCLUDES := -I$(AE_SDK_ROOT)/Examples/Headers \
               -I$(AE_SDK_ROOT)/Examples/Util         \
               -I$(AE_SDK_ROOT)/Examples/Headers/SP   \
               $(INCLUDES)

AE_COMMON_SRC := $(sort $(wildcard src/ae/*.cpp))
AE_COMMON_OBJ := $(patsubst src/ae/%.cpp,$(BUILD)/obj/ae/%.o,$(AE_COMMON_SRC))

AE_PLUGIN_SRC  := $(sort $(wildcard src/ae/plugins/*.cpp))
AE_PLUGIN_NAMES := $(patsubst src/ae/plugins/%.cpp,%,$(AE_PLUGIN_SRC))

AEGP_SRC := $(sort $(wildcard src/ae/aegp/*.cpp))

# On Linux the SDK ships as .aex; macOS uses a bundle directory, and Windows a
# .aex DLL. The build only ever targets the host, because an AE plug-in must be
# compiled for the platform AE is running on.
UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
  AE_EXT     := plugin
  AE_LDFLAGS := -bundle -undefined dynamic_lookup
else ifeq ($(OS),Windows_NT)
  AE_EXT     := aex
  AE_LDFLAGS := -shared -Wl,--kill-at
else
  AE_EXT     := aex
  AE_LDFLAGS := -shared
endif

AE_OUT := $(BUILD)/plugins

check-sdk:
ifndef AE_SDK_ROOT
	$(error AE_SDK_ROOT is not set. Download the After Effects SDK from \
	        https://www.adobe.io/after-effects/ and pass AE_SDK_ROOT=/path/to/sdk)
endif
	@test -d "$(AE_SDK_ROOT)/Examples/Headers" || \
	  (echo "AE_SDK_ROOT does not look like an After Effects SDK:"; \
	   echo "  expected $(AE_SDK_ROOT)/Examples/Headers"; exit 1)

$(BUILD)/obj/ae/%.o: src/ae/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(AE_INCLUDES) -MMD -MP -c $< -o $@

# Each plug-in links: its own tiny translation unit + the shared AE glue + core.
$(AE_OUT)/%.$(AE_EXT): src/ae/plugins/%.cpp $(AE_COMMON_OBJ) $(CORE_LIB) check-sdk
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(AE_INCLUDES) $(AE_LDFLAGS) $< $(AE_COMMON_OBJ) \
	  $(CORE_LIB) -o $@

# The PiPL resources are regenerated first: they carry the out-flags that must
# match what the plug-ins report at PF_Cmd_GLOBAL_SETUP time.
plugins: gen-pipl $(patsubst %,$(AE_OUT)/%.$(AE_EXT),$(AE_PLUGIN_NAMES))
	@echo "PiPL resources are in resources/pipl -- see docs/BUILDING.md for the"
	@echo "platform step that attaches them to the binaries (pipltool on Windows,"
	@echo "Rez on macOS). An effect without its resource will not appear in AE.")

$(AE_OUT)/MotionGraphicsToolkit_AEGP.$(AE_EXT): $(AEGP_SRC) $(CORE_LIB) check-sdk
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(AE_INCLUDES) $(AE_LDFLAGS) $(AEGP_SRC) \
	  $(CORE_LIB) -o $@

aegp: $(AE_OUT)/MotionGraphicsToolkit_AEGP.$(AE_EXT)

# -----------------------------------------------------------------------------
#  PiPL resources
#
#  tools/gen_pipl reads the effect registry -- the same table the plug-ins use
#  at runtime -- and writes one .r per effect into resources/pipl. This is what
#  keeps the out-flags in the resource files identical to the ones
#  PF_Cmd_GLOBAL_SETUP reports: AE refuses to load a plug-in whose PiPL and
#  global setup disagree, and the disagreeing values are impossible to spot by
#  eye because PF_OutFlag_* are enums, not macros, so a .r file cannot even
#  spell the expression.
# -----------------------------------------------------------------------------
$(BUILD)/gen_pipl: tools/gen_pipl.cpp tools/ae_shim_stubs.cpp src/ae/registry.cpp \
                   src/ae/mgtk_ae.cpp src/ae/effect_spec.hpp src/ae/effect_registry.hpp $(CORE_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -Isrc/ae -Itests/ae_shim tools/gen_pipl.cpp \
	  tools/ae_shim_stubs.cpp src/ae/registry.cpp src/ae/mgtk_ae.cpp $(CORE_LIB) -o $@

gen-pipl: $(BUILD)/gen_pipl
	@./$(BUILD)/gen_pipl resources/pipl

pipl: gen-pipl

# -----------------------------------------------------------------------------
#  Housekeeping
# -----------------------------------------------------------------------------
clean:
	rm -rf $(BUILD)

help:
	@echo "Motion Graphics Toolkit -- build targets"
	@echo ""
	@echo "  make help        this message"
	@echo "  make             build the core library and the test suite"
	@echo "  make test        build and run every core unit test"
	@echo "  make test-one F=blur    run only tests whose name contains 'blur'"
	@echo "  make test-glue   build and run only the After Effects glue tests"
	@echo "  make gen-pipl    regenerate resources/pipl/*.r from the effect registry"
	@echo "  make plugins AE_SDK_ROOT=/path/to/sdk   build the AE effects"
	@echo "  make aegp AE_SDK_ROOT=/path/to/sdk      build the AEGP workflow companion"
	@echo "  make clean       remove everything under build/"
