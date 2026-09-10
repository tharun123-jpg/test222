// =============================================================================
//  mgtk/fx_slitscan.cpp -- Slit Scan
//
//  Four temporal operations over a stack of past frames:
//
//    TimeSlice    -- each spatial strip comes from a different frame
//    TimeBlend    -- a uniform average across the stack (temporal motion blur)
//    TimeEcho     -- a weighted trail, newest frames dominant
//    TimeDisplace -- the frame-to-frame difference drives a spatial displacement
//
//  The frame stack is supplied by the caller. In After Effects the glue layer
//  checks out N frames in the past with PF_CHECKOUT_PARAM and hands them over
//  newest-first. Keeping the stack outside the core is what lets the whole
//  thing be tested with synthetic frames and no host at all.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// Coordinate that drives which frame a pixel reads from.
inline float direction_coordinate(SlitDirection dir, float x, float y,
                                  const RadialFrame& frame, float angle_rad) {
    switch (dir) {
        case SlitDirection::Vertical:
            return y;

        case SlitDirection::Radial: {
            const float dx = x - frame.cx;
            const float dy = y - frame.cy;
            return std::sqrt(dx * dx + dy * dy);
        }

        case SlitDirection::Angular: {
            // Angle measured from the user's reference direction, unwrapped to
            // a positive range so that the strip pattern can be animated past
            // a full turn without a discontinuity.
            const float dx = x - frame.cx;
            const float dy = y - frame.cy;
            const float a = std::atan2(dy, dx) - angle_rad;
            return fmod_positive(a, kTwoPi) * frame.inv_half_diag;
        }

        case SlitDirection::Horizontal:
        case SlitDirection::Count:
        default:
            return x;
    }
}

// Wrap an index into the middle of the available range. Wrapping rather than
// clamping means a large `frames` setting still produces a repeating band
// pattern instead of a flat region once the pattern runs off the start of the
// history.
inline int wrap_index(int index, int count) {
    if (count <= 0) return 0;
    return imod(index, count);
}

// Ping-pong the index so the pattern reads forwards then backwards.
inline int mirror_index(int index, int count) {
    if (count <= 1) return 0;
    const int period = 2 * count - 2;
    int m = imod(index, period);
    if (m >= count) m = period - m;
    return m;
}

}  // namespace

void apply_slit_scan(const Image& src, const std::vector<Image>& history, Image& dst,
                     const SlitScanParams& p, const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }
    if (dst.width() != w || dst.height() != h) dst.resize(w, h);

    // A missing or undersized history degrades to a pass-through rather than
    // producing garbage. This happens legitimately when the effect sits at
    // frame 0 of a composition and there is nothing behind it yet.
    const int available = static_cast<int>(history.size());
    if (available <= 0) {
        dst = src;
        return;
    }

    const int usable = clamp(std::min(available, p.frames), 1, available);

    const ImageView sv = src.view();
    const RadialFrame frame = RadialFrame::make(w, h, p.center);
    const float angle_rad = p.angle_deg * kDegToRad;

    const float slice_width = std::max(p.slice_width, 0.5f);
    const float offset = p.offset;
    const float falloff = saturate(p.falloff);
    const float intensity = p.intensity;
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);
    const BlendMode blend = p.blend;

    // Precompute the echo weights for TimeEcho. `intensity` acts as the
    // per-step decay, so it doubles as the echo's decay control.
    const float echo_decay = clamp(intensity, 0.0f, 1.0f);
    float echo_weight_sum = 0.0f;
    std::vector<float> echo_weights(static_cast<std::size_t>(usable));
    {
        float wgt = 1.0f;
        for (int i = 0; i < usable; ++i) {
            echo_weights[static_cast<std::size_t>(i)] = wgt;
            echo_weight_sum += wgt;
            wgt *= echo_decay;
        }
        if (echo_weight_sum <= kEpsilon) echo_weight_sum = 1.0f;
    }

    const float blend_ramp = falloff;

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && ctx.aborted()) return;

        const Float4* s = src.row(y);
        Float4* d = dst.row(y);
        const float fy = static_cast<float>(y);

        for (int x = 0; x < w; ++x) {
            const float fx = static_cast<float>(x);
            const float coord =
                direction_coordinate(p.direction, fx, fy, frame, angle_rad);

            Float4 out;

            switch (p.mode) {
                case SlitScanMode::TimeSlice: {
                    // Which strip of time does this pixel sit in?
                    const float f = (coord + offset) / slice_width;
                    const float f0 = std::floor(f);
                    const float frac = f - f0;

                    int i0 = static_cast<int>(f0);
                    int i1 = i0 + 1;
                    if (p.mirror) {
                        i0 = mirror_index(i0, usable);
                        i1 = mirror_index(i1, usable);
                    } else {
                        i0 = wrap_index(i0, usable);
                        i1 = wrap_index(i1, usable);
                    }

                    // falloff controls the width of the transition between
                    // adjacent strips. At 0 the strips butt together with hard
                    // edges; at 1 the whole strip cross-fades into the next.
                    float t;
                    if (blend_ramp <= kEpsilon) {
                        t = (frac >= 0.5f) ? 1.0f : 0.0f;
                    } else {
                        t = smoothstep(0.5f - blend_ramp * 0.5f,
                                       0.5f + blend_ramp * 0.5f, frac);
                    }

                    const Float4 a = history[static_cast<std::size_t>(i0)].view().at(x, y);
                    const Float4 b = history[static_cast<std::size_t>(i1)].view().at(x, y);
                    out = lerp(a, b, t);
                    break;
                }

                case SlitScanMode::TimeBlend: {
                    // A straight average is exactly what a real shutter does.
                    Float4 acc{};
                    for (int i = 0; i < usable; ++i) {
                        acc += history[static_cast<std::size_t>(i)].view().at(x, y);
                    }
                    out = acc * (1.0f / static_cast<float>(usable));
                    break;
                }

                case SlitScanMode::TimeEcho: {
                    Float4 acc{};
                    for (int i = 0; i < usable; ++i) {
                        acc += history[static_cast<std::size_t>(i)].view().at(x, y) *
                               echo_weights[static_cast<std::size_t>(i)];
                    }
                    out = acc * (1.0f / echo_weight_sum);
                    break;
                }

                case SlitScanMode::TimeDisplace: {
                    // Displace along the frame-to-frame difference. This is a
                    // cheap stand-in for optical flow: it tracks the *sign* of
                    // motion, so a moving edge smears in the direction it is
                    // travelling.
                    Float4 diff{};
                    if (usable > 1) {
                        for (int i = 0; i + 1 < usable; ++i) {
                            const Float4 a = history[static_cast<std::size_t>(i)].view().at(x, y);
                            const Float4 b = history[static_cast<std::size_t>(i + 1)].view().at(x, y);
                            diff += a - b;
                        }
                        diff = diff * (1.0f / static_cast<float>(usable - 1));
                    }

                    // Scale the pixel-space displacement. The factor is chosen
                    // so that intensity = 1 on a full-black-to-full-white edge
                    // gives roughly a 100px smear, which is the useful range.
                    const float scale = intensity * 100.0f;
                    out = sv.sample_bilinear(fx - diff.r * scale,
                                             fy - diff.g * scale, WrapMode::Clamp);
                    break;
                }

                case SlitScanMode::Count:
                default:
                    out = s[x];
                    break;
            }

            if (blend != BlendMode::Normal) {
                const Float4 blended = blend_pixel(blend, s[x], out);
                out.r = blended.r;
                out.g = blended.g;
                out.b = blended.b;
            }

            d[x] = identity_mix ? out : lerp(s[x], out, mix);
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
