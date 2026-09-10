// =============================================================================
//  mgtk/fx_common.cpp -- helpers shared between the effects
// =============================================================================
#include "mgtk/effects.hpp"

namespace mgtk {

// ---------------------------------------------------------------------------
//  Quality
// ---------------------------------------------------------------------------
const char* quality_name(Quality q) {
    switch (q) {
        case Quality::Draft: return "Draft (Fastest)";
        case Quality::Good:  return "Good";
        case Quality::Best:  return "Best (Slowest)";
        case Quality::Count:
        default:             return "Good";
    }
}

int quality_count() { return static_cast<int>(Quality::Count); }

Quality quality_from_index(int index) {
    const int n = quality_count();
    if (index < 0 || index >= n) return Quality::Good;
    return static_cast<Quality>(index);
}

// ---------------------------------------------------------------------------
//  Colour helpers
// ---------------------------------------------------------------------------
Float4 rotate_hue(Float4 c, float degrees) {
    if (std::fabs(degrees) < kEpsilon) return c;

    const float rad = degrees * kDegToRad;
    const float cs = std::cos(rad);
    const float sn = std::sin(rad);

    // The hueRotate matrix from the SVG/CSS colour-matrix spec. It rotates
    // about the luma axis, which keeps perceived brightness stable as the hue
    // sweeps -- the naive RGB rotation turns grey into coloured noise.
    const float m00 = 0.213f + cs * 0.787f - sn * 0.213f;
    const float m01 = 0.715f - cs * 0.715f - sn * 0.715f;
    const float m02 = 0.072f - cs * 0.072f + sn * 0.928f;

    const float m10 = 0.213f - cs * 0.213f + sn * 0.143f;
    const float m11 = 0.715f + cs * 0.285f + sn * 0.140f;
    const float m12 = 0.072f - cs * 0.072f - sn * 0.283f;

    const float m20 = 0.213f - cs * 0.213f - sn * 0.787f;
    const float m21 = 0.715f - cs * 0.715f + sn * 0.715f;
    const float m22 = 0.072f + cs * 0.928f + sn * 0.072f;

    return Float4{m00 * c.r + m01 * c.g + m02 * c.b,
                  m10 * c.r + m11 * c.g + m12 * c.b,
                  m20 * c.r + m21 * c.g + m22 * c.b,
                  c.a};
}

Float4 adjust_saturation(Float4 c, float saturation) {
    if (std::fabs(saturation - 1.0f) < kEpsilon) return c;
    const float l = luma(c.r, c.g, c.b);
    return Float4{lerp(l, c.r, saturation),
                  lerp(l, c.g, saturation),
                  lerp(l, c.b, saturation),
                  c.a};
}

Float4 apply_tint(Float4 c, Float4 tint, float amount) {
    if (amount <= 0.0f) return c;
    const float a = clamp(amount, 0.0f, 1.0f);
    // Multiply keeps highlights bright, which reads as "coloured light" rather
    // than "painted over" -- the right behaviour for a glow tint.
    return Float4{lerp(c.r, c.r * tint.r, a),
                  lerp(c.g, c.g * tint.g, a),
                  lerp(c.b, c.b * tint.b, a),
                  c.a};
}

// ---------------------------------------------------------------------------
//  Radial frame
// ---------------------------------------------------------------------------
RadialFrame RadialFrame::make(int width, int height, Vec2 center_norm) {
    RadialFrame f;
    // Normalised coordinate -> pixel-index coordinate. Normalised (0.5, 0.5) is
    // the layer centre, which in index space is (width-1)/2; subtracting the
    // half pixel is what makes that identity hold.
    f.cx = center_norm.x * static_cast<float>(width) - 0.5f;
    f.cy = center_norm.y * static_cast<float>(height) - 0.5f;

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    // Half the diagonal: the distance from the centre to a corner. Normalising
    // by this rather than by width or height means a radial falloff reaches 1
    // exactly at the corners, whatever the aspect ratio.
    const float half_diag = 0.5f * std::sqrt(w * w + h * h);
    f.inv_half_diag = (half_diag > kEpsilon) ? (1.0f / half_diag) : 1.0f;
    return f;
}

float RadialFrame::radius_at(float x, float y) const {
    const float dx = x - cx;
    const float dy = y - cy;
    return std::sqrt(dx * dx + dy * dy) * inv_half_diag;
}

}  // namespace mgtk
