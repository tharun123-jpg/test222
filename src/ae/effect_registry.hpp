// =============================================================================
//  src/ae/effect_registry.hpp -- named access to the effect table.
//
//  One accessor per effect, rather than a string lookup at load time, so a
//  typo in a plug-in's translation unit is a link error instead of a plug-in
//  that silently does nothing.
// =============================================================================
#pragma once

#include "effect_spec.hpp"

namespace mgtk {
namespace ae {

const EffectSpec& feedback_echo_spec();
const EffectSpec& chromatic_split_spec();
const EffectSpec& anamorphic_glow_spec();
const EffectSpec& fractal_warp_spec();
const EffectSpec& kaleidoscope_spec();
const EffectSpec& halftone_pro_spec();
const EffectSpec& slit_scan_spec();
const EffectSpec& pixel_sort_spec();

}  // namespace ae
}  // namespace mgtk
