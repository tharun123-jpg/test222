// =============================================================================
//  mgtk/fx_glow.cpp -- Anamorphic Glow
//
//  Three stages:
//    1. a soft bright pass,
//    2. a radial bloom, computed at reduced resolution so that a 300px radius
//       costs roughly the same as a 20px one,
//    3. one or more directional "anamorphic" streaks, plus optional channel
//       separation for a lens-fringe look.
//
//  The resolution-reduction trick in stage 2 is worth spelling out, because it
//  is the difference between a usable effect and an unusable one. Blurring at
//  full resolution with sigma s costs O(pixels); blurring at 1/2^k resolution
//  with sigma s/2^k costs O(pixels/4^k) and yields the same result once scaled
//  back up, as long as s/2^k is large enough that the reduced grid still
//  resolves the blur. For a 60px radius on a 1920x1080 frame that is a 64x
//  saving.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// The largest sigma we allow at the working resolution. Above this the blur
// starts to show the working grid's own pixel structure once magnified.
constexpr float kMaxWorkingSigma = 4.0f;

// How far we are willing to reduce. Beyond 1/16 the upsample artefacts start to
// become visible as soft blockiness on hard-edged sources.
constexpr int kMaxShift = 4;

// A streak's sample spacing is `length / samples`, so a fixed sample count
// across a 300px streak puts a sample every 18px and the streak degenerates
// into discrete ghost copies of the source. Dropping the working resolution for
// long streaks fixes that by making the samples dense *relative to the working
// pixel* while also making the pass cheap: a 300px streak at 1/16 resolution
// and one sample per working pixel costs a fraction of 16 samples at full
// resolution. Short streaks -- where the detail actually lives -- stay exact.
constexpr float kStreakWorkingLength = 24.0f;

struct QualitySettings {
    int max_shift;
    int streak_samples;
};

QualitySettings settings_for(Quality q) {
    switch (q) {
        case Quality::Draft: return {2, 6};
        case Quality::Best:  return {kMaxShift, 32};
        case Quality::Good:
        case Quality::Count:
        default:             return {3, 16};
    }
}

// Blur `img` by `sigma` at full-frame scale, transparently dropping to a
// reduced working resolution first. `work` is scratch and is left sized to the
// working resolution.
void blur_with_reduction(Image& img, float sigma, int max_shift, WrapMode mode,
                         Image& work) {
    if (sigma <= 0.0f) return;

    int shift = 0;
    while (shift < max_shift &&
           sigma / static_cast<float>(1 << (shift + 1)) > kMaxWorkingSigma) {
        ++shift;
    }

    if (shift == 0) {
        gaussian_blur(img, sigma, mode);
        return;
    }

    const int ww = std::max(1, img.width() >> shift);
    const int wh = std::max(1, img.height() >> shift);

    resize_bilinear(img, work, ww, wh, mode);
    gaussian_blur(work, sigma / static_cast<float>(1 << shift), mode);

    // Scale back up into the caller's buffer.
    Image up;
    resize_bilinear(work, up, img.width(), img.height(), mode);
    img.swap(up);
}

// Directional streak, computed at reduced resolution when it is long enough to
// warrant it. `min_samples` is the quality setting's floor; the actual count is
// raised to about one sample per working pixel so that the streak stays
// continuous no matter how far it reaches.
void streak_pass(Image& img, float angle_deg, float length, int min_samples,
                 int max_shift, WrapMode mode, Image& scratch) {
    if (length <= 0.0f || img.empty()) return;

    int shift = 0;
    while (shift < max_shift &&
           length / static_cast<float>(1 << (shift + 1)) > kStreakWorkingLength) {
        ++shift;
    }

    const float scale = static_cast<float>(1 << shift);
    const int samples = clamp(static_cast<int>(std::ceil(length / scale)),
                              clamp(min_samples, 2, 128), 128);

    if (shift == 0) {
        directional_blur(img, angle_deg, length, samples, mode, &scratch);
        return;
    }

    const int ww = std::max(1, img.width() >> shift);
    const int wh = std::max(1, img.height() >> shift);

    // Geometry is resolution independent: the same angle and the same length in
    // working pixels describe the same streak once it is scaled back up.
    resize_bilinear(img, scratch, ww, wh, mode);
    directional_blur(scratch, angle_deg, length / scale, samples, mode, nullptr);

    Image up;
    resize_bilinear(scratch, up, img.width(), img.height(), mode);
    img.swap(up);
}

// Per-channel radial offset, applied to an already-computed glow. Reusing the
// glow as the source means the separation reads as a prism fringe on the light
// itself rather than as a second copy of the image.
void separate_channels(Image& glow, float pixels_at_corner, Vec2 center_norm) {
    if (std::fabs(pixels_at_corner) < kEpsilon || glow.empty()) return;

    const int w = glow.width();
    const int h = glow.height();
    const RadialFrame frame = RadialFrame::make(w, h, center_norm);
    const float half_diag = 1.0f / frame.inv_half_diag;

    const Image src_copy = glow;
    const ImageView sv = src_copy.view();
    ImageView dv = glow.view();

    // Red reads outwards, blue inwards -- the same ordering a real lens gives.
    const float offset_at_corner = pixels_at_corner;
    const float scale_r = 1.0f + offset_at_corner / half_diag;
    const float scale_b = 1.0f - offset_at_corner / half_diag;

    for (int y = 0; y < h; ++y) {
        Float4* d = dv.row(y);
        const float vy = static_cast<float>(y) - frame.cy;
        for (int x = 0; x < w; ++x) {
            const float vx = static_cast<float>(x) - frame.cx;

            const Float4 gr = sv.sample_bilinear(frame.cx + vx * scale_r,
                                                 frame.cy + vy * scale_r,
                                                 WrapMode::Transparent);
            const Float4 gb = sv.sample_bilinear(frame.cx + vx * scale_b,
                                                 frame.cy + vy * scale_b,
                                                 WrapMode::Transparent);
            d[x] = Float4{gr.r, d[x].g, gb.b, d[x].a};
        }
    }
}

}  // namespace

void apply_anamorphic_glow(const Image& src, Image& dst,
                           const AnamorphicGlowParams& p, const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }
    if (dst.width() != w || dst.height() != h) dst.resize(w, h);

    const QualitySettings qs = settings_for(p.quality);

    // -----------------------------------------------------------------------
    //  Stage 1 -- bright pass
    // -----------------------------------------------------------------------
    // The bright pass lives here rather than in blur.hpp because it needs to
    // know something a generic primitive cannot: whether the incoming pixels
    // are linear. The *decision* (is this pixel bright enough?) is made
    // perceptually so that Threshold means the same thing in an 8-bit and a
    // 32-bit project, while the extracted *colour* stays in the working space
    // so that adding it back is physically correct light addition.
    Image glow(w, h);
    {
        const float thresh = std::max(0.0f, p.threshold);
        const float knee = std::max(0.0f, p.knee);
        const float intensity = std::max(0.0f, p.intensity);
        const bool linear = ctx.input_linear;
        const bool soft = (knee > kEpsilon);

        // Peak of RGB rather than luma: a saturated red should bloom just as
        // strongly as a saturated green, and luma weighting would bias the glow
        // towards green.
        for (int y = 0; y < h; ++y) {
            const Float4* s = src.row(y);
            Float4* d = glow.row(y);
            for (int x = 0; x < w; ++x) {
                const float peak =
                    to_perceptual(max3(s[x].r, s[x].g, s[x].b), linear);

                const float ramp = soft
                    ? smoothstep(thresh - knee, thresh + knee, peak)
                    : (peak >= thresh ? 1.0f : 0.0f);

                const float scale = ramp * intensity;
                d[x] = Float4{s[x].r * scale, s[x].g * scale,
                              s[x].b * scale, s[x].a * scale};
            }
        }
    }

    ctx.report(0.2f);
    if (ctx.aborted()) return;

    // -----------------------------------------------------------------------
    //  Stage 2 -- radial bloom
    // -----------------------------------------------------------------------
    // Split the bright pass into a radial pool and a streak source. They are
    // kept separate so the two components can be tinted and balanced
    // independently.
    Image radial = glow;
    Image work;
    blur_with_reduction(radial, p.radius / 3.0f, qs.max_shift, WrapMode::Clamp, work);

    ctx.report(0.5f);
    if (ctx.aborted()) return;

    // -----------------------------------------------------------------------
    //  Stage 3 -- anamorphic streaks
    // -----------------------------------------------------------------------
    Image streak;
    const int streak_count = clamp(p.streak_count, 1, 8);
    const bool want_streak =
        (p.streak_length > 0.5f) && (p.streak_intensity > kEpsilon);

    if (want_streak) {
        streak.resize(w, h);

        // Each streak lives in its own buffer and is averaged in. Accumulating
        // straight into one buffer would let a bright streak dominate a dim one
        // at a different angle.
        Image single;
        const float weight = 1.0f / static_cast<float>(streak_count);

        for (int i = 0; i < streak_count; ++i) {
            // Star streaks are spread evenly around the circle, starting at the
            // user's angle.
            const float angle = p.streak_angle_deg +
                                (streak_count > 1
                                     ? (180.0f * static_cast<float>(i) /
                                        static_cast<float>(streak_count))
                                     : 0.0f);

            single = glow;
            streak_pass(single, angle, p.streak_length, qs.streak_samples,
                        qs.max_shift, WrapMode::Clamp, work);

            // Average into the streak buffer.
            for (int y = 0; y < h; ++y) {
                const Float4* s = single.row(y);
                Float4* d = streak.row(y);
                for (int x = 0; x < w; ++x) d[x] += s[x] * weight;
            }
        }
    }

    ctx.report(0.8f);
    if (ctx.aborted()) return;

    // -----------------------------------------------------------------------
    //  Stage 4 -- combine, tint, composite
    // -----------------------------------------------------------------------
    const float streak_balance = saturate(p.streak_intensity);
    const float glow_balance = 1.0f;
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);

    const Vec2 center = Vec2{0.5f, 0.5f};
    if (std::fabs(p.rgb_separation) > kEpsilon) {
        separate_channels(radial, p.rgb_separation, center);
    }

    const bool do_glow_tint = (p.glow_tint_amount > kEpsilon);
    const bool do_streak_tint = (p.streak_tint_amount > kEpsilon);

    const Float4* sr = streak.data();

    for (int y = 0; y < h; ++y) {
        if ((y & 63) == 0 && ctx.aborted()) return;

        const Float4* s = src.row(y);
        const Float4* g = radial.row(y);
        const Float4* st = want_streak ? (sr + static_cast<std::ptrdiff_t>(y) * w)
                                       : nullptr;
        Float4* d = dst.row(y);

        for (int x = 0; x < w; ++x) {
            Float4 light = g[x] * glow_balance;
            if (st != nullptr) {
                light += st[x] * streak_balance;
            }

            if (do_glow_tint) {
                light = apply_tint(light, p.glow_tint, p.glow_tint_amount);
            }
            if (do_streak_tint && st != nullptr) {
                // Tint only the streak's contribution, preserving whatever the
                // pool already contributed.
                const Float4 tinted_streak =
                    apply_tint(st[x] * streak_balance, p.streak_tint,
                               p.streak_tint_amount);
                const Float4 untinted_streak = st[x] * streak_balance;
                light += tinted_streak - untinted_streak;
            }

            // Confine the light to the source's matte.
            if (p.preserve_alpha) {
                const float a = saturate(s[x].a);
                light.r *= a;
                light.g *= a;
                light.b *= a;
                light.a *= a;
            }

            const Float4 blended = blend_pixel(p.blend, s[x], light);
            Float4 out;
            out.r = blended.r;
            out.g = blended.g;
            out.b = blended.b;
            out.a = p.preserve_alpha ? s[x].a : max2(s[x].a, light.a);

            d[x] = identity_mix ? out : lerp(s[x], out, mix);
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
