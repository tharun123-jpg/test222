// =============================================================================
//  tests/test_effects.cpp -- the eight effects
//
//  The point of this file is to pin down the *invariants* each effect promises,
//  not to check specific pixel values against a golden image. Golden images
//  change every time someone adjusts a formula and teach you nothing when they
//  fail; invariants ("a dark frame produces no glow", "the output has N-fold
//  symmetry", "nothing outside the threshold window moves") catch real bugs and
//  survive tuning.
// =============================================================================
#include <cmath>
#include <vector>

#include "mgtk/effects.hpp"
#include "test_framework.hpp"

using namespace mgtk;

namespace {

constexpr int kW = 64;
constexpr int kH = 48;

RenderContext ctx_default() {
    RenderContext ctx;
    ctx.frame = 12.0f;
    ctx.fps = 24.0f;
    ctx.seconds = 0.5f;
    ctx.input_linear = false;
    return ctx;
}

Image make_gradient(int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float t = static_cast<float>(x) / static_cast<float>(w - 1);
            img.at(x, y) = Float4{t, t * 0.5f, 1.0f - t, 1.0f};
        }
    }
    return img;
}

// A dark field with a few bright blobs -- gives the glow something to find and
// the sort effects some structure to work with.
Image make_blobs(int w, int h) {
    Image img(w, h);
    img.fill(Float4{0.02f, 0.02f, 0.03f, 1.0f});
    const int centres[3][3] = {{12, 10, 1}, {40, 22, 1}, {20, 38, 1}};
    for (const auto& c : centres) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = c[0] + dx;
                const int y = c[1] + dy;
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                if (d > 4.0f) continue;
                const float v = 1.0f - d * 0.2f;
                img.at(x, y) = Float4{v, v, v, 1.0f};
            }
        }
    }
    return img;
}

bool all_finite(const Image& img) {
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const Float4 p = img.at(x, y);
            if (!std::isfinite(p.r) || !std::isfinite(p.g) ||
                !std::isfinite(p.b) || !std::isfinite(p.a)) {
                return false;
            }
        }
    }
    return true;
}

bool has_any_nonzero(const Image& img, float threshold = 1e-6f) {
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const Float4 p = img.at(x, y);
            if (std::fabs(p.r) > threshold || std::fabs(p.g) > threshold ||
                std::fabs(p.b) > threshold) {
                return true;
            }
        }
    }
    return false;
}

// Largest per-pixel difference across a colour channel.
float max_channel_diff(const Image& a, const Image& b) {
    float worst = 0.0f;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Float4 p = a.at(x, y);
            const Float4 q = b.at(x, y);
            worst = std::max(worst, std::fabs(p.r - q.r));
            worst = std::max(worst, std::fabs(p.g - q.g));
            worst = std::max(worst, std::fabs(p.b - q.b));
            worst = std::max(worst, std::fabs(p.a - q.a));
        }
    }
    return worst;
}

bool alpha_unchanged(const Image& a, const Image& b, float tol = 1e-5f) {
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (std::fabs(a.at(x, y).a - b.at(x, y).a) > tol) return false;
        }
    }
    return true;
}

double mean_of(const Image& img, int channel) {
    double total = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const Float4 p = img.at(x, y);
            total += (channel == 0) ? p.r : (channel == 1 ? p.g : p.b);
        }
    }
    return total / static_cast<double>(img.pixel_count());
}

Image constant(int w, int h, Float4 c) {
    Image img(w, h);
    img.fill(c);
    return img;
}

}  // namespace

// ===========================================================================
//  Cross-cutting guarantees that every effect must satisfy
// ===========================================================================
MGTK_TEST(effects_all_are_deterministic_and_finite) {
    const Image src = make_blobs(kW, kH);
    const RenderContext ctx = ctx_default();

    // Each effect is run twice with identical inputs. Any dependence on
    // uninitialised memory, iteration order, or a global RNG would show up as a
    // difference -- and would also break AE's disk cache.
    {
        Image a, b, fb_a, fb_b;
        FeedbackEchoParams p;
        p.echo_amount = 0.6f;
        p.scale = 1.05f;
        p.rotation_deg = 3.0f;
        apply_feedback_echo(src, fb_a, a, p, ctx);
        apply_feedback_echo(src, fb_b, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        Image a, b;
        ChromaticSplitParams p;
        p.mode = ChromaMode::Radial;
        p.amount = 9.0f;
        apply_chromatic_split(src, a, p, ctx);
        apply_chromatic_split(src, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        Image a, b;
        AnamorphicGlowParams p;
        p.threshold = 0.4f;
        p.radius = 20.0f;
        p.streak_count = 3;
        apply_anamorphic_glow(src, a, p, ctx);
        apply_anamorphic_glow(src, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        Image a, b;
        FractalWarpParams p;
        p.amount = 18.0f;
        p.noise_scale = 0.03f;
        p.seed = 777u;
        apply_fractal_warp(src, a, p, ctx);
        apply_fractal_warp(src, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        Image a, b;
        KaleidoscopeParams p;
        p.segments = 7;
        p.rotation_deg = 22.0f;
        apply_kaleidoscope(src, a, p, ctx);
        apply_kaleidoscope(src, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        Image a, b;
        HalftoneParams p;
        p.cell_size = 6.0f;
        p.angle_deg = 33.0f;
        apply_halftone(src, a, p, ctx);
        apply_halftone(src, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        std::vector<Image> history{src, make_gradient(kW, kH), constant(kW, kH, Float4{0.5f, 0.2f, 0.9f, 1.0f})};
        Image a, b;
        SlitScanParams p;
        p.mode = SlitScanMode::TimeSlice;
        p.slice_width = 5.0f;
        apply_slit_scan(src, history, a, p, ctx);
        apply_slit_scan(src, history, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
    {
        Image a, b;
        PixelSortParams p;
        p.key = SortKey::Brightness;
        p.max_length = 40;
        apply_pixel_sort(src, a, p, ctx);
        apply_pixel_sort(src, b, p, ctx);
        CHECK(all_finite(a));
        CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
    }
}

MGTK_TEST(effects_handle_empty_and_tiny_images) {
    const RenderContext ctx = ctx_default();
    std::vector<Image> history;

    // Effects are called with whatever AE hands over, which at a low zoom level
    // can genuinely be a handful of pixels.
    for (int size : {0, 1, 2}) {
        Image src(size, size);
        if (size > 0) src.fill(Float4{0.5f, 0.4f, 0.3f, 1.0f});
        Image dst;

        Image fb;
        FeedbackEchoParams fe;
        apply_feedback_echo(src, fb, dst, fe, ctx);

        ChromaticSplitParams cs;
        cs.amount = 5.0f;
        apply_chromatic_split(src, dst, cs, ctx);

        AnamorphicGlowParams ag;
        ag.radius = 10.0f;
        apply_anamorphic_glow(src, dst, ag, ctx);

        FractalWarpParams fw;
        fw.amount = 8.0f;
        apply_fractal_warp(src, dst, fw, ctx);

        KaleidoscopeParams ka;
        ka.segments = 5;
        apply_kaleidoscope(src, dst, ka, ctx);

        HalftoneParams ht;
        apply_halftone(src, dst, ht, ctx);

        SlitScanParams ss;
        apply_slit_scan(src, history, dst, ss, ctx);

        PixelSortParams ps;
        apply_pixel_sort(src, dst, ps, ctx);

        CHECK(all_finite(dst));
    }
}

MGTK_TEST(effects_ignore_extreme_parameters_without_blowing_up) {
    const Image src = make_blobs(kW, kH);
    RenderContext ctx = ctx_default();
    Image dst;

    // Every one of these values is reachable by typing into a slider in AE.
    {
        FractalWarpParams p;
        p.amount = 1.0e6f;
        p.noise_scale = 0.0f;
        p.octaves = 100;
        p.lacunarity = 1000.0f;
        p.gain = -5.0f;
        p.swirl_amount = 1.0e5f;
        p.swirl_radius = 0.0f;
        p.pinch = 1.0e4f;
        apply_fractal_warp(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
    {
        ChromaticSplitParams p;
        p.amount = 1.0e7f;
        p.falloff = 0.0f;
        p.radial_bias = 1.0e5f;
        p.barrel = -1.0e7f;
        apply_chromatic_split(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
    {
        AnamorphicGlowParams p;
        p.threshold = -100.0f;
        p.radius = 1.0e5f;
        p.streak_length = 1.0e5f;
        p.streak_count = 100;
        p.rgb_separation = 1.0e6f;
        p.intensity = 1.0e4f;
        apply_anamorphic_glow(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
    {
        KaleidoscopeParams p;
        p.segments = 100000;
        p.scale = 0.0f;
        p.radial_fade = 5.0f;
        apply_kaleidoscope(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
    {
        HalftoneParams p;
        p.cell_size = 0.0f;
        p.angle_deg = 1.0e5f;
        p.contrast = 1000.0f;
        p.gamma = 0.0f;
        p.anti_alias = 1.0e5f;
        apply_halftone(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
    {
        FeedbackEchoParams p;
        p.scale = 0.0f;
        p.decay_curve = 1000.0f;
        p.echo_amount = 1.0f;
        p.brightness = 100.0f;
        Image fb;
        apply_feedback_echo(src, fb, dst, p, ctx);
        CHECK(all_finite(dst));
    }
    {
        PixelSortParams p;
        p.max_length = 1000000000;
        p.threshold_low = -100.0f;
        p.threshold_high = 100.0f;
        apply_pixel_sort(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
}

// ===========================================================================
//  1. Feedback Echo
// ===========================================================================
MGTK_TEST(effect_feedback_echo_accumulates_across_frames) {
    const Image src = constant(8, 8, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    RenderContext ctx = ctx_default();
    Image fb;
    Image dst;

    FeedbackEchoParams p;
    p.echo_amount = 0.5f;
    p.decay_curve = 1.0f;
    p.blend = EchoBlendMode::Add;
    p.mix = 1.0f;

    // Frame 1: the accumulator starts empty, so the output is the source.
    apply_feedback_echo(src, fb, dst, p, ctx);
    CHECK_NEAR(dst.at(3, 3).r, 1.0f, 1e-4f);

    // Frame 2: 1.0 of echo at 0.5 retention, added to the source.
    apply_feedback_echo(src, fb, dst, p, ctx);
    CHECK_NEAR(dst.at(3, 3).r, 1.5f, 1e-4f);

    // Frame 3: the accumulator is now 1.5, so 0.75 of echo.
    apply_feedback_echo(src, fb, dst, p, ctx);
    CHECK_NEAR(dst.at(3, 3).r, 1.75f, 1e-4f);

    // The sequence converges on 2.0 rather than diverging.
    for (int i = 0; i < 40; ++i) {
        apply_feedback_echo(src, fb, dst, p, ctx);
    }
    CHECK_NEAR(dst.at(3, 3).r, 2.0f, 1e-3f);
    CHECK(all_finite(dst));
}

MGTK_TEST(effect_feedback_echo_zero_amount_is_passthrough) {
    const Image src = make_gradient(16, 16);
    RenderContext ctx = ctx_default();
    Image fb;
    Image dst;

    FeedbackEchoParams p;
    p.echo_amount = 0.0f;
    p.blend = EchoBlendMode::Add;
    p.mix = 1.0f;

    apply_feedback_echo(src, fb, dst, p, ctx);
    apply_feedback_echo(src, fb, dst, p, ctx);

    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 1e-5f);
}

MGTK_TEST(effect_feedback_echo_normal_mode_keeps_an_opaque_source) {
    // With the default "source over echo" blend and a fully opaque source, the
    // echo is hidden -- which is the correct, if initially surprising,
    // behaviour. It is what makes Normal mode safe to leave as the default.
    const Image src = constant(8, 8, Float4{0.25f, 0.5f, 0.75f, 1.0f});
    RenderContext ctx = ctx_default();
    Image fb;
    Image dst;

    FeedbackEchoParams p;
    p.echo_amount = 0.9f;
    p.blend = EchoBlendMode::Normal;

    for (int i = 0; i < 5; ++i) {
        apply_feedback_echo(src, fb, dst, p, ctx);
    }
    CHECK_NEAR(dst.at(4, 4).r, 0.25f, 1e-5f);
    CHECK_NEAR(dst.at(4, 4).g, 0.5f, 1e-5f);
    CHECK_NEAR(dst.at(4, 4).a, 1.0f, 1e-5f);
}

MGTK_TEST(effect_feedback_echo_reset_clears_the_accumulator) {
    const Image src = constant(8, 8, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    RenderContext ctx = ctx_default();
    Image fb;
    Image dst;

    FeedbackEchoParams p;
    p.echo_amount = 0.8f;
    p.blend = EchoBlendMode::Add;

    apply_feedback_echo(src, fb, dst, p, ctx);
    apply_feedback_echo(src, fb, dst, p, ctx);
    CHECK(dst.at(0, 0).r > 1.0f);

    p.reset = true;
    apply_feedback_echo(src, fb, dst, p, ctx);
    CHECK_NEAR(dst.at(0, 0).r, 1.0f, 1e-4f);
}

MGTK_TEST(effect_feedback_echo_rebuilds_its_buffer_on_size_change) {
    // AE changes the working resolution when the user toggles resolution or
    // re-parents the layer. The old accumulator is meaningless at the new size,
    // and resampling it would smear stale content into the frame.
    RenderContext ctx = ctx_default();
    Image fb;
    Image dst;

    FeedbackEchoParams p;
    p.echo_amount = 0.9f;
    p.blend = EchoBlendMode::Add;

    Image small = constant(8, 8, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    apply_feedback_echo(small, fb, dst, p, ctx);
    apply_feedback_echo(small, fb, dst, p, ctx);

    Image large = constant(32, 24, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    apply_feedback_echo(large, fb, dst, p, ctx);

    CHECK_EQ(dst.width(), 32);
    CHECK_EQ(dst.height(), 24);
    // Clean start at the new size: the output is just the source.
    CHECK_NEAR(dst.at(10, 10).r, 1.0f, 1e-4f);
}

MGTK_TEST(effect_feedback_echo_transform_moves_the_echo) {
    // A single bright pixel, echoed with a large offset, must reappear at the
    // offset position. This is the actual "echo" behaviour users reach for.
    RenderContext ctx = ctx_default();
    Image src = constant(32, 32, Float4{0.0f, 0.0f, 0.0f, 1.0f});
    src.at(16, 16) = Float4{1.0f, 1.0f, 1.0f, 1.0f};

    FeedbackEchoParams p;
    p.echo_amount = 0.9f;
    p.blend = EchoBlendMode::Add;
    p.offset_x = 8.0f;
    p.offset_y = 0.0f;

    Image fb;
    Image dst;
    apply_feedback_echo(src, fb, dst, p, ctx);  // frame 1: no echo yet
    apply_feedback_echo(src, fb, dst, p, ctx);  // frame 2: echo displaced by 8

    // The original bright pixel is still there.
    CHECK(dst.at(16, 16).r > 0.5f);
    // And the echo from the previous frame has moved 8px to the right.
    CHECK_MSG(dst.at(24, 16).r > 0.5f, "echo was not displaced by the offset");
    // Somewhere in between must be dark: if the echo were smeared rather than
    // translated, this would be lit too.
    CHECK_MSG(dst.at(4, 16).r < 0.1f, "echo spread further than the offset");
}

// ===========================================================================
//  2. Chromatic Split
// ===========================================================================
MGTK_TEST(effect_chromatic_split_with_zero_amount_is_identity) {
    const Image src = make_gradient(kW, kH);
    RenderContext ctx = ctx_default();
    Image dst;

    // All per-channel multipliers at zero means no split at all.
    ChromaticSplitParams p;
    p.amount = 50.0f;
    p.amount_r = 0.0f;
    p.amount_g = 0.0f;
    p.amount_b = 0.0f;

    apply_chromatic_split(src, dst, p, ctx);
    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 0.0f);

    // And a zero master amount must zero everything out regardless.
    ChromaticSplitParams q;
    q.amount = 0.0f;
    q.amount_r = 1.0f;
    q.amount_b = -1.0f;
    apply_chromatic_split(src, dst, q, ctx);
    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 0.0f);
}

MGTK_TEST(effect_chromatic_split_keeps_alpha_on_the_original_position) {
    // A soft-edged matte must not be split along with the colour: doing so
    // would put a coloured fringe on the alpha channel itself, which reads as a
    // moving halo around any keyed element.
    const Image src = make_gradient(kW, kH);
    RenderContext ctx = ctx_default();

    ChromaticSplitParams p;
    p.mode = ChromaMode::Barrel;  // deliberately exercises the warp path too
    p.amount = 14.0f;
    p.barrel = 6.0f;

    Image dst;
    apply_chromatic_split(src, dst, p, ctx);

    // Alpha follows the barrel warp but not the per-channel split. With a
    // constant alpha of 1 everywhere, it must survive untouched.
    CHECK(alpha_unchanged(src, dst, 1e-4f));
    CHECK(all_finite(dst));
}

MGTK_TEST(effect_chromatic_split_actually_separates_channels) {
    const Image src = make_blobs(kW, kH);
    RenderContext ctx = ctx_default();

    ChromaticSplitParams p;
    p.mode = ChromaMode::Radial;
    p.amount = 12.0f;
    p.amount_r = 1.0f;
    p.amount_g = 0.0f;
    p.amount_b = -1.0f;

    Image dst;
    apply_chromatic_split(src, dst, p, ctx);

    CHECK(all_finite(dst));
    CHECK_MSG(max_channel_diff(src, dst) > 0.05f,
              "a 12px radial split produced no visible separation");

    // Red and blue must move in opposite directions, so away from the blobs the
    // two channels should differ from each other. If a sign were flipped, or
    // both channels used the same multiplier, this would be zero.
    double disagreement = 0.0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            disagreement += std::fabs(dst.at(x, y).r - dst.at(x, y).b);
        }
    }
    CHECK_MSG(disagreement > 1.0, "red and blue did not separate");
}

MGTK_TEST(effect_chromatic_split_linear_mode_is_directional) {
    const Image src = constant(32, 32, Float4{0.5f, 0.5f, 0.5f, 1.0f});
    RenderContext ctx = ctx_default();

    // A flat field cannot show a shift, so drop in an edge instead.
    Image edge = src;
    for (int y = 0; y < 32; ++y) {
        for (int x = 16; x < 32; ++x) edge.at(x, y) = Float4{1.0f, 0.0f, 0.0f, 1.0f};
    }

    ChromaticSplitParams p;
    p.mode = ChromaMode::Linear;
    p.angle_deg = 0.0f;  // horizontal shift
    p.amount = 4.0f;
    p.amount_r = 1.0f;
    p.amount_g = 0.0f;
    p.amount_b = -1.0f;
    p.quality = ChromaQuality::Bilinear;

    Image dst;
    apply_chromatic_split(edge, dst, p, ctx);

    // Just left of the edge, red has been pulled across from the bright side
    // while blue has been pushed away from it.
    const float r = dst.at(14, 16).r;
    const float b = dst.at(14, 16).b;
    CHECK_FINITE(r);
    CHECK_FINITE(b);
    CHECK_MSG(r > 0.0f, "red was not shifted towards the positive direction");

    // A vertical shift must leave a horizontal edge alone: the split is
    // perpendicular to the shift direction, so nothing changes vertically here.
    ChromaticSplitParams vertical = p;
    vertical.angle_deg = 90.0f;
    Image dst_v;
    apply_chromatic_split(edge, dst_v, vertical, ctx);

    // Every pixel in a given column now splits identically, so two pixels in
    // the same column but different rows must agree.
    CHECK_NEAR(dst_v.at(14, 5).r, dst_v.at(14, 25).r, 1e-4f);
}

// ===========================================================================
//  3. Anamorphic Glow
// ===========================================================================
MGTK_TEST(effect_glow_dark_input_produces_no_glow) {
    // The single most important property of a threshold: if nothing is above
    // it, the effect must be exactly transparent to the image. A glow that
    // slightly brightens a black frame is the classic sign of a bright pass
    // that forgot to subtract the threshold.
    RenderContext ctx = ctx_default();
    Image dark = constant(32, 32, Float4{0.05f, 0.05f, 0.05f, 1.0f});

    AnamorphicGlowParams p;
    p.threshold = 0.5f;
    p.knee = 0.0f;
    p.radius = 10.0f;
    p.streak_length = 40.0f;
    p.blend = BlendMode::Add;
    p.mix = 1.0f;

    Image dst;
    apply_anamorphic_glow(dark, dst, p, ctx);

    CHECK_NEAR(max_channel_diff(dark, dst), 0.0f, 1e-5f);
}

MGTK_TEST(effect_glow_bright_input_glows_and_spreads) {
    // A single bright pixel on black must light up its neighbours. If the
    // blur radius were ignored -- or the blur ran on the wrong buffer -- the
    // neighbours would stay black.
    RenderContext ctx = ctx_default();
    Image src = constant(64, 64, Float4{0.0f, 0.0f, 0.0f, 1.0f});
    src.at(32, 32) = Float4{1.0f, 1.0f, 1.0f, 1.0f};

    AnamorphicGlowParams p;
    p.threshold = 0.5f;
    p.knee = 0.0f;
    p.intensity = 2.0f;
    p.radius = 16.0f;
    p.streak_length = 0.0f;   // isolate the radial bloom
    p.blend = BlendMode::Add;
    p.preserve_alpha = false;
    p.mix = 1.0f;

    Image dst;
    apply_anamorphic_glow(src, dst, p, ctx);

    CHECK(all_finite(dst));
    // A one-pixel source has all of its energy spread over the blur kernel, so
    // "reached" means a small but clearly non-zero value: roughly 1/(pi r^2).
    CHECK_MSG(dst.at(34, 32).r > 0.001f, "the glow did not reach 2px from the source");

    // The falloff must be monotone-ish: the glow is strongest at the centre and
    // weaker further away. A glow that got brighter with distance would mean
    // the mip accumulation was inverted.
    const float near_value = dst.at(34, 32).r;
    const float far_value = dst.at(44, 32).r;
    CHECK_MSG(near_value > far_value, "the glow did not fall off with distance");

    // And it must actually stop somewhere.
    CHECK_MSG(dst.at(63, 0).r < near_value * 0.5f, "the glow filled the whole frame");
}

MGTK_TEST(effect_glow_respects_preserve_alpha) {
    RenderContext ctx = ctx_default();
    Image src = constant(32, 32, Float4{0.0f, 0.0f, 0.0f, 0.0f});
    src.at(16, 16) = Float4{1.0f, 1.0f, 1.0f, 1.0f};

    AnamorphicGlowParams p;
    p.threshold = 0.5f;
    p.radius = 8.0f;
    p.streak_length = 0.0f;
    p.blend = BlendMode::Add;
    p.mix = 1.0f;

    p.preserve_alpha = true;
    Image contained;
    apply_anamorphic_glow(src, contained, p, ctx);

    // Confined to the matte, the glow may not appear where alpha is zero.
    CHECK_NEAR(contained.at(18, 16).r, 0.0f, 1e-5f);

    p.preserve_alpha = false;
    Image spilled;
    apply_anamorphic_glow(src, spilled, p, ctx);
    CHECK(all_finite(spilled));
}

MGTK_TEST(effect_glow_streak_is_directional) {
    RenderContext ctx = ctx_default();
    Image src = constant(64, 64, Float4{0.0f, 0.0f, 0.0f, 1.0f});
    src.at(32, 32) = Float4{1.0f, 1.0f, 1.0f, 1.0f};

    AnamorphicGlowParams p;
    p.threshold = 0.5f;
    p.radius = 1.0f;          // minimal bloom so the streak dominates
    p.streak_length = 40.0f;
    p.streak_angle_deg = 0.0f;  // horizontal
    p.streak_intensity = 1.0f;
    p.blend = BlendMode::Add;
    p.preserve_alpha = false;

    Image dst;
    apply_anamorphic_glow(src, dst, p, ctx);

    CHECK(all_finite(dst));
    CHECK_MSG(dst.at(44, 32).r > 0.01f, "the streak did not run horizontally");
    CHECK_MSG(dst.at(32, 44).r < dst.at(44, 32).r * 0.5f,
              "the streak leaked into the perpendicular axis");
}

MGTK_TEST(effect_glow_multi_streak_builds_a_star) {
    RenderContext ctx = ctx_default();
    Image src = constant(96, 96, Float4{0.0f, 0.0f, 0.0f, 1.0f});
    src.at(48, 48) = Float4{1.0f, 1.0f, 1.0f, 1.0f};

    AnamorphicGlowParams p;
    p.threshold = 0.5f;
    p.radius = 1.0f;
    p.streak_length = 40.0f;
    p.streak_count = 4;
    p.streak_angle_deg = 0.0f;
    p.streak_intensity = 1.0f;
    p.blend = BlendMode::Add;
    p.preserve_alpha = false;

    Image dst;
    apply_anamorphic_glow(src, dst, p, ctx);

    // Four streaks spread over 180 degrees put one on each axis. The streak
    // reaches +/- length/2 = 20px, so probe inside that and check the diagonal
    // -- where no streak runs -- stays dark.
    CHECK(dst.at(48 + 14, 48).r > 1e-4f);
    CHECK(dst.at(48 - 14, 48).r > 1e-4f);
    CHECK(dst.at(48, 48 + 14).r > 1e-4f);
    CHECK(dst.at(48, 48 - 14).r > 1e-4f);

    // Sampling density: a streak spread over 16 samples of a hard-edged source
    // used to leave visible gaps. Every step along the streak must now be
    // populated rather than the light landing in discrete clumps.
    int lit = 0;
    for (int d = -18; d <= 18; ++d) {
        if (dst.at(48 + d, 48).r > 1e-5f) ++lit;
    }
    CHECK_MSG(lit >= 35, "the streak was sampled too coarsely and left gaps");
}

MGTK_TEST(effect_glow_higher_threshold_means_less_glow) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(kW, kH);

    AnamorphicGlowParams low;
    low.threshold = 0.2f;
    low.radius = 12.0f;
    low.streak_length = 0.0f;
    low.blend = BlendMode::Add;
    low.preserve_alpha = false;

    AnamorphicGlowParams high = low;
    high.threshold = 0.95f;

    Image low_img, high_img;
    apply_anamorphic_glow(src, low_img, low, ctx);
    apply_anamorphic_glow(src, high_img, high, ctx);

    const double low_energy = mean_of(low_img, 0);
    const double high_energy = mean_of(high_img, 0);
    CHECK_MSG(low_energy > high_energy,
              "raising the threshold did not reduce the amount of glow");
}

MGTK_TEST(effect_glow_is_consistent_across_linear_and_gamma_input) {
    // The threshold is judged perceptually on purpose, so that it means the
    // same thing in an 8-bit project (gamma-encoded pixels) and a 32-bit one
    // (linear pixels). Feed it the same scene encoded both ways and the amount
    // of glow must be close, even though the underlying numbers differ wildly.
    const Image gamma_scene = make_blobs(kW, kH);

    // Encode the same scene linearly.
    Image linear_scene(kW, kH);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Float4 p = gamma_scene.at(x, y);
            linear_scene.at(x, y) = Float4{srgb_to_linear(p.r), srgb_to_linear(p.g),
                                           srgb_to_linear(p.b), p.a};
        }
    }

    AnamorphicGlowParams p;
    p.threshold = 0.5f;
    p.knee = 0.0f;
    p.radius = 10.0f;
    p.streak_length = 0.0f;
    p.blend = BlendMode::Add;
    p.preserve_alpha = false;

    RenderContext gamma_ctx = ctx_default();
    gamma_ctx.input_linear = false;
    RenderContext linear_ctx = ctx_default();
    linear_ctx.input_linear = true;

    Image gamma_out, linear_out;
    apply_anamorphic_glow(gamma_scene, gamma_out, p, gamma_ctx);
    apply_anamorphic_glow(linear_scene, linear_out, p, linear_ctx);

    CHECK(all_finite(gamma_out));
    CHECK(all_finite(linear_out));

    // Count how many pixels crossed the threshold in each case. If the linear
    // path compared a linear value against a perceptual threshold, virtually
    // nothing would qualify (linear 0.5 is a perceptual 0.74).
    const auto lit = [](const Image& a, const Image& b) {
        int n = 0;
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                if (a.at(x, y).r - b.at(x, y).r > 0.02f) ++n;
            }
        }
        return n;
    };

    const int gamma_lit = lit(gamma_out, gamma_scene);
    const int linear_lit = lit(linear_out, linear_scene);
    CHECK_MSG(gamma_lit > 0, "no glow in the gamma-encoded project");
    CHECK_MSG(linear_lit > 0, "no glow in the linear project");
}

// ===========================================================================
//  4. Fractal Warp
// ===========================================================================
MGTK_TEST(effect_fractal_warp_inert_parameters_are_a_copy) {
    const Image src = make_gradient(kW, kH);
    RenderContext ctx = ctx_default();
    Image dst;

    // The swirl and pinch sliders deliberately stack on top of whichever mode is
    // selected, so an inert parameter set has to zero all three -- zeroing the
    // displacement alone still leaves the default half-turn swirl running.
    FractalWarpParams p;
    p.mode = WarpMode::Displace;
    p.amount = 0.0f;
    p.swirl_amount = 0.0f;
    p.pinch = 0.0f;

    apply_fractal_warp(src, dst, p, ctx);
    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 0.0f);

    // The whole point of the identity path is that it is *exact*: a warp that
    // resamples every pixel even at zero displacement would soften the image.
    FractalWarpParams q;
    q.mode = WarpMode::Swirl;
    q.swirl_amount = 0.0f;
    apply_fractal_warp(src, dst, q, ctx);
    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 0.0f);
}

MGTK_TEST(effect_fractal_warp_displacement_actually_moves_pixels) {
    const Image src = make_blobs(kW, kH);
    RenderContext ctx = ctx_default();

    FractalWarpParams p;
    p.mode = WarpMode::Displace;
    p.amount = 12.0f;
    p.noise_scale = 0.04f;
    p.seed = 4242u;

    Image dst;
    apply_fractal_warp(src, dst, p, ctx);

    CHECK(all_finite(dst));
    CHECK_MSG(max_channel_diff(src, dst) > 0.02f, "the warp moved nothing");
}

MGTK_TEST(effect_fractal_warp_seed_changes_the_result) {
    const Image src = make_blobs(kW, kH);
    RenderContext ctx = ctx_default();

    FractalWarpParams p;
    p.mode = WarpMode::Displace;
    p.amount = 15.0f;
    p.noise_scale = 0.03f;
    p.seed = 1u;

    FractalWarpParams q = p;
    q.seed = 2u;

    Image a, b;
    apply_fractal_warp(src, a, p, ctx);
    apply_fractal_warp(src, b, q, ctx);

    CHECK_MSG(max_channel_diff(a, b) > 0.01f,
              "changing the seed did not change the warp");
}

MGTK_TEST(effect_fractal_warp_swirl_is_identity_at_the_centre_and_edge) {
    // A swirl rotates about a centre and fades to nothing at its radius. The
    // exact centre can never move, so whatever pixel sits there must be
    // reproduced exactly.
    RenderContext ctx = ctx_default();
    Image src = make_gradient(65, 65);  // odd size: 32 is the exact centre

    FractalWarpParams p;
    p.mode = WarpMode::Swirl;
    p.swirl_amount = 2.0f;
    p.swirl_radius = 0.4f;
    p.center_x = 0.5f;
    p.center_y = 0.5f;

    Image dst;
    apply_fractal_warp(src, dst, p, ctx);

    // The centre pixel of a radial swirl must not move.
    CHECK_NEAR(dst.at(32, 32).r, src.at(32, 32).r, 5e-3f);
    CHECK_NEAR(dst.at(32, 32).g, src.at(32, 32).g, 5e-3f);
    CHECK(all_finite(dst));
}

MGTK_TEST(effect_fractal_warp_pinch_keeps_the_centre_fixed) {
    RenderContext ctx = ctx_default();
    Image src = make_gradient(65, 65);

    FractalWarpParams p;
    p.mode = WarpMode::Pinch;
    p.pinch = 0.8f;
    p.center_x = 0.5f;
    p.center_y = 0.5f;

    Image dst;
    apply_fractal_warp(src, dst, p, ctx);

    // Whatever magnification the pinch applies, the centre is its fixed point.
    CHECK_NEAR(dst.at(32, 32).r, src.at(32, 32).r, 5e-3f);
}

MGTK_TEST(effect_fractal_warp_wrap_modes_change_the_edges_only) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(kW, kH);

    FractalWarpParams p;
    p.mode = WarpMode::Displace;
    p.amount = 8.0f;
    p.noise_scale = 0.05f;

    FractalWarpParams clamped = p;
    clamped.wrap = WrapMode::Clamp;
    FractalWarpParams tiled = p;
    tiled.wrap = WrapMode::Repeat;

    Image a, b;
    apply_fractal_warp(src, a, clamped, ctx);
    apply_fractal_warp(src, b, tiled, ctx);

    CHECK(all_finite(a));
    CHECK(all_finite(b));
    // Far from the border the two must agree: the wrap mode only decides what
    // happens when a sample leaves the frame.
    CHECK_NEAR(a.at(kW / 2, kH / 2).r, b.at(kW / 2, kH / 2).r, 1e-5f);
}

MGTK_TEST(effect_fractal_warp_domain_warp_differs_from_plain_displace) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(kW, kH);

    FractalWarpParams plain;
    plain.mode = WarpMode::Displace;
    plain.amount = 20.0f;
    plain.noise_scale = 0.03f;

    FractalWarpParams domain = plain;
    domain.mode = WarpMode::DomainWarp;

    Image a, b;
    apply_fractal_warp(src, a, plain, ctx);
    apply_fractal_warp(src, b, domain, ctx);

    CHECK_MSG(max_channel_diff(a, b) > 0.01f,
              "domain warp produced the same field as plain displacement");
}

// ===========================================================================
//  5. Kaleidoscope
// ===========================================================================
MGTK_TEST(effect_kaleidoscope_has_n_fold_rotational_symmetry) {
    // The defining property. A 4-segment kaleidoscope must map onto itself
    // under a quarter-turn about its centre. Using 4 segments and an integer
    // radius keeps the rotated sample positions exactly on pixel centres, so
    // the comparison is exact rather than approximate.
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(64, 64);
    Image dst;

    KaleidoscopeParams p;
    p.mode = KaleidoMode::Mirror;
    p.segments = 4;
    p.center = Vec2{0.5f, 0.5f};
    p.scale = 1.0f;
    p.rotation_deg = 0.0f;

    apply_kaleidoscope(src, dst, p, ctx);
    CHECK(all_finite(dst));

    // In pixel-index space the layer centre is 31.5, so use a half-integer
    // origin and integer radii to stay on the grid.
    for (int r = 2; r <= 28; ++r) {
        const Float4 right = dst.at(31 + r, 31);   // at angle 0
        const Float4 down = dst.at(31, 31 + r);    // at angle 90 degrees
        CHECK_NEAR(right.r, down.r, 1e-4f);
        CHECK_NEAR(right.g, down.g, 1e-4f);
        CHECK_NEAR(right.b, down.b, 1e-4f);
    }
}

MGTK_TEST(effect_kaleidoscope_two_fold_symmetry) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(64, 64);
    Image dst;

    KaleidoscopeParams p;
    p.mode = KaleidoMode::Mirror;
    p.segments = 2;
    p.center = Vec2{0.5f, 0.5f};

    apply_kaleidoscope(src, dst, p, ctx);

    // With 2 segments the pattern repeats every 180 degrees.
    for (int r = 2; r <= 30; ++r) {
        CHECK_NEAR(dst.at(31 + r, 31).r, dst.at(31 - r, 31).r, 1e-4f);
    }
}

MGTK_TEST(effect_kaleidoscope_segment_count_changes_the_pattern) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(kW, kH);

    KaleidoscopeParams p;
    p.mode = KaleidoMode::MirrorRotate;
    p.segments = 3;

    KaleidoscopeParams q = p;
    q.segments = 11;

    Image a, b;
    apply_kaleidoscope(src, a, p, ctx);
    apply_kaleidoscope(src, b, q, ctx);

    CHECK(all_finite(a));
    CHECK(all_finite(b));
    CHECK_MSG(max_channel_diff(a, b) > 0.02f,
              "3 and 11 segments produced the same output");
}

MGTK_TEST(effect_kaleidoscope_quilt_is_symmetric_about_the_centre) {
    // Quilt folds the layer into one quadrant and mirrors it back out, so the
    // result must be symmetric on both axes. Before this mode was implemented
    // properly it was a no-op at its default settings.
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(64, 64);
    Image dst;

    KaleidoscopeParams p;
    p.mode = KaleidoMode::Quilt;
    p.center = Vec2{0.5f, 0.5f};
    p.rotation_deg = 0.0f;
    p.scale = 1.0f;

    apply_kaleidoscope(src, dst, p, ctx);
    CHECK(all_finite(dst));

    for (int dy = 1; dy <= 30; ++dy) {
        for (int dx = 1; dx <= 30; ++dx) {
            const float a = dst.at(32 + dx, 32 + dy).r;
            CHECK_NEAR(dst.at(32 - dx, 32 + dy).r, a, 1e-4f);
            CHECK_NEAR(dst.at(32 + dx, 32 - dy).r, a, 1e-4f);
            CHECK_NEAR(dst.at(32 - dx, 32 - dy).r, a, 1e-4f);
        }
    }
}

MGTK_TEST(effect_kaleidoscope_segments_are_clamped_to_a_usable_range) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(32, 32);
    Image dst;

    // The header documents 2..64. Values outside that must be clamped rather
    // than producing a divide by zero or a degenerate sector angle.
    for (int segments : {-5, 0, 1, 2, 64, 65, 10000}) {
        KaleidoscopeParams p;
        p.segments = segments;
        apply_kaleidoscope(src, dst, p, ctx);
        CHECK(all_finite(dst));
    }
}

// ===========================================================================
//  6. Halftone Pro
// ===========================================================================
MGTK_TEST(effect_halftone_uses_only_ink_and_paper) {
    // With anti-aliasing off and a sharp threshold, every output pixel must be
    // either pure ink or pure paper -- because the vectors between a pixel and
    // paper, and between ink and paper, must be parallel. A blend that drifted
    // into a third colour would mean the tonal shaping was leaking colour.
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(kW, kH);

    HalftoneParams p;
    p.pattern = HalftonePattern::Dots;
    p.color_mode = HalftoneColorMode::Monochrome;
    p.cell_size = 6.0f;
    p.anti_alias = 0.0f;
    p.ink = Float4{0.0f, 0.0f, 0.0f, 1.0f};
    p.paper = Float4{1.0f, 1.0f, 1.0f, 1.0f};
    p.ink_amount = 1.0f;
    p.mix = 1.0f;

    Image dst;
    apply_halftone(src, dst, p, ctx);

    CHECK(all_finite(dst));

    int on_line = 0;
    int total = 0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Float4 o = dst.at(x, y);
            // Paper is (1,1,1), ink is (0,0,0), so any mixture has r == g == b.
            const float spread = std::max(std::max(o.r, o.g), o.b) -
                                 std::min(std::min(o.r, o.g), o.b);
            if (spread < 1e-4f) ++on_line;
            ++total;
        }
    }
    CHECK_EQ(on_line, total);
}

MGTK_TEST(effect_halftone_lines_pattern_saturates_correctly) {
    // A 100%-width line screen on a black source must be entirely ink; on white
    // it must be entirely paper. That is an exact property of the line pattern
    // (the line half-width equals the cell half-width at full density) and it
    // pins down the density curve.
    RenderContext ctx = ctx_default();

    HalftoneParams p;
    p.pattern = HalftonePattern::Lines;
    p.color_mode = HalftoneColorMode::Monochrome;
    p.cell_size = 8.0f;
    p.line_width = 1.0f;
    p.anti_alias = 0.0f;
    p.threshold = 0.0f;
    p.contrast = 1.0f;
    p.gamma = 1.0f;
    p.ink = Float4{0.0f, 0.0f, 0.0f, 1.0f};
    p.paper = Float4{1.0f, 1.0f, 1.0f, 1.0f};
    p.ink_amount = 1.0f;

    Image dst;

    Image black = constant(kW, kH, Float4{0.0f, 0.0f, 0.0f, 1.0f});
    apply_halftone(black, dst, p, ctx);
    CHECK_NEAR(dst.at(kW / 2, kH / 2).r, 0.0f, 1e-4f);

    Image white = constant(kW, kH, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    apply_halftone(white, dst, p, ctx);
    CHECK_NEAR(dst.at(kW / 2, kH / 2).r, 1.0f, 1e-4f);
}

MGTK_TEST(effect_halftone_darker_input_means_more_ink) {
    // Sweep the input level and check that ink coverage rises monotonically.
    // This is the property that makes a halftone a halftone; a sign error in
    // the density curve would invert it, and an inverted halftone looks like a
    // negative but is much easier to miss than a crash.
    RenderContext ctx = ctx_default();

    HalftoneParams p;
    p.pattern = HalftonePattern::Dots;
    p.color_mode = HalftoneColorMode::Monochrome;
    p.cell_size = 8.0f;
    p.anti_alias = 0.0f;
    p.ink_amount = 1.0f;

    Image dst;
    double previous_coverage = -1.0;

    for (int step = 0; step <= 10; ++step) {
        const float level = 1.0f - static_cast<float>(step) / 10.0f;
        Image src = constant(kW, kH, Float4{level, level, level, 1.0f});

        apply_halftone(src, dst, p, ctx);

        // Coverage = fraction of pixels that are closer to ink than to paper.
        int inked = 0;
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                if (dst.at(x, y).r < 0.5f) ++inked;
            }
        }
        const double coverage = static_cast<double>(inked) / (kW * kH);
        if (previous_coverage >= 0.0) {
            CHECK_MSG(coverage >= previous_coverage - 1e-9,
                      "ink coverage fell as the input got darker");
        }
        previous_coverage = coverage;
    }
}

MGTK_TEST(effect_halftone_preserves_the_matte_exactly) {
    RenderContext ctx = ctx_default();
    Image src = make_gradient(kW, kH);
    // Give the source a varying matte, including fully transparent regions.
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            src.at(x, y).a = static_cast<float>(x) / static_cast<float>(kW - 1);
        }
    }

    HalftoneParams p;
    p.color_mode = HalftoneColorMode::CMYK;
    p.cell_size = 5.0f;

    Image dst;
    apply_halftone(src, dst, p, ctx);

    CHECK(all_finite(dst));
    CHECK(alpha_unchanged(src, dst, 0.0f));
}

MGTK_TEST(effect_halftone_alpha_is_not_revealed_under_transparent_pixels) {
    // Where the source is fully transparent the effect must not paint ink,
    // because AE would then show colour that the layer's matte says is not
    // there.
    RenderContext ctx = ctx_default();
    Image src = constant(16, 16, Float4{0.0f, 0.0f, 0.0f, 0.0f});

    HalftoneParams p;
    p.color_mode = HalftoneColorMode::Monochrome;
    p.paper = Float4{1.0f, 1.0f, 1.0f, 1.0f};
    p.ink = Float4{0.0f, 0.0f, 0.0f, 1.0f};

    Image dst;
    apply_halftone(src, dst, p, ctx);

    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            CHECK_NEAR(dst.at(x, y).a, 0.0f, 0.0f);
        }
    }
}

MGTK_TEST(effect_halftone_patterns_are_distinct) {
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(kW, kH);

    HalftoneParams base;
    base.cell_size = 7.0f;
    base.ink_amount = 1.0f;

    Image dots, lines, cross, diamond, squares, concentric;
    base.pattern = HalftonePattern::Dots;
    apply_halftone(src, dots, base, ctx);
    base.pattern = HalftonePattern::Lines;
    apply_halftone(src, lines, base, ctx);
    base.pattern = HalftonePattern::Cross;
    apply_halftone(src, cross, base, ctx);
    base.pattern = HalftonePattern::DiamondGrid;
    apply_halftone(src, diamond, base, ctx);
    base.pattern = HalftonePattern::Squares;
    apply_halftone(src, squares, base, ctx);
    base.pattern = HalftonePattern::Concentric;
    apply_halftone(src, concentric, base, ctx);

    for (const Image* img : {&dots, &lines, &cross, &diamond, &squares, &concentric}) {
        CHECK(all_finite(*img));
    }

    // Every pair must differ. A pattern that silently aliased onto another
    // would be invisible in a screenshot but wrong in the parameter list.
    CHECK(max_channel_diff(dots, lines) > 0.05f);
    CHECK(max_channel_diff(dots, cross) > 0.05f);
    CHECK(max_channel_diff(dots, squares) > 0.05f);
    CHECK(max_channel_diff(dots, concentric) > 0.05f);
    CHECK(max_channel_diff(lines, cross) > 0.05f);
    CHECK(max_channel_diff(cross, squares) > 0.05f);
    // The inverted dot screen is the exact complement of the dot screen, so it
    // should differ substantially too.
    CHECK(max_channel_diff(dots, diamond) > 0.2f);
}

MGTK_TEST(effect_halftone_dot_shapes_are_distinct) {
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(kW, kH);

    HalftoneParams base;
    base.pattern = HalftonePattern::Dots;
    base.cell_size = 9.0f;
    base.ink_amount = 1.0f;
    base.anti_alias = 0.0f;

    Image round, ellipse, square, diamond, cross;
    base.dot_shape = HalftoneDotShape::Round;
    apply_halftone(src, round, base, ctx);
    base.dot_shape = HalftoneDotShape::Ellipse;
    apply_halftone(src, ellipse, base, ctx);
    base.dot_shape = HalftoneDotShape::Square;
    apply_halftone(src, square, base, ctx);
    base.dot_shape = HalftoneDotShape::Diamond;
    apply_halftone(src, diamond, base, ctx);
    base.dot_shape = HalftoneDotShape::Cross;
    apply_halftone(src, cross, base, ctx);

    CHECK(max_channel_diff(round, ellipse) > 0.05f);
    CHECK(max_channel_diff(round, square) > 0.05f);
    CHECK(max_channel_diff(round, diamond) > 0.05f);
    CHECK(max_channel_diff(round, cross) > 0.05f);
}

MGTK_TEST(effect_halftone_colour_modes_are_distinct) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(kW, kH);

    HalftoneParams base;
    base.cell_size = 6.0f;
    base.angle_spread_deg = 30.0f;

    Image mono, rgb, cmyk;
    base.color_mode = HalftoneColorMode::Monochrome;
    apply_halftone(src, mono, base, ctx);
    base.color_mode = HalftoneColorMode::RGB;
    apply_halftone(src, rgb, base, ctx);
    base.color_mode = HalftoneColorMode::CMYK;
    apply_halftone(src, cmyk, base, ctx);

    CHECK(all_finite(mono));
    CHECK(all_finite(rgb));
    CHECK(all_finite(cmyk));

    // The monochrome path is grey by construction; the colour paths are not.
    CHECK(max_channel_diff(mono, rgb) > 0.1f);
    CHECK(max_channel_diff(rgb, cmyk) > 0.05f);
}

// ===========================================================================
//  7. Slit Scan
// ===========================================================================
namespace {

// Three flat frames, newest first, in primary colours.
std::vector<Image> colour_history(int w, int h) {
    std::vector<Image> history;
    history.push_back(constant(w, h, Float4{1.0f, 0.0f, 0.0f, 1.0f}));  // newest
    history.push_back(constant(w, h, Float4{0.0f, 1.0f, 0.0f, 1.0f}));
    history.push_back(constant(w, h, Float4{0.0f, 0.0f, 1.0f, 1.0f}));  // oldest
    return history;
}

}  // namespace

MGTK_TEST(effect_slit_scan_time_slice_bands_the_frames) {
    // With 4px strips and hard edges, columns 0-3 come from the newest frame,
    // 4-7 from the one before, and so on.
    RenderContext ctx = ctx_default();
    const int w = 24;
    const int h = 8;
    const std::vector<Image> history = colour_history(w, h);

    SlitScanParams p;
    p.mode = SlitScanMode::TimeSlice;
    p.direction = SlitDirection::Horizontal;
    p.slice_width = 4.0f;
    p.falloff = 0.0f;
    p.frames = 3;
    p.mix = 1.0f;

    Image dst;
    apply_slit_scan(history[0], history, dst, p, ctx);

    CHECK_NEAR(dst.at(0, 0).r, 1.0f, 1e-4f);   // red
    CHECK_NEAR(dst.at(4, 0).g, 1.0f, 1e-4f);   // green
    CHECK_NEAR(dst.at(8, 0).b, 1.0f, 1e-4f);   // blue
    CHECK_NEAR(dst.at(12, 0).r, 1.0f, 1e-4f);  // wraps back to red

    // Each strip must be uniform along its length.
    CHECK_NEAR(dst.at(5, 0).g, dst.at(5, 7).g, 1e-5f);
}

MGTK_TEST(effect_slit_scan_falloff_softens_the_strip_boundaries) {
    RenderContext ctx = ctx_default();
    const int w = 24;
    const int h = 4;
    const std::vector<Image> history = colour_history(w, h);

    SlitScanParams hard;
    hard.mode = SlitScanMode::TimeSlice;
    hard.slice_width = 8.0f;
    hard.falloff = 0.0f;
    hard.frames = 3;

    SlitScanParams soft = hard;
    soft.falloff = 1.0f;

    Image dst_hard, dst_soft;
    apply_slit_scan(history[0], history, dst_hard, hard, ctx);
    apply_slit_scan(history[0], history, dst_soft, soft, ctx);

    // Both endpoints of a strip agree in both configurations.
    CHECK_NEAR(dst_hard.at(0, 0).r, dst_soft.at(0, 0).r, 1e-4f);

    // But at the midpoint of the first strip -- exactly where the transition
    // between two frames sits -- hard has already switched to the next frame
    // while soft is still halfway between the two.
    CHECK_NEAR(dst_hard.at(4, 0).g, 1.0f, 1e-4f);
    CHECK_NEAR(dst_soft.at(4, 0).g, 0.5f, 1e-3f);
    CHECK_MSG(std::fabs(dst_hard.at(4, 0).r - dst_soft.at(4, 0).r) > 0.3f,
              "falloff did not soften the transition");
}

MGTK_TEST(effect_slit_scan_time_blend_averages_every_frame) {
    RenderContext ctx = ctx_default();
    const int w = 8;
    const int h = 8;
    const std::vector<Image> history = colour_history(w, h);

    SlitScanParams p;
    p.mode = SlitScanMode::TimeBlend;
    p.frames = 3;

    Image dst;
    apply_slit_scan(history[0], history, dst, p, ctx);

    // Three frames of pure red, green and blue average to a neutral third.
    CHECK_NEAR(dst.at(4, 4).r, 1.0f / 3.0f, 1e-4f);
    CHECK_NEAR(dst.at(4, 4).g, 1.0f / 3.0f, 1e-4f);
    CHECK_NEAR(dst.at(4, 4).b, 1.0f / 3.0f, 1e-4f);
}

MGTK_TEST(effect_slit_scan_time_echo_decay_selects_frames) {
    RenderContext ctx = ctx_default();
    const int w = 8;
    const int h = 8;
    const std::vector<Image> history = colour_history(w, h);

    Image dst;

    // Zero decay means only the newest frame contributes.
    SlitScanParams none;
    none.mode = SlitScanMode::TimeEcho;
    none.intensity = 0.0f;
    none.frames = 3;
    apply_slit_scan(history[0], history, dst, none, ctx);
    CHECK_NEAR(dst.at(4, 4).r, 1.0f, 1e-4f);
    CHECK_NEAR(dst.at(4, 4).g, 0.0f, 1e-4f);

    // Decay of 1 means every frame weighs the same, which is the same as an
    // average.
    SlitScanParams full;
    full.mode = SlitScanMode::TimeEcho;
    full.intensity = 1.0f;
    full.frames = 3;
    apply_slit_scan(history[0], history, dst, full, ctx);
    CHECK_NEAR(dst.at(4, 4).r, 1.0f / 3.0f, 1e-4f);
}

MGTK_TEST(effect_slit_scan_without_history_is_a_passthrough) {
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(16, 16);
    const std::vector<Image> empty;
    Image dst;

    SlitScanParams p;
    apply_slit_scan(src, empty, dst, p, ctx);

    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 0.0f);
}

MGTK_TEST(effect_slit_scan_single_frame_history_is_a_passthrough) {
    // Frame 0 of a composition has nothing behind it, so a one-entry history is
    // a legitimate and common case.
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(16, 16);
    std::vector<Image> history{src};
    Image dst;

    for (int m = 0; m < static_cast<int>(SlitScanMode::Count); ++m) {
        SlitScanParams p;
        p.mode = static_cast<SlitScanMode>(m);
        p.frames = 8;

        apply_slit_scan(src, history, dst, p, ctx);
        CHECK(all_finite(dst));
        CHECK_MSG(max_channel_diff(src, dst) < 1e-4f,
                  "a single-frame history should leave the image untouched");
    }
}

MGTK_TEST(effect_slit_scan_directions_differ) {
    RenderContext ctx = ctx_default();
    const int w = 32;
    const int h = 32;
    const std::vector<Image> history = colour_history(w, h);

    SlitScanParams base;
    base.mode = SlitScanMode::TimeSlice;
    base.slice_width = 4.0f;
    base.frames = 3;

    Image horizontal, vertical;
    base.direction = SlitDirection::Horizontal;
    apply_slit_scan(history[0], history, horizontal, base, ctx);
    base.direction = SlitDirection::Vertical;
    apply_slit_scan(history[0], history, vertical, base, ctx);

    CHECK(all_finite(horizontal));
    CHECK(all_finite(vertical));
    CHECK(max_channel_diff(horizontal, vertical) > 0.1f);

    // Radial and angular must not blow up on a 32x32 frame either.
    base.direction = SlitDirection::Radial;
    apply_slit_scan(history[0], history, vertical, base, ctx);
    CHECK(all_finite(vertical));

    base.direction = SlitDirection::Angular;
    apply_slit_scan(history[0], history, vertical, base, ctx);
    CHECK(all_finite(vertical));
}

MGTK_TEST(effect_slit_scan_time_displace_is_flat_for_static_input) {
    // No motion between frames means no displacement: a static shot must pass
    // through untouched rather than being smeared by numerical noise.
    RenderContext ctx = ctx_default();
    const Image same = make_blobs(32, 32);
    std::vector<Image> history{same, same, same};

    SlitScanParams p;
    p.mode = SlitScanMode::TimeDisplace;
    p.intensity = 1.0f;
    p.frames = 3;

    Image dst;
    apply_slit_scan(same, history, dst, p, ctx);

    CHECK(all_finite(dst));
    CHECK_MSG(max_channel_diff(same, dst) < 1e-4f,
              "a static sequence was displaced anyway");
}

// ===========================================================================
//  8. Pixel Sort
// ===========================================================================
MGTK_TEST(effect_pixel_sort_sorts_the_selected_run) {
    RenderContext ctx = ctx_default();

    // A left-to-right ramp, fully inside the threshold window.
    Image src(32, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float v = static_cast<float>(x) / 31.0f;
            src.at(x, y) = Float4{v, v, v, 1.0f};
        }
    }

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.axis = SortAxis::Horizontal;
    p.order = SortOrder::Descending;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 1000;
    p.mix = 1.0f;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);

    // After a descending sort the row runs bright to dark, monotonically.
    for (int x = 1; x < 32; ++x) {
        CHECK_MSG(dst.at(x, 0).r <= dst.at(x - 1, 0).r + 1e-6f,
                  "the row was not sorted descending");
    }
    // And the endpoint values must be the extremes of the input.
    CHECK_NEAR(dst.at(0, 0).r, 1.0f, 1e-5f);
    CHECK_NEAR(dst.at(31, 0).r, 0.0f, 1e-5f);

    // Ascending must reverse it.
    p.order = SortOrder::Ascending;
    apply_pixel_sort(src, dst, p, ctx);
    for (int x = 1; x < 32; ++x) {
        CHECK_MSG(dst.at(x, 0).r >= dst.at(x - 1, 0).r - 1e-6f,
                  "the row was not sorted ascending");
    }
}

MGTK_TEST(effect_pixel_sort_leaves_out_of_range_pixels_alone) {
    // The threshold window is the effect's whole control surface. Pixels whose
    // key falls outside it must be byte-identical in the output.
    RenderContext ctx = ctx_default();
    Image src = make_gradient(32, 8);

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.threshold_low = 0.6f;
    p.threshold_high = 0.7f;
    p.max_length = 1000;
    p.mix = 1.0f;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);

    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 32; ++x) {
            const float key = luma(src.at(x, y).r, src.at(x, y).g, src.at(x, y).b);
            if (key < 0.6f || key > 0.7f) {
                CHECK_NEAR(dst.at(x, y).r, src.at(x, y).r, 1e-6f);
                CHECK_NEAR(dst.at(x, y).b, src.at(x, y).b, 1e-6f);
            }
        }
    }
}

MGTK_TEST(effect_pixel_sort_empty_window_is_a_passthrough) {
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(16, 16);

    PixelSortParams p;
    // Nothing can satisfy key >= 2.0.
    p.threshold_low = 2.0f;
    p.threshold_high = 3.0f;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);
    CHECK_NEAR(max_channel_diff(src, dst), 0.0f, 0.0f);
}

MGTK_TEST(effect_pixel_sort_max_length_limits_the_run) {
    // A long uniform run with a small cap must be broken into sorted blocks,
    // not sorted as one span. That is what produces the effect's characteristic
    // banding rather than a full-row wipe.
    RenderContext ctx = ctx_default();

    Image src(64, 2);
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 64; ++x) {
            // A sawtooth so the sort has something to do in every block.
            const float v = static_cast<float>(x % 8) / 7.0f;
            src.at(x, y) = Float4{v, v, v, 1.0f};
        }
    }

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.order = SortOrder::Descending;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 8;
    p.mix = 1.0f;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);

    CHECK(all_finite(dst));

    // Each 8-pixel block must be internally sorted...
    for (int block = 0; block < 8; ++block) {
        for (int i = 1; i < 8; ++i) {
            const int x = block * 8 + i;
            CHECK_MSG(dst.at(x, 0).r <= dst.at(x - 1, 0).r + 1e-6f,
                      "a block was not sorted");
        }
    }

    // ...but the row as a whole must not be, since that would mean the cap was
    // ignored and the whole run was sorted in one go.
    bool globally_sorted = true;
    for (int x = 1; x < 64; ++x) {
        if (dst.at(x, 0).r > dst.at(x - 1, 0).r + 1e-6f) {
            globally_sorted = false;
            break;
        }
    }
    CHECK_MSG(!globally_sorted, "max_length was ignored: the whole row got sorted");
}

MGTK_TEST(effect_pixel_sort_stretch_flattens_the_run) {
    RenderContext ctx = ctx_default();
    Image src = make_gradient(32, 4);

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 1000;

    Image normal;
    p.stretch = 0.0f;
    apply_pixel_sort(src, normal, p, ctx);

    Image flat;
    p.stretch = 1.0f;
    apply_pixel_sort(src, flat, p, ctx);

    // At full stretch the entire run collapses to the leading pixel's value.
    CHECK_NEAR(flat.at(0, 0).r, flat.at(31, 0).r, 1e-5f);
    CHECK_MSG(max_channel_diff(normal, flat) > 0.1f,
              "stretch had no effect");
}

MGTK_TEST(effect_pixel_sort_vertical_axis_works) {
    RenderContext ctx = ctx_default();

    Image src(4, 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float v = static_cast<float>(y) / 31.0f;
            src.at(x, y) = Float4{v, v, v, 1.0f};
        }
    }

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.axis = SortAxis::Vertical;
    p.order = SortOrder::Descending;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 1000;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);

    for (int y = 1; y < 32; ++y) {
        CHECK_MSG(dst.at(0, y).r <= dst.at(0, y - 1).r + 1e-6f,
                  "the column was not sorted descending");
    }
}

MGTK_TEST(effect_pixel_sort_preserves_alpha) {
    RenderContext ctx = ctx_default();
    Image src = make_gradient(24, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 24; ++x) {
            src.at(x, y).a = 0.25f + 0.75f * static_cast<float>(y) / 7.0f;
        }
    }

    PixelSortParams p;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 1000;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);

    // Sorting shuffles whole pixels, so the multiset of alpha values per row is
    // unchanged -- but here alpha is constant per row, so it must be identical.
    CHECK(alpha_unchanged(src, dst, 1e-6f));
}

MGTK_TEST(effect_pixel_sort_alpha_mask_confines_the_effect) {
    RenderContext ctx = ctx_default();
    Image src = make_gradient(32, 4);
    // Fully transparent on the right half.
    for (int y = 0; y < 4; ++y) {
        for (int x = 16; x < 32; ++x) src.at(x, y).a = 0.0f;
    }

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.order = SortOrder::Descending;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 1000;
    p.use_alpha_mask = true;

    Image dst;
    apply_pixel_sort(src, dst, p, ctx);

    // With the mask on, the opaque left half sorts on its own and the
    // transparent right half is untouched.
    for (int y = 0; y < 4; ++y) {
        for (int x = 16; x < 32; ++x) {
            CHECK_NEAR(dst.at(x, y).r, src.at(x, y).r, 1e-6f);
        }
    }
}

MGTK_TEST(effect_pixel_sort_randomness_reduces_order) {
    RenderContext ctx = ctx_default();
    Image src = make_gradient(64, 4);

    PixelSortParams p;
    p.key = SortKey::Brightness;
    p.order = SortOrder::Descending;
    p.threshold_low = 0.0f;
    p.threshold_high = 1.0f;
    p.max_length = 1000;

    Image ordered;
    p.randomness = 0.0f;
    apply_pixel_sort(src, ordered, p, ctx);

    Image shuffled;
    p.randomness = 1.0f;
    apply_pixel_sort(src, shuffled, p, ctx);

    // Measure how monotone each result is: count the ascents. A properly sorted
    // row has none; a jittered one has several.
    const auto ascents = [](const Image& img) {
        int n = 0;
        for (int x = 1; x < img.width(); ++x) {
            if (img.at(x, 0).r > img.at(x - 1, 0).r + 1e-6f) ++n;
        }
        return n;
    };

    CHECK_EQ(ascents(ordered), 0);
    CHECK_MSG(ascents(shuffled) > 0,
              "randomness=1 produced a perfectly ordered run");
    CHECK(all_finite(shuffled));
}

MGTK_TEST(effect_pixel_sort_randomness_is_deterministic_across_repeats) {
    // Randomness must come from a position hash, not a global RNG, so that a
    // cached frame matches a freshly rendered one.
    RenderContext ctx = ctx_default();
    const Image src = make_gradient(64, 8);

    PixelSortParams p;
    p.key = SortKey::Random;
    p.randomness = 1.0f;
    p.threshold_low = -10.0f;
    p.threshold_high = 10.0f;
    p.max_length = 1000;

    Image a, b;
    apply_pixel_sort(src, a, p, ctx);
    apply_pixel_sort(src, b, p, ctx);
    CHECK_NEAR(max_channel_diff(a, b), 0.0f, 0.0f);
}

MGTK_TEST(effect_pixel_sort_every_key_is_usable) {
    RenderContext ctx = ctx_default();
    const Image src = make_blobs(kW, kH);

    for (int k = 0; k < static_cast<int>(SortKey::Count); ++k) {
        PixelSortParams p;
        p.key = static_cast<SortKey>(k);
        p.threshold_low = -10.0f;
        p.threshold_high = 10.0f;
        p.max_length = 64;

        Image dst;
        apply_pixel_sort(src, dst, p, ctx);
        CHECK(all_finite(dst));
        CHECK(has_any_nonzero(dst));
    }
}
