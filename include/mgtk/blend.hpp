// =============================================================================
//  mgtk/blend.hpp -- blend modes and composite helpers
//
//  Modes operate on straight (unpremultiplied) colour and return straight
//  colour; alpha is handled by `composite_over`. Keeping the two concerns
//  separate is what stops blend modes from producing the muddy halos you get
//  when someone blends premultiplied values.
// =============================================================================
#pragma once

#include "mgtk/image.hpp"

namespace mgtk {

enum class BlendMode : int {
    Normal = 0,
    Add,
    Subtract,
    Multiply,
    Screen,
    Overlay,
    SoftLight,
    HardLight,
    Difference,
    Exclusion,
    Lighten,
    Darken,
    ColorDodge,
    ColorBurn,
    LinearDodge,
    LinearBurn,
    VividLight,
    LinearLight,
    PinLight,
    HardMix,
    Divide,
    Average,
    Count
};

// Blend a single channel. `b` is the backdrop, `s` the source.
float blend_channel(BlendMode mode, float b, float s);

// Blend a whole pixel's colour channels; alpha is taken from `src`'s alpha
// handling performed by the caller.
Float4 blend_pixel(BlendMode mode, Float4 backdrop, Float4 source);

// Straight-alpha compositing of `over` onto `under`.
// Equivalent to AE's "Normal" mode with the layers' own alpha.
Float4 composite_over(Float4 under, Float4 over);

// Composite with an explicit opacity multiplier on the source.
Float4 composite_over(Float4 under, Float4 over, float opacity);

// Human-readable name, for building AE popup menus.
const char* blend_mode_name(BlendMode mode);

// Number of entries in the BlendMode enum (excluding the Count sentinel).
int blend_mode_count();

// Map an AE popup index to a BlendMode, clamped to a valid entry.
BlendMode blend_mode_from_index(int index);

const char* wrap_mode_name(WrapMode mode);
int wrap_mode_count();
WrapMode wrap_mode_from_index(int index);

}  // namespace mgtk
