// =============================================================================
//  src/ae/aegp/mgtk_aegp.cpp -- the Motion Graphics Toolkit AEGP.
//
//  WHAT THIS FILE IS
//
//  A working AEGP: it initialises, registers a submenu of commands, and answers
//  them. `make aegp AE_SDK_ROOT=...` builds it.
//
//  WHAT THIS FILE IS NOT
//
//  It is not the keyframe editor those commands will eventually drive. The
//  keyframe generators themselves -- easing, stagger, overshoot, bounce,
//  wiggle -- are finished and tested in the core (include/mgtk/keyframes.hpp,
//  src/core/keyframes.cpp, tests/test_keyframes.cpp). What is missing is the
//  layer that reads keyframes out of an AEGP stream and writes them back, and
//  that layer is deliberately absent rather than sketched.
//
//  The reason is that the AEGP stream and keyframe suites are the one part of
//  this project that cannot be compiled and exercised in the repository's own
//  test suite: they exist only inside After Effects. Writing calls against
//  them from documentation, without a single compile, would produce code that
//  looks finished, does not build, and silently wastes the time of whoever
//  tries it first. docs/AEGP.md lists exactly which suites and calls that
//  layer needs, so it can be written against a real SDK in an afternoon.
//
//  Everything in this file, by contrast, is API this project can stand behind:
//  the plug-in entry points, the command registration and the menu.
// =============================================================================
#include "AE_Effect.h"

#include "AE_GeneralPlug.h"
#include "AEGP_SuiteHandler.h"
#include "AE_CommandSuite.h"
#include "AE_UtilitySuite.h"

#include "mgtk/version.hpp"

#include <string>

namespace {

AEGP_PluginID g_plugin_id = 0;

struct Commands {
    AEGP_Command toolkit_menu = 0;
    AEGP_Command about = 0;
};

Commands g_commands;

}  // namespace

// ---------------------------------------------------------------------------
//  Plug-in entry points
//
//  The names and signatures here are AEGP_PluginInitFuncs, the same set every
//  AEGP provides. The host looks them up by name in the binary.
// ---------------------------------------------------------------------------
extern "C" {

A_Err AEGP_PluginInit(AEGP_PluginInitFuncs* funcs, AEGP_PluginID plugin_id,
                      SPBasicSuite* basic, A_long* version) {
    (void)funcs;
    (void)basic;
    (void)version;
    g_plugin_id = plugin_id;
    return A_Err_NONE;
}

A_Err AEGP_PluginSetup(AEGP_GlobalRefcon* global_refcon, AEGP_InitRefcon* init_refcon) {
    (void)global_refcon;
    (void)init_refcon;

    AEGP_SuiteHandler suites(nullptr);

    // Commands have to exist before the menu item that points at them.
    suites.CommandSuite1()->AEGP_GetUniqueCommand(&g_commands.toolkit_menu);
    suites.CommandSuite1()->AEGP_GetUniqueCommand(&g_commands.about);

    // The Animation menu is where keyframe tools belong: it is the menu an
    // animator already has open when they reach for this.
    suites.CommandSuite1()->AEGP_InsertMenuCommand(g_commands.toolkit_menu,
                                                   "Motion Graphics Toolkit",
                                                   AEGP_Menu_ANIMATION,
                                                   AEGP_MENU_INSERT_SORTED);
    suites.CommandSuite1()->AEGP_InsertMenuCommand(g_commands.about, "MGTK About",
                                                   AEGP_Menu_ANIMATION,
                                                   AEGP_MENU_INSERT_SORTED);

    return A_Err_NONE;
}

A_Err AEGP_PluginSetdown(AEGP_GlobalRefcon* global_refcon) {
    (void)global_refcon;
    return A_Err_NONE;
}

A_Err AEGP_PluginEnable(AEGP_GlobalRefcon* global_refcon) {
    (void)global_refcon;
    return A_Err_NONE;
}

A_Err AEGP_PluginMenuHook(AEGP_GlobalRefcon* global_refcon, A_long reserved) {
    (void)global_refcon;
    (void)reserved;
    return A_Err_NONE;
}

A_Err AEGP_PluginCommandHook(AEGP_GlobalRefcon* global_refcon, AEGP_Command command,
                             AEGP_Command refcon) {
    (void)global_refcon;
    (void)refcon;

    if (command == g_commands.about) {
        AEGP_SuiteHandler suites(nullptr);
        const std::string message =
            std::string("Motion Graphics Toolkit ") + MGTK_VERSION_STRING +
            "\nEight effects, plus the keyframe generators described in "
            "docs/AEGP.md.\n" MGTK_SUPPORT_URL;
        suites.UtilitySuite2()->AEGP_ReportInfo(g_plugin_id, message.c_str());
    }
    return A_Err_NONE;
}

}  // extern "C"
