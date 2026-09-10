// =============================================================================
//  tests/test_blend.cpp -- blend modes and straight-alpha compositing
// =============================================================================
#include <cmath>

#include "mgtk/blend.hpp"
#include "test_framework.hpp"

using namespace mgtk;

MGTK_TEST(blend_normal_returns_the_source) {
    const Float4 b{0.2f, 0.4f, 0.6f, 1.0f};
    const Float4 s{0.9f, 0.1f, 0.3f, 0.5f};
    const Float4 out = blend_pixel(BlendMode::Normal, b, s);

    CHECK_NEAR(out.r, s.r, 0.0f);
    CHECK_NEAR(out.g, s.g, 0.0f);
    CHECK_NEAR(out.b, s.b, 0.0f);
    CHECK_NEAR(out.a, s.a, 0.0f);
}

MGTK_TEST(blend_modes_have_correct_identities) {
    // Each of these modes has a value that makes it a no-op, which is the
    // quickest way to catch an inverted or mistranscribed formula.
    const float v = 0.35f;

    CHECK_NEAR(blend_channel(BlendMode::Multiply, v, 1.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Screen, v, 0.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Add, v, 0.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Subtract, v, 0.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Lighten, v, 0.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Darken, v, 1.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Overlay, v, 0.5f),
               2.0f * v * 0.5f, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Difference, v, 0.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Exclusion, v, 0.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Divide, v, 1.0f), v, 1e-6f);
    CHECK_NEAR(blend_channel(BlendMode::Average, v, v), v, 1e-6f);
}

MGTK_TEST(blend_difference_of_identical_is_zero) {
    for (float v = 0.0f; v <= 1.0f; v += 0.1f) {
        CHECK_NEAR(blend_channel(BlendMode::Difference, v, v), 0.0f, 1e-6f);
        CHECK_NEAR(blend_channel(BlendMode::Exclusion, v, v),
                   2.0f * v - 2.0f * v * v, 1e-6f);
    }
}

MGTK_TEST(blend_screen_and_multiply_are_duals) {
    // screen(a,b) == 1 - multiply(1-a, 1-b), the standard duality.
    for (int i = 0; i <= 10; ++i) {
        const float a = static_cast<float>(i) / 10.0f;
        for (int j = 0; j <= 10; ++j) {
            const float b = static_cast<float>(j) / 10.0f;
            const float screen = blend_channel(BlendMode::Screen, a, b);
            const float dual = 1.0f - blend_channel(BlendMode::Multiply, 1.0f - a, 1.0f - b);
            CHECK_NEAR(screen, dual, 1e-5f);
        }
    }
}

MGTK_TEST(blend_hard_light_is_an_argument_swap_of_overlay) {
    for (int i = 0; i <= 10; ++i) {
        const float backdrop = static_cast<float>(i) / 10.0f;
        for (int j = 0; j <= 10; ++j) {
            const float source = static_cast<float>(j) / 10.0f;
            const float hl = blend_channel(BlendMode::HardLight, backdrop, source);
            const float ol = blend_channel(BlendMode::Overlay, source, backdrop);
            CHECK_NEAR(hl, ol, 1e-5f);
        }
    }
}

MGTK_TEST(blend_soft_light_is_continuous) {
    // Soft light is the one mode with a piecewise definition, and the classic
    // bug is a discontinuity where the two pieces meet. Sweep across the
    // boundary and check the result never jumps.
    // Seed `previous` from the same backdrop as the sweep, or the first
    // comparison is against an unrelated value.
    float previous = blend_channel(BlendMode::SoftLight, 0.5f, 0.0f);
    for (int i = 0; i <= 2000; ++i) {
        const float s = static_cast<float>(i) / 2000.0f;
        const float v = blend_channel(BlendMode::SoftLight, 0.5f, s);
        CHECK_FINITE(v);
        CHECK_MSG(std::fabs(v - previous) < 0.01f, "soft light is discontinuous in s");
        previous = v;
    }

    // The same sweep across several backdrops, including the 0.25 boundary
    // where the spec's two `d` branches meet.
    for (float b : {0.0f, 0.1f, 0.25f, 0.4f, 0.75f, 1.0f}) {
        float prev = blend_channel(BlendMode::SoftLight, b, 0.0f);
        for (int i = 1; i <= 1000; ++i) {
            const float s = static_cast<float>(i) / 1000.0f;
            const float v = blend_channel(BlendMode::SoftLight, b, s);
            CHECK_MSG(std::fabs(v - prev) < 0.02f, "soft light is discontinuous in s");
            prev = v;
        }
    }
}

MGTK_TEST(blend_soft_light_survives_hdr_input) {
    // Soft light is defined on [0,1] and its piecewise formula diverges well
    // outside it. In a 32-bit float project the effects routinely feed these
    // modes values above 1, so the mode must clamp rather than explode.
    for (float b : {-4.0f, -1.0f, 0.0f, 0.5f, 1.0f, 3.0f, 100.0f}) {
        for (float s : {-4.0f, -1.0f, 0.0f, 0.5f, 1.0f, 3.0f, 100.0f}) {
            const float v = blend_channel(BlendMode::SoftLight, b, s);
            CHECK_FINITE(v);
            CHECK_MSG(v >= -0.001f && v <= 1.001f,
                      "soft light left the unit range on HDR input");
        }
    }
}

MGTK_TEST(blend_all_modes_are_finite_and_in_range) {
    // Including out-of-range inputs: the effects routinely feed these modes
    // values above 1 from an additive glow, and a mode that explodes on HDR
    // input would be a crash in production.
    const float probes[] = {-2.0f, -0.5f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 4.0f};

    for (int m = 0; m < blend_mode_count(); ++m) {
        const auto mode = static_cast<BlendMode>(m);
        for (float a : probes) {
            for (float b : probes) {
                const float v = blend_channel(mode, a, b);
                CHECK_FINITE(v);

                // Every mode is a polynomial in its inputs, so on this probe
                // range each one is bounded by a modest number. Multiply(4,4)
                // is 16, Exclusion(-2,4) is 24, Divide(4,0.25) is 16. The
                // bound below is far above all of those and far below what a
                // mistranscribed formula (or a division by a value that should
                // have been clamped) would produce -- soft light, before the
                // input clamp, hit -660 on exactly this range.
                CHECK_MSG(std::fabs(v) <= 32.0f,
                          std::string(blend_mode_name(mode)) +
                              " produced an out-of-range result");
            }
        }
    }
}

MGTK_TEST(blend_pixel_preserves_source_alpha) {
    for (int m = 0; m < blend_mode_count(); ++m) {
        const auto mode = static_cast<BlendMode>(m);
        const Float4 b{0.3f, 0.3f, 0.3f, 0.2f};
        const Float4 s{0.7f, 0.7f, 0.7f, 0.65f};
        const Float4 out = blend_pixel(mode, b, s);
        CHECK_NEAR(out.a, s.a, 0.0f);
    }
}

MGTK_TEST(blend_composite_over_straight_alpha) {
    const Float4 under{1.0f, 0.0f, 0.0f, 1.0f};
    const Float4 over{0.0f, 0.0f, 1.0f, 1.0f};

    // An opaque source completely replaces the backdrop.
    const Float4 opaque = composite_over(under, over);
    CHECK_NEAR(opaque.b, 1.0f, 1e-5f);
    CHECK_NEAR(opaque.r, 0.0f, 1e-5f);
    CHECK_NEAR(opaque.a, 1.0f, 1e-5f);

    // A fully transparent source changes nothing.
    const Float4 transparent = composite_over(under, Float4{0.0f, 1.0f, 0.0f, 0.0f});
    CHECK_NEAR(transparent.r, 1.0f, 1e-5f);
    CHECK_NEAR(transparent.g, 0.0f, 1e-5f);
    CHECK_NEAR(transparent.a, 1.0f, 1e-5f);

    // Halfway: the result is half of each.
    const Float4 half = composite_over(under, Float4{0.0f, 0.0f, 1.0f, 0.5f});
    CHECK_NEAR(half.r, 0.5f, 1e-5f);
    CHECK_NEAR(half.b, 0.5f, 1e-5f);
    CHECK_NEAR(half.a, 1.0f, 1e-5f);
}

MGTK_TEST(blend_composite_over_transparent_backdrop) {
    // Compositing onto nothing must return the source, unmodified.
    const Float4 over{0.4f, 0.6f, 0.8f, 0.5f};
    const Float4 out = composite_over(Float4{}, over);
    CHECK_NEAR(out.r, over.r, 1e-5f);
    CHECK_NEAR(out.g, over.g, 1e-5f);
    CHECK_NEAR(out.b, over.b, 1e-5f);
    CHECK_NEAR(out.a, over.a, 1e-5f);

    // Two fully transparent pixels must not produce a divide by zero.
    const Float4 nothing = composite_over(Float4{}, Float4{});
    CHECK_FINITE(nothing.r);
    CHECK_NEAR(nothing.a, 0.0f, 0.0f);
}

MGTK_TEST(blend_composite_with_opacity) {
    const Float4 under{1.0f, 1.0f, 1.0f, 1.0f};
    const Float4 over{0.0f, 0.0f, 0.0f, 1.0f};

    // Opacity 0 must be a no-op, opacity 1 must fully replace.
    const Float4 none = composite_over(under, over, 0.0f);
    CHECK_NEAR(none.r, 1.0f, 1e-5f);

    const Float4 full = composite_over(under, over, 1.0f);
    CHECK_NEAR(full.r, 0.0f, 1e-5f);

    const Float4 half = composite_over(under, over, 0.5f);
    CHECK_NEAR(half.r, 0.5f, 1e-5f);
}

MGTK_TEST(blend_mode_enum_and_names_agree) {
    CHECK_EQ(blend_mode_count(), static_cast<int>(BlendMode::Count));
    CHECK_EQ(blend_mode_count(), 22);

    // Every mode must have a non-empty display name and round-trip through the
    // popup index helpers that the AE parameter setup relies on.
    for (int m = 0; m < blend_mode_count(); ++m) {
        const auto mode = static_cast<BlendMode>(m);
        const char* name = blend_mode_name(mode);
        REQUIRE(name != nullptr);
        CHECK(name[0] != '\0');
        CHECK_EQ(static_cast<int>(blend_mode_from_index(m)), m);
    }

    // Out-of-range indices clamp rather than reading off the end of the table.
    CHECK_EQ(static_cast<int>(blend_mode_from_index(-1)),
             static_cast<int>(BlendMode::Normal));
    CHECK_EQ(static_cast<int>(blend_mode_from_index(999)),
             static_cast<int>(BlendMode::Normal));
}

MGTK_TEST(blend_wrap_mode_enum_and_names_agree) {
    CHECK_EQ(wrap_mode_count(), static_cast<int>(WrapMode::Count));
    CHECK_EQ(wrap_mode_count(), 4);

    for (int m = 0; m < wrap_mode_count(); ++m) {
        const auto mode = static_cast<WrapMode>(m);
        const char* name = wrap_mode_name(mode);
        REQUIRE(name != nullptr);
        CHECK(name[0] != '\0');
        CHECK_EQ(static_cast<int>(wrap_mode_from_index(m)), m);
    }

    CHECK_EQ(static_cast<int>(wrap_mode_from_index(-5)),
             static_cast<int>(WrapMode::Clamp));
    CHECK_EQ(static_cast<int>(wrap_mode_from_index(42)),
             static_cast<int>(WrapMode::Clamp));
}
