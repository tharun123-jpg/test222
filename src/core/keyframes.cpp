// =============================================================================
//  src/core/keyframes.cpp -- keyframe operations for the motion graphics
//                            toolkit.
//
//  See mgtk/keyframes.hpp for what these are for. The one rule that runs
//  through all of them: the first and last keyframes are poses the animator
//  chose, and no operation here is allowed to move them. Everything else is a
//  suggestion the generator is free to reinterpret.
// =============================================================================
#include "mgtk/keyframes.hpp"

#include <algorithm>
#include <cmath>

namespace mgtk {
namespace {

constexpr float kEps = 1.0e-6f;

// The value a path is heading towards, used as the settle target for overshoot
// and bounce.
float target_value(const std::vector<Keyframe>& keys) {
    return keys.empty() ? 0.0f : keys.back().value;
}

float start_value(const std::vector<Keyframe>& keys) {
    return keys.empty() ? 0.0f : keys.front().value;
}

// Progress of `time` through the whole list, 0 at the first key and 1 at the
// last. A degenerate list (one key, or all keys at the same time) reads as 1:
// the animation is already over.
float normalised_time(const std::vector<Keyframe>& keys, float time) {
    const float t0 = keys.front().time;
    const float t1 = keys.back().time;
    const float span = t1 - t0;
    if (span <= kEps) return 1.0f;
    return clamp((time - t0) / span, 0.0f, 1.0f);
}

}  // namespace

void sort_keyframes(std::vector<Keyframe>& keys) {
    // Stable, so that two keyframes at the same time keep the order they
    // arrived in rather than being shuffled by the sort. AE can hand back
    // simultaneous keys when a stream has both a keyframe and a hold.
    std::stable_sort(keys.begin(), keys.end(),
                     [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

float keyframe_mean_step(const std::vector<Keyframe>& keys) {
    if (keys.size() < 2) return 0.0f;
    return (keys.back().time - keys.front().time) /
           static_cast<float>(keys.size() - 1);
}

// ---------------------------------------------------------------------------
//  Easing
// ---------------------------------------------------------------------------
void retime_easing(std::vector<Keyframe>& keys, Easing curve, float strength) {
    if (keys.size() < 3) return;  // nothing between the ends to re-space
    const float s = saturate(strength);
    if (s <= 0.0f || curve == Easing::Linear) return;

    const float t0 = keys.front().time;
    const float t1 = keys.back().time;
    const float span = t1 - t0;
    if (span <= kEps) return;

    // For each interior key, work out where it currently sits in normalised
    // time, then ask the curve where that *should* sit. The inverse of the
    // easing function is what would be needed for an exact retime; evaluating
    // the curve directly is the standard approximation and, importantly, it is
    // monotonic for every curve in the table, so the keyframes cannot cross.
    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        const float u = (keys[i].time - t0) / span;
        const float eased = clamp(evaluate_easing(curve, u), 0.0f, 1.0f);
        const float blended = lerp(u, eased, s);
        keys[i].time = t0 + blended * span;
    }

    // The ends are untouched by construction, but re-sorting is cheap insurance
    // against a curve with a flat spot producing a duplicate time.
    sort_keyframes(keys);
}

// ---------------------------------------------------------------------------
//  Stagger
// ---------------------------------------------------------------------------
void stagger_keyframes(std::vector<Keyframe>& keys, float step, bool ping_pong) {
    if (keys.size() < 2 || std::fabs(step) < kEps) return;

    const int n = static_cast<int>(keys.size());
    for (int i = 0; i < n; ++i) {
        // Ping-pong folds the index back on itself: 0, 1, 2, 1, 0 for five
        // elements. That makes the last element land level with the first
        // instead of trailing the whole row, which is what a "ripple out and
        // back" wants.
        float index = static_cast<float>(i);
        if (ping_pong) {
            const float half = static_cast<float>(n - 1) * 0.5f;
            index = half - std::fabs(static_cast<float>(i) - half);
        }
        keys[static_cast<size_t>(i)].time += index * step;
    }

    // A negative step reverses the order; the caller's keyframes are still
    // valid, but everything downstream assumes ascending time.
    sort_keyframes(keys);
}

// ---------------------------------------------------------------------------
//  Overshoot and bounce
// ---------------------------------------------------------------------------
void apply_overshoot(std::vector<Keyframe>& keys, float overshoot, float anticipation) {
    if (keys.size() < 3) return;

    const float from = start_value(keys);
    const float to = target_value(keys);
    const float distance = to - from;

    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        const float u = normalised_time(keys, keys[i].time);
        const float shape = overshoot_curve(u, overshoot, anticipation);
        keys[i].value = from + distance * shape;
    }
    // The ends keep their values: the last one especially, because the point of
    // the whole exercise is to arrive at the pose the animator set.
    keys.front().value = from;
    keys.back().value = to;
}

void apply_bounce(std::vector<Keyframe>& keys, int bounces, float decay, float height) {
    if (keys.size() < 3) return;

    const int count = std::max(bounces, 1);
    const float from = start_value(keys);
    const float to = target_value(keys);
    const float distance = to - from;
    const float d = clamp(decay, 0.0f, 1.0f);

    // bounce_curve() is a settle curve running 0 -> 1 with `count` impacts. It
    // already places the minima at 1 - decay^i, so all that is left is to map
    // its output onto the distance the animation travels.
    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        const float u = normalised_time(keys, keys[i].time);
        const float shape = bounce_curve(u, count, d);
        keys[i].value = from + distance * shape * height + distance * (1.0f - height);
    }
    keys.front().value = from;
    keys.back().value = to;
}

// ---------------------------------------------------------------------------
//  Wiggle
// ---------------------------------------------------------------------------
void apply_wiggle(std::vector<Keyframe>& keys, float amplitude, float frequency,
                  uint32_t seed, int octaves) {
    if (keys.empty() || std::fabs(amplitude) < kEps) return;

    const int octave_count = clamp(octaves, 1, 6);
    const float base_seed = static_cast<float>(seed & 0xffffu);

    for (size_t i = 0; i < keys.size(); ++i) {
        float offset = 0.0f;
        float octave_amplitude = 1.0f;
        // The 2.13 ratio is the same inharmonic stack the core's wiggle_1d
        // uses; a stack of exact octaves sounds (and looks) mechanical, and the
        // point of a wiggle is that it does not.
        float octave_frequency = frequency;
        for (int o = 0; o < octave_count; ++o) {
            // The seed is offset per keyframe index as well as per octave, so
            // neighbouring keyframes do not move together -- which is the
            // difference between a wiggle and a wobble.
            const float local_seed = base_seed + static_cast<float>(o) * 17.0f +
                                     static_cast<float>(i) * 101.0f;
            offset += octave_amplitude * wiggle_1d(keys[i].time, octave_frequency, local_seed);
            octave_amplitude *= 0.5f;
            octave_frequency *= 2.13f;
        }
        keys[i].value += offset * amplitude;
    }
}

}  // namespace mgtk
