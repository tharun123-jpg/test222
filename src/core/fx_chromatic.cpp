// =============================================================================
//  mgtk/fx_chromatic.cpp -- Chromatic Split
//
//  Per-channel sampling with five different dispersion geometries. Everything
//  is expressed through a single "where does this channel read from" function
//  so the modes share one sampling path.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// Unit conversion for Zoom mode: the Amount parameter is a percentage there
// and pixels everywhere else. Documented in the parameter's name.
constexpr float kZoomPercent = 0.01f;

// Real lens distortion follows r' = r * (1 + k1*r^2). We expose `barrel` as
// the number of pixels the warp reaches at the layer's corners, then solve for
// k1, so the parameter stays meaningful regardless of frame size.
inline float solve_barrel_k(float barrel_pixels, float half_diag) {
    if (std::fabs(half_diag) < kEpsilon) return 0.0f;
    const float r2 = half_diag * half_diag;
    return barrel_pixels / (half_diag * r2);
}

}  // namespace

void apply_chromatic_split(const Image& src, Image& dst,
                           const ChromaticSplitParams& p, const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }
    if (dst.width() != w || dst.height() != h) dst.resize(w, h);

    const ImageView sv = src.view();

    const RadialFrame frame = RadialFrame::make(w, h, p.center);
    const float half_diag = 1.0f / frame.inv_half_diag;

    const float angle_rad = p.angle_deg * kDegToRad;
    const float dir_x = std::cos(angle_rad);
    const float dir_y = std::sin(angle_rad);

    const float k1 = solve_barrel_k(p.barrel, half_diag);
    const bool do_barrel = std::fabs(p.barrel) > kEpsilon;

    const float falloff = clamp(p.falloff, 0.05f, 8.0f);
    const bool multiplicative = (p.mode == ChromaMode::Zoom);

    // Per-channel multipliers, pre-multiplied by the master amount.
    const float mul_r = p.amount_r;
    const float mul_g = p.amount_g;
    const float mul_b = p.amount_b;

    const bool bicubic = (p.quality == ChromaQuality::Bicubic);
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);

    // If every channel multiplier is zero the effect is a no-op apart from the
    // mix, which is worth short-circuiting: users hit this when they first
    // apply the effect.
    const bool no_split = (std::fabs(mul_r) < kEpsilon &&
                           std::fabs(mul_g) < kEpsilon &&
                           std::fabs(mul_b) < kEpsilon &&
                           !do_barrel);

    for (int y = 0; y < h; ++y) {
        if ((y & 63) == 0 && ctx.aborted()) return;

        Float4* d = dst.row(y);
        const float fy = static_cast<float>(y);

        for (int x = 0; x < w; ++x) {
            if (no_split) {
                d[x] = sv.at(x, y);
                continue;
            }

            const float fx = static_cast<float>(x);

            // Vector from the dispersion centre.
            const float vx = fx - frame.cx;
            const float vy = fy - frame.cy;
            const float dist = std::sqrt(vx * vx + vy * vy);
            const float radial = dist * frame.inv_half_diag;

            // Barrel distortion first: it applies to every channel including
            // alpha, so the matte follows the warp.
            float bx = vx;
            float by = vy;
            if (do_barrel) {
                const float factor = 1.0f + k1 * dist * dist;
                bx = vx * factor;
                by = vy * factor;
            }

            // Direction and magnitude of the split at this pixel.
            float ux = 0.0f;
            float uy = 0.0f;
            float magnitude = 0.0f;

            switch (p.mode) {
                case ChromaMode::Radial:
                case ChromaMode::Barrel:
                    if (dist > kEpsilon) { ux = vx / dist; uy = vy / dist; }
                    magnitude = p.amount * p.radial_bias *
                                std::pow(std::max(radial, 0.0f), falloff);
                    break;

                case ChromaMode::Linear:
                    ux = dir_x;
                    uy = dir_y;
                    magnitude = p.amount;
                    break;

                case ChromaMode::Zoom:
                    // Multiplicative: reads further from the centre, so the
                    // centre stays perfectly sharp and the fringe grows with
                    // distance. Amount is a percentage in this mode.
                    if (dist > kEpsilon) { ux = vx / dist; uy = vy / dist; }
                    magnitude = 0.0f;
                    break;

                case ChromaMode::Spin:
                    // Tangential: rotates each channel by a slightly different
                    // angle instead of translating it.
                    if (dist > kEpsilon) { ux = -vy / dist; uy = vx / dist; }
                    magnitude = p.amount * p.radial_bias *
                                std::pow(std::max(radial, 0.0f), falloff);
                    break;

                case ChromaMode::Count:
                default:
                    break;
            }

            // Resolve the three channel sample positions.
            const auto channel_pos = [&](float mul, float& ox, float& oy) {
                const float m = multiplicative ? (p.amount * mul * kZoomPercent)
                                               : (magnitude * mul);
                if (multiplicative) {
                    ox = frame.cx + bx * (1.0f + m);
                    oy = frame.cy + by * (1.0f + m);
                } else {
                    ox = frame.cx + bx + ux * m;
                    oy = frame.cy + by + uy * m;
                }
            };

            float rx, ry, gx, gy, bxx, byy;
            channel_pos(mul_r, rx, ry);
            channel_pos(mul_g, gx, gy);
            channel_pos(mul_b, bxx, byy);

            // The un-split position -- what alpha is sampled from.
            const float ax = frame.cx + bx;
            const float ay = frame.cy + by;

            Float4 sr, sg, sb;
            if (bicubic) {
                sr = sv.sample_bicubic(rx, ry, p.wrap);
                sg = sv.sample_bicubic(gx, gy, p.wrap);
                sb = sv.sample_bicubic(bxx, byy, p.wrap);
            } else {
                sr = sv.sample_bilinear(rx, ry, p.wrap);
                sg = sv.sample_bilinear(gx, gy, p.wrap);
                sb = sv.sample_bilinear(bxx, byy, p.wrap);
            }

            // Reuse a channel's sample for alpha when it already sits at the
            // un-split position -- common when green is left at zero.
            Float4 sa;
            const auto same_as_ref = [&](float px, float py) {
                return std::fabs(px - ax) < 1.0e-4f && std::fabs(py - ay) < 1.0e-4f;
            };
            if (same_as_ref(gx, gy)) {
                sa = sg;
            } else if (same_as_ref(rx, ry)) {
                sa = sr;
            } else if (same_as_ref(bxx, byy)) {
                sa = sb;
            } else {
                sa = bicubic ? sv.sample_bicubic(ax, ay, p.wrap)
                             : sv.sample_bilinear(ax, ay, p.wrap);
            }

            const Float4 outc{sr.r, sg.g, sb.b, sa.a};
            d[x] = identity_mix ? outc : lerp(sv.at(x, y), outc, mix);
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
