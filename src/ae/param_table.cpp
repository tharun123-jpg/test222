// =============================================================================
//  src/ae/param_table.cpp -- turns a ParamSpec table into AE parameters.
//
//  This file is the only place in the project that touches PF_ParamDef's
//  union, so it is also the only place that has to be revisited if a future
//  SDK reshuffles a parameter structure. Registration is done by filling the
//  def directly and calling PF_ADD_PARAM rather than through the PF_ADD_*
//  convenience macros: the macros have changed arity between SDK versions
//  (PF_ADD_FLOAT_SLIDERX in particular), while the structures and PF_ADD_PARAM
//  have not.
// =============================================================================
#include "effect_spec.hpp"

#include "AE_Macros.h"

#include <cstring>

namespace mgtk {
namespace ae {
namespace {

// AE stores several values as 16.16 fixed point. FLOAT2FIX is the SDK's own
// conversion; spelling it out here would only invite a rounding difference.
inline PF_Fixed to_fixed(double v) { return FLOAT2FIX(v); }

PF_Precision to_precision(A_long p) {
    switch (p) {
        case 0: return PF_Precision_INTEGER;
        case 1: return PF_Precision_TENTHS;
        case 3: return PF_Precision_THOUSANDTHS;
        case 2:
        default: return PF_Precision_HUNDREDTHS;
    }
}

PF_Err add_one(PF_InData* in_data, const ParamSpec& spec) {
    PF_ParamDef def;
    // Clearing first is not optional: PF_ADD_PARAM reads fields the caller did
    // not set, and a stale stack value there produces the kind of bug that
    // only shows up on someone else's machine.
    AEFX_CLR_STRUCT(def);

    def.uu.id = spec.id;
    def.ui_flags = PF_PUI_NONE;
    def.flags = PF_ParamFlag_NONE;
    PF_STRCPY(def.name, spec.name);

    switch (spec.kind) {
        case ParamKind::FloatSlider:
            def.param_type = PF_Param_FLOAT_SLIDER;
            def.u.fs_d.valid_min = static_cast<PF_FpShort>(spec.f_valid_min);
            def.u.fs_d.valid_max = static_cast<PF_FpShort>(spec.f_valid_max);
            def.u.fs_d.slider_min = static_cast<PF_FpShort>(spec.f_slider_min);
            def.u.fs_d.slider_max = static_cast<PF_FpShort>(spec.f_slider_max);
            def.u.fs_d.dephault = static_cast<PF_FpShort>(spec.f_default);
            def.u.fs_d.precision = to_precision(spec.f_precision);
            def.u.fs_d.display_flags = static_cast<PF_ValueDisplayFlags>(spec.f_display_flags);
            def.u.fs_d.fs_flags = PF_FSliderFlag_NONE;
            def.u.fs_d.curve_tolerance = 0;
            def.u.fs_d.useExponent = FALSE;
            def.u.fs_d.exponent = 1.0f;
            if (spec.f_value_desc != nullptr) {
                PF_STRCPY(def.u.fs_d.value_desc, spec.f_value_desc);
            }
            break;

        case ParamKind::Popup:
            def.param_type = PF_Param_POPUP;
            def.u.pd.num_choices = static_cast<A_short>(spec.choice_count);
            def.u.pd.dephault = static_cast<A_short>(spec.popup_default);
            // AE copies the string during PF_ADD_PARAM, so a literal in the
            // registry is fine and outlives nothing in particular.
            def.u.pd.u.namesptr = spec.choices;
            break;

        case ParamKind::Checkbox:
            // Values in a checkbox cannot be interpolated -- AE forces
            // PF_ParamFlag_CANNOT_INTERP on -- so a checkbox is a hard switch
            // and never a ramp.
            def.param_type = PF_Param_CHECKBOX;
            def.u.bd.dephault = spec.bool_default ? TRUE : FALSE;
            break;

        case ParamKind::Color:
            // A colour *default* lives in PF_ColorDef's 8-bit member; the
            // colour the user actually picks is read back at float precision
            // through PF_ColorParamSuite1 in mgtk_ae.cpp.
            //
            // This is the one spot in the project that indexes PF_ColorDef
            // directly, and it is written the way the SDK's own PF_ADD_COLOR
            // macro writes it (deployt.red / .green / .blue / .alpha). If a
            // future SDK changes PF_UnionablePixel's member names, this is the
            // single line to fix.
            def.param_type = PF_Param_COLOR;
            def.u.cd.dephault.alpha = PF_MAX_CHAN8;
            def.u.cd.dephault.red =
                static_cast<A_u_char>(saturate(spec.color_default.r) * PF_MAX_CHAN8 + 0.5f);
            def.u.cd.dephault.green =
                static_cast<A_u_char>(saturate(spec.color_default.g) * PF_MAX_CHAN8 + 0.5f);
            def.u.cd.dephault.blue =
                static_cast<A_u_char>(saturate(spec.color_default.b) * PF_MAX_CHAN8 + 0.5f);
            break;

        case ParamKind::Point:
            def.param_type = PF_Param_POINT;
            def.u.td.restrict_bounds = spec.restrict_bounds ? TRUE : FALSE;
            // Point *defaults* are percentages of the layer, unlike the point
            // *values* handed back at render time, which are in layer pixels.
            def.u.td.x_dephault = to_fixed(spec.point_x);
            def.u.td.y_dephault = to_fixed(spec.point_y);
            break;

        case ParamKind::Angle:
            def.param_type = PF_Param_ANGLE;
            def.u.ad.dephault = to_fixed(spec.f_default);
            def.u.ad.valid_min = to_fixed(-3600.0);
            def.u.ad.valid_max = to_fixed(3600.0);
            break;

        case ParamKind::Button:
            def.param_type = PF_Param_BUTTON;
            // PF_ParamFlag_SUPERVISE is what makes AE send
            // PF_Cmd_USER_CHANGED_PARAM when the button is pressed; without it
            // the button is decorative.
            def.flags = PF_ParamFlag_SUPERVISE;
            break;
    }

    return PF_ADD_PARAM(in_data, -1, &def);
}

}  // namespace

// Registers an effect's whole parameter table. `params[0]` is AE's implicit
// input layer and is not registered here, so num_params is one more than the
// table length.
PF_Err setup_params(PF_InData* in_data, PF_OutData* out_data, const EffectSpec& spec) {
    PF_Err err = PF_Err_NONE;
    for (int i = 0; i < spec.param_count && err == PF_Err_NONE; ++i) {
        err = add_one(in_data, spec.params[i]);
    }
    if (err == PF_Err_NONE) {
        out_data->num_params = spec.param_count + 1;
    }
    return err;
}

}  // namespace ae
}  // namespace mgtk
