# =============================================================================
#  Motion Graphics Toolkit for Adobe After Effects
#
#  Two things can be built here, and they have very different requirements:
#
#    * The core library and its test suites need nothing but a C++17 compiler.
#      They are the supported way to work on the pixel algorithms, because they
#      build everywhere and run in under a second.
#
#    * The After Effects plug-ins need the Adobe After Effects SDK. Point
#      AE_SDK_ROOT at it and they are added to the build automatically.
#
#  The same two targets exist in the Makefile; this file is the supported path
#  on Windows and for anyone who prefers CMake.
# =============================================================================
cmake_minimum_required(VERSION 3.16)
project(MotionGraphicsToolkit
        VERSION 1.0.0
        DESCRIPTION "Motion graphics effects and keyframe generators for Adobe After Effects"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MGTK_BUILD_TESTS "Build the core and glue test suites" ON)
option(MGTK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# -----------------------------------------------------------------------------
#  Warning set
#
#  Shared deliberately: the same flags are used for the core, the plug-ins and
#  the tests, so a warning cannot hide in the layer nobody looks at.
# -----------------------------------------------------------------------------
if(MSVC)
  add_compile_options(/W4 /permissive-)
  if(MGTK_WARNINGS_AS_ERRORS)
    add_compile_options(/WX)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
  if(MGTK_WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
  endif()
endif()

# -----------------------------------------------------------------------------
#  The core
#
#  Flat, static and free of Adobe headers by construction: the source list is a
#  glob of src/core, and nothing in that directory is allowed to include an AE
#  header. That is what keeps the algorithms testable off an After Effects
#  machine, so it is worth keeping true.
# -----------------------------------------------------------------------------
file(GLOB MGTK_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp")

add_library(mgtk_core STATIC ${MGTK_CORE_SOURCES})
target_include_directories(mgtk_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# -----------------------------------------------------------------------------
#  Tests
# -----------------------------------------------------------------------------
if(MGTK_BUILD_TESTS)
  enable_testing()

  file(GLOB MGTK_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
  list(REMOVE_ITEM MGTK_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ae_glue.cpp")

  add_executable(mgtk_tests ${MGTK_TEST_SOURCES} tests/test_main.cpp)
  target_link_libraries(mgtk_tests PRIVATE mgtk_core)
  target_include_directories(mgtk_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_test(NAME mgtk_core COMMAND mgtk_tests)

  # The glue suite drives the real dispatcher and registry through the stand-in
  # host in tests/ae_shim, so a mistake in the AE-facing code is caught here
  # rather than on first launch.
  file(GLOB MGTK_AE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ae/*.cpp")
  file(GLOB MGTK_PLUGIN_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/src/ae/plugins/*.cpp")

  add_executable(mgtk_glue_tests
                 tests/test_ae_glue.cpp
                 tests/ae_shim/host.cpp
                 tests/test_main.cpp
                 ${MGTK_AE_SOURCES})
  target_link_libraries(mgtk_glue_tests PRIVATE mgtk_core)
  target_include_directories(mgtk_glue_tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/tests"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/ae_shim"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/ae")
  add_test(NAME mgtk_glue COMMAND mgtk_glue_tests)

  # A tool, not a test: it reads the effect registry and writes the PiPL
  # resource files, which is how the out-flags in those files are guaranteed to
  # match what PF_Cmd_GLOBAL_SETUP reports.
  add_executable(gen_pipl
                 tools/gen_pipl.cpp
                 tools/ae_shim_stubs.cpp
                 src/ae/registry.cpp
                 src/ae/mgtk_ae.cpp)
  target_link_libraries(gen_pipl PRIVATE mgtk_core)
  target_include_directories(gen_pipl PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/ae"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/ae_shim")
endif()

# -----------------------------------------------------------------------------
#  After Effects plug-ins
#
#  Enabled by the presence of the SDK, not by a flag, so that a build on a
#  machine without it still succeeds instead of failing at configure time.
#
#  Each effect is its own code fragment with its own PiPL resource, which is
#  what Adobe recommends: one PiPL and one plug-in per fragment. They all link
#  the same static core, so nothing about the algorithms is duplicated.
# -----------------------------------------------------------------------------
if(DEFINED AE_SDK_ROOT AND EXISTS "${AE_SDK_ROOT}/Examples/Headers")
  message(STATUS "After Effects SDK found at ${AE_SDK_ROOT}; building plug-ins")

  file(GLOB MGTK_AE_COMMON CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ae/*.cpp")
  file(GLOB MGTK_EFFECT_SOURCES CONFIGURE_DEPENDS
       "${CMAKE_CURRENT_SOURCE_DIR}/src/ae/plugins/effect_*.cpp")

  if(APPLE)
    set(MGTK_BUNDLE_SUFFIX ".plugin")
  else()
    set(MGTK_BUNDLE_SUFFIX ".aex")
  endif()

  foreach(effect_source ${MGTK_EFFECT_SOURCES})
    get_filename_component(effect_name ${effect_source} NAME_WE)

    add_library(${effect_name} MODULE ${effect_source} ${MGTK_AE_COMMON})
    target_link_libraries(${effect_name} PRIVATE mgtk_core)
    target_include_directories(${effect_name} PRIVATE
        "${AE_SDK_ROOT}/Examples/Headers"
        "${AE_SDK_ROOT}/Examples/Util"
        "${AE_SDK_ROOT}/Examples/Headers/SP"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ae")

    # An AE plug-in is a bundle on macOS and a plain shared library elsewhere.
    if(APPLE)
      set_target_properties(${effect_name} PROPERTIES
          BUNDLE TRUE
          BUNDLE_EXTENSION "plugin"
          MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in")
    endif()
    set_target_properties(${effect_name} PROPERTIES PREFIX "" SUFFIX "${MGTK_BUNDLE_SUFFIX}")

    # The PiPL resource is attached after linking: pipltool converts the .r to
    # a .rc on Windows, and Rez appends it to the bundle on macOS. Both steps
    # are documented in docs/BUILDING.md, and both need the SDK's tools.
    add_custom_command(TARGET ${effect_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo
                "Attach resources/pipl/*.r to $<TARGET_FILE_NAME:${effect_name}> -- see docs/BUILDING.md"
        VERBATIM)
  endforeach()

  file(GLOB MGTK_AEGP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ae/aegp/*.cpp")
  if(MGTK_AEGP_SOURCES)
    add_library(MotionGraphicsToolkit_AEGP MODULE ${MGTK_AEGP_SOURCES})
    target_link_libraries(MotionGraphicsToolkit_AEGP PRIVATE mgtk_core)
    target_include_directories(MotionGraphicsToolkit_AEGP PRIVATE
        "${AE_SDK_ROOT}/Examples/Headers"
        "${AE_SDK_ROOT}/Examples/Util"
        "${AE_SDK_ROOT}/Examples/Headers/SP")
    set_target_properties(MotionGraphicsToolkit_AEGP PROPERTIES
        PREFIX "" SUFFIX "${MGTK_BUNDLE_SUFFIX}")
  endif()
elseif(DEFINED AE_SDK_ROOT)
  message(WARNING "AE_SDK_ROOT is set to '${AE_SDK_ROOT}' but ${AE_SDK_ROOT}/Examples/Headers "
                  "does not exist; the plug-ins will not be built. See docs/BUILDING.md.")
else()
  message(STATUS "AE_SDK_ROOT is not set; building the core and tests only. "
                 "See docs/BUILDING.md to build the After Effects plug-ins.")
endif()

# -----------------------------------------------------------------------------
#  Convenience targets
# -----------------------------------------------------------------------------
add_custom_target(checks
    COMMENT "Building everything that can be built without the After Effects SDK"
    DEPENDS mgtk_tests mgtk_glue_tests)

if(TARGET gen_pipl)
  add_custom_target(pipl
      COMMAND gen_pipl "${CMAKE_CURRENT_SOURCE_DIR}/resources/pipl"
      DEPENDS gen_pipl
      COMMENT "Regenerating resources/pipl from the effect registry")
endif()
