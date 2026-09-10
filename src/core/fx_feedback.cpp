// =============================================================================
//  mgtk/fx_feedback.cpp -- Feedback Echo
//
//  A temporal recursion: each rendered frame is folded back into an accumulator
//  that is then transformed, attenuated and re-composited under the next frame.
//  This is the effect that produces infinite-zoom tunnels, ghost trails and
//  smeared light streaks.
//
//  AE-specific note: the accumulator lives in the effect's *sequence data*, not
//  here. After Effects may render frames out of order and, with multi-frame
//  rendering enabled, in parallel -- which is why the glue marks this effect as
//  not thread-safe and why the echo is documented as order-dependent.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// Result of the geometric transform that is applied to the accumulator on each
// step. Precomputed once per frame rather than per pixel.
struct EchoTransform {
    float cos_t = 1.0f;
    float sin_t = 0.0f;
    float inv_scale = 1.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float ox = 0.0f;
    float oy = 0.0f;

    // Map an output pixel to the accumulator coordinate it should read from.
    Vec2 map(float x, float y) const {
        // Content is offset, then rotated, then scaled about the centre. To
        // make the *content* move that way we invert the transform when
        // sampling: p' = c + R(-t) * (p - c - offset) / scale.
        const float dx = (x - cx) - ox;
        const float dy = (y - cy) - oy;

        const float rx = dx * cos_t + dy * sin_t;   // R(-t)
        const float ry = -dx * sin_t + dy * cos_t;

        return Vec2{cx + rx * inv_scale, cy + ry * inv_scale};
    }
};

inline Float4 echo_blend(EchoBlendMode mode, Float4 src, Float4 echo) {
    switch (mode) {
        case EchoBlendMode::Normal:
            // The classic look: the source sits on top, echoes trail behind it.
            return composite_over(echo, src);

        case EchoBlendMode::Add:
            return Float4{src.r + echo.r, src.g + echo.g, src.b + echo.b, src.a};

        case EchoBlendMode::Screen:
            return Float4{blend_channel(BlendMode::Screen, src.r, echo.r),
                          blend_channel(BlendMode::Screen, src.g, echo.g),
                          blend_channel(BlendMode::Screen, src.b, echo.b),
                          src.a};

        case EchoBlendMode::Lighten:
            return Float4{max2(src.r, echo.r), max2(src.g, echo.g),
                          max2(src.b, echo.b), src.a};

        case EchoBlendMode::Darken:
            return Float4{min2(src.r, echo.r), min2(src.g, echo.g),
                          min2(src.b, echo.b), src.a};

        case EchoBlendMode::Difference:
            return Float4{std::fabs(src.r - echo.r), std::fabs(src.g - echo.g),
                          std::fabs(src.b - echo.b), src.a};

        case EchoBlendMode::Overlay:
            return Float4{blend_channel(BlendMode::Overlay, src.r, echo.r),
                          blend_channel(BlendMode::Overlay, src.g, echo.g),
                          blend_channel(BlendMode::Overlay, src.b, echo.b),
                          src.a};

        case EchoBlendMode::Count:
        default:
            return composite_over(echo, src);
    }
}

}  // namespace

void apply_feedback_echo(const Image& src, Image& feedback, Image& dst,
                         const FeedbackEchoParams& p, const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }

    if (dst.width() != w || dst.height() != h) dst.resize(w, h);

    // The accumulator has to match the current buffer. If AE changed the
    // working resolution (a resolution change, or a pre-compose resize) the old
    // accumulator is meaningless -- start clean rather than resampling stale
    // pixels into the new size.
    if (feedback.width() != w || feedback.height() != h || p.reset) {
        feedback.assign(w, h, Float4{});
    }

    // ---------------------------------------------------------------------
    //  Per-step retention
    // ---------------------------------------------------------------------
    const float amount = saturate(p.echo_amount);
    const float decay = std::pow(std::max(amount, kEpsilon), clamp(p.decay_curve, 0.05f, 8.0f));

    const float hue = p.hue_shift_deg;
    const float brightness = p.brightness;
    const float saturation = p.saturation;

    // ---------------------------------------------------------------------
    //  Build the transform
    // ---------------------------------------------------------------------
    EchoTransform xf;
    const float rad = p.rotation_deg * kDegToRad;
    xf.cos_t = std::cos(rad);
    xf.sin_t = std::sin(rad);

    // Guard against a scale of exactly zero, which would make the inverse
    // infinite. A tiny scale is a legitimate "suck everything into the centre"
    // setting, so clamp rather than reject.
    float scale = p.scale;
    if (std::fabs(scale) < 1.0e-3f) scale = scale < 0.0f ? -1.0e-3f : 1.0e-3f;
    xf.inv_scale = 1.0f / scale;
    xf.cx = p.center.x * static_cast<float>(w);
    xf.cy = p.center.y * static_cast<float>(h);
    xf.ox = p.offset_x;
    xf.oy = p.offset_y;

    // ---------------------------------------------------------------------
    //  Transform + attenuate the accumulator, composite the source over it,
    //  and write the result back into the accumulator.
    //
    //  We read from `feedback` and write to `dst`, then swap, so that no
    //  pixel is ever read after it has been overwritten.
    // ---------------------------------------------------------------------
    const ImageView fbv = feedback.view();

    // Fast path: an identity transform means we can skip the sampling entirely,
    // which matters because this effect runs at every single frame.
    const bool identity =
        std::fabs(p.scale - 1.0f) < kEpsilon &&
        std::fabs(p.rotation_deg) < kEpsilon &&
        std::fabs(p.offset_x) < kEpsilon &&
        std::fabs(p.offset_y) < kEpsilon;

    const bool identity_colour =
        std::fabs(hue) < kEpsilon &&
        std::fabs(brightness - 1.0f) < kEpsilon &&
        std::fabs(saturation - 1.0f) < kEpsilon;

    const float mix = saturate(p.mix);

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && ctx.aborted()) return;

        const Float4* s = src.row(y);
        Float4* d = dst.row(y);

        for (int x = 0; x < w; ++x) {
            Float4 echo;
            if (identity) {
                echo = fbv.at(x, y);
            } else {
                const Vec2 sp = xf.map(static_cast<float>(x), static_cast<float>(y));
                echo = fbv.sample_bilinear(sp.x, sp.y, p.wrap);
            }

            // Attenuate, then grade the colour.
            echo.r *= decay;
            echo.g *= decay;
            echo.b *= decay;
            echo.a *= decay;

            if (!identity_colour) {
                echo = rotate_hue(echo, hue);
                echo = adjust_saturation(echo, saturation);
                echo.r *= brightness;
                echo.g *= brightness;
                echo.b *= brightness;
            }

            const Float4 combined = echo_blend(p.blend, s[x], echo);
            d[x] = (mix >= 1.0f) ? combined : lerp(s[x], combined, mix);
        }
    }

    // The accumulator for the next frame is this frame's output. This is a
    // deliberate copy rather than a swap: `dst` is AE's output buffer, which we
    // do not own past the end of this call, while `feedback` has to survive
    // into the next frame.
    feedback = dst;

    ctx.report(1.0f);
}

}  // namespace mgtk
