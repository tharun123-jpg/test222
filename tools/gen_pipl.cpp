// =============================================================================
//  tools/gen_pipl.cpp -- writes the PiPL resource files for every effect.
//
//  A PiPL is the small resource (Windows: .rc, macOS: .r) that tells After
//  Effects what is inside a binary: its kind, its name, the entry point symbol
//  and -- the part that bites people -- its global out-flags.
//
//  Those out-flags must be byte-identical to what the plug-in writes to
//  out_data->out_flags in PF_Cmd_GLOBAL_SETUP, or AE refuses to load it with
//  "the values in the pipl must correlate to the values set in the plug-ins
//  global setup call". Worse, they cannot be written as expressions:
//  PF_OutFlag_* are C enums, not #defines, so a .r file containing
//  `PF_OutFlag_USE_OUTPUT_EXTENT | PF_OutFlag_DEEP_COLOR_AWARE` preprocesses to
//  `0 | 0` and the effect silently registers no flags at all (some resource
//  compilers reject it outright with "CloseBrace Expected").
//
//  Both problems disappear if the numbers are generated from the same table
//  the plug-in itself reads. That is what this program does: it links against
//  the real effect registry and prints kOutFlags / kOutFlags2 as literals.
//
//  Usage:  gen_pipl [output-directory]        (default: resources/pipl)
// =============================================================================
#include "effect_registry.hpp"

#include "mgtk/version.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// PF_VERSION packs the components the same way AE does; AE_Effect_Version in
// the PiPL carries the *effect's* version in this encoding.
A_u_long pack_version(int major, int minor, int bugfix, int stage, int build) {
    return (static_cast<A_u_long>(major) << 19) | (static_cast<A_u_long>(minor) << 15) |
           (static_cast<A_u_long>(bugfix) << 11) | (static_cast<A_u_long>(stage) << 9) |
           static_cast<A_u_long>(build);
}

// The out-flags the effect will report at runtime, computed exactly the way
// dispatcher.cpp computes them.
A_u_long runtime_out_flags(const mgtk::ae::EffectSpec& spec) {
    A_u_long flags = mgtk::ae::kOutFlags;
    if (spec.varies_per_frame) flags |= 4u;  // PF_OutFlag_NON_PARAM_VARY
    return flags;
}

std::string file_slug(const std::string& match_name) {
    std::string out;
    for (char c : match_name) {
        if (c == '_') {
            out += '_';
        } else if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
        } else {
            out += c;
        }
    }
    return out;
}

bool write_pipl(const mgtk::ae::EffectSpec& spec, const std::string& directory, FILE* log) {
    const std::string path = directory + "/" + file_slug(spec.match_name) + ".r";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::fprintf(stderr, "gen_pipl: cannot write %s\n", path.c_str());
        return false;
    }

    const A_u_long version = pack_version(MGTK_VERSION_MAJOR, MGTK_VERSION_MINOR,
                                          MGTK_VERSION_PATCH, /*PF_Stage_RELEASE=*/3,
                                          MGTK_BUILD_NUMBER);

    std::fprintf(f,
        "// ===========================================================================\n"
        "//  %s.r\n"
        "//\n"
        "//  GENERATED FILE -- do not edit. Edit src/ae/registry.cpp and run\n"
        "//  `make pipl` (or ./build/gen_pipl resources/pipl) instead.\n"
        "//\n"
        "//  The out-flag values below are literals on purpose: the PF_OutFlag_*\n"
        "//  names are C enums, so writing them here would expand to 0 and AE would\n"
        "//  refuse to load the plug-in. They are printed from the same constants\n"
        "//  PF_Cmd_GLOBAL_SETUP assigns, which is what keeps the two in step.\n"
        "//\n"
        "//  Resource files are stored big-endian: one .r serves both platforms, and\n"
        "//  Windows converts it with pipltool.exe as part of the build.\n"
        "// ===========================================================================\n"
        "#include \"AEConfig.h\"\n"
        "#include \"AE_EffectVers.h\"\n"
        "\n"
        "resource 'PiPL' (16000) {\n"
        "    {\n"
        "        Kind { AEEffect },\n"
        "        Name { \"%s\" },\n"
        "        Category { \"%s\" },\n"
        // Rez understands #ifdef/#else; it does not understand defined().
        // AEConfig.h -- included just above -- is what defines AE_OS_WIN on
        // Windows and leaves it undefined everywhere else.
        "#ifdef AE_OS_WIN\n"
        "        AE_Effect_Windows_Entry_Point { \"EffectMain\" },\n"
        "#else\n"
        "        AE_Effect_Mac_Entry_Point { \"EffectMain\" },\n"
        "#endif\n"
        "        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },\n"
        "        AE_Effect_Version { %u },\n"
        "        AE_Effect_Info_Flags { 0 },\n"
        "        AE_Effect_Global_OutFlags { 0x%08X },\n"
        "        AE_Effect_Global_OutFlags_2 { 0x%08X },\n"
        "        AE_Effect_Match_Name { \"%s\" },\n"
        "        AE_Reserved_Info { 0 }\n"
        "    }\n"
        "};\n",
        file_slug(spec.match_name).c_str(), spec.display_name, spec.category,
        static_cast<unsigned>(version), static_cast<unsigned>(runtime_out_flags(spec)),
        static_cast<unsigned>(mgtk::ae::kOutFlags2), spec.match_name);

    std::fclose(f);
    if (log != nullptr) {
        std::fprintf(log, "  %-44s  out_flags 0x%08X  out_flags2 0x%08X\n", path.c_str(),
                     static_cast<unsigned>(runtime_out_flags(spec)),
                     static_cast<unsigned>(mgtk::ae::kOutFlags2));
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string directory = (argc > 1) ? argv[1] : "resources/pipl";

    int count = 0;
    const mgtk::ae::EffectSpec* const* effects = mgtk::ae::all_effects(&count);
    if (effects == nullptr || count == 0) {
        std::fprintf(stderr, "gen_pipl: the effect registry is empty\n");
        return 1;
    }

    std::fprintf(stdout, "gen_pipl: writing %d PiPL resources to %s\n", count, directory.c_str());
    for (int i = 0; i < count; ++i) {
        if (!write_pipl(*effects[i], directory, stdout)) return 1;
    }
    std::fprintf(stdout,
                 "gen_pipl: done. Every effect claims the same flags, so a mismatch between\n"
                 "          these files and PF_Cmd_GLOBAL_SETUP is impossible by construction.\n");
    return 0;
}
