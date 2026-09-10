// =============================================================================
//  Motion Graphics Toolkit (MGTK) for Adobe After Effects
//  version.hpp -- single source of truth for version numbers
// =============================================================================
#pragma once

// Human-readable version, shown in the About box and the Effects Manager.
#define MGTK_VERSION_MAJOR 1
#define MGTK_VERSION_MINOR 0
#define MGTK_VERSION_PATCH 0
#define MGTK_VERSION_STAGE "release"
#define MGTK_BUILD_NUMBER 1

#define MGTK_STRINGIZE_IMPL(x) #x
#define MGTK_STRINGIZE(x) MGTK_STRINGIZE_IMPL(x)

#define MGTK_VERSION_STRING            \
    MGTK_STRINGIZE(MGTK_VERSION_MAJOR) \
    "." MGTK_STRINGIZE(MGTK_VERSION_MINOR) "." MGTK_STRINGIZE(MGTK_VERSION_PATCH)

// The AE SDK "effect spec version" this build targets. AE 23.0 == spec version 13.7.
// Bumping this lets AE know which SDK generation the plug-in was built against.
#define MGTK_AE_SPEC_VERSION_MAJOR 13
#define MGTK_AE_SPEC_VERSION_MINOR 8

// Match-name namespace. Every effect gets "<MGTK_VENDOR_PREFIX>_<effect>", and
// the match name is what AE uses to identify an effect across versions and
// localisations. NEVER change a match name after shipping -- saved projects
// reference effects by it.
#define MGTK_VENDOR_PREFIX "MotionGraphicsToolkit"

// Support URL shown in AE's Effects Manager (AE 23.5+).
#define MGTK_SUPPORT_URL "https://github.com/tharun123-jpg/test222"

// Shared category string so every effect lands in one submenu of the
// Effects & Presets panel.
#define MGTK_CATEGORY "Motion Graphics Toolkit"
