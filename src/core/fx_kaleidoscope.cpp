// =============================================================================
//  mgtk/fx_kaleidoscope.cpp -- Kaleidoscope
//
//  The whole effect reduces to one question asked per pixel: "given this
//  pixel's angle and distance from the centre, which single point of the source
//  should it read from?". Answer that and the four modes fall out of it.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// Fold an angle into [0, sector). Handles negative angles and angles beyond a
// full turn, both of which are routine once the user animates Rotation.
inline float fold_to_sector(float angle, float sector) {
    return fmod_positive(angle, sector);
}

// Reflect within the wedge so that the two halves of every sector are mirror
// images -- the seam is what makes a kaleidoscope read as a kaleidoscope
// rather than as a pinwheel.
inline float mirror_within_sector(float local, float sector) {
    const float half = sector * 0.5f;
    return (local > half) ? (sector - local) : local;
}

}  // namespace

void apply_kaleidoscope(const Image& src, Image& dst,
                        const KaleidoscopeParams& p, const RenderContext& ctx) {
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

    const int segments = clamp(p.segments, 2, 64);
    const float sector = kTwoPi / static_cast<float>(segments);

    const float rotation = p.rotation_deg * kDegToRad;
    const float scale = (std::fabs(p.scale) < 1.0e-4f) ? 1.0e-4f : p.scale;
    const float inv_scale = 1.0f / scale;

    const float ox = p.offset_x;
    const float oy = p.offset_y;

    const KaleidoMode mode = p.mode;
    const bool mirrored = (mode == KaleidoMode::Mirror || mode == KaleidoMode::MirrorRotate);
    const bool quilt = (mode == KaleidoMode::Quilt);

    // MirrorRotate additionally rotates the source content within each wedge,
    // which moves the mirror seam off the sector boundary and produces a
    // denser, more crystalline mandala than plain Mirror.
    const float content_rotation_deg = (mode == KaleidoMode::MirrorRotate)
                                           ? p.rotation_deg + 90.0f / static_cast<float>(segments)
                                           : p.rotation_deg;
    const float content_rotation = content_rotation_deg * kDegToRad;

    const bool bicubic = (p.quality == ChromaQuality::Bicubic);
    const float fade = saturate(p.radial_fade);
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);

    // Half-extents used by the quilt mode's folding.
    const float half_w = static_cast<float>(w) * 0.5f;
    const float half_h = static_cast<float>(h) * 0.5f;

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && ctx.aborted()) return;

        Float4* d = dst.row(y);
        const float fy = static_cast<float>(y);

        for (int x = 0; x < w; ++x) {
            const float fx = static_cast<float>(x);

            // Move into centre-relative space, undo the offset, and undo the
            // scale so that a larger Scale zooms the source up.
            float vx = (fx - frame.cx - ox) * inv_scale;
            float vy = (fy - frame.cy - oy) * inv_scale;

            float sample_x;
            float sample_y;

            if (quilt) {
                // Fold the layer into one quadrant and mirror it back out, so
                // the result is symmetric about the centre on both axes.
                //
                // mirror_coord reflects a coordinate into [0, extent] about
                // both 0 and `extent`. Running the (possibly rotated) offset
                // through it gives the fold for free, and -- because it keeps
                // folding rather than clamping -- a Scale below 1 turns the
                // single fold into a repeating mirrored tile instead of
                // smearing the edge pixels.
                const float cs = std::cos(rotation);
                const float sn = std::sin(rotation);
                const float rvx = vx * cs + vy * sn;
                const float rvy = -vx * sn + vy * cs;

                sample_x = frame.cx + mirror_coord(rvx, half_w);
                sample_y = frame.cy + mirror_coord(rvy, half_h);
            } else {
                // Undo the user's rotation, then fold the angle into one wedge.
                const float cs = std::cos(rotation);
                const float sn = std::sin(rotation);
                const float rvx = vx * cs + vy * sn;
                const float rvy = -vx * sn + vy * cs;

                const float radius = std::sqrt(rvx * rvx + rvy * rvy);
                const float angle = std::atan2(rvy, rvx);

                float local = fold_to_sector(angle, sector);
                if (mirrored) {
                    local = mirror_within_sector(local, sector);
                }

                // Rebuild a position from the folded angle and the original
                // radius.
                const float out_angle = local + content_rotation;
                sample_x = frame.cx + std::cos(out_angle) * radius;
                sample_y = frame.cy + std::sin(out_angle) * radius;
            }

            Float4 out = bicubic ? sv.sample_bicubic(sample_x, sample_y, WrapMode::Clamp)
                                 : sv.sample_bilinear(sample_x, sample_y, WrapMode::Clamp);

            if (fade > 0.0f) {
                // Dim towards the outer edge of each wedge. Applied to the
                // sampled result rather than as a vignette on the output, so
                // that it follows the wedge rather than the frame.
                const float dx = sample_x - frame.cx;
                const float dy = sample_y - frame.cy;
                const float r = std::sqrt(dx * dx + dy * dy) / half_diag;
                const float attenuation = 1.0f - fade * saturate(r);
                out.r *= attenuation;
                out.g *= attenuation;
                out.b *= attenuation;
            }

            d[x] = identity_mix ? out : lerp(sv.at(x, y), out, mix);
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
