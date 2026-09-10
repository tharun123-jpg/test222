// =============================================================================
//  mgtk/fx_halftone.cpp -- Halftone Pro
//
//  A screen-angled halftone with six geometrically distinct patterns, five dot
//  shapes, and three colour models.
//
//  The design in one sentence: every pattern is expressed as
//
//      "how far is this pixel from the nearest element centre, normalised so
//       that a value of 1 means the element exactly fills its cell?"
//
//  Once a pattern answers that, the whole rest of the effect -- ink density,
//  gamma, contrast, anti-aliasing, colour models -- is shared.
//
//  Note on `pattern` versus `dot_shape`: they are two independent axes, not
//  duplicates. `pattern` chooses the screen geometry (a dot lattice, a line
//  screen, a cross-hatch, an inverted dot screen, an offset lattice, or
//  concentric rings). `dot_shape` refines the element used *within* the Dots
//  pattern only.
// =============================================================================
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

// Area coefficient `k` such that (element area / cell area) = k * r^2 for an
// element of "radius" r in cell-local coordinates spanning [-1,1]^2.
//
// Derivation for the round dot: the cell has area 4; a disc of radius r has
// area pi*r^2; so the coverage is pi*r^2/4, giving k = pi/4.
constexpr float kAreaRound = 0.7853981634f;   // pi/4
constexpr float kAreaSquare = 1.0f;           // half-width r -> area (2r)^2 = 4r^2
constexpr float kAreaDiamond = 0.5f;          // |x|+|y| <= r -> area 2r^2
constexpr float kAreaEllipse = kAreaRound;    // metric is area-preserving by construction

// The plus/cross element is the union of two bars, whose area does not have a
// tidy closed form. This coefficient was measured numerically at a few
// densities and is a close fit across the useful range. A couple of percent of
// density error in one of five shapes is not something an eye can pick out,
// and the alternative -- a numerical solve per pixel -- is absurd.
constexpr float kAreaCross = 0.62f;
constexpr float kCrossBarRatio = 0.35f;

// Every pattern ultimately produces this: the signed distance, in cell-local
// units, from the pixel to the nearest element boundary.
struct ElementSample {
    float distance = 0.0f;   // normalised so 1.0 == element exactly fills the cell
    float area_coeff = kAreaRound;
};

// Cell-local coordinates run -1..1 across one cell.
inline void cell_local(float rx, float ry, float cell_size, float& lx, float& ly) {
    const float inv = 1.0f / cell_size;
    const float cx = std::floor(rx * inv);
    const float cy = std::floor(ry * inv);
    lx = (rx * inv - cx) * 2.0f - 1.0f;
    ly = (ry * inv - cy) * 2.0f - 1.0f;
}

ElementSample dot_element(float lx, float ly, HalftoneDotShape shape) {
    ElementSample e;
    switch (shape) {
        case HalftoneDotShape::Round:
            e.distance = std::sqrt(lx * lx + ly * ly);
            e.area_coeff = kAreaRound;
            break;

        case HalftoneDotShape::Ellipse: {
            // A 2:1 ellipse. The metric is scaled anisotropically in a way that
            // preserves area (sqrt(2) by 1/sqrt(2)), so the density curve is
            // identical to the round dot's.
            constexpr float kAx = 1.41421356f;
            constexpr float kAy = 0.70710678f;
            e.distance = std::sqrt(lx * kAx * lx * kAx + ly * kAy * ly * kAy);
            e.area_coeff = kAreaEllipse;
            break;
        }

        case HalftoneDotShape::Square:
            e.distance = max2(std::fabs(lx), std::fabs(ly));
            e.area_coeff = kAreaSquare;
            break;

        case HalftoneDotShape::Diamond:
            e.distance = std::fabs(lx) + std::fabs(ly);
            e.area_coeff = kAreaDiamond;
            break;

        case HalftoneDotShape::Cross: {
            const float ax = std::fabs(lx);
            const float ay = std::fabs(ly);
            // Union of a horizontal and a vertical bar.
            const float horizontal = max2(ax, ay * kCrossBarRatio);
            const float vertical = max2(ax * kCrossBarRatio, ay);
            e.distance = min2(horizontal, vertical);
            e.area_coeff = kAreaCross;
            break;
        }

        case HalftoneDotShape::Count:
        default:
            e.distance = std::sqrt(lx * lx + ly * ly);
            e.area_coeff = kAreaRound;
            break;
    }
    return e;
}

// Tonal shaping shared by every colour model. Operates in perceptual space.
inline float shape_tone(float v, float threshold, float contrast, float gamma) {
    // Gamma first: it is a display-referred correction.
    if (std::fabs(gamma - 1.0f) > kEpsilon && v > 0.0f) {
        v = std::pow(v, 1.0f / clamp(gamma, 0.05f, 20.0f));
    }
    // Contrast about the mid point, then threshold as an offset.
    v = (v - 0.5f) * contrast + 0.5f + threshold;
    return v;
}

}  // namespace

void apply_halftone(const Image& src, Image& dst, const HalftoneParams& p,
                    const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }
    if (dst.width() != w || dst.height() != h) dst.resize(w, h);

    // Note: no ImageView here. Unlike the other effects, halftone never samples
    // off-pixel -- every output pixel is derived from the input pixel at the
    // same position, plus the screen geometry.
    const float cell = std::max(p.cell_size, 1.0f);
    const float contrast = clamp(p.contrast, 0.0f, 8.0f);
    const float gamma = clamp(p.gamma, 0.05f, 20.0f);
    const float threshold = p.threshold;
    const float ink_amount = saturate(p.ink_amount);
    const float line_width = clamp(p.line_width, 0.01f, 1.0f);
    const bool invert = p.invert;
    const bool linear = ctx.input_linear;

    const float aa_pixels = std::max(p.anti_alias, 0.0f);
    // Convert the anti-alias width from pixels into cell-local units: one cell
    // spans two cell-local units, so a pixel is 2/cell units.
    const float aa = (aa_pixels > kEpsilon) ? (aa_pixels * 2.0f / cell) : 0.0f;

    // Screen angle per channel. The 30-degree spacing between process inks is
    // what stops the three screens from beating against each other, and it is
    // why a naive RGB halftone with no angle spread looks like moire soup.
    const float base_angle = p.angle_deg * kDegToRad;
    const float spread = p.angle_spread_deg * kDegToRad;

    // The concentric pattern is the only one whose elements are laid out
    // relative to the frame rather than to a tiled lattice.
    const RadialFrame center_frame = RadialFrame::make(w, h, Vec2{0.5f, 0.5f});

    const int channel_count = (p.color_mode == HalftoneColorMode::CMYK) ? 4 : 3;

    // Precompute the per-channel rotation.
    float chan_cos[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float chan_sin[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int c = 0; c < channel_count; ++c) {
        const float a = base_angle + spread * static_cast<float>(c);
        chan_cos[c] = std::cos(a);
        chan_sin[c] = std::sin(a);
    }

    const BlendMode blend = p.blend;
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);

    // Resolves the ink coverage for one channel of one pixel. Returns a value
    // in [0,1] where 1 means "this pixel is inside the ink element" and 0 means
    // "bare substrate". Anti-aliasing turns the hard test into a ramp.
    const auto coverage = [&](float value, int channel, float x, float y) -> float {
        // Ink density rises as the source gets darker.
        float ink = 1.0f - saturate(shape_tone(value, threshold, contrast, gamma));
        if (invert) ink = 1.0f - ink;

        // Rotate into the screen's coordinate frame.
        const float rx = x * chan_cos[channel] - y * chan_sin[channel];
        const float ry = x * chan_sin[channel] + y * chan_cos[channel];

        float lx, ly;
        cell_local(rx, ry, cell, lx, ly);

        const float inv_cell = 1.0f / cell;

        switch (p.pattern) {
            case HalftonePattern::Lines: {
                // Distance from the line centre, in cell-local units.
                const float d = std::fabs(ly);
                const float radius = ink * line_width;
                return (aa > 0.0f) ? (1.0f - smoothstep(radius - aa, radius + aa, d))
                                   : ((d <= radius) ? 1.0f : 0.0f);
            }

            case HalftonePattern::Cross: {
                const float dx = std::fabs(lx);
                const float dy = std::fabs(ly);
                const float radius = ink * line_width;
                const float a = (aa > 0.0f)
                                    ? (1.0f - smoothstep(radius - aa, radius + aa, dy))
                                    : ((dy <= radius) ? 1.0f : 0.0f);
                const float b = (aa > 0.0f)
                                    ? (1.0f - smoothstep(radius - aa, radius + aa, dx))
                                    : ((dx <= radius) ? 1.0f : 0.0f);
                return max2(a, b);
            }

            case HalftonePattern::Concentric: {
                // Rings centred on the layer rather than on a tiled lattice.
                const float px = x - center_frame.cx;
                const float py = y - center_frame.cy;
                const float radius = std::sqrt(px * px + py * py);
                const float phase = fmod_positive(radius + cell * 0.5f, cell) - cell * 0.5f;
                const float d = std::fabs(phase) * 2.0f * inv_cell;
                const float limit = ink * line_width;
                return (aa > 0.0f) ? (1.0f - smoothstep(limit - aa, limit + aa, d))
                                   : ((d <= limit) ? 1.0f : 0.0f);
            }

            case HalftonePattern::Squares: {
                // Offset the lattice by half a cell on alternate rows, which
                // breaks the square grid's diagonal banding.
                const float row = std::floor(ry * inv_cell);
                const float shifted = (static_cast<int>(row) & 1) ? (rx + cell * 0.5f) : rx;
                const float shifted_lx = [&] {
                    const float c = std::floor(shifted * inv_cell);
                    return (shifted * inv_cell - c) * 2.0f - 1.0f;
                }();
                ElementSample e = dot_element(shifted_lx, ly, HalftoneDotShape::Square);
                const float r = std::sqrt(saturate(ink) / e.area_coeff);
                return (aa > 0.0f) ? (1.0f - smoothstep(r - aa, r + aa, e.distance))
                                   : ((e.distance <= r) ? 1.0f : 0.0f);
            }

            case HalftonePattern::DiamondGrid: {
                // The inverse dot screen: ink fills the cell except for a
                // dot-shaped hole. Reading as "inverted halftone", it is the
                // pattern behind a lot of high-contrast poster art.
                ElementSample e = dot_element(lx, ly, HalftoneDotShape::Diamond);
                const float r = std::sqrt(saturate(1.0f - ink) / e.area_coeff);
                const float inside = (aa > 0.0f)
                                         ? (1.0f - smoothstep(r - aa, r + aa, e.distance))
                                         : ((e.distance <= r) ? 1.0f : 0.0f);
                return 1.0f - inside;
            }

            case HalftonePattern::Dots:
            case HalftonePattern::Count:
            default: {
                ElementSample e = dot_element(lx, ly, p.dot_shape);
                const float r = std::sqrt(saturate(ink) / e.area_coeff);
                return (aa > 0.0f) ? (1.0f - smoothstep(r - aa, r + aa, e.distance))
                                   : ((e.distance <= r) ? 1.0f : 0.0f);
            }
        }
    };

    for (int y = 0; y < h; ++y) {
        if ((y & 31) == 0 && ctx.aborted()) return;

        const Float4* s = src.row(y);
        Float4* d = dst.row(y);
        const float fy = static_cast<float>(y);

        for (int x = 0; x < w; ++x) {
            const float fx = static_cast<float>(x);
            const Float4 src_px = s[x];

            // Perceptual channel values drive ink density, so that the effect
            // looks the same in an 8-bit and a 32-bit project.
            const float vr = to_perceptual(src_px.r, linear);
            const float vg = to_perceptual(src_px.g, linear);
            const float vb = to_perceptual(src_px.b, linear);

            Float4 halftoned;

            switch (p.color_mode) {
                case HalftoneColorMode::Monochrome: {
                    const float v = luma(vr, vg, vb);
                    const float cov = coverage(v, 0, fx, fy);
                    halftoned = lerp(p.paper, p.ink, cov * ink_amount);
                    halftoned.a = src_px.a;
                    break;
                }

                case HalftoneColorMode::RGB: {
                    const float cr = coverage(vr, 0, fx, fy);
                    const float cg = coverage(vg, 1, fx, fy);
                    const float cb = coverage(vb, 2, fx, fy);
                    const Float4 ink_color{cr, cg, cb, 1.0f};
                    halftoned = lerp(p.paper, ink_color, ink_amount);
                    halftoned.a = src_px.a;
                    break;
                }

                case HalftoneColorMode::CMYK: {
                    // cyan absorbs red, magenta absorbs green, yellow absorbs
                    // blue; black absorbs everything.
                    const float c = coverage(vr, 0, fx, fy);
                    const float m = coverage(vg, 1, fx, fy);
                    const float yl = coverage(vb, 2, fx, fy);
                    const float k = coverage(luma(vr, vg, vb), 3, fx, fy);

                    const float rr = p.paper.r * (1.0f - c) * (1.0f - k);
                    const float gg = p.paper.g * (1.0f - m) * (1.0f - k);
                    const float bb = p.paper.b * (1.0f - yl) * (1.0f - k);

                    const Float4 process{rr, gg, bb, src_px.a};
                    halftoned = lerp(p.paper, process, ink_amount);
                    break;
                }

                case HalftoneColorMode::Count:
                default:
                    halftoned = src_px;
                    break;
            }

            // The pattern must not reveal colour underneath a soft matte, so
            // the halftone is faded in by the source's own alpha and the matte
            // passes through untouched.
            const float a = saturate(src_px.a);
            Float4 mixed{lerp(src_px.r, halftoned.r, a),
                         lerp(src_px.g, halftoned.g, a),
                         lerp(src_px.b, halftoned.b, a),
                         src_px.a};

            if (blend != BlendMode::Normal) {
                const Float4 blended = blend_pixel(blend, src_px, mixed);
                mixed.r = blended.r;
                mixed.g = blended.g;
                mixed.b = blended.b;
            }
            mixed.a = src_px.a;   // the matte is never the effect's business

            d[x] = identity_mix ? mixed : lerp(src_px, mixed, mix);
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
