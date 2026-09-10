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

TEST_SRC := $(sort $(wildcard tests/*.cpp))
TEST_OBJ := $(patsubst tests/%.cpp,$(BUILD)/obj/tests/%.o,$(TEST_SRC))

CORE_LIB := $(BUILD)/libmgtk_core.a
TEST_BIN := $(BUILD)/mgtk_tests

.PHONY: all test clean help plugins aegp check-sdk

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

test: $(TEST_BIN)
	@./$(TEST_BIN)

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

plugins: $(patsubst %,$(AE_OUT)/%.$(AE_EXT),$(AE_PLUGIN_NAMES))

$(AE_OUT)/MotionGraphicsToolkit_AEGP.$(AE_EXT): $(AEGP_SRC) $(CORE_LIB) check-sdk
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(AE_INCLUDES) $(AE_LDFLAGS) $(AEGP_SRC) \
	  $(CORE_LIB) -o $@

aegp: $(AE_OUT)/MotionGraphicsToolkit_AEGP.$(AE_EXT)

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
	@echo "  make plugins AE_SDK_ROOT=/path/to/sdk   build the AE effects"
	@echo "  make aegp AE_SDK_ROOT=/path/to/sdk      build the AEGP workflow companion"
	@echo "  make clean       remove everything under build/"
