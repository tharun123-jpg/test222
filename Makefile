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

# The core archive is linked into the .aex/.plugin bundles, which are shared
# objects, so its objects must be position independent. -shared implies -fPIC
# for sources compiled in the same command, but not for a prebuilt archive.
PICFLAGS ?= -fPIC

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
	$(CXX) $(CXXFLAGS) $(PICFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

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
# src/ae and src/ae/plugins must be on the include path: the plug-in
# translation units in src/ae/plugins reach the shared glue with
# #include "plugin_entry.hpp", and quoted includes only fall back to the
# including file's own directory.
AE_INCLUDES := -I$(AE_SDK_ROOT)/Examples/Headers \
               -I$(AE_SDK_ROOT)/Examples/Util         \
               -I$(AE_SDK_ROOT)/Examples/Headers/SP   \
               -Isrc/ae -Isrc/ae/plugins               \
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
	@echo "Rez on macOS). An effect without its resource will not appear in AE."

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
#  Release packaging
#
#    make release                       -- tests + source archive + (with a real
#                                          SDK) the plug-in binaries, tarred up
#                                          in dist/
#    make release AE_SDK_ROOT=/path/sdk -- the same, plus the built bundles
#    make release-src                   -- source archive only
#    make release-clean                 -- delete dist/
#
#  A release is gated on the test suite: packaging a tree whose own tests fail
#  is not a release, it is a snapshot of a mistake. The binary half is gated on
#  the real After Effects SDK, because a .aex linked against anything else is
#  not something anyone should be able to install by accident -- if AE_SDK_ROOT
#  is unset the release is source-only and says so in the manifest.
# -----------------------------------------------------------------------------
MGTK_VERSION := $(shell awk '/^#define MGTK_VERSION_MAJOR/{a=$$3} \
                             /^#define MGTK_VERSION_MINOR/{b=$$3} \
                             /^#define MGTK_VERSION_PATCH/{c=$$3} \
                             END{print a"."b"."c}' include/mgtk/version.hpp)

# Platform tag for the archive name. uname is the portable answer on macOS and
# Linux; Windows make (MSYS, or nmake with a POSIX shim) may not have it, so
# fall back to the OS/arch environment variables.
ifneq ($(OS),Windows_NT)
  REL_PLATFORM ?= $(shell uname -s | tr 'A-Z' 'a-z')-$(shell uname -m)
else
  REL_PLATFORM ?= windows-$(shell echo "$(PROCESSOR_ARCHITECTURE)" | tr 'A-Z' 'a-z')
endif

DIST      := dist
REL_BUILD := $(BUILD)/release
REL_AE_OUT := $(REL_BUILD)/plugins
REL_NAME  := mgtk-$(MGTK_VERSION)-$(REL_PLATFORM)
REL_DIR   := $(DIST)/$(REL_NAME)
REL_TAR   := $(DIST)/$(REL_NAME).tar.gz
REL_ZIP   := $(DIST)/$(REL_NAME).zip
REL_SRC   := $(DIST)/$(REL_NAME)-src.tar.gz

# -O2 is the default for hacking; a shipped build is -O3 with asserts off.
RELEASE_OPT ?= -O3 -DNDEBUG

# The SDK is present and looks like an SDK (not just a directory name).
SDK_OK := $(shell test -n "$(AE_SDK_ROOT)" && \
                test -d "$(AE_SDK_ROOT)/Examples/Headers" && echo yes)

GIT_DESCRIBE := $(shell git describe --dirty --always --tags 2>/dev/null || echo unknown)

.PHONY: release release-src release-bin release-clean release-check \
        release-manifest release-checksums

# $(if ...) rather than an ifdef/else, so that release-checksums runs last in
# both cases -- it has to see the archives the other prerequisites just wrote.
release: release-check release-src $(if $(SDK_OK),release-bin) release-checksums
	@if [ -z "$(SDK_OK)" ]; then \
	  echo ""; \
	  echo "release: source archive only -- AE_SDK_ROOT is not set (or does not look"; \
	  echo "         like an After Effects SDK), so no plug-in binaries were packaged."; \
	  echo "         For an installable build: make release AE_SDK_ROOT=/path/to/sdk"; \
	fi

# The test gate. Runs before anything is written into dist/.
release-check: test
	@echo "release: test suite passed, packaging $(REL_NAME)"

# Source archive straight out of git, so it contains exactly what the tag
# contains and never a stray build artefact.
release-src:
	@mkdir -p $(DIST)
	@if ! git diff --quiet HEAD 2>/dev/null; then \
	  echo "release: WARNING -- working tree has uncommitted changes."; \
	  echo "         git archive packages HEAD, so those changes are NOT in"; \
	  echo "         $(REL_SRC). Commit them first if they belong in the release."; \
	fi
	git archive --format=tar.gz --prefix=$(REL_NAME)-src/ HEAD -o $(REL_SRC)
	@echo "release: $(REL_SRC)"

# The binary half. Requires the SDK; also re-runs the PiPL generator so the
# resources in the package cannot drift from the registry they were built from.
#
# It builds into build/release rather than build/: release flags are different
# from development flags, and reusing the objects already sitting in build/
# would silently link an -O2 build into a package whose manifest claims -O3.
#
# REL_WITH_AEGP is off by default: the AEGP's keyframe read/write layer is not
# written yet (see docs/AEGP.md), so a release would put one half-finished
# plug-in next to eight finished ones. Set REL_WITH_AEGP=1 once that layer
# exists and is tested.
REL_WITH_AEGP ?= 0
ifeq ($(REL_WITH_AEGP),1)
  REL_AEGP_TARGETS := aegp
  REL_AEGP_COPY := cp $(REL_AE_OUT)/MotionGraphicsToolkit_AEGP.$(AE_EXT) $(REL_DIR)/plugins/
endif

release-bin: gen-pipl
	$(MAKE) --no-print-directory BUILD=$(REL_BUILD) OPT="$(RELEASE_OPT)" \
	        plugins $(REL_AEGP_TARGETS) AE_SDK_ROOT=$(AE_SDK_ROOT)
	@$(REL_AEGP_COPY)
	@rm -rf $(REL_DIR)
	@mkdir -p $(REL_DIR)/plugins $(REL_DIR)/resources/pipl $(REL_DIR)/docs $(REL_DIR)/scripts
	@cp README.md LICENSE $(REL_DIR)/
	@cp docs/*.md $(REL_DIR)/docs/
	@cp scripts/install.sh scripts/build_windows.bat $(REL_DIR)/scripts/ 2>/dev/null || true
	@cp resources/pipl/*.r $(REL_DIR)/resources/pipl/
	@cp $(addprefix $(REL_AE_OUT)/,$(addsuffix .$(AE_EXT),$(AE_PLUGIN_NAMES))) \
	    $(REL_DIR)/plugins/
	@$(REL_AEGP_COPY)
	@$(MAKE) --no-print-directory release-manifest
	@tar -czf $(REL_TAR) -C $(DIST) $(REL_NAME)
	@if command -v zip >/dev/null 2>&1; then \
	   (cd $(DIST) && zip -qr $(REL_NAME).zip $(REL_NAME)); \
	 else \
	   echo "release: zip not installed -- $(REL_ZIP) not produced"; \
	 fi
	@$(MAKE) --no-print-directory release-checksums
	@echo "release: $(REL_TAR)"

# Records what is in the package and what it was built from. The AE_SDK_ROOT
# line is the one that matters: it is how you tell later whether a given bundle
# came from Adobe's headers or from the stand-in ones in tests/ae_shim.
release-manifest:
	@{ \
	  echo "Motion Graphics Toolkit (MGTK) $(MGTK_VERSION)"; \
	  echo "platform     : $(REL_PLATFORM)"; \
	  echo "git revision : $(GIT_DESCRIBE)"; \
	  echo "built        : `date -u '+%Y-%m-%dT%H:%M:%SZ'`"; \
	  echo "compiler     : `$(CXX) --version 2>/dev/null | head -1`"; \
	  echo "ae sdk       : $(AE_SDK_ROOT)"; \
	  echo "build flags  : $(RELEASE_OPT)"; \
	  echo ""; \
	  echo "Contents"; \
	  echo "--------"; \
	  echo "  plugins/       built .$(AE_EXT) bundles (one code fragment per effect)"; \
	  echo "  resources/pipl PiPL .r files -- attach with pipltool (Windows) or Rez"; \
	  echo "                 (macOS) before the bundle will load; see docs/BUILDING.md"; \
	  echo "  docs/          BUILDING.md, AEGP.md"; \
	  echo "  scripts/       install.sh, build_windows.bat"; \
	  echo ""; \
	  echo "Install: copy plugins/* into the AE Plug-ins folder, then attach each"; \
	  echo "PiPL resource to its bundle. scripts/install.sh does both on macOS."; \
	} > $(REL_DIR)/MANIFEST.txt
	@echo "release: $(REL_DIR)/MANIFEST.txt"

release-checksums:
	@cd $(DIST) && { \
	  for f in $$(ls *.tar.gz *.zip 2>/dev/null); do \
	    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$$f"; \
	    else shasum -a 256 "$$f"; fi; \
	  done; \
	} > SHA256SUMS
	@echo "release: $(DIST)/SHA256SUMS"

release-clean:
	rm -rf $(DIST)

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
	@echo ""
	@echo "  Release"
	@echo "  make release     run the tests, then package dist/mgtk-<version>-<platform>"
	@echo "                   (source archive always; plug-in binaries when AE_SDK_ROOT"
	@echo "                   points at a real SDK)"
	@echo "  make release-src     source archive only"
	@echo "  make release-clean   delete dist/"
	@echo ""
	@echo "  make clean       remove everything under build/"
