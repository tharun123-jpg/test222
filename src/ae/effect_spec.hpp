// =============================================================================
//  src/ae/effect_spec.hpp -- one description per effect, shared by the AE glue
//                            and the PiPL generator.
//
//  There is exactly one place in the project that knows the name of an effect,
//  the match name AE stores in saved projects, the parameters it exposes and
//  the numeric out-flags it claims. The entry point reads that table, and
//  tools/gen_pipl.cpp walks the same table to emit resources/pipl/*.r, so the
//  "out flags in the PiPL must byte-match what PF_Cmd_GLOBAL_SETUP writes"
//  rule is enforced by construction rather than by discipline.
// =============================================================================
#pragma once

#include "AE_Effect.h"
#include "AE_EffectCB.h"

#include <cstddef>
#include <vector>

#include "mgtk/effects.hpp"
#include "mgtk/version.hpp"

namespace mgtk {
namespace ae {

// ---------------------------------------------------------------------------
//  Out flags
// ---------------------------------------------------------------------------
// The SDK declares these as enums, not macros, which matters a great deal:
// a .r resource file cannot contain `PF_OutFlag_A | PF_OutFlag_B` because the
// resource compiler's C preprocessor expands both names to 0 (and some
// versions then fail with "CloseBrace Expected"). Only the literal numbers
// below may appear in a PiPL. gen_pipl.cpp prints these constants, so the two
// places can never disagree.
//
// What is claimed, and why:
//
//   PF_OutFlag_USE_OUTPUT_EXTENT   the effect only fills the region it
//                                  declares, so AE is free to hand over a
//                                  sub-rectangle of the layer.
//   PF_OutFlag_DEEP_COLOR_AWARE    the effect understands 32-bit float.
//   PF_OutFlag_NON_PARAM_VARY      only for the two effects that read frames
//                                  other than the current one. It tells AE
//                                  that an unchanged still can still produce a
//                                  different frame, so the result must not be
//                                  cached as if it were static.
//
// Deliberately NOT claimed:
//
//   PF_OutFlag_PIX_INDEPENDENT  every effect here samples neighbouring pixels
//                               (blur, streak, warp, sort), so this would be a
//                               lie and would let AE skip input pixels the
//                               effect actually needs.
//   PF_OutFlag2_DOESNT_NEED_EMPTY_PIXELS
//                               it lets AE trim the input to its non-zero
//                               alpha, which would cut the very transparent
//                               pixels a bloom needs to spill into.
constexpr A_u_long kOutFlags =
    static_cast<A_u_long>(PF_OutFlag_USE_OUTPUT_EXTENT) |
    static_cast<A_u_long>(PF_OutFlag_DEEP_COLOR_AWARE);

constexpr A_u_long kOutFlags2 =
    static_cast<A_u_long>(PF_OutFlag2_FLOAT_COLOR_AWARE) |
    static_cast<A_u_long>(PF_OutFlag2_SUPPORTS_SMART_RENDER);

// ---------------------------------------------------------------------------
//  Parameter description
// ---------------------------------------------------------------------------
enum class ParamKind : int {
    FloatSlider,
    Popup,
    Checkbox,
    Color,
    Point,
    Angle,
    // A parameter with no persistent value that fires PF_Cmd_USER_CHANGED_PARAM
    // once when pressed. Used for "Reset" style actions.
    Button
};

struct ParamSpec {
    ParamKind kind = ParamKind::Button;
    A_long id = 0;
    const char* name = "";

    // FloatSlider
    double f_valid_min = 0.0;
    double f_valid_max = 1.0;
    double f_slider_min = 0.0;
    double f_slider_max = 1.0;
    double f_default = 0.0;
    A_long f_precision = 2;         // PF_Precision_TENTHS / HUNDREDTHS / THOUSANDTHS
    A_long f_display_flags = 0;     // PF_ValueDisplayFlags
    const char* f_value_desc = "";

    // Popup
    const char* choices = nullptr;  // "First / Second / (-  / Third"
    int choice_count = 0;
    int popup_default = 0;

    // Checkbox / Button
    bool bool_default = false;

    // Color
    Float4 color_default = Float4{1.0f, 1.0f, 1.0f, 1.0f};

    // Point (defaults are in percent of the layer, as AE requires)
    double point_x = 50.0;
    double point_y = 50.0;
    bool restrict_bounds = true;
};

// Small builders, so a parameter table reads like a declaration rather than a
// wall of field assignments.
inline ParamSpec float_slider(A_long id, const char* name, double valid_min,
                              double valid_max, double dflt, const char* unit = "",
                              int precision = 2) {
    ParamSpec p;
    p.kind = ParamKind::FloatSlider;
    p.id = id;
    p.name = name;
    p.f_valid_min = valid_min;
    p.f_valid_max = valid_max;
    p.f_slider_min = valid_min;
    p.f_slider_max = valid_max;
    p.f_default = dflt;
    p.f_precision = precision;
    p.f_value_desc = unit;
    return p;
}

inline ParamSpec popup(A_long id, const char* name, const char* choices, int count, int dflt) {
    ParamSpec p;
    p.kind = ParamKind::Popup;
    p.id = id;
    p.name = name;
    p.choices = choices;
    p.choice_count = count;
    p.popup_default = dflt;
    return p;
}

inline ParamSpec checkbox(A_long id, const char* name, bool dflt) {
    ParamSpec p;
    p.kind = ParamKind::Checkbox;
    p.id = id;
    p.name = name;
    p.bool_default = dflt;
    return p;
}

inline ParamSpec color_param(A_long id, const char* name, float r, float g, float b) {
    ParamSpec p;
    p.kind = ParamKind::Color;
    p.id = id;
    p.name = name;
    p.color_default = Float4{r, g, b, 1.0f};
    return p;
}

inline ParamSpec point_param(A_long id, const char* name, double x_percent, double y_percent) {
    ParamSpec p;
    p.kind = ParamKind::Point;
    p.id = id;
    p.name = name;
    p.point_x = x_percent;
    p.point_y = y_percent;
    p.restrict_bounds = true;
    return p;
}

inline ParamSpec angle_param(A_long id, const char* name, double dflt_degrees) {
    ParamSpec p;
    p.kind = ParamKind::Angle;
    p.id = id;
    p.name = name;
    p.f_default = dflt_degrees;
    return p;
}

inline ParamSpec button(A_long id, const char* name) {
    ParamSpec p;
    p.kind = ParamKind::Button;
    p.id = id;
    p.name = name;
    return p;
}

// ---------------------------------------------------------------------------
//  Effect description
// ---------------------------------------------------------------------------
// What kind of time-varying input an effect needs beyond the current frame.
enum class InputKind : int {
    // Just the current frame.
    Single,
    // A persistent accumulator that the effect reads and rewrites. Lives in
    // the effect's sequence data, so it survives between frames and is saved
    // with the project.
    Feedback,
    // A stack of past frames, newest first. The AE glue checks them out.
    History
};

// The bundle handed to an effect's render function. Anything the effect does
// not use is null.
struct EffectInputs {
    const Image* src = nullptr;
    Image* feedback = nullptr;
    const std::vector<Image>* history = nullptr;
};

struct EffectSpec {
    // Identity. The match name is the contract with saved projects: AE looks
    // effects up by it, and it must never change after release.
    const char* match_name = "";
    const char* display_name = "";
    const char* category = MGTK_CATEGORY;
    const char* description = "";

    AlphaPolicy alpha_policy = AlphaPolicy::Straight;
    InputKind input = InputKind::Single;

    // Set for the two effects whose output changes with time even when no
    // parameter does. Maps to PF_OutFlag_NON_PARAM_VARY.
    bool varies_per_frame = false;

    // Parameter id holding the number of past frames a History effect wants,
    // or -1. Read during both pre-render and render so the two passes agree.
    A_long history_frames_param = -1;

    // Parameter id of the button that clears a Feedback effect's accumulator,
    // or -1.
    A_long reset_param = -1;

    // Parameter table. params[0] is always AE's implicit input layer, so the
    // ids in here start at 1.
    const ParamSpec* params = nullptr;
    int param_count = 0;

    // Marshalling. `state_size` is sizeof() of the effect's plain parameter
    // struct from mgtk/effects.hpp; the dispatcher allocates one per call.
    std::size_t state_size = 0;
    // Fills `state` with the effect's default parameter values. Called before
    // `read` so a parameter that fails to check out falls back to something
    // sensible instead of to zeroed memory.
    void (*defaults)(void* state) = nullptr;
    // Overwrites `state` from the checked-out AE parameters. May assume the
    // state already holds the defaults. `out_data` is needed because reading a
    // colour requires acquiring a suite, and suites are acquired against both
    // the in and out blocks.
    void (*read)(PF_InData* in_data, PF_OutData* out_data, void* state) = nullptr;
    // The render step itself.
    void (*apply)(const EffectInputs& in, Image& dst, const void* state,
                  const RenderContext& ctx) = nullptr;
};

// ---------------------------------------------------------------------------
//  The registry
// ---------------------------------------------------------------------------
const EffectSpec* const* all_effects(int* count);
const EffectSpec* find_effect(const char* match_name);

}  // namespace ae
}  // namespace mgtk
