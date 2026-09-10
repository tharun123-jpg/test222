// =============================================================================
//  mgtk/blend.cpp
// =============================================================================
#include "mgtk/blend.hpp"

namespace mgtk {

namespace {

// W3C compositing spec helper curve.
//
// The inputs are clamped to [0,1] first, and that is not cosmetic. The spec
// formula is only defined over that range; given HDR values -- entirely normal
// in a 32-bit float project, where an additive glow easily pushes a channel to
// 4.0 -- its `d` branch diverges. soft_light(-2, 4) evaluates to roughly -660,
// which would punch a black hole through the composite. Clamping the *inputs*
// rather than the result keeps soft light behaving as the gentle contrast
// operator it is meant to be, at any exposure.
inline float soft_light_channel(float b, float s) {
    b = saturate(b);
    s = saturate(s);
    if (s <= 0.5f) {
        return b - (1.0f - 2.0f * s) * b * (1.0f - b);
    }
    const float d = (b <= 0.25f) ? ((16.0f * b - 12.0f) * b + 4.0f) * b : std::sqrt(b);
    return b + (2.0f * s - 1.0f) * (d - b);
}

inline float dodge_channel(float b, float s) {
    if (b <= 0.0f) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    return saturate(b / (1.0f - s));
}

inline float burn_channel(float b, float s) {
    if (b >= 1.0f) return 1.0f;
    if (s <= 0.0f) return 0.0f;
    return 1.0f - saturate((1.0f - b) / s);
}

inline float vivid_light_channel(float b, float s) {
    return (s <= 0.5f) ? burn_channel(b, 2.0f * s)
                       : dodge_channel(b, 2.0f * s - 1.0f);
}

inline float linear_light_channel(float b, float s) {
    return saturate(b + 2.0f * s - 1.0f);
}

inline float pin_light_channel(float b, float s) {
    if (s <= 0.5f) return min2(b, 2.0f * s);
    return max2(b, 2.0f * s - 1.0f);
}

}  // namespace

float blend_channel(BlendMode mode, float b, float s) {
    switch (mode) {
        case BlendMode::Normal:      return s;

        case BlendMode::Add:
        case BlendMode::LinearDodge: return b + s;

        case BlendMode::Subtract:    return b - s;
        case BlendMode::Multiply:    return b * s;
        case BlendMode::Divide:      return (std::fabs(s) > kEpsilon) ? b / s : 0.0f;

        case BlendMode::Screen:      return b + s - b * s;

        case BlendMode::Overlay:     return (b <= 0.5f) ? (2.0f * b * s)
                                                        : (1.0f - 2.0f * (1.0f - b) * (1.0f - s));
        case BlendMode::SoftLight:   return soft_light_channel(b, s);
        case BlendMode::HardLight:   return (s <= 0.5f) ? (2.0f * b * s)
                                                        : (1.0f - 2.0f * (1.0f - b) * (1.0f - s));

        case BlendMode::Difference:  return std::fabs(b - s);
        case BlendMode::Exclusion:   return b + s - 2.0f * b * s;

        case BlendMode::Lighten:     return max2(b, s);
        case BlendMode::Darken:      return min2(b, s);

        case BlendMode::ColorDodge:  return dodge_channel(b, s);
        case BlendMode::ColorBurn:   return burn_channel(b, s);
        case BlendMode::LinearBurn:  return b + s - 1.0f;
        case BlendMode::VividLight:  return vivid_light_channel(b, s);
        case BlendMode::LinearLight: return linear_light_channel(b, s);
        case BlendMode::PinLight:    return pin_light_channel(b, s);
        case BlendMode::HardMix:     return (b + s >= 1.0f) ? 1.0f : 0.0f;

        case BlendMode::Average:     return (b + s) * 0.5f;

        case BlendMode::Count:
        default:                     return s;
    }
}

Float4 blend_pixel(BlendMode mode, Float4 backdrop, Float4 source) {
    if (mode == BlendMode::Normal) return source;
    return Float4{blend_channel(mode, backdrop.r, source.r),
                  blend_channel(mode, backdrop.g, source.g),
                  blend_channel(mode, backdrop.b, source.b),
                  // Alpha is never "blended" by the colour formulas; it always
                  // follows the source.
                  source.a};
}

Float4 composite_over(Float4 under, Float4 over) {
    // Straight-alpha source-over. The colour terms are the *premultiplied*
    // numerator, so they must be divided by the result alpha to come back to
    // straight colour -- which is what this module promises to work in.
    // Omitting that division is a subtle bug: an opaque source hides it
    // completely, because out_a is then 1, and it only shows up when a
    // semi-transparent element is composited onto something transparent.
    const float av = saturate(over.a);
    const float ua = saturate(under.a);
    const float inv = 1.0f - av;
    const float out_a = av + ua * inv;
    if (out_a <= kEpsilon) return Float4{};

    const float inv_a = 1.0f / out_a;
    return Float4{(over.r * av + under.r * ua * inv) * inv_a,
                  (over.g * av + under.g * ua * inv) * inv_a,
                  (over.b * av + under.b * ua * inv) * inv_a,
                  out_a};
}

Float4 composite_over(Float4 under, Float4 over, float opacity) {
    over.a *= saturate(opacity);
    return composite_over(under, over);
}

const char* blend_mode_name(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:      return "Normal";
        case BlendMode::Add:         return "Add";
        case BlendMode::Subtract:    return "Subtract";
        case BlendMode::Multiply:    return "Multiply";
        case BlendMode::Screen:      return "Screen";
        case BlendMode::Overlay:     return "Overlay";
        case BlendMode::SoftLight:   return "Soft Light";
        case BlendMode::HardLight:   return "Hard Light";
        case BlendMode::Difference:  return "Difference";
        case BlendMode::Exclusion:   return "Exclusion";
        case BlendMode::Lighten:     return "Lighten";
        case BlendMode::Darken:      return "Darken";
        case BlendMode::ColorDodge:  return "Color Dodge";
        case BlendMode::ColorBurn:   return "Color Burn";
        case BlendMode::LinearDodge: return "Linear Dodge";
        case BlendMode::LinearBurn:  return "Linear Burn";
        case BlendMode::VividLight:  return "Vivid Light";
        case BlendMode::LinearLight: return "Linear Light";
        case BlendMode::PinLight:    return "Pin Light";
        case BlendMode::HardMix:     return "Hard Mix";
        case BlendMode::Divide:      return "Divide";
        case BlendMode::Average:     return "Average";
        case BlendMode::Count:
        default:                     return "Normal";
    }
}

int blend_mode_count() { return static_cast<int>(BlendMode::Count); }

BlendMode blend_mode_from_index(int index) {
    const int n = blend_mode_count();
    if (index < 0 || index >= n) return BlendMode::Normal;
    return static_cast<BlendMode>(index);
}

const char* wrap_mode_name(WrapMode mode) {
    switch (mode) {
        case WrapMode::Clamp:       return "Clamp (Extend Edge)";
        case WrapMode::Repeat:      return "Repeat (Tile)";
        case WrapMode::Mirror:      return "Mirror (Reflect)";
        case WrapMode::Transparent: return "Transparent (Black)";
        case WrapMode::Count:
        default:                    return "Clamp (Extend Edge)";
    }
}

int wrap_mode_count() { return static_cast<int>(WrapMode::Count); }

WrapMode wrap_mode_from_index(int index) {
    const int n = wrap_mode_count();
    if (index < 0 || index >= n) return WrapMode::Clamp;
    return static_cast<WrapMode>(index);
}

}  // namespace mgtk
