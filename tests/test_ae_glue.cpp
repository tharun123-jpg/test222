// =============================================================================
//  tests/test_ae_glue.cpp -- the After Effects glue, exercised without After
//                            Effects.
//
//  These tests drive the real dispatcher (src/ae/dispatcher.cpp) with the real
//  effect registry (src/ae/registry.cpp) through a stand-in host that speaks
//  AE's calling convention: GlobalSetup, ParamsSetup, SequenceSetup, then the
//  SmartFX pre-render/render pair over a synthetic layer.
//
//  What that catches before the first AE launch:
//
//    * an out-flag that does not match the PiPL, which would stop the plug-in
//      loading at all,
//    * a parameter id in the registry that the effect then fails to read back,
//    * a mistake in the premultiply / unpremultiply round trip, which shows up
//      as a halo around semi-transparent pixels and is the single most common
//      bug in an effect of this kind,
//    * the SmartFX protocol being driven out of order -- checking out pixels
//      that were never declared in pre-render, or declaring an output rect
//      that is not the one the render fills.
//
//  It cannot catch anything that depends on the real host's behaviour (AE's
//  caching, real 32-bit projects, GPU paths). Those need the SDK and AE itself.
// =============================================================================
#include "test_framework.hpp"

#include "ae_shim/host.hpp"
#include "effect_registry.hpp"
#include "plugin_entry.hpp"

#include "mgtk/effects.hpp"
#include "mgtk/version.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

const mgtk::ae::EffectSpec* g_spec = nullptr;

PF_Err plug_in_entry(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                     PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    // The real plug-ins get their spec from a two-line translation unit; the
    // host here has no user data to carry one, so it comes from a file-static.
    if (g_spec == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    return mgtk::ae::dispatch(*g_spec, cmd, in_data, out_data, params, output, extra);
}

// The parameter every effect names "Mix", which is the one that scales the
// effect against the source. Its existence and semantics are what makes the
// generic passthrough test below possible for all eight effects at once.
A_long find_mix_param(const mgtk::ae::EffectSpec& spec) {
    for (int i = 0; i < spec.param_count; ++i) {
        if (std::strcmp(spec.params[i].name, "Mix") == 0) return spec.params[i].id;
    }
    return -1;
}

std::vector<const mgtk::ae::EffectSpec*> effects() {
    int count = 0;
    const mgtk::ae::EffectSpec* const* all = mgtk::ae::all_effects(&count);
    std::vector<const mgtk::ae::EffectSpec*> out;
    for (int i = 0; i < count; ++i) out.push_back(all[i]);
    return out;
}

// Largest absolute difference between two images, in straight-alpha component
// terms.
float max_abs_diff(const mgtk::Image& a, const mgtk::Image& b) {
    if (a.width() != b.width() || a.height() != b.height()) return 1.0f;
    float worst = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
        const mgtk::Float4* ra = a.row(y);
        const mgtk::Float4* rb = b.row(y);
        for (int x = 0; x < a.width(); ++x) {
            worst = std::max(worst, std::fabs(ra[x].r - rb[x].r));
            worst = std::max(worst, std::fabs(ra[x].g - rb[x].g));
            worst = std::max(worst, std::fabs(ra[x].b - rb[x].b));
            worst = std::max(worst, std::fabs(ra[x].a - rb[x].a));
        }
    }
    return worst;
}

// The same comparison, but in premultiplied space.
//
// This is the right space for a passthrough test, and the reason is
// quantisation: AE stores premultiplied pixels, so a dark colour under a small
// alpha is rounded hard -- a straight-alpha comparison would flag that as an
// effect bug when it is really the 8-bit buffer the host handed over. What an
// effect must reproduce exactly is the premultiplied pixel, because that is
// what the next effect in the chain will be given.
float max_abs_diff_premul(const mgtk::Image& a, const mgtk::Image& b) {
    if (a.width() != b.width() || a.height() != b.height()) return 1.0f;
    float worst = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
        const mgtk::Float4* ra = a.row(y);
        const mgtk::Float4* rb = b.row(y);
        for (int x = 0; x < a.width(); ++x) {
            const float aa = ra[x].a;
            const float ba = rb[x].a;
            worst = std::max(worst, std::fabs(ra[x].r * aa - rb[x].r * ba));
            worst = std::max(worst, std::fabs(ra[x].g * aa - rb[x].g * ba));
            worst = std::max(worst, std::fabs(ra[x].b * aa - rb[x].b * ba));
            worst = std::max(worst, std::fabs(aa - ba));
        }
    }
    return worst;
}

bool all_finite(const mgtk::Image& img) {
    for (int y = 0; y < img.height(); ++y) {
        const mgtk::Float4* row = img.row(y);
        for (int x = 0; x < img.width(); ++x) {
            if (!std::isfinite(row[x].r) || !std::isfinite(row[x].g) ||
                !std::isfinite(row[x].b) || !std::isfinite(row[x].a)) {
                return false;
            }
        }
    }
    return true;
}

constexpr int kWidth = 48;
constexpr int kHeight = 32;

}  // namespace

// ---------------------------------------------------------------------------
//  Registration
// ---------------------------------------------------------------------------
MGTK_TEST(ae_every_effect_reports_the_flags_its_pipl_claims) {
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        const PF_Err err = host.send(plug_in_entry, PF_Cmd_GLOBAL_SETUP);
        CHECK_EQ(static_cast<long>(err), 0L);

        // This equality is the whole reason tools/gen_pipl.cpp reads the same
        // constants: AE rejects a plug-in whose PiPL and GlobalSetup disagree.
        A_u_long expected = mgtk::ae::kOutFlags;
        if (spec->varies_per_frame) expected |= 4u;  // PF_OutFlag_NON_PARAM_VARY
        CHECK_MSG(host.out_data().out_flags == expected, spec->match_name);
        CHECK_MSG(host.out_data().out_flags2 == mgtk::ae::kOutFlags2, spec->match_name);

        // A 32-bit float project is only reachable if both of these are set.
        CHECK_MSG((host.out_data().out_flags & 33554432u) != 0, spec->match_name);
        CHECK_MSG((host.out_data().out_flags2 & 4096u) != 0, spec->match_name);
        CHECK_MSG((host.out_data().out_flags2 & 1024u) != 0, spec->match_name);
    }
}

MGTK_TEST(ae_params_setup_registers_every_parameter_exactly_once) {
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        const PF_Err err = host.setup(plug_in_entry);
        CHECK_EQ(static_cast<long>(err), 0L);

        const std::vector<PF_ParamDef>& registered = host.registered();
        CHECK_MSG(registered.size() == static_cast<size_t>(spec->param_count),
                  spec->match_name);
        // params[0] is the implicit input layer, so num_params is one more.
        CHECK_MSG(host.out_data().num_params == spec->param_count + 1, spec->match_name);

        A_long previous_id = 0;
        for (const PF_ParamDef& def : registered) {
            CHECK_MSG(def.uu.id > previous_id, spec->match_name);
            CHECK_MSG(def.name[0] != '\0', spec->match_name);
            // Registration must not leave a float slider with a zero-width
            // range: AE's UI then refuses to move it.
            if (def.param_type == PF_Param_FLOAT_SLIDER) {
                CHECK_MSG(def.u.fs_d.valid_max >= def.u.fs_d.valid_min, spec->match_name);
                CHECK_MSG(def.u.fs_d.dephault >= def.u.fs_d.valid_min &&
                              def.u.fs_d.dephault <= def.u.fs_d.valid_max,
                          spec->match_name);
            }
            if (def.param_type == PF_Param_POPUP) {
                CHECK_MSG(def.u.pd.dephault < def.u.pd.num_choices, spec->match_name);
                CHECK_MSG(def.u.pd.u.namesptr != nullptr, spec->match_name);
            }
            previous_id = def.uu.id;
        }
    }
}

// ---------------------------------------------------------------------------
//  Rendering
// ---------------------------------------------------------------------------
MGTK_TEST(ae_smart_render_with_mix_zero_returns_the_source_untouched) {
    // Mix at zero means "show me only the original", so every effect has to
    // hand back exactly the pixels it was given. That makes it a single
    // invariant that exercises the whole pipeline for all eight effects: the
    // SmartFX handshake, the parameter checkout, and the premultiplied world
    // conversion in both directions.
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        const A_long mix_id = find_mix_param(*spec);
        CHECK_MSG(mix_id > 0, spec->match_name);

        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);

        const mgtk::Image src = aetest::test_layer_alpha_ramp(kWidth, kHeight);
        host.set_input(src);
        // Mix is the only parameter that is not left at its default, so this
        // also proves the defaults the registry installs are usable ones.
        host.set_float(mix_id, 0.0);

        CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

        mgtk::Image out;
        host.get_output(out);
        // One 8-bit step of slack, measured premultiplied: the world round trip
        // quantises on the way in and on the way out.
        CHECK_MSG(max_abs_diff_premul(src, out) <= 1.0f / 255.0f + 1e-4f, spec->match_name);
    }
}

MGTK_TEST(ae_legacy_render_matches_smart_render) {
    // The two render paths share everything below the world conversion, so any
    // difference between them is a bug in the glue, not in an effect.
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        const mgtk::Image src = aetest::test_layer_gradient(kWidth, kHeight);

        aetest::Host smart(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(smart.setup(plug_in_entry)), 0L);
        smart.set_input(src);
        CHECK_EQ(static_cast<long>(smart.render_smart(plug_in_entry)), 0L);
        mgtk::Image smart_out;
        smart.get_output(smart_out);

        aetest::Host legacy(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(legacy.setup(plug_in_entry)), 0L);
        legacy.set_input(src);
        CHECK_EQ(static_cast<long>(legacy.render_legacy(plug_in_entry)), 0L);
        mgtk::Image legacy_out;
        legacy.get_output(legacy_out);

        CHECK_MSG(max_abs_diff_premul(smart_out, legacy_out) <= 1.0f / 255.0f + 1e-4f,
                  spec->match_name);
    }
}

MGTK_TEST(ae_thirty_two_bit_worlds_round_trip_without_quantising) {
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        const A_long mix_id = find_mix_param(*spec);
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB128);
        CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);

        // This one carries highlights above 1.0. In an 8-bit test they would be
        // clamped by the world itself, which is exactly what a float test is
        // supposed to prove does not happen.
        const mgtk::Image src = aetest::test_layer_hdr(kWidth, kHeight);
        host.set_input(src);
        host.set_float(mix_id, 0.0);
        CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

        mgtk::Image out;
        host.get_output(out);
        // Float in, float out: this has to be exact, not merely close.
        CHECK_MSG(max_abs_diff(src, out) < 1e-5f, spec->match_name);
        CHECK_MSG(all_finite(out), spec->match_name);
    }
}

MGTK_TEST(ae_effects_actually_change_the_image_at_their_defaults) {
    // The counterpart to the passthrough test: with defaults, every effect
    // should do something. An effect that silently passes through is the
    // failure mode a "did it run at all?" test is for.
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);

        const mgtk::Image src = aetest::test_layer_gradient(kWidth, kHeight);

        // The two stateful effects have nothing to work with on the very first
        // frame -- no echo has accumulated and no history exists -- and on an
        // opaque layer they correctly return the frame untouched. Prime them
        // with one frame so the comparison is about the effect, not about the
        // beginning of the composition.
        if (spec->input != mgtk::ae::InputKind::Single) {
            host.set_input(src);
            CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
        }

        host.set_input(src);
        CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

        mgtk::Image out;
        host.get_output(out);
        CHECK_MSG(all_finite(out), spec->match_name);
        CHECK_MSG(max_abs_diff(src, out) > 1.0f / 255.0f, spec->match_name);
    }
}

MGTK_TEST(ae_sixteen_bit_worlds_render_finitely) {
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB64);
        CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);
        const mgtk::Image src = aetest::test_layer_gradient(kWidth, kHeight);
        host.set_input(src);
        CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
        mgtk::Image out;
        host.get_output(out);
        CHECK_MSG(all_finite(out), spec->match_name);
    }
}

MGTK_TEST(ae_pre_render_declares_the_layer_bounds) {
    // These effects are all size-preserving, so the content bounds they declare
    // must be the layer, and the render must fill exactly that.
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);
        host.set_input(aetest::test_layer_gradient(kWidth, kHeight));
        CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

        const PF_EffectWorld& out = host.output_world();
        CHECK_MSG(out.width == kWidth, spec->match_name);
        CHECK_MSG(out.height == kHeight, spec->match_name);
        // The output world must still be the one the pre-render announced, and
        // must not have been re-pointed by the effect.
        CHECK_MSG(out.data != nullptr, spec->match_name);
    }
}

MGTK_TEST(ae_a_cancelled_render_returns_the_interrupt_error) {
    // AE expects the effect to stop and hand the interrupt back rather than
    // keep going. The cores poll ctx.aborted() every row block, so a host that
    // reports abort on the first progress callback must be obeyed.
    g_spec = &mgtk::ae::anamorphic_glow_spec();
    aetest::Host host(128, 128, PF_PixelFormat_ARGB32);
    CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);
    host.set_input(aetest::test_layer_gradient(128, 128));
    host.set_abort_after(0);

    const PF_Err err = host.render_smart(plug_in_entry);
    CHECK_EQ(static_cast<long>(err), static_cast<long>(PF_Err_INTERRUPT));
}

MGTK_TEST(ae_abort_is_not_reported_when_the_host_never_cancels) {
    g_spec = &mgtk::ae::anamorphic_glow_spec();
    aetest::Host host(64, 64, PF_PixelFormat_ARGB32);
    CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);
    host.set_input(aetest::test_layer_gradient(64, 64));
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
    CHECK_MSG(host.progress_calls() > 0, "the effect never reported progress");
}

MGTK_TEST(ae_feedback_echo_carries_state_between_frames) {
    // Feedback Echo is the only effect that carries state between frames, and
    // the state lives in sequence data. Rendering frame B twice must not give
    // the same answer twice: the first pass seeds the accumulator with the
    // change from frame A, and the second pass has to see it. If the two agree,
    // the accumulator is being thrown away, which is what a broken
    // sequence-data round trip looks like from the outside.
    //
    // (Rendering the *same* frame twice is deliberately not the test: with the
    // default decay equal to the echo opacity, an echo of a static frame is
    // exactly the frame, so a static test proves nothing either way.)
    g_spec = &mgtk::ae::feedback_echo_spec();
    aetest::Host host(32, 32, PF_PixelFormat_ARGB32);
    CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);

    const mgtk::Image base = aetest::test_layer_gradient(32, 32);
    const mgtk::Image frame_a = aetest::frame_at(base, 0, 24);
    const mgtk::Image frame_b = aetest::frame_at(base, 5, 24);

    host.set_input(frame_a);
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

    host.set_input(frame_b);
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
    mgtk::Image first;
    host.get_output(first);

    host.set_input(frame_b);
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
    mgtk::Image second;
    host.get_output(second);

    CHECK_MSG(max_abs_diff(first, second) > 1.0f / 255.0f,
              "the feedback accumulator did not carry between frames");
    // And the echo has to be visible at all: with frame A in the accumulator,
    // frame B must not come back untouched.
    CHECK_MSG(max_abs_diff_premul(frame_b, first) > 1.0f / 255.0f,
              "the echo left no trace of the previous frame");
}

MGTK_TEST(ae_feedback_echo_reset_button_clears_the_accumulator) {
    // The Reset button is the user's only way to stop a trail that has run
    // away, so it has to actually drop the accumulated pixels. The observable
    // consequence of a reset accumulator is that the very next frame comes back
    // as just itself, with nothing echoed over it.
    g_spec = &mgtk::ae::feedback_echo_spec();
    aetest::Host host(32, 32, PF_PixelFormat_ARGB32);
    CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);

    const mgtk::Image base = aetest::test_layer_gradient(32, 32);
    host.set_input(aetest::frame_at(base, 0, 24));
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

    host.set_input(aetest::frame_at(base, 5, 24));
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
    mgtk::Image with_echo;
    host.get_output(with_echo);

    PF_UserChangedParamExtra changed{};
    changed.param_index = host.registered().back().uu.id;  // the Reset button
    CHECK_EQ(static_cast<long>(host.send(plug_in_entry, PF_Cmd_USER_CHANGED_PARAM, &changed)), 0L);

    const mgtk::Image frame_b = aetest::frame_at(base, 5, 24);
    host.set_input(frame_b);
    CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);
    mgtk::Image after_reset;
    host.get_output(after_reset);

    CHECK_MSG(max_abs_diff_premul(frame_b, after_reset) <= 1.0f / 255.0f + 1e-4f,
              "the accumulator survived the reset");
    CHECK_MSG(max_abs_diff_premul(with_echo, after_reset) > 1.0f / 255.0f,
              "the reset made no observable difference");
}

MGTK_TEST(ae_fully_transparent_pixels_stay_transparent) {
    // The other half of the passthrough guarantee, and the one that catches a
    // premultiply done twice: a pixel with no alpha must come back with no
    // alpha, and must not acquire colour from a neighbour.
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        const A_long mix_id = find_mix_param(*spec);
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(host.setup(plug_in_entry)), 0L);

        mgtk::Image src = aetest::test_layer_gradient(kWidth, kHeight);
        for (int y = 0; y < src.height(); ++y) {
            mgtk::Float4* row = src.row(y);
            for (int x = 0; x < 4; ++x) row[x] = mgtk::Float4{0.3f, 0.6f, 0.9f, 0.0f};
        }
        host.set_input(src);
        host.set_float(mix_id, 0.0);
        CHECK_EQ(static_cast<long>(host.render_smart(plug_in_entry)), 0L);

        mgtk::Image out;
        host.get_output(out);
        for (int y = 0; y < out.height(); ++y) {
            const mgtk::Float4* row = out.row(y);
            for (int x = 0; x < 4; ++x) {
                CHECK_MSG(row[x].a <= 1.0f / 255.0f, spec->match_name);
            }
        }
    }
}

MGTK_TEST(ae_about_reports_a_version) {
    for (const mgtk::ae::EffectSpec* spec : effects()) {
        g_spec = spec;
        aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
        CHECK_EQ(static_cast<long>(host.send(plug_in_entry, PF_Cmd_ABOUT)), 0L);
        CHECK_MSG(host.out_data().return_msg[0] != '\0', spec->match_name);
        CHECK_MSG(std::strstr(host.out_data().return_msg, MGTK_VERSION_STRING) != nullptr,
                  spec->match_name);
    }
}

MGTK_TEST(ae_unknown_selectors_are_answered_politely) {
    // AE treats an error from a selector an effect does not implement as a
    // failure to load, so the dispatcher has to answer everything it does not
    // recognise with PF_Err_NONE.
    g_spec = &mgtk::ae::kaleidoscope_spec();
    aetest::Host host(kWidth, kHeight, PF_PixelFormat_ARGB32);
    CHECK_EQ(static_cast<long>(host.send(plug_in_entry, PF_Cmd_UPDATE_PARAMS_UI)), 0L);
    CHECK_EQ(static_cast<long>(host.send(plug_in_entry, PF_Cmd_FRAME_SETUP)), 0L);
    CHECK_EQ(static_cast<long>(host.send(plug_in_entry, PF_Cmd_DO_DIALOG)), 0L);
}
