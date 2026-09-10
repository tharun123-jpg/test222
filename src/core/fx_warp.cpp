// =============================================================================
//  mgtk/fx_warp.cpp -- Fractal Warp
//
//  One sampling pass that composes three independent modifiers:
//
//    * a displacement field, either plain fBm or a domain-warped fBm,
//    * a rotational swirl about a centre,
//    * a radial pinch / bulge.
//
//  `WarpMode` selects the displacement source and which modifiers are forced
//  on; any modifier whose slider is non-zero still applies on top. That keeps
//  every mode visibly different while letting a user stack modifiers without
//  hunting for the "right" mode.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// Distance, in noise-space units, by which the domain-warp pass perturbs the
// sample position before re-reading the field. 2.0 cells gives the liquid,
// marbled look; smaller values are too subtle to read as a warp at all.
constexpr float kDomainWarpStrength = 2.0f;

// Arbitrary irrational-ish offsets used to decorrelate noise channels. Reading
// the same field at two shifted positions is cheaper than evaluating a second
// octave stack with a different seed, and it decorrelates just as well.
constexpr float kChannelOffsetA = 37.13f;
constexpr float kChannelOffsetB = 17.31f;

// Clamp on the pinch parameter. A value of exactly 1.0 would map the entire
// centre to a single pixel, which is neither useful nor numerically stable.
constexpr float kMaxPinch = 0.99f;

}  // namespace

void apply_fractal_warp(const Image& src, Image& dst,
                        const FractalWarpParams& p, const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }
    if (dst.width() != w || dst.height() != h) dst.resize(w, h);

    const ImageView sv = src.view();

    // -----------------------------------------------------------------------
    //  Resolve which modifiers are active
    // -----------------------------------------------------------------------
    const WarpMode mode = p.mode;

    const bool use_displacement =
        (mode == WarpMode::Displace || mode == WarpMode::DomainWarp ||
         mode == WarpMode::DisplaceSwirl);
    const bool domain_warp = (mode == WarpMode::DomainWarp);

    const bool force_swirl = (mode == WarpMode::Swirl || mode == WarpMode::DisplaceSwirl);
    const bool force_pinch = (mode == WarpMode::Pinch);

    const bool swirl_on = force_swirl || (std::fabs(p.swirl_amount) > kEpsilon);
    const bool pinch_on = force_pinch || (std::fabs(p.pinch) > kEpsilon);

    // -----------------------------------------------------------------------
    //  Geometry
    // -----------------------------------------------------------------------
    const RadialFrame frame = RadialFrame::make(w, h, Vec2{p.center_x, p.center_y});
    const float half_diag = 1.0f / frame.inv_half_diag;

    // The swirl radius is expressed as a fraction of the half-diagonal, so a
    // swirl that reaches the corners on a 16:9 frame also reaches them on a
    // square one.
    const float swirl_extent = std::max(p.swirl_radius, 0.01f) * half_diag;

    const float swirl_turns = clamp(p.swirl_amount, -20.0f, 20.0f);
    const float pinch = clamp(p.pinch, -kMaxPinch, kMaxPinch);

    // Precomputed rotation for the swirl is impossible (the angle varies per
    // pixel), so we only precompute the constant part.
    const float swirl_angle_scale = swirl_turns * kTwoPi;

    // -----------------------------------------------------------------------
    //  Noise setup
    // -----------------------------------------------------------------------
    FbmParams fp;
    fp.octaves = p.octaves;
    fp.lacunarity = p.lacunarity;
    fp.gain = p.gain;
    fp.frequency = 1.0f;   // the per-pixel scale is folded into the coordinate

    const float ns = (std::fabs(p.noise_scale) < 1e-7f) ? 1e-7f : p.noise_scale;
    const float t = p.evolution;

    const uint32_t seed_a = p.seed;
    const uint32_t seed_b = mix32(p.seed + 0x5bf03635u);

    const bool bicubic = (p.quality == ChromaQuality::Bicubic);
    const float amount = p.amount;
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);

    // A completely inert parameter set should be a straight copy. Worth
    // detecting: it saves the whole noise evaluation when a user drops the
    // effect on and has not dialled anything in yet.
    const bool inert = !use_displacement && !swirl_on && !pinch_on;

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && ctx.aborted()) return;

        Float4* d = dst.row(y);
        const float fy = static_cast<float>(y);

        for (int x = 0; x < w; ++x) {
            if (inert) {
                d[x] = sv.at(x, y);
                continue;
            }

            const float fx = static_cast<float>(x);

            // Displacement from the noise field, in pixels.
            float disp_x = 0.0f;
            float disp_y = 0.0f;

            if (use_displacement) {
                // Noise space. Multiplying by noise_scale here rather than
                // baking it into each octave means an animated noise_scale
                // stays coherent -- the field zooms rather than reshuffling.
                const float nx = fx * ns;
                const float ny = fy * ns;

                if (domain_warp) {
                    const float w1 = fbm_3d(nx, ny, t, seed_a, fp);
                    const float w2 = fbm_3d(nx + kChannelOffsetA, ny + kChannelOffsetB,
                                            t, seed_a, fp);

                    disp_x = fbm_3d(nx + w1 * kDomainWarpStrength,
                                    ny + w2 * kDomainWarpStrength, t, seed_b, fp) * amount;
                    disp_y = fbm_3d(nx + w2 * kDomainWarpStrength,
                                    ny + w1 * kDomainWarpStrength, t, seed_a, fp) * amount;
                } else {
                    disp_x = fbm_3d(nx, ny, t, seed_a, fp) * amount;
                    disp_y = fbm_3d(nx + kChannelOffsetA, ny + kChannelOffsetB, t,
                                    seed_b, fp) * amount;
                }
            }

            // Work in centre-relative coordinates from here on.
            float vx = fx + disp_x - frame.cx;
            float vy = fy + disp_y - frame.cy;

            // --- Swirl -------------------------------------------------------
            if (swirl_on) {
                const float dist = std::sqrt(vx * vx + vy * vy);
                // Influence falls to zero at swirl_extent. Squaring the
                // normalised falloff keeps the rotation smooth at the boundary;
                // a linear ramp leaves a visible crease where it terminates.
                float influence = 1.0f - saturate(dist / swirl_extent);
                influence *= influence;

                if (influence > kEpsilon) {
                    const float angle = swirl_angle_scale * influence;
                    // Sample at the inverse rotation so the content appears to
                    // rotate by +angle.
                    const float cs = std::cos(angle);
                    const float sn = std::sin(angle);
                    const float rx = vx * cs + vy * sn;
                    const float ry = -vx * sn + vy * cs;
                    vx = rx;
                    vy = ry;
                }
            }

            // --- Pinch / bulge ----------------------------------------------
            if (pinch_on) {
                const float dist = std::sqrt(vx * vx + vy * vy);
                const float radial = saturate(dist * frame.inv_half_diag);

                // Full strength at the centre, easing out to none at the edge.
                const float influence = 1.0f - radial;
                // Capped at 0.75 so the strongest setting magnifies 4x rather
                // than collapsing the image to a point.
                const float scale = 1.0f - pinch * influence * 0.75f;
                vx *= scale;
                vy *= scale;
            }

            const float sx = frame.cx + vx;
            const float sy = frame.cy + vy;

            const Float4 out = bicubic ? sv.sample_bicubic(sx, sy, p.wrap)
                                       : sv.sample_bilinear(sx, sy, p.wrap);
            d[x] = identity_mix ? out : lerp(sv.at(x, y), out, mix);
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
