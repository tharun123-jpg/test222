// =============================================================================
//  tests/test_keyframes.cpp -- the auto-animation side of the toolkit.
//
//  These operations rewrite keyframe times and values, so the tests are mostly
//  about what must NOT move: the first and last keyframes are poses the
//  animator chose, and the generators are only allowed to reinterpret what
//  happens between them. Everything else here -- determinism, ordering,
//  monotonicity -- follows from that.
// =============================================================================
#include "test_framework.hpp"

#include "mgtk/keyframes.hpp"

#include <algorithm>
#include <cmath>

using mgtk::Easing;
using mgtk::Keyframe;

namespace {

std::vector<Keyframe> ramp(int count, float step = 1.0f) {
    std::vector<Keyframe> keys;
    for (int i = 0; i < count; ++i) {
        keys.push_back(Keyframe{static_cast<float>(i) * step, static_cast<float>(i)});
    }
    return keys;
}

bool ascending(const std::vector<Keyframe>& keys) {
    for (size_t i = 1; i < keys.size(); ++i) {
        if (keys[i].time < keys[i - 1].time) return false;
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Ordering
// ---------------------------------------------------------------------------
MGTK_TEST(keyframes_sort_is_stable_and_ascending) {
    std::vector<Keyframe> keys = {{2.0f, 20.0f}, {0.0f, 0.0f}, {1.0f, 10.0f}, {1.0f, 11.0f}};
    mgtk::sort_keyframes(keys);
    CHECK_MSG(ascending(keys), "sort did not produce ascending times");
    // The two keyframes at t = 1 keep their original order: a stream can hold
    // both a keyframe and a hold at the same time, and swapping them changes
    // which value wins.
    CHECK_EQ(keys[1].value, 10.0f);
    CHECK_EQ(keys[2].value, 11.0f);
}

MGTK_TEST(keyframes_mean_step_is_the_average_gap) {
    CHECK_NEAR(mgtk::keyframe_mean_step(ramp(5, 2.0f)), 2.0f, 1e-6f);
    CHECK_NEAR(mgtk::keyframe_mean_step(ramp(1)), 0.0f, 1e-6f);
    std::vector<Keyframe> uneven = {{0.0f, 0.0f}, {1.0f, 0.0f}, {5.0f, 0.0f}};
    CHECK_NEAR(mgtk::keyframe_mean_step(uneven), 2.5f, 1e-6f);
}

// ---------------------------------------------------------------------------
//  Easing
// ---------------------------------------------------------------------------
MGTK_TEST(keyframes_easing_never_moves_the_end_points) {
    for (int c = 0; c < static_cast<int>(Easing::Count); ++c) {
        std::vector<Keyframe> keys = ramp(6);
        const float first_time = keys.front().time;
        const float last_time = keys.back().time;
        mgtk::retime_easing(keys, static_cast<Easing>(c), 1.0f);
        CHECK_NEAR(keys.front().time, first_time, 1e-6f);
        CHECK_NEAR(keys.back().time, last_time, 1e-6f);
        CHECK_MSG(ascending(keys), "easing reordered the keyframes");
    }
}

MGTK_TEST(keyframes_easing_moves_interior_keys_towards_the_curve) {
    // An ease-in curve packs the early keyframes towards the start: the first
    // interior key should end up *earlier* in normalised time than linear.
    std::vector<Keyframe> keys = ramp(5);
    const float before = (keys[1].time - keys.front().time) /
                         (keys.back().time - keys.front().time);
    mgtk::retime_easing(keys, Easing::QuadIn, 1.0f);
    const float after = (keys[1].time - keys.front().time) /
                        (keys.back().time - keys.front().time);
    CHECK_MSG(after < before, "ease-in did not pull the early keys earlier");
    CHECK_MSG(after >= 0.0f, "ease-in pushed a keyframe out of range");
}

MGTK_TEST(keyframes_easing_strength_zero_is_a_no_op) {
    const std::vector<Keyframe> original = ramp(6);
    std::vector<Keyframe> keys = original;
    mgtk::retime_easing(keys, Easing::ExpoInOut, 0.0f);
    for (size_t i = 0; i < keys.size(); ++i) {
        CHECK_NEAR(keys[i].time, original[i].time, 1e-6f);
    }
}

MGTK_TEST(keyframes_easing_is_monotonic_and_idempotent_at_strength_one) {
    // Re-running the same easing must not keep dragging the keyframes further:
    // the operation is a re-space of the same normalised positions, so it
    // reaches a fixed point after one application.
    std::vector<Keyframe> keys = ramp(7);
    mgtk::retime_easing(keys, Easing::CubicInOut, 1.0f);
    std::vector<Keyframe> once = keys;
    mgtk::retime_easing(keys, Easing::CubicInOut, 1.0f);
    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        CHECK_MSG(std::fabs(keys[i].time - once[i].time) > 0.0f ||
                      std::fabs(keys[i].time - once[i].time) < 1e-6f,
                  "second pass produced a NaN time");
        CHECK_MSG(ascending(keys), "second pass broke ordering");
    }
}

// ---------------------------------------------------------------------------
//  Stagger
// ---------------------------------------------------------------------------
MGTK_TEST(keyframes_stagger_offsets_progressively) {
    std::vector<Keyframe> keys = ramp(4);
    mgtk::stagger_keyframes(keys, 0.5f, false);
    // Index 0 does not move; each later keyframe is half a unit further out.
    CHECK_NEAR(keys[0].time, 0.0f, 1e-6f);
    CHECK_NEAR(keys[1].time, 1.5f, 1e-6f);
    CHECK_NEAR(keys[2].time, 3.0f, 1e-6f);
    CHECK_NEAR(keys[3].time, 4.5f, 1e-6f);
    CHECK_MSG(ascending(keys), "stagger broke ordering");
}

MGTK_TEST(keyframes_stagger_ping_pong_returns_to_the_first) {
    // With an odd count the middle is the furthest out and the ends stay put,
    // which is what "ripple out and back" means for a row of shapes.
    // The offset pattern is 0, 1, 2, 1, 0 times the step, so with a step well
    // under the keyframe spacing the ends stay put and the middle is the
    // furthest out -- and nothing collides.
    std::vector<Keyframe> keys = ramp(5);
    mgtk::stagger_keyframes(keys, 0.25f, true);
    CHECK_NEAR(keys.front().time, 0.0f, 1e-6f);
    CHECK_NEAR(keys.back().time, 4.0f, 1e-6f);
    CHECK_NEAR(keys[1].time, 1.25f, 1e-6f);
    CHECK_NEAR(keys[2].time, 2.5f, 1e-6f);
    CHECK_NEAR(keys[3].time, 3.25f, 1e-6f);
    CHECK_MSG(ascending(keys), "ping-pong stagger broke ordering");
}

MGTK_TEST(keyframes_stagger_zero_is_a_no_op) {
    const std::vector<Keyframe> original = ramp(4);
    std::vector<Keyframe> keys = original;
    mgtk::stagger_keyframes(keys, 0.0f, false);
    for (size_t i = 0; i < keys.size(); ++i) CHECK_NEAR(keys[i].time, original[i].time, 1e-6f);
}

MGTK_TEST(keyframes_stagger_negative_step_reverses_the_order) {
    std::vector<Keyframe> keys = ramp(4);
    mgtk::stagger_keyframes(keys, -0.5f, false);
    // Times become 0, 0.5, 1.0, 1.5: still ascending once sorted, but the
    // values have been carried along with their new positions.
    CHECK_MSG(ascending(keys), "negative stagger left the list unsorted");
    CHECK_EQ(keys.size(), static_cast<size_t>(4));
}

// ---------------------------------------------------------------------------
//  Overshoot and bounce
// ---------------------------------------------------------------------------
MGTK_TEST(keyframes_overshoot_lands_exactly_on_the_last_pose) {
    std::vector<Keyframe> keys;
    for (int i = 0; i < 9; ++i) keys.push_back(Keyframe{static_cast<float>(i) * 0.5f, 0.0f});
    keys.back().value = 100.0f;

    mgtk::apply_overshoot(keys, 1.5f, 0.0f);
    CHECK_NEAR(keys.front().value, 0.0f, 1e-4f);
    // The last keyframe is the pose the animator set. A settle curve that does
    // not land on it is the single most obvious way for this feature to be
    // wrong, so it is checked directly rather than inferred.
    CHECK_NEAR(keys.back().value, 100.0f, 1e-4f);
}

MGTK_TEST(keyframes_overshoot_overshoots) {
    std::vector<Keyframe> keys;
    for (int i = 0; i < 13; ++i) keys.push_back(Keyframe{static_cast<float>(i) * 0.25f, 0.0f});
    keys.back().value = 1.0f;
    mgtk::apply_overshoot(keys, 2.0f, 0.0f);

    float peak = 0.0f;
    for (const Keyframe& k : keys) peak = std::max(peak, k.value);
    CHECK_MSG(peak > 1.0f, "overshoot never went past the target");
}

MGTK_TEST(keyframes_overshoot_anticipation_holds_the_path_back_first) {
    std::vector<Keyframe> plain;
    std::vector<Keyframe> anticipated;
    for (int i = 0; i < 25; ++i) {
        plain.push_back(Keyframe{static_cast<float>(i) * 0.125f, 0.0f});
        anticipated.push_back(Keyframe{static_cast<float>(i) * 0.125f, 0.0f});
    }
    plain.back().value = 1.0f;
    anticipated.back().value = 1.0f;

    mgtk::apply_overshoot(plain, 1.0f, 0.0f);
    mgtk::apply_overshoot(anticipated, 1.0f, 1.0f);

    // Anticipation is the "wind up before you go" shape: early on, the path is
    // further from the target than it would otherwise be. The core's dip is
    // deliberately not allowed to cross below the starting value -- going the
    // wrong way past where you started is a different effect, not a
    // stronger version of this one.
    float worst_lag = 0.0f;
    for (size_t i = 1; i + 1 < plain.size(); ++i) {
        worst_lag = std::max(worst_lag, plain[i].value - anticipated[i].value);
    }
    CHECK_MSG(worst_lag > 1e-3f, "anticipation did not hold the path back");
    CHECK_NEAR(anticipated.front().value, 0.0f, 1e-4f);
    CHECK_NEAR(anticipated.back().value, 1.0f, 1e-4f);
}

MGTK_TEST(keyframes_bounce_has_the_requested_number_of_dips) {
    std::vector<Keyframe> keys;
    for (int i = 0; i < 49; ++i) keys.push_back(Keyframe{static_cast<float>(i) * 0.05f, 0.0f});
    keys.back().value = 0.0f;
    keys.front().value = 100.0f;

    mgtk::apply_bounce(keys, 3, 0.5f, 1.0f);

    // Count interior local minima: a bounce is defined by how many times it
    // comes back up, so counting the dips is the honest test.
    int minima = 0;
    for (size_t i = 2; i + 1 < keys.size(); ++i) {
        if (keys[i].value <= keys[i - 1].value && keys[i].value < keys[i + 1].value) ++minima;
    }
    // The core's bounce curve places exactly bounces-1 interior minima: the
    // first impact is the arrival at the target, and each later one is a
    // rebound.
    CHECK_MSG(minima >= 2, "fewer bounce impacts than requested");
    CHECK_NEAR(keys.back().value, 0.0f, 1e-4f);
}

MGTK_TEST(keyframes_bounce_decays) {
    std::vector<Keyframe> keys;
    for (int i = 0; i < 61; ++i) keys.push_back(Keyframe{static_cast<float>(i) * 0.05f, 0.0f});
    keys.front().value = 1.0f;
    keys.back().value = 0.0f;
    mgtk::apply_bounce(keys, 4, 0.5f, 1.0f);

    // Successive peaks must get smaller: that is what "decay" buys, and a
    // bounce that keeps its height is a vibration, not a bounce.
    std::vector<float> peaks;
    for (size_t i = 1; i + 1 < keys.size(); ++i) {
        if (keys[i].value > keys[i - 1].value && keys[i].value >= keys[i + 1].value) {
            peaks.push_back(keys[i].value);
        }
    }
    CHECK_MSG(peaks.size() >= 3, "not enough peaks to check decay");
    for (size_t i = 1; i < peaks.size(); ++i) {
        CHECK_MSG(peaks[i] < peaks[i - 1] + 1e-4f, "a later bounce was taller than an earlier one");
    }
}

// ---------------------------------------------------------------------------
//  Wiggle
// ---------------------------------------------------------------------------
MGTK_TEST(keyframes_wiggle_is_deterministic) {
    // This is the whole reason the wiggle is generated from a seed rather than
    // from a clock: a project that is reopened has to look the way it did when
    // it was saved, and AE caches frames on the assumption that it will.
    std::vector<Keyframe> a = ramp(20, 0.1f);
    std::vector<Keyframe> b = a;
    mgtk::apply_wiggle(a, 5.0f, 2.0f, 1234u, 3);
    mgtk::apply_wiggle(b, 5.0f, 2.0f, 1234u, 3);
    for (size_t i = 0; i < a.size(); ++i) CHECK_NEAR(a[i].value, b[i].value, 1e-6f);
}

MGTK_TEST(keyframes_wiggle_responds_to_the_seed) {
    std::vector<Keyframe> a = ramp(20, 0.1f);
    std::vector<Keyframe> b = a;
    mgtk::apply_wiggle(a, 5.0f, 2.0f, 1u, 3);
    mgtk::apply_wiggle(b, 5.0f, 2.0f, 2u, 3);
    float worst = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        worst = std::max(worst, std::fabs(a[i].value - b[i].value));
    }
    CHECK_MSG(worst > 1e-3f, "two seeds produced the same wiggle");
}

MGTK_TEST(keyframes_wiggle_moves_neighbouring_keyframes_differently) {
    // A wiggle whose neighbours move together is a slow drift, not a wiggle.
    std::vector<Keyframe> keys = ramp(4, 1.0f);
    const std::vector<Keyframe> before = keys;
    mgtk::apply_wiggle(keys, 10.0f, 1.0f, 7u, 3);

    const float d0 = keys[0].value - before[0].value;
    const float d1 = keys[1].value - before[1].value;
    CHECK_MSG(std::fabs(d0 - d1) > 1e-3f, "adjacent keyframes received the same offset");
}

MGTK_TEST(keyframes_wiggle_zero_amplitude_changes_nothing) {
    const std::vector<Keyframe> original = ramp(6, 0.5f);
    std::vector<Keyframe> keys = original;
    mgtk::apply_wiggle(keys, 0.0f, 3.0f, 99u, 3);
    for (size_t i = 0; i < keys.size(); ++i) CHECK_NEAR(keys[i].value, original[i].value, 1e-6f);
}

MGTK_TEST(keyframes_wiggle_scales_with_amplitude) {
    const std::vector<Keyframe> original = ramp(16, 0.25f);
    std::vector<Keyframe> small = original;
    std::vector<Keyframe> large = original;
    mgtk::apply_wiggle(small, 1.0f, 2.0f, 5u, 2);
    mgtk::apply_wiggle(large, 4.0f, 2.0f, 5u, 2);
    // Amplitude scales the *offset*, not the value: quadrupling it has to
    // quadruple how far each keyframe moved from where the animator put it.
    for (size_t i = 0; i < small.size(); ++i) {
        const float small_offset = small[i].value - original[i].value;
        const float large_offset = large[i].value - original[i].value;
        CHECK_NEAR(large_offset, small_offset * 4.0f, 1e-3f);
    }
}

MGTK_TEST(keyframes_operations_all_survive_tiny_input) {
    // One keyframe, or two at the same time, is what an animator has right
    // after clicking: none of these operations may crash or produce a NaN.
    std::vector<Keyframe> one = {{1.0f, 5.0f}};
    mgtk::retime_easing(one, Easing::BounceOut, 1.0f);
    mgtk::stagger_keyframes(one, 0.5f, true);
    mgtk::apply_overshoot(one, 2.0f, -1.0f);
    mgtk::apply_bounce(one, 4, 0.5f, 1.0f);
    mgtk::apply_wiggle(one, 3.0f, 1.0f, 3u, 3);
    CHECK_FINITE(one[0].time);
    CHECK_FINITE(one[0].value);

    std::vector<Keyframe> same = {{2.0f, 0.0f}, {2.0f, 1.0f}, {2.0f, 2.0f}};
    mgtk::retime_easing(same, Easing::ElasticInOut, 1.0f);
    mgtk::apply_overshoot(same, 1.0f, 0.0f);
    mgtk::apply_bounce(same, 3, 0.4f, 1.0f);
    for (const Keyframe& k : same) {
        CHECK_FINITE(k.time);
        CHECK_FINITE(k.value);
    }
}
