// =============================================================================
//  tests/test_math.cpp -- scalar helpers, hashing and easing curves
// =============================================================================
#include "mgtk/math.hpp"
#include "test_framework.hpp"

using namespace mgtk;

MGTK_TEST(math_clamp_and_range_helpers) {
    CHECK_EQ(clamp(5, 0, 3), 3);
    CHECK_EQ(clamp(-5, 0, 3), 0);
    CHECK_EQ(clamp(2, 0, 3), 2);

    CHECK_NEAR(saturate(-0.5f), 0.0f, 0.0f);
    CHECK_NEAR(saturate(0.5f), 0.5f, 0.0f);
    CHECK_NEAR(saturate(1.5f), 1.0f, 0.0f);

    CHECK_NEAR(lerp(10.0f, 20.0f, 0.0f), 10.0f, 1e-6f);
    CHECK_NEAR(lerp(10.0f, 20.0f, 1.0f), 20.0f, 1e-6f);
    CHECK_NEAR(lerp(10.0f, 20.0f, 0.5f), 15.0f, 1e-6f);
}

MGTK_TEST(math_smoothstep_endpoints_and_midpoint) {
    CHECK_NEAR(smoothstep(0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(smoothstep(1.0f), 1.0f, 1e-6f);
    CHECK_NEAR(smoothstep(0.5f), 0.5f, 1e-6f);

    // Clamping behaviour outside [0,1].
    CHECK_NEAR(smoothstep(-2.0f), 0.0f, 1e-6f);
    CHECK_NEAR(smoothstep(3.0f), 1.0f, 1e-6f);

    // Edge-based form.
    CHECK_NEAR(smoothstep(10.0f, 20.0f, 15.0f), 0.5f, 1e-6f);
}

MGTK_TEST(math_remap_and_modulo) {
    CHECK_NEAR(remap(5.0f, 0.0f, 10.0f, 0.0f, 100.0f), 50.0f, 1e-5f);
    CHECK_NEAR(remap(150.0f, 0.0f, 100.0f, 0.0f, 100.0f), 150.0f, 1e-5f);
    CHECK_NEAR(remap_clamped(150.0f, 0.0f, 100.0f, 0.0f, 100.0f), 100.0f, 1e-5f);

    // A degenerate input range must not divide by zero.
    CHECK_FINITE(remap(1.0f, 5.0f, 5.0f, 0.0f, 1.0f));

    // Modulo must be non-negative for positive moduli.
    CHECK_EQ(imod(-1, 5), 4);
    CHECK_EQ(imod(7, 5), 2);
    CHECK_EQ(imod(-6, 5), 4);
    CHECK_NEAR(fmod_positive(-0.25f, 1.0f), 0.75f, 1e-6f);
}

MGTK_TEST(math_mirror_coord) {
    // Mirroring is the operation that makes reflected tiling seamless.
    CHECK_NEAR(mirror_coord(0.0f, 4.0f), 0.0f, 1e-5f);
    CHECK_NEAR(mirror_coord(3.0f, 4.0f), 3.0f, 1e-5f);
    CHECK_NEAR(mirror_coord(4.0f, 4.0f), 4.0f, 1e-5f);
    CHECK_NEAR(mirror_coord(5.0f, 4.0f), 3.0f, 1e-5f);
    CHECK_NEAR(mirror_coord(-1.0f, 4.0f), 1.0f, 1e-5f);
    // A degenerate extent must not produce NaN.
    CHECK_FINITE(mirror_coord(3.0f, 0.0f));
}

MGTK_TEST(math_srgb_roundtrip) {
    for (float v = 0.0f; v <= 1.0f; v += 0.05f) {
        const float back = linear_to_srgb(srgb_to_linear(v));
        CHECK_NEAR(back, v, 1e-4f);
    }
    CHECK_NEAR(srgb_to_linear(0.0f), 0.0f, 1e-9f);
    CHECK_NEAR(srgb_to_linear(1.0f), 1.0f, 1e-4f);
    // The linear segment of the curve.
    CHECK_NEAR(srgb_to_linear(0.04f), 0.04f / 12.92f, 1e-6f);
}

MGTK_TEST(math_luma_weights_sum_to_one) {
    // Pure white must map to 1.0 under the luma weights.
    CHECK_NEAR(luma(1.0f, 1.0f, 1.0f), 1.0f, 1e-5f);
    CHECK_NEAR(luma(0.0f, 0.0f, 0.0f), 0.0f, 1e-6f);
    // Green must contribute the most, blue the least.
    CHECK(luma(0.0f, 1.0f, 0.0f) > luma(1.0f, 0.0f, 0.0f));
    CHECK(luma(1.0f, 0.0f, 0.0f) > luma(0.0f, 0.0f, 1.0f));
}

MGTK_TEST(math_vec2_operations) {
    const Vec2 a{3.0f, 4.0f};
    CHECK_NEAR(length(a), 5.0f, 1e-5f);
    CHECK_NEAR(length_sq(a), 25.0f, 1e-5f);

    const Vec2 n = normalize(a);
    CHECK_NEAR(length(n), 1.0f, 1e-5f);

    // A zero vector must not produce NaN.
    CHECK_FINITE(normalize(Vec2{0.0f, 0.0f}).x);

    // Rotating by 90 degrees turns +X into +Y.
    const Vec2 r = rotate(Vec2{1.0f, 0.0f}, kHalfPi);
    CHECK_NEAR(r.x, 0.0f, 1e-5f);
    CHECK_NEAR(r.y, 1.0f, 1e-5f);

    // A full turn is the identity.
    const Vec2 full = rotate(a, kTwoPi);
    CHECK_NEAR(full.x, a.x, 1e-4f);
    CHECK_NEAR(full.y, a.y, 1e-4f);

    CHECK_NEAR(dot(Vec2{1.0f, 0.0f}, Vec2{0.0f, 1.0f}), 0.0f, 1e-6f);
}

MGTK_TEST(math_hash_is_deterministic_and_well_distributed) {
    // Same input, same output -- every time. This is the property that keeps
    // AE's disk cache valid.
    for (int i = 0; i < 100; ++i) {
        CHECK_EQ(hash_2i(17, 42), hash_2i(17, 42));
    }
    CHECK(hash_2i(1, 2) != hash_2i(2, 1));
    CHECK(hash_3i(1, 2, 3) != hash_3i(3, 2, 1));

    // Values must land in [0,1).
    for (int y = -5; y < 5; ++y) {
        for (int x = -5; x < 5; ++x) {
            const float v = rand_2i(x, y);
            CHECK(v >= 0.0f && v < 1.0f);
        }
    }

    // Rough uniformity: the mean of a large sample should be near 0.5.
    double sum = 0.0;
    int count = 0;
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            sum += rand_2i(x, y);
            ++count;
        }
    }
    const double mean = sum / count;
    CHECK_MSG(mean > 0.45 && mean < 0.55, "hash mean was off centre");
}

MGTK_TEST(math_easing_endpoints_are_exact) {
    // Every curve that advertises itself as an ease must hit 0 at t=0 and 1 at
    // t=1. Overshoot is allowed strictly inside the range.
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        const float at0 = evaluate_easing(e, 0.0f);
        const float at1 = evaluate_easing(e, 1.0f);
        CHECK_NEAR(at0, 0.0f, 1e-5f);
        CHECK_NEAR(at1, 1.0f, 1e-5f);
    }
}

MGTK_TEST(math_easing_is_finite_across_the_range) {
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const auto e = static_cast<Easing>(i);
        for (int s = 0; s <= 100; ++s) {
            const float t = static_cast<float>(s) / 100.0f;
            CHECK_FINITE(evaluate_easing(e, t));
        }
        // Out-of-range input must be clamped, not extrapolated.
        CHECK_FINITE(evaluate_easing(e, -5.0f));
        CHECK_FINITE(evaluate_easing(e, 7.0f));
    }
}

MGTK_TEST(math_easing_monotone_curves) {
    // The non-overshooting curves must be monotonically non-decreasing.
    const Easing monotone[] = {
        Easing::Linear,   Easing::SineIn,     Easing::SineOut, Easing::SineInOut,
        Easing::QuadIn,   Easing::QuadOut,    Easing::CubicIn, Easing::CubicOut,
        Easing::QuartIn,  Easing::QuartOut,   Easing::QuintIn, Easing::ExpoOut,
        Easing::CircIn,   Easing::CircOut,
    };
    for (Easing e : monotone) {
        float previous = -1.0e9f;
        for (int s = 0; s <= 200; ++s) {
            const float v = evaluate_easing(e, static_cast<float>(s) / 200.0f);
            CHECK_MSG(v >= previous - 1e-5f, easing_name(e));
            previous = v;
        }
    }
}

MGTK_TEST(math_easing_names_are_populated) {
    for (int i = 0; i < static_cast<int>(Easing::Count); ++i) {
        const char* n = easing_name(static_cast<Easing>(i));
        REQUIRE(n != nullptr);
        CHECK(n[0] != '\0');
    }
    CHECK_EQ(static_cast<int>(Easing::Count), 31);
}

MGTK_TEST(math_overshoot_curve_behaviour) {
    // Zero overshoot must be smooth and monotone.
    float previous = -1.0e9f;
    for (int s = 0; s <= 200; ++s) {
        const float v = overshoot_curve(static_cast<float>(s) / 200.0f, 0.0f, 0.0f);
        CHECK_FINITE(v);
        CHECK_MSG(v >= previous - 1e-4f, "damped curve went backwards");
        previous = v;
    }
    CHECK_NEAR(overshoot_curve(0.0f, 0.0f, 0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(overshoot_curve(1.0f, 0.0f, 0.0f), 1.0f, 1e-6f);

    // With overshoot the curve must exceed 1 somewhere in the middle.
    bool exceeds = false;
    for (int s = 1; s < 200; ++s) {
        if (overshoot_curve(static_cast<float>(s) / 200.0f, 1.0f, 0.0f) > 1.0f) {
            exceeds = true;
            break;
        }
    }
    CHECK_MSG(exceeds, "overshoot=1 never exceeded 1.0");

    // Endpoints stay exact no matter the settings.
    CHECK_NEAR(overshoot_curve(0.0f, 1.0f, 0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(overshoot_curve(1.0f, 1.0f, 0.0f), 1.0f, 1e-6f);

    // Anticipation must dip below zero early on, and still resolve to 0 and 1.
    bool dips = false;
    for (int s = 1; s < 100; ++s) {
        if (overshoot_curve(static_cast<float>(s) / 200.0f, 0.0f, 1.0f) < 0.0f) {
            dips = true;
            break;
        }
    }
    CHECK_MSG(dips, "anticipation=1 never dipped below 0");
    CHECK_NEAR(overshoot_curve(0.0f, 0.0f, 1.0f), 0.0f, 1e-6f);
    CHECK_NEAR(overshoot_curve(1.0f, 0.0f, 1.0f), 1.0f, 1e-6f);
}

MGTK_TEST(math_bounce_curve_behaviour) {
    CHECK_NEAR(bounce_curve(0.0f, 4, 0.5f), 0.0f, 1e-6f);
    CHECK_NEAR(bounce_curve(1.0f, 4, 0.5f), 1.0f, 1e-6f);

    // The shape of a bounce curve is: monotone rise to 1, then a repeating
    // pattern of "dip to the rebound apex and return to 1". So a 4-bounce curve
    // must have exactly 3 interior local minima, and they must sit at
    // 1 - decay^i for i = 1..3, because the apex height after bounce i is
    // decay^i.
    {
        constexpr int kBounces = 4;
        constexpr float kDecay = 0.5f;
        constexpr int kSamples = 2000;
        std::vector<float> v(kSamples + 1);
        for (int s = 0; s <= kSamples; ++s) {
            v[s] = bounce_curve(static_cast<float>(s) / kSamples, kBounces, kDecay);
            CHECK_FINITE(v[s]);
            CHECK(v[s] >= -1e-4f && v[s] <= 1.0f + 1e-4f);
        }

        int minima = 0;
        for (int s = 1; s < kSamples; ++s) {
            if (v[s] < v[s - 1] && v[s] <= v[s + 1]) {
                ++minima;
                const float expected = 1.0f - std::pow(kDecay, static_cast<float>(minima));
                CHECK_NEAR(v[s], expected, 0.01f);
            }
        }
        CHECK_EQ(minima, kBounces - 1);
    }

    // Degenerate inputs must not produce NaN.
    CHECK_FINITE(bounce_curve(0.5f, 1, 0.0f));
    CHECK_FINITE(bounce_curve(0.5f, 0, 0.0f));
    CHECK_FINITE(bounce_curve(0.5f, 999, 5.0f));

    // A fully decayed curve is still a valid, finite curve.
    for (int s = 0; s <= 100; ++s) {
        CHECK_FINITE(bounce_curve(static_cast<float>(s) / 100.0f, 6, 0.05f));
    }
}

MGTK_TEST(math_wiggle_is_deterministic_and_bounded) {
    // Determinism.
    for (int i = 0; i < 20; ++i) {
        CHECK_NEAR(wiggle_1d(3.5f, 2.0f, 7.0f), wiggle_1d(3.5f, 2.0f, 7.0f), 0.0f);
    }

    // Different seeds must give different signals.
    CHECK(std::fabs(wiggle_1d(3.5f, 2.0f, 7.0f) -
                    wiggle_1d(3.5f, 2.0f, 8.0f)) > 1e-4f);

    // X and Y channels of the 2D wiggle must be decorrelated.
    CHECK(std::fabs(wiggle_2d_x(1.0f, 2.0f, 3.0f) -
                    wiggle_2d_y(1.0f, 2.0f, 3.0f)) > 1e-3f);

    // Bounded and finite everywhere.
    for (int s = 0; s <= 500; ++s) {
        const float v = wiggle_1d(static_cast<float>(s) * 0.01f, 2.0f, 1.0f);
        CHECK_FINITE(v);
        CHECK(v >= -1.001f && v <= 1.001f);
    }

    // -------------------------------------------------------------------
    //  Band-limiting, tested rigorously rather than by picking a threshold.
    //
    //  For a continuous, band-limited signal the typical step over a grid of
    //  spacing dt shrinks in proportion to dt. If the signal carried content
    //  above the sampling rate's Nyquist limit, the step size would stay
    //  roughly constant as dt shrank -- that is precisely what aliasing is.
    //  So the test is: refine the time step tenfold and check that the step
    //  size collapses with it.
    // -------------------------------------------------------------------
    struct StepStats {
        float max_step = 0.0f;
        float rms_step = 0.0f;
    };

    const auto measure = [](float freq, float dt, int samples) {
        StepStats st;
        double sum_sq = 0.0;
        float prev = wiggle_1d(0.0f, freq, 3.0f);
        for (int i = 1; i <= samples; ++i) {
            const float v = wiggle_1d(static_cast<float>(i) * dt, freq, 3.0f);
            const float d = std::fabs(v - prev);
            st.max_step = std::max(st.max_step, d);
            sum_sq += static_cast<double>(d) * d;
            prev = v;
        }
        st.rms_step = static_cast<float>(std::sqrt(sum_sq / samples));
        return st;
    };

    const StepStats coarse = measure(2.0f, 1.0f / 24.0f, 480);
    const StepStats fine = measure(2.0f, 1.0f / 240.0f, 4800);

    CHECK(fine.max_step < coarse.max_step);
    CHECK_MSG(fine.max_step < coarse.max_step * 0.6f,
              "step size did not collapse when the time step shrank -- the "
              "signal is not band-limited");

    // On a 24 fps grid the largest step must stay well below the 2.0 a real
    // discontinuity would produce on a signal spanning -1..1.
    CHECK_MSG(coarse.max_step < 1.0f, "per-frame wiggle step is too large");

    // The requested frequency must actually control the speed of motion.
    const StepStats slow = measure(1.0f, 1.0f / 24.0f, 480);
    const StepStats fast = measure(4.0f, 1.0f / 24.0f, 480);
    CHECK_MSG(fast.rms_step > slow.rms_step * 1.5f,
              "doubling the frequency did not meaningfully increase motion");
}
