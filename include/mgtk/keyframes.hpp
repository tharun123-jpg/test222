// =============================================================================
//  mgtk/keyframes.hpp -- keyframe operations for the motion graphics toolkit
//
//  These are the "auto-animation" tools: they take a list of keyframes and
//  rewrite their times or values to produce easing, stagger, overshoot, bounce
//  and wiggle. Nothing here knows about After Effects; the AEGP in
//  src/ae/aegp hands these functions the keyframes it read out of a stream and
//  writes the results back.
//
//  Keeping the maths here rather than in the plug-in is what makes it
//  testable. Retiming a keyframe list is fiddly -- the last keyframe of a
//  bounce has to land exactly on the value the user put there, stagger has to
//  keep the list sorted, and a wiggle has to be identical every time the
//  project is reopened -- and all three are much easier to get right against a
//  unit test than against a timeline.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "mgtk/math.hpp"

namespace mgtk {

// One keyframe of a scalar stream, in whatever time units the caller uses
// (the AEGP passes composition seconds).
struct Keyframe {
    float time = 0.0f;
    float value = 0.0f;
};

// ---------------------------------------------------------------------------
//  Easing
// ---------------------------------------------------------------------------
// Re-times the *intervals* between keyframes according to `curve`, without
// moving the keyframes themselves.
//
// This is the operation people actually want when they say "ease this": the
// value at each pose stays exactly where they put it, and only the pacing
// between poses changes. `strength` blends the result with the original linear
// spacing, so 0 is a no-op and 1 is the full curve. Keyframes outside the
// selected range are left alone -- the caller passes only the range it means to
// affect.
void retime_easing(std::vector<Keyframe>& keys, Easing curve, float strength = 1.0f);

// ---------------------------------------------------------------------------
//  Stagger
// ---------------------------------------------------------------------------
// Offsets each keyframe in time by `step` times its index, which is how a row
// of shapes is made to ripple instead of moving together.
//
// `ping_pong` folds the offset back on itself after the first pass, so the
// last element catches up with the first rather than trailing the whole way.
void stagger_keyframes(std::vector<Keyframe>& keys, float step, bool ping_pong = false);

// ---------------------------------------------------------------------------
//  Overshoot and bounce
// ---------------------------------------------------------------------------
// Replaces the value path with a damped curve that settles on `keys.back()`,
// the value of the final keyframe. The last keyframe is never moved: a bounce
// that does not land on the pose the animator set is a bug, not a style.
//
//   overshoot:    a 0..1 knob (the core clamps it) that trades damping for
//                 ringing: 0 settles without overshooting, 1 shoots well past
//                 the target before coming back.
//   anticipation: a 0..1 knob for the opposite feel -- the path first moves
//                 briefly away from the target, then travels to it. Zero means
//                 no dip.
//   bounce:       `bounces` impacts, each one `decay` times shorter than the
//                 last, with the peaks reaching `height` of the distance
//                 travelled on the first impact.
void apply_overshoot(std::vector<Keyframe>& keys, float overshoot, float anticipation = 0.0f);
void apply_bounce(std::vector<Keyframe>& keys, int bounces, float decay = 0.55f,
                  float height = 1.0f);

// ---------------------------------------------------------------------------
//  Wiggle
// ---------------------------------------------------------------------------
// Adds deterministic noise to each keyframe's value. The noise is a function of
// (seed, keyframe index), never of wall-clock time, so reopening a project
// produces the identical wiggle -- which is the entire difference between a
// usable wiggle and one that has to be baked.
//
// `frequency` is in cycles per unit of `time`, `octaves` stacks progressively
// finer detail at the usual 2.13 inharmonic ratio.
void apply_wiggle(std::vector<Keyframe>& keys, float amplitude, float frequency,
                  uint32_t seed, int octaves = 3);

// ---------------------------------------------------------------------------
//  Shared helpers
// ---------------------------------------------------------------------------
// Sorts by time. Every operation above assumes ascending time and leaves it
// ascending; the AEGP calls this after reading a stream, because AE does not
// promise to hand back keyframes in order.
void sort_keyframes(std::vector<Keyframe>& keys);

// The largest time gap between consecutive keyframes, or 0 for fewer than two.
float keyframe_mean_step(const std::vector<Keyframe>& keys);

}  // namespace mgtk
