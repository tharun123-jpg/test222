// =============================================================================
//  src/ae/registry.cpp -- the table of every MGTK effect.
//
//  This is the single source of truth for:
//
//    * the match names AE stores in saved projects,
//    * the name and category shown in the Effects & Presets panel,
//    * the parameters each effect exposes and their defaults,
//    * how AE's parameter values become an mgtk parameter struct,
//    * which core apply_* function does the work.
//
//  tools/gen_pipl.cpp walks the same table to write resources/pipl/*.r, which
//  is how the out-flags in the resource file are guaranteed to match what
//  PF_Cmd_GLOBAL_SETUP reports. Editing an effect's parameters means editing
//  this file and re-running the generator; nothing else needs to know.
// =============================================================================
#include "effect_registry.hpp"

#include "mgtk_ae.hpp"
#include "mgtk/version.hpp"

#include <cstring>
#include <string>

namespace mgtk {
namespace ae {
namespace {

// ---------------------------------------------------------------------------
//  Small helpers for reading parameters
// ---------------------------------------------------------------------------
// Each helper takes the current value as a fallback, so a parameter that fails
// to check out -- which happens when a project saved by an older build of the
// plug-in is opened by a newer one -- degrades to the default instead of to
// zero. A zero radius or a zero mix is a visible bug; the default is not.

float read_f(PF_InData* in_data, PF_OutData* out_data, A_long id, float fallback) {
    Param p(in_data, out_data, id);
    return p.ok() ? p.slider() : fallback;
}

int read_popup(PF_InData* in_data, PF_OutData* out_data, A_long id, int fallback) {
    Param p(in_data, out_data, id);
    return p.ok() ? static_cast<int>(p.popup()) : fallback;
}

bool read_bool(PF_InData* in_data, PF_OutData* out_data, A_long id, bool fallback) {
    Param p(in_data, out_data, id);
    return p.ok() ? p.checkbox() : fallback;
}

float read_angle(PF_InData* in_data, PF_OutData* out_data, A_long id, float fallback) {
    Param p(in_data, out_data, id);
    return p.ok() ? p.angle_degrees() : fallback;
}

Float4 read_color(PF_InData* in_data, PF_OutData* out_data, A_long id, Float4 fallback) {
    Param p(in_data, out_data, id);
    return p.color(fallback);
}

// Point parameters arrive in layer pixels; the core wants a normalised centre.
// The two differ by exactly the layer dimensions, and normalising here keeps
// the effects resolution-independent, which matters because a motion graphics
// toolkit gets used on 1920x1080 comps and 400x400 precomps alike.
Vec2 read_point_norm(PF_InData* in_data, PF_OutData* out_data, A_long id, Vec2 fallback) {
    Param p(in_data, out_data, id);
    if (!p.ok()) return fallback;
    const Vec2 pixels = p.point();
    const float w = static_cast<float>(in_data->width > 0 ? in_data->width : 1);
    const float h = static_cast<float>(in_data->height > 0 ? in_data->height : 1);
    return Vec2{clamp(pixels.x / w, -2.0f, 3.0f), clamp(pixels.y / h, -2.0f, 3.0f)};
}

// ---------------------------------------------------------------------------
//  Popup choice strings
// ---------------------------------------------------------------------------
// Built once per effect from the core's own name tables, so a popup can never
// disagree with the enum it indexes.
std::string join_names(int count, const char* (*name_at)(int)) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (i > 0) out += " / ";
        out += name_at(i);
    }
    return out;
}

const char* blend_name_at(int i) { return blend_mode_name(blend_mode_from_index(i)); }
const char* wrap_name_at(int i) { return wrap_mode_name(wrap_mode_from_index(i)); }
const char* quality_name_at(int i) { return quality_name(quality_from_index(i)); }

// Every parameter struct in mgtk/effects.hpp has default member initialisers,
// so value-initialising is the same thing as filling in the defaults.
template <typename ParamsT>
void fill_defaults(void* state) {
    *static_cast<ParamsT*>(state) = ParamsT{};
}

// ---------------------------------------------------------------------------
//  1. Feedback Echo
// ---------------------------------------------------------------------------
enum EchoParam : A_long {
    kEchoAmount = 1,
    kEchoDecay,
    kEchoBlend,
    kEchoScale,
    kEchoRotation,
    kEchoOffsetX,
    kEchoOffsetY,
    kEchoCenter,
    kEchoHue,
    kEchoBrightness,
    kEchoSaturation,
    kEchoMix,
    kEchoWrap,
    kEchoReset
};
constexpr int kEchoParamCount = kEchoReset;

const char* const kEchoBlendNames[] = {"Normal", "Add", "Screen", "Lighten",
                                       "Darken", "Difference", "Overlay"};
const char* echo_blend_name_at(int i) { return kEchoBlendNames[i]; }
static_assert(sizeof(kEchoBlendNames) / sizeof(kEchoBlendNames[0]) ==
                  static_cast<size_t>(EchoBlendMode::Count),
              "echo blend popup has drifted from EchoBlendMode");

void echo_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<FeedbackEchoParams*>(state);
    s->echo_amount = read_f(in_data, out_data, kEchoAmount, s->echo_amount);
    s->decay_curve = read_f(in_data, out_data, kEchoDecay, s->decay_curve);
    s->blend = static_cast<EchoBlendMode>(read_popup(in_data, out_data, kEchoBlend,
                                                     static_cast<int>(s->blend)));
    s->scale = read_f(in_data, out_data, kEchoScale, s->scale);
    s->rotation_deg = read_angle(in_data, out_data, kEchoRotation, s->rotation_deg);
    s->offset_x = read_f(in_data, out_data, kEchoOffsetX, s->offset_x);
    s->offset_y = read_f(in_data, out_data, kEchoOffsetY, s->offset_y);
    s->center = read_point_norm(in_data, out_data, kEchoCenter, s->center);
    s->hue_shift_deg = read_f(in_data, out_data, kEchoHue, s->hue_shift_deg);
    s->brightness = read_f(in_data, out_data, kEchoBrightness, s->brightness);
    s->saturation = read_f(in_data, out_data, kEchoSaturation, s->saturation);
    s->mix = read_f(in_data, out_data, kEchoMix, s->mix);
    s->wrap = wrap_mode_from_index(read_popup(in_data, out_data, kEchoWrap,
                                              static_cast<int>(s->wrap)));
    // The reset button is handled by PF_Cmd_USER_CHANGED_PARAM, which clears
    // the sequence data. Nothing is read from it here.
}

void echo_apply(const EffectInputs& in, Image& dst, const void* state, const RenderContext& ctx) {
    apply_feedback_echo(*in.src, *in.feedback, dst, *static_cast<const FeedbackEchoParams*>(state),
                        ctx);
}

const EffectSpec& echo_spec() {
    static const std::string kEchoBlendChoices =
        join_names(static_cast<int>(EchoBlendMode::Count), echo_blend_name_at);
    static const std::string kWrapChoices = join_names(wrap_mode_count(), wrap_name_at);
    static const ParamSpec kParams[] = {
        // The default echo blend is Add rather than Normal, and that is a
        // deliberate choice about the common case rather than an arbitrary one.
        //
        // "Normal" composites the current frame over the echo, so on an opaque
        // layer -- which is what most motion graphics layers are -- the echo is
        // completely hidden and the effect looks broken on insert. Add lets the
        // trail through an opaque layer, which is the look people reach for
        // this effect to get. Anyone who wants the hidden-echo behaviour can
        // still pick Normal.
        float_slider(kEchoAmount, "Echo Amount", 0.0, 1.0, 0.80, "", 2),
        float_slider(kEchoDecay, "Decay Curve", 0.1, 4.0, 1.0, "", 2),
        popup(kEchoBlend, "Blend Mode", kEchoBlendChoices.c_str(),
              static_cast<int>(EchoBlendMode::Count), static_cast<int>(EchoBlendMode::Add)),
        float_slider(kEchoScale, "Scale", 0.05, 4.0, 1.0, "x", 3),
        angle_param(kEchoRotation, "Rotation", 0.0),
        float_slider(kEchoOffsetX, "Offset X", -1000.0, 1000.0, 0.0, "px", 1),
        float_slider(kEchoOffsetY, "Offset Y", -1000.0, 1000.0, 0.0, "px", 1),
        point_param(kEchoCenter, "Center", 50.0, 50.0),
        float_slider(kEchoHue, "Hue Shift", -360.0, 360.0, 0.0, "deg", 1),
        float_slider(kEchoBrightness, "Brightness", 0.0, 4.0, 1.0, "", 3),
        float_slider(kEchoSaturation, "Saturation", 0.0, 4.0, 1.0, "", 3),
        float_slider(kEchoMix, "Mix", 0.0, 1.0, 1.0, "", 2),
        popup(kEchoWrap, "Edge Mode", kWrapChoices.c_str(), wrap_mode_count(), 0),
        button(kEchoReset, "Reset Accumulator"),
    };
    static_assert(sizeof(kParams) / sizeof(kParams[0]) ==
                      static_cast<size_t>(kEchoParamCount),
                  "the parameter table and kEchoParamCount have drifted apart");
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_FeedbackEcho";
        s.display_name = "MGTK Feedback Echo";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Feedback Echo " MGTK_VERSION_STRING
            " - accumulates the layer across frames with a transform, colour "
            "shift and blend, for infinite echoes, zoom tunnels and trails.";
        s.alpha_policy = AlphaPolicy::Straight;
        s.input = InputKind::Feedback;
        s.varies_per_frame = true;
        s.params = kParams;
        s.param_count = kEchoParamCount;
        s.state_size = sizeof(FeedbackEchoParams);
        s.defaults = fill_defaults<FeedbackEchoParams>;
        s.read = echo_read;
        s.apply = echo_apply;
        s.reset_param = kEchoReset;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  2. Chromatic Split
// ---------------------------------------------------------------------------
enum ChromaParam : A_long {
    kChromaMode = 1,
    kChromaAmount,
    kChromaR,
    kChromaG,
    kChromaB,
    kChromaCenter,
    kChromaAngle,
    kChromaRadialBias,
    kChromaFalloff,
    kChromaBarrel,
    kChromaQuality,
    kChromaWrap,
    kChromaMix
};
constexpr int kChromaParamCount = kChromaMix;

const char* const kChromaModeNames[] = {"Radial", "Linear", "Zoom", "Spin", "Barrel"};
static_assert(sizeof(kChromaModeNames) / sizeof(kChromaModeNames[0]) ==
                  static_cast<size_t>(ChromaMode::Count),
              "chromatic split mode popup has drifted from ChromaMode");

const char* const kChromaQualityNames[] = {"Bilinear", "Bicubic"};
static_assert(sizeof(kChromaQualityNames) / sizeof(kChromaQualityNames[0]) ==
                  static_cast<size_t>(ChromaQuality::Count),
              "chromatic split quality popup has drifted from ChromaQuality");

const char* chroma_mode_name_at(int i) { return kChromaModeNames[i]; }
const char* chroma_quality_name_at(int i) { return kChromaQualityNames[i]; }

void chroma_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<ChromaticSplitParams*>(state);
    s->mode = static_cast<ChromaMode>(read_popup(in_data, out_data, kChromaMode,
                                                 static_cast<int>(s->mode)));
    s->amount = read_f(in_data, out_data, kChromaAmount, s->amount);
    s->amount_r = read_f(in_data, out_data, kChromaR, s->amount_r);
    s->amount_g = read_f(in_data, out_data, kChromaG, s->amount_g);
    s->amount_b = read_f(in_data, out_data, kChromaB, s->amount_b);
    s->center = read_point_norm(in_data, out_data, kChromaCenter, s->center);
    s->angle_deg = read_angle(in_data, out_data, kChromaAngle, s->angle_deg);
    s->radial_bias = read_f(in_data, out_data, kChromaRadialBias, s->radial_bias);
    s->falloff = read_f(in_data, out_data, kChromaFalloff, s->falloff);
    s->barrel = read_f(in_data, out_data, kChromaBarrel, s->barrel);
    s->quality = static_cast<ChromaQuality>(read_popup(in_data, out_data, kChromaQuality,
                                                       static_cast<int>(s->quality)));
    s->wrap = wrap_mode_from_index(read_popup(in_data, out_data, kChromaWrap,
                                              static_cast<int>(s->wrap)));
    s->mix = read_f(in_data, out_data, kChromaMix, s->mix);
}

void chroma_apply(const EffectInputs& in, Image& dst, const void* state, const RenderContext& ctx) {
    apply_chromatic_split(*in.src, dst, *static_cast<const ChromaticSplitParams*>(state), ctx);
}

const EffectSpec& chroma_spec() {
    static const std::string kModeChoices = join_names(static_cast<int>(ChromaMode::Count),
                                                       chroma_mode_name_at);
    static const std::string kQualityChoices = join_names(static_cast<int>(ChromaQuality::Count),
                                                          chroma_quality_name_at);
    static const std::string kWrapChoices = join_names(wrap_mode_count(), wrap_name_at);
    static const ParamSpec kParams[] = {
        popup(kChromaMode, "Mode", kModeChoices.c_str(), static_cast<int>(ChromaMode::Count), 0),
        float_slider(kChromaAmount, "Amount", -200.0, 200.0, 12.0, "px", 1),
        float_slider(kChromaR, "Red Multiplier", -4.0, 4.0, 1.0, "", 2),
        float_slider(kChromaG, "Green Multiplier", -4.0, 4.0, 0.0, "", 2),
        float_slider(kChromaB, "Blue Multiplier", -4.0, 4.0, -1.0, "", 2),
        point_param(kChromaCenter, "Center", 50.0, 50.0),
        angle_param(kChromaAngle, "Angle", 0.0),
        float_slider(kChromaRadialBias, "Radial Bias", 0.0, 4.0, 1.0, "", 2),
        float_slider(kChromaFalloff, "Falloff", 0.1, 4.0, 1.0, "", 2),
        float_slider(kChromaBarrel, "Barrel Warp", -200.0, 200.0, 0.0, "px", 1),
        popup(kChromaQuality, "Sampling", kQualityChoices.c_str(),
              static_cast<int>(ChromaQuality::Count), 0),
        popup(kChromaWrap, "Edge Mode", kWrapChoices.c_str(), wrap_mode_count(), 0),
        float_slider(kChromaMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_ChromaticSplit";
        s.display_name = "MGTK Chromatic Split";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Chromatic Split " MGTK_VERSION_STRING
            " - separates colour channels radially, linearly, by zoom, by spin "
            "or with a barrel warp, with independent per-channel amounts.";
        s.alpha_policy = AlphaPolicy::Straight;
        s.input = InputKind::Single;
        s.params = kParams;
        s.param_count = kChromaParamCount;
        s.state_size = sizeof(ChromaticSplitParams);
        s.defaults = fill_defaults<ChromaticSplitParams>;
        s.read = chroma_read;
        s.apply = chroma_apply;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  3. Anamorphic Glow
// ---------------------------------------------------------------------------
enum GlowParam : A_long {
    kGlowThreshold = 1,
    kGlowKnee,
    kGlowIntensity,
    kGlowRadius,
    kGlowStreakLength,
    kGlowStreakAngle,
    kGlowStreakCount,
    kGlowStreakSamples,
    kGlowStreakIntensity,
    kGlowStreakTint,
    kGlowStreakTintAmount,
    kGlowTint,
    kGlowTintAmount,
    kGlowRgbSeparation,
    kGlowBlend,
    kGlowPreserveAlpha,
    kGlowQuality,
    kGlowMix
};
constexpr int kGlowParamCount = kGlowMix;

void glow_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<AnamorphicGlowParams*>(state);
    s->threshold = read_f(in_data, out_data, kGlowThreshold, s->threshold);
    s->knee = read_f(in_data, out_data, kGlowKnee, s->knee);
    s->intensity = read_f(in_data, out_data, kGlowIntensity, s->intensity);
    s->radius = read_f(in_data, out_data, kGlowRadius, s->radius);
    s->streak_length = read_f(in_data, out_data, kGlowStreakLength, s->streak_length);
    s->streak_angle_deg = read_angle(in_data, out_data, kGlowStreakAngle, s->streak_angle_deg);
    s->streak_count = clamp(static_cast<int>(read_f(in_data, out_data, kGlowStreakCount,
                                                    static_cast<float>(s->streak_count))), 1, 8);
    s->streak_samples = clamp(static_cast<int>(read_f(in_data, out_data, kGlowStreakSamples,
                                                      static_cast<float>(s->streak_samples))),
                              4, 128);
    s->streak_intensity = read_f(in_data, out_data, kGlowStreakIntensity, s->streak_intensity);
    s->streak_tint = read_color(in_data, out_data, kGlowStreakTint, s->streak_tint);
    s->streak_tint_amount = read_f(in_data, out_data, kGlowStreakTintAmount,
                                   s->streak_tint_amount);
    s->glow_tint = read_color(in_data, out_data, kGlowTint, s->glow_tint);
    s->glow_tint_amount = read_f(in_data, out_data, kGlowTintAmount, s->glow_tint_amount);
    s->rgb_separation = read_f(in_data, out_data, kGlowRgbSeparation, s->rgb_separation);
    s->blend = blend_mode_from_index(read_popup(in_data, out_data, kGlowBlend,
                                                static_cast<int>(s->blend)));
    s->preserve_alpha = read_bool(in_data, out_data, kGlowPreserveAlpha, s->preserve_alpha);
    s->quality = quality_from_index(read_popup(in_data, out_data, kGlowQuality,
                                               static_cast<int>(s->quality)));
    s->mix = read_f(in_data, out_data, kGlowMix, s->mix);
}

void glow_apply(const EffectInputs& in, Image& dst, const void* state, const RenderContext& ctx) {
    apply_anamorphic_glow(*in.src, dst, *static_cast<const AnamorphicGlowParams*>(state), ctx);
}

const EffectSpec& glow_spec() {
    static const std::string kBlendChoices = join_names(blend_mode_count(), blend_name_at);
    static const std::string kQualityChoices = join_names(quality_count(), quality_name_at);
    static const ParamSpec kParams[] = {
        float_slider(kGlowThreshold, "Threshold", -1.0, 4.0, 1.0, "", 3),
        float_slider(kGlowKnee, "Knee", 0.0, 1.0, 0.25, "", 3),
        float_slider(kGlowIntensity, "Glow Intensity", 0.0, 8.0, 1.0, "", 2),
        float_slider(kGlowRadius, "Bloom Radius", 0.0, 400.0, 60.0, "px", 1),
        float_slider(kGlowStreakLength, "Streak Length", 0.0, 2000.0, 300.0, "px", 1),
        angle_param(kGlowStreakAngle, "Streak Angle", 0.0),
        float_slider(kGlowStreakCount, "Streak Count", 1.0, 8.0, 1.0, "", 0),
        float_slider(kGlowStreakSamples, "Streak Samples", 4.0, 128.0, 16.0, "", 0),
        float_slider(kGlowStreakIntensity, "Streak Intensity", 0.0, 4.0, 0.6, "", 2),
        color_param(kGlowStreakTint, "Streak Tint", 0.35f, 0.6f, 1.0f),
        float_slider(kGlowStreakTintAmount, "Streak Tint Amount", 0.0, 1.0, 0.0, "", 2),
        color_param(kGlowTint, "Glow Tint", 1.0f, 1.0f, 1.0f),
        float_slider(kGlowTintAmount, "Glow Tint Amount", 0.0, 1.0, 0.0, "", 2),
        float_slider(kGlowRgbSeparation, "RGB Separation", -100.0, 100.0, 0.0, "px", 1),
        popup(kGlowBlend, "Blend Mode", kBlendChoices.c_str(), blend_mode_count(),
              static_cast<int>(BlendMode::Add)),
        checkbox(kGlowPreserveAlpha, "Preserve Alpha", true),
        popup(kGlowQuality, "Quality", kQualityChoices.c_str(), quality_count(),
              static_cast<int>(Quality::Good)),
        float_slider(kGlowMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_AnamorphicGlow";
        s.display_name = "MGTK Anamorphic Glow";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Anamorphic Glow " MGTK_VERSION_STRING
            " - threshold-driven bloom with a sampled anamorphic streak pass, "
            "per-component tints and an optional channel split.";
        s.alpha_policy = AlphaPolicy::Straight;
        s.input = InputKind::Single;
        s.params = kParams;
        s.param_count = kGlowParamCount;
        s.state_size = sizeof(AnamorphicGlowParams);
        s.defaults = fill_defaults<AnamorphicGlowParams>;
        s.read = glow_read;
        s.apply = glow_apply;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  4. Fractal Warp
// ---------------------------------------------------------------------------
enum WarpParam : A_long {
    kWarpMode = 1,
    kWarpAmount,
    kWarpNoiseScale,
    kWarpOctaves,
    kWarpLacunarity,
    kWarpGain,
    kWarpEvolution,
    kWarpSeed,
    kWarpSwirlAmount,
    kWarpSwirlRadius,
    kWarpPinch,
    kWarpCenter,
    kWarpWrap,
    kWarpQuality,
    kWarpMix
};
constexpr int kWarpParamCount = kWarpMix;

const char* const kWarpModeNames[] = {"Displace", "Swirl", "Pinch", "Displace + Swirl",
                                      "Domain Warp"};
static_assert(sizeof(kWarpModeNames) / sizeof(kWarpModeNames[0]) ==
                  static_cast<size_t>(WarpMode::Count),
              "fractal warp mode popup has drifted from WarpMode");

const char* warp_mode_name_at(int i) { return kWarpModeNames[i]; }
const char* warp_quality_name_at(int i) { return kChromaQualityNames[i]; }

void warp_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<FractalWarpParams*>(state);
    s->mode = static_cast<WarpMode>(read_popup(in_data, out_data, kWarpMode,
                                               static_cast<int>(s->mode)));
    s->amount = read_f(in_data, out_data, kWarpAmount, s->amount);
    s->noise_scale = read_f(in_data, out_data, kWarpNoiseScale, s->noise_scale);
    s->octaves = clamp(static_cast<int>(read_f(in_data, out_data, kWarpOctaves,
                                               static_cast<float>(s->octaves))), 1, 10);
    s->lacunarity = read_f(in_data, out_data, kWarpLacunarity, s->lacunarity);
    s->gain = read_f(in_data, out_data, kWarpGain, s->gain);
    s->evolution = read_f(in_data, out_data, kWarpEvolution, s->evolution);
    s->seed = static_cast<uint32_t>(read_f(in_data, out_data, kWarpSeed,
                                           static_cast<float>(s->seed)));
    s->swirl_amount = read_f(in_data, out_data, kWarpSwirlAmount, s->swirl_amount);
    s->swirl_radius = read_f(in_data, out_data, kWarpSwirlRadius, s->swirl_radius);
    s->pinch = read_f(in_data, out_data, kWarpPinch, s->pinch);
    const Vec2 center = read_point_norm(in_data, out_data, kWarpCenter,
                                        Vec2{s->center_x, s->center_y});
    s->center_x = center.x;
    s->center_y = center.y;
    s->wrap = wrap_mode_from_index(read_popup(in_data, out_data, kWarpWrap,
                                              static_cast<int>(s->wrap)));
    s->quality = static_cast<ChromaQuality>(read_popup(in_data, out_data, kWarpQuality,
                                                       static_cast<int>(s->quality)));
    s->mix = read_f(in_data, out_data, kWarpMix, s->mix);
}

void warp_apply(const EffectInputs& in, Image& dst, const void* state, const RenderContext& ctx) {
    apply_fractal_warp(*in.src, dst, *static_cast<const FractalWarpParams*>(state), ctx);
}

const EffectSpec& warp_spec() {
    static const std::string kModeChoices = join_names(static_cast<int>(WarpMode::Count),
                                                       warp_mode_name_at);
    static const std::string kQualityChoices = join_names(static_cast<int>(ChromaQuality::Count),
                                                          warp_quality_name_at);
    static const std::string kWrapChoices = join_names(wrap_mode_count(), wrap_name_at);
    static const ParamSpec kParams[] = {
        popup(kWarpMode, "Mode", kModeChoices.c_str(), static_cast<int>(WarpMode::Count),
              static_cast<int>(WarpMode::DisplaceSwirl)),
        float_slider(kWarpAmount, "Amount", -400.0, 400.0, 40.0, "px", 1),
        float_slider(kWarpNoiseScale, "Noise Scale", 0.0001, 0.1, 0.005, "cyc/px", 4),
        float_slider(kWarpOctaves, "Octaves", 1.0, 10.0, 4.0, "", 0),
        float_slider(kWarpLacunarity, "Lacunarity", 1.01, 8.0, 2.0, "", 2),
        float_slider(kWarpGain, "Gain", 0.05, 0.95, 0.5, "", 2),
        float_slider(kWarpEvolution, "Evolution", -600.0, 600.0, 0.0, "s", 2),
        float_slider(kWarpSeed, "Seed", 0.0, 9999.0, 0.0, "", 0),
        float_slider(kWarpSwirlAmount, "Swirl Amount", -8.0, 8.0, 0.5, "turns", 2),
        float_slider(kWarpSwirlRadius, "Swirl Radius", 0.01, 2.0, 0.5, "", 2),
        float_slider(kWarpPinch, "Pinch", -1.0, 1.0, 0.0, "", 2),
        point_param(kWarpCenter, "Center", 50.0, 50.0),
        popup(kWarpWrap, "Edge Mode", kWrapChoices.c_str(), wrap_mode_count(), 0),
        popup(kWarpQuality, "Sampling", kQualityChoices.c_str(),
              static_cast<int>(ChromaQuality::Count), 0),
        float_slider(kWarpMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_FractalWarp";
        s.display_name = "MGTK Fractal Warp";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Fractal Warp " MGTK_VERSION_STRING
            " - fractal-noise displacement, swirl, pinch and domain warping "
            "with animatable evolution and a reproducible seed.";
        s.alpha_policy = AlphaPolicy::Straight;
        s.input = InputKind::Single;
        s.params = kParams;
        s.param_count = kWarpParamCount;
        s.state_size = sizeof(FractalWarpParams);
        s.defaults = fill_defaults<FractalWarpParams>;
        s.read = warp_read;
        s.apply = warp_apply;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  5. Kaleidoscope
// ---------------------------------------------------------------------------
enum KaleidoParam : A_long {
    kKaleidoMode = 1,
    kKaleidoSegments,
    kKaleidoRotation,
    kKaleidoCenter,
    kKaleidoScale,
    kKaleidoOffsetX,
    kKaleidoOffsetY,
    kKaleidoRadialFade,
    kKaleidoQuality,
    kKaleidoMix
};
constexpr int kKaleidoParamCount = kKaleidoMix;

const char* const kKaleidoModeNames[] = {"Mirror", "Rotate", "Mirror + Rotate", "Quilt"};
static_assert(sizeof(kKaleidoModeNames) / sizeof(kKaleidoModeNames[0]) ==
                  static_cast<size_t>(KaleidoMode::Count),
              "kaleidoscope mode popup has drifted from KaleidoMode");

const char* kaleido_mode_name_at(int i) { return kKaleidoModeNames[i]; }
const char* kaleido_quality_name_at(int i) { return kChromaQualityNames[i]; }

void kaleido_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<KaleidoscopeParams*>(state);
    s->mode = static_cast<KaleidoMode>(read_popup(in_data, out_data, kKaleidoMode,
                                                  static_cast<int>(s->mode)));
    s->segments = clamp(static_cast<int>(read_f(in_data, out_data, kKaleidoSegments,
                                                static_cast<float>(s->segments))), 2, 64);
    s->rotation_deg = read_angle(in_data, out_data, kKaleidoRotation, s->rotation_deg);
    s->center = read_point_norm(in_data, out_data, kKaleidoCenter, s->center);
    s->scale = read_f(in_data, out_data, kKaleidoScale, s->scale);
    s->offset_x = read_f(in_data, out_data, kKaleidoOffsetX, s->offset_x);
    s->offset_y = read_f(in_data, out_data, kKaleidoOffsetY, s->offset_y);
    s->radial_fade = read_f(in_data, out_data, kKaleidoRadialFade, s->radial_fade);
    s->quality = static_cast<ChromaQuality>(read_popup(in_data, out_data, kKaleidoQuality,
                                                       static_cast<int>(s->quality)));
    s->mix = read_f(in_data, out_data, kKaleidoMix, s->mix);
}

void kaleido_apply(const EffectInputs& in, Image& dst, const void* state,
                   const RenderContext& ctx) {
    apply_kaleidoscope(*in.src, dst, *static_cast<const KaleidoscopeParams*>(state), ctx);
}

const EffectSpec& kaleido_spec() {
    static const std::string kModeChoices = join_names(static_cast<int>(KaleidoMode::Count),
                                                       kaleido_mode_name_at);
    static const std::string kQualityChoices = join_names(static_cast<int>(ChromaQuality::Count),
                                                          kaleido_quality_name_at);
    static const ParamSpec kParams[] = {
        popup(kKaleidoMode, "Mode", kModeChoices.c_str(), static_cast<int>(KaleidoMode::Count),
              static_cast<int>(KaleidoMode::MirrorRotate)),
        float_slider(kKaleidoSegments, "Segments", 2.0, 64.0, 6.0, "", 0),
        angle_param(kKaleidoRotation, "Rotation", 0.0),
        point_param(kKaleidoCenter, "Center", 50.0, 50.0),
        float_slider(kKaleidoScale, "Scale", 0.05, 8.0, 1.0, "x", 3),
        float_slider(kKaleidoOffsetX, "Offset X", -2000.0, 2000.0, 0.0, "px", 1),
        float_slider(kKaleidoOffsetY, "Offset Y", -2000.0, 2000.0, 0.0, "px", 1),
        float_slider(kKaleidoRadialFade, "Radial Fade", 0.0, 1.0, 0.0, "", 2),
        popup(kKaleidoQuality, "Sampling", kQualityChoices.c_str(),
              static_cast<int>(ChromaQuality::Count), 0),
        float_slider(kKaleidoMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_Kaleidoscope";
        s.display_name = "MGTK Kaleidoscope";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Kaleidoscope " MGTK_VERSION_STRING
            " - mirrored, rotated or quilted radial symmetry with an animatable "
            "segment count and centre.";
        s.alpha_policy = AlphaPolicy::Straight;
        s.input = InputKind::Single;
        s.params = kParams;
        s.param_count = kKaleidoParamCount;
        s.state_size = sizeof(KaleidoscopeParams);
        s.defaults = fill_defaults<KaleidoscopeParams>;
        s.read = kaleido_read;
        s.apply = kaleido_apply;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  6. Halftone Pro
// ---------------------------------------------------------------------------
enum HalftoneParam : A_long {
    kHtPattern = 1,
    kHtDotShape,
    kHtColorMode,
    kHtCellSize,
    kHtAngle,
    kHtAngleSpread,
    kHtThreshold,
    kHtContrast,
    kHtGamma,
    kHtAntiAlias,
    kHtInk,
    kHtPaper,
    kHtInkAmount,
    kHtLineWidth,
    kHtInvert,
    kHtBlend,
    kHtMix
};
constexpr int kHalftoneParamCount = kHtMix;

const char* const kHtPatternNames[] = {"Dots", "Lines", "Cross", "Diamond Grid",
                                       "Squares", "Concentric"};
static_assert(sizeof(kHtPatternNames) / sizeof(kHtPatternNames[0]) ==
                  static_cast<size_t>(HalftonePattern::Count),
              "halftone pattern popup has drifted from HalftonePattern");

const char* const kHtDotNames[] = {"Round", "Ellipse", "Square", "Diamond", "Cross"};
static_assert(sizeof(kHtDotNames) / sizeof(kHtDotNames[0]) ==
                  static_cast<size_t>(HalftoneDotShape::Count),
              "halftone dot popup has drifted from HalftoneDotShape");

const char* const kHtColorModeNames[] = {"Monochrome", "RGB", "CMYK"};
static_assert(sizeof(kHtColorModeNames) / sizeof(kHtColorModeNames[0]) ==
                  static_cast<size_t>(HalftoneColorMode::Count),
              "halftone colour mode popup has drifted from HalftoneColorMode");

const char* ht_pattern_name_at(int i) { return kHtPatternNames[i]; }
const char* ht_dot_name_at(int i) { return kHtDotNames[i]; }
const char* ht_color_mode_name_at(int i) { return kHtColorModeNames[i]; }

void halftone_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<HalftoneParams*>(state);
    s->pattern = static_cast<HalftonePattern>(read_popup(in_data, out_data, kHtPattern,
                                                         static_cast<int>(s->pattern)));
    s->dot_shape = static_cast<HalftoneDotShape>(read_popup(in_data, out_data, kHtDotShape,
                                                            static_cast<int>(s->dot_shape)));
    s->color_mode = static_cast<HalftoneColorMode>(read_popup(in_data, out_data, kHtColorMode,
                                                              static_cast<int>(s->color_mode)));
    s->cell_size = read_f(in_data, out_data, kHtCellSize, s->cell_size);
    s->angle_deg = read_angle(in_data, out_data, kHtAngle, s->angle_deg);
    s->angle_spread_deg = read_angle(in_data, out_data, kHtAngleSpread, s->angle_spread_deg);
    s->threshold = read_f(in_data, out_data, kHtThreshold, s->threshold);
    s->contrast = read_f(in_data, out_data, kHtContrast, s->contrast);
    s->gamma = read_f(in_data, out_data, kHtGamma, s->gamma);
    s->anti_alias = read_f(in_data, out_data, kHtAntiAlias, s->anti_alias);
    s->ink = read_color(in_data, out_data, kHtInk, s->ink);
    s->paper = read_color(in_data, out_data, kHtPaper, s->paper);
    s->ink_amount = read_f(in_data, out_data, kHtInkAmount, s->ink_amount);
    s->line_width = read_f(in_data, out_data, kHtLineWidth, s->line_width);
    s->invert = read_bool(in_data, out_data, kHtInvert, s->invert);
    s->blend = blend_mode_from_index(read_popup(in_data, out_data, kHtBlend,
                                                static_cast<int>(s->blend)));
    s->mix = read_f(in_data, out_data, kHtMix, s->mix);
}

void halftone_apply(const EffectInputs& in, Image& dst, const void* state,
                    const RenderContext& ctx) {
    apply_halftone(*in.src, dst, *static_cast<const HalftoneParams*>(state), ctx);
}

const EffectSpec& halftone_spec() {
    static const std::string kPatternChoices = join_names(static_cast<int>(HalftonePattern::Count),
                                                          ht_pattern_name_at);
    static const std::string kDotChoices = join_names(static_cast<int>(HalftoneDotShape::Count),
                                                      ht_dot_name_at);
    static const std::string kColorModeChoices =
        join_names(static_cast<int>(HalftoneColorMode::Count), ht_color_mode_name_at);
    static const std::string kBlendChoices = join_names(blend_mode_count(), blend_name_at);
    static const ParamSpec kParams[] = {
        popup(kHtPattern, "Pattern", kPatternChoices.c_str(),
              static_cast<int>(HalftonePattern::Count), static_cast<int>(HalftonePattern::Dots)),
        popup(kHtDotShape, "Dot Shape", kDotChoices.c_str(),
              static_cast<int>(HalftoneDotShape::Count), static_cast<int>(HalftoneDotShape::Round)),
        popup(kHtColorMode, "Colour Mode", kColorModeChoices.c_str(),
              static_cast<int>(HalftoneColorMode::Count),
              static_cast<int>(HalftoneColorMode::Monochrome)),
        float_slider(kHtCellSize, "Cell Size", 1.0, 200.0, 8.0, "px", 2),
        angle_param(kHtAngle, "Screen Angle", 15.0),
        angle_param(kHtAngleSpread, "Angle Spread", 30.0),
        float_slider(kHtThreshold, "Threshold", -1.0, 1.0, 0.0, "", 3),
        float_slider(kHtContrast, "Contrast", 0.0, 4.0, 1.0, "", 2),
        float_slider(kHtGamma, "Gamma", 0.1, 4.0, 1.0, "", 3),
        float_slider(kHtAntiAlias, "Anti-Alias", 0.0, 4.0, 1.0, "px", 2),
        color_param(kHtInk, "Ink", 0.0f, 0.0f, 0.0f),
        color_param(kHtPaper, "Paper", 1.0f, 1.0f, 1.0f),
        float_slider(kHtInkAmount, "Ink Amount", 0.0, 1.0, 1.0, "", 2),
        float_slider(kHtLineWidth, "Line Width", 0.05, 4.0, 1.0, "", 2),
        checkbox(kHtInvert, "Invert", false),
        popup(kHtBlend, "Blend Mode", kBlendChoices.c_str(), blend_mode_count(),
              static_cast<int>(BlendMode::Normal)),
        float_slider(kHtMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_HalftonePro";
        s.display_name = "MGTK Halftone Pro";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Halftone Pro " MGTK_VERSION_STRING
            " - dot, line, cross, diamond, square and concentric halftone "
            "screens in monochrome, RGB or CMYK with per-channel screen angles.";
        s.alpha_policy = AlphaPolicy::StraightKeepSourceAlpha;
        s.input = InputKind::Single;
        s.params = kParams;
        s.param_count = kHalftoneParamCount;
        s.state_size = sizeof(HalftoneParams);
        s.defaults = fill_defaults<HalftoneParams>;
        s.read = halftone_read;
        s.apply = halftone_apply;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  7. Slit Scan
// ---------------------------------------------------------------------------
enum SlitParam : A_long {
    kSlitMode = 1,
    kSlitDirection,
    kSlitFrames,
    kSlitSliceWidth,
    kSlitOffset,
    kSlitFalloff,
    kSlitAngle,
    kSlitCenter,
    kSlitMirror,
    kSlitIntensity,
    kSlitBlend,
    kSlitMix
};
constexpr int kSlitParamCount = kSlitMix;

const char* const kSlitModeNames[] = {"Time Slice", "Time Blend", "Time Displace",
                                      "Time Echo"};
static_assert(sizeof(kSlitModeNames) / sizeof(kSlitModeNames[0]) ==
                  static_cast<size_t>(SlitScanMode::Count),
              "slit scan mode popup has drifted from SlitScanMode");

const char* const kSlitDirectionNames[] = {"Horizontal", "Vertical", "Radial", "Angular"};
static_assert(sizeof(kSlitDirectionNames) / sizeof(kSlitDirectionNames[0]) ==
                  static_cast<size_t>(SlitDirection::Count),
              "slit scan direction popup has drifted from SlitDirection");

const char* slit_mode_name_at(int i) { return kSlitModeNames[i]; }
const char* slit_direction_name_at(int i) { return kSlitDirectionNames[i]; }

void slit_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<SlitScanParams*>(state);
    s->mode = static_cast<SlitScanMode>(read_popup(in_data, out_data, kSlitMode,
                                                   static_cast<int>(s->mode)));
    s->direction = static_cast<SlitDirection>(read_popup(in_data, out_data, kSlitDirection,
                                                         static_cast<int>(s->direction)));
    s->frames = clamp(static_cast<int>(read_f(in_data, out_data, kSlitFrames,
                                              static_cast<float>(s->frames))), 1, 24);
    s->slice_width = read_f(in_data, out_data, kSlitSliceWidth, s->slice_width);
    s->offset = read_f(in_data, out_data, kSlitOffset, s->offset);
    s->falloff = read_f(in_data, out_data, kSlitFalloff, s->falloff);
    s->angle_deg = read_angle(in_data, out_data, kSlitAngle, s->angle_deg);
    s->center = read_point_norm(in_data, out_data, kSlitCenter, s->center);
    s->mirror = read_bool(in_data, out_data, kSlitMirror, s->mirror);
    s->intensity = read_f(in_data, out_data, kSlitIntensity, s->intensity);
    s->blend = blend_mode_from_index(read_popup(in_data, out_data, kSlitBlend,
                                                static_cast<int>(s->blend)));
    s->mix = read_f(in_data, out_data, kSlitMix, s->mix);
}

void slit_apply(const EffectInputs& in, Image& dst, const void* state, const RenderContext& ctx) {
    apply_slit_scan(*in.src, *in.history, dst, *static_cast<const SlitScanParams*>(state), ctx);
}

const EffectSpec& slit_spec() {
    static const std::string kModeChoices = join_names(static_cast<int>(SlitScanMode::Count),
                                                       slit_mode_name_at);
    static const std::string kDirectionChoices = join_names(
        static_cast<int>(SlitDirection::Count), slit_direction_name_at);
    static const std::string kBlendChoices = join_names(blend_mode_count(), blend_name_at);
    static const ParamSpec kParams[] = {
        popup(kSlitMode, "Mode", kModeChoices.c_str(), static_cast<int>(SlitScanMode::Count),
              static_cast<int>(SlitScanMode::TimeSlice)),
        popup(kSlitDirection, "Direction", kDirectionChoices.c_str(),
              static_cast<int>(SlitDirection::Count),
              static_cast<int>(SlitDirection::Horizontal)),
        float_slider(kSlitFrames, "Frames", 1.0, 24.0, 12.0, "", 0),
        float_slider(kSlitSliceWidth, "Slice Width", 1.0, 200.0, 4.0, "px", 2),
        float_slider(kSlitOffset, "Offset", -2000.0, 2000.0, 0.0, "px", 1),
        float_slider(kSlitFalloff, "Falloff", 0.0, 1.0, 0.0, "", 2),
        angle_param(kSlitAngle, "Angle", 0.0),
        point_param(kSlitCenter, "Center", 50.0, 50.0),
        checkbox(kSlitMirror, "Ping-Pong", false),
        float_slider(kSlitIntensity, "Intensity", 0.0, 4.0, 1.0, "", 2),
        popup(kSlitBlend, "Blend Mode", kBlendChoices.c_str(), blend_mode_count(),
              static_cast<int>(BlendMode::Normal)),
        float_slider(kSlitMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_SlitScan";
        s.display_name = "MGTK Slit Scan";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Slit Scan " MGTK_VERSION_STRING
            " - time displacement with slices, blends, echoes and motion-driven "
            "offsets across up to 24 frames of history.";
        s.alpha_policy = AlphaPolicy::Straight;
        s.input = InputKind::History;
        s.varies_per_frame = true;
        s.params = kParams;
        s.param_count = kSlitParamCount;
        s.state_size = sizeof(SlitScanParams);
        s.defaults = fill_defaults<SlitScanParams>;
        s.read = slit_read;
        s.apply = slit_apply;
        s.history_frames_param = kSlitFrames;
        return s;
    }();
    return kSpec;
}

// ---------------------------------------------------------------------------
//  8. Pixel Sort
// ---------------------------------------------------------------------------
enum SortParam : A_long {
    kSortKey = 1,
    kSortAxis,
    kSortOrder,
    kSortThresholdLow,
    kSortThresholdHigh,
    kSortMaxLength,
    kSortRandomness,
    kSortStretch,
    kSortAlphaMask,
    kSortBlend,
    kSortMix
};
constexpr int kSortParamCount = kSortMix;

const char* const kSortKeyNames[] = {"Brightness", "Hue", "Saturation", "Red",
                                     "Green", "Blue", "Random"};
static_assert(sizeof(kSortKeyNames) / sizeof(kSortKeyNames[0]) ==
                  static_cast<size_t>(SortKey::Count),
              "pixel sort key popup has drifted from SortKey");

const char* const kSortAxisNames[] = {"Horizontal", "Vertical"};
static_assert(sizeof(kSortAxisNames) / sizeof(kSortAxisNames[0]) ==
                  static_cast<size_t>(SortAxis::Count),
              "pixel sort axis popup has drifted from SortAxis");

const char* const kSortOrderNames[] = {"Ascending", "Descending"};
static_assert(sizeof(kSortOrderNames) / sizeof(kSortOrderNames[0]) ==
                  static_cast<size_t>(SortOrder::Count),
              "pixel sort order popup has drifted from SortOrder");

const char* sort_key_name_at(int i) { return kSortKeyNames[i]; }
const char* sort_axis_name_at(int i) { return kSortAxisNames[i]; }
const char* sort_order_name_at(int i) { return kSortOrderNames[i]; }

void sort_read(PF_InData* in_data, PF_OutData* out_data, void* state) {
    auto* s = static_cast<PixelSortParams*>(state);
    s->key = static_cast<SortKey>(read_popup(in_data, out_data, kSortKey,
                                             static_cast<int>(s->key)));
    s->axis = static_cast<SortAxis>(read_popup(in_data, out_data, kSortAxis,
                                               static_cast<int>(s->axis)));
    s->order = static_cast<SortOrder>(read_popup(in_data, out_data, kSortOrder,
                                                 static_cast<int>(s->order)));
    s->threshold_low = read_f(in_data, out_data, kSortThresholdLow, s->threshold_low);
    s->threshold_high = read_f(in_data, out_data, kSortThresholdHigh, s->threshold_high);
    s->max_length = clamp(static_cast<int>(read_f(in_data, out_data, kSortMaxLength,
                                                  static_cast<float>(s->max_length))),
                          1, 4096);
    s->randomness = read_f(in_data, out_data, kSortRandomness, s->randomness);
    s->stretch = read_f(in_data, out_data, kSortStretch, s->stretch);
    s->use_alpha_mask = read_bool(in_data, out_data, kSortAlphaMask, s->use_alpha_mask);
    s->blend = blend_mode_from_index(read_popup(in_data, out_data, kSortBlend,
                                                static_cast<int>(s->blend)));
    s->mix = read_f(in_data, out_data, kSortMix, s->mix);
}

void sort_apply(const EffectInputs& in, Image& dst, const void* state, const RenderContext& ctx) {
    apply_pixel_sort(*in.src, dst, *static_cast<const PixelSortParams*>(state), ctx);
}

const EffectSpec& sort_spec() {
    static const std::string kKeyChoices = join_names(static_cast<int>(SortKey::Count),
                                                      sort_key_name_at);
    static const std::string kAxisChoices = join_names(static_cast<int>(SortAxis::Count),
                                                       sort_axis_name_at);
    static const std::string kOrderChoices = join_names(static_cast<int>(SortOrder::Count),
                                                        sort_order_name_at);
    static const std::string kBlendChoices = join_names(blend_mode_count(), blend_name_at);
    static const ParamSpec kParams[] = {
        popup(kSortKey, "Sort Key", kKeyChoices.c_str(), static_cast<int>(SortKey::Count),
              static_cast<int>(SortKey::Brightness)),
        popup(kSortAxis, "Axis", kAxisChoices.c_str(), static_cast<int>(SortAxis::Count),
              static_cast<int>(SortAxis::Horizontal)),
        popup(kSortOrder, "Order", kOrderChoices.c_str(), static_cast<int>(SortOrder::Count),
              static_cast<int>(SortOrder::Descending)),
        float_slider(kSortThresholdLow, "Threshold Low", -1.0, 4.0, 0.15, "", 3),
        float_slider(kSortThresholdHigh, "Threshold High", -1.0, 4.0, 1.0, "", 3),
        float_slider(kSortMaxLength, "Max Length", 1.0, 4096.0, 200.0, "px", 0),
        float_slider(kSortRandomness, "Randomness", 0.0, 1.0, 0.0, "", 2),
        float_slider(kSortStretch, "Stretch", 0.0, 1.0, 0.0, "", 2),
        checkbox(kSortAlphaMask, "Alpha Mask", false),
        popup(kSortBlend, "Blend Mode", kBlendChoices.c_str(), blend_mode_count(),
              static_cast<int>(BlendMode::Normal)),
        float_slider(kSortMix, "Mix", 0.0, 1.0, 1.0, "", 2),
    };
    static const EffectSpec kSpec = [] {
        EffectSpec s;
        s.match_name = "MotionGraphicsToolkit_PixelSort";
        s.display_name = "MGTK Pixel Sort";
        s.category = MGTK_CATEGORY;
        s.description =
            "MGTK Pixel Sort " MGTK_VERSION_STRING
            " - datamosh pixel sorting by brightness, hue, saturation or a "
            "channel, with thresholding, run capping and optional stretching.";
        s.alpha_policy = AlphaPolicy::StraightKeepSourceAlpha;
        s.input = InputKind::Single;
        s.params = kParams;
        s.param_count = kSortParamCount;
        s.state_size = sizeof(PixelSortParams);
        s.defaults = fill_defaults<PixelSortParams>;
        s.read = sort_read;
        s.apply = sort_apply;
        return s;
    }();
    return kSpec;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Public registry
// ---------------------------------------------------------------------------
const EffectSpec& feedback_echo_spec() { return echo_spec(); }
const EffectSpec& chromatic_split_spec() { return chroma_spec(); }
const EffectSpec& anamorphic_glow_spec() { return glow_spec(); }
const EffectSpec& fractal_warp_spec() { return warp_spec(); }
const EffectSpec& kaleidoscope_spec() { return kaleido_spec(); }
const EffectSpec& halftone_pro_spec() { return halftone_spec(); }
const EffectSpec& slit_scan_spec() { return slit_spec(); }
const EffectSpec& pixel_sort_spec() { return sort_spec(); }

const EffectSpec* const* all_effects(int* count) {
    // Order matters only for the Effects & Presets menu, which sorts
    // alphabetically by display name anyway; this order matches the numbering
    // used throughout the documentation.
    static const EffectSpec* const kAll[] = {
        &feedback_echo_spec(), &chromatic_split_spec(), &anamorphic_glow_spec(),
        &fractal_warp_spec(),   &kaleidoscope_spec(),   &halftone_pro_spec(),
        &slit_scan_spec(),      &pixel_sort_spec(),
    };
    if (count != nullptr) *count = static_cast<int>(sizeof(kAll) / sizeof(kAll[0]));
    return kAll;
}

const EffectSpec* find_effect(const char* match_name) {
    if (match_name == nullptr) return nullptr;
    int count = 0;
    const EffectSpec* const* all = all_effects(&count);
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(all[i]->match_name, match_name) == 0) return all[i];
    }
    return nullptr;
}

}  // namespace ae
}  // namespace mgtk
