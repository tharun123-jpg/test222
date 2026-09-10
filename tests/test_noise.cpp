// =============================================================================
//  tests/test_noise.cpp -- procedural noise
//
//  The property that matters most here is *determinism*. AE caches rendered
//  frames to disk and, with multi-frame rendering enabled, renders several
//  frames at once on different threads. If the same coordinate and seed could
//  produce two different values, cached frames would not match fresh ones and
//  the whole plugin would look unstable.
// =============================================================================
#include <cmath>
#include <set>

#include "mgtk/noise.hpp"
#include "test_framework.hpp"

using namespace mgtk;

namespace {

// Sample a 2D noise function over a grid and return basic statistics.
struct Stats {
    double mean = 0.0;
    double min_value = 1e30;
    double max_value = -1e30;
    int non_finite = 0;
};

template <typename Fn>
Stats sample_grid(Fn fn, int n, float step, float origin) {
    Stats s;
    double total = 0.0;
    int count = 0;
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            const float x = origin + static_cast<float>(i) * step;
            const float y = origin + static_cast<float>(j) * step;
            const float v = fn(x, y);
            if (!std::isfinite(v)) {
                ++s.non_finite;
                continue;
            }
            total += v;
            s.min_value = std::min(s.min_value, static_cast<double>(v));
            s.max_value = std::max(s.max_value, static_cast<double>(v));
            ++count;
        }
    }
    s.mean = count > 0 ? total / count : 0.0;
    return s;
}

}  // namespace

MGTK_TEST(noise_generators_are_deterministic) {
    const uint32_t seed = 12345u;

    // Calling twice with the same arguments must give bit-identical results.
    for (int i = 0; i < 50; ++i) {
        const float x = static_cast<float>(i) * 0.37f;
        const float y = static_cast<float>(i) * -0.21f;

        CHECK_NEAR(value_noise_2d(x, y, seed), value_noise_2d(x, y, seed), 0.0f);
        CHECK_NEAR(gradient_noise_2d(x, y, seed), gradient_noise_2d(x, y, seed), 0.0f);
        CHECK_NEAR(simplex_noise_2d(x, y, seed), simplex_noise_2d(x, y, seed), 0.0f);
        CHECK_NEAR(worley_noise_2d(x, y, seed), worley_noise_2d(x, y, seed), 0.0f);
        CHECK_NEAR(worley_edge_2d(x, y, seed), worley_edge_2d(x, y, seed), 0.0f);

        CHECK_NEAR(value_noise_3d(x, y, 0.5f, seed),
                   value_noise_3d(x, y, 0.5f, seed), 0.0f);
        CHECK_NEAR(gradient_noise_3d(x, y, 0.5f, seed),
                   gradient_noise_3d(x, y, 0.5f, seed), 0.0f);
        CHECK_NEAR(simplex_noise_3d(x, y, 0.5f, seed),
                   simplex_noise_3d(x, y, 0.5f, seed), 0.0f);
        CHECK_NEAR(worley_noise_3d(x, y, 0.5f, seed),
                   worley_noise_3d(x, y, 0.5f, seed), 0.0f);
    }
}

MGTK_TEST(noise_seeds_actually_change_the_field) {
    // A seed that does not change the output would make the Seed parameter a
    // lie, and would make two effects on the same layer identical.
    int differences = 0;
    for (int i = 0; i < 40; ++i) {
        const float x = static_cast<float>(i) * 0.41f;
        const float y = static_cast<float>(i) * 0.13f;
        if (std::fabs(gradient_noise_2d(x, y, 1u) -
                      gradient_noise_2d(x, y, 2u)) > 1e-4f) {
            ++differences;
        }
    }
    CHECK_MSG(differences > 35, "changing the seed barely changed the field");
}

MGTK_TEST(noise_is_continuous) {
    // Noise that is not continuous produces visible seams when used as a
    // displacement field. Walk a fine line and make sure nothing jumps.
    constexpr uint32_t seed = 99u;

    float prev_value = value_noise_2d(0.0f, 0.0f, seed);
    float prev_gradient = gradient_noise_2d(0.0f, 0.0f, seed);
    float prev_simplex = simplex_noise_2d(0.0f, 0.0f, seed);

    for (int i = 1; i <= 4000; ++i) {
        const float t = static_cast<float>(i) * 0.0025f;

        const float v = value_noise_2d(t, t * 0.5f, seed);
        const float g = gradient_noise_2d(t, t * 0.5f, seed);
        const float s = simplex_noise_2d(t, t * 0.5f, seed);

        CHECK_MSG(std::fabs(v - prev_value) < 0.05f, "value noise jumped");
        CHECK_MSG(std::fabs(g - prev_gradient) < 0.05f, "gradient noise jumped");
        CHECK_MSG(std::fabs(s - prev_simplex) < 0.05f, "simplex noise jumped");

        prev_value = v;
        prev_gradient = g;
        prev_simplex = s;
    }
}

MGTK_TEST(noise_stays_finite_far_from_the_origin) {
    // Coordinates far from the origin are routine -- a warp samples in a space
    // scaled by noise_scale, and a long animation pushes time up. Integer
    // conversion must not overflow or produce NaN.
    const uint32_t seed = 7u;
    const float probes[] = {-1.0e7f, -1.0e5f, -1000.0f, 0.0f,
                            1000.0f, 1.0e5f, 1.0e7f};

    for (float x : probes) {
        for (float y : probes) {
            CHECK_FINITE(value_noise_2d(x, y, seed));
            CHECK_FINITE(gradient_noise_2d(x, y, seed));
            CHECK_FINITE(simplex_noise_2d(x, y, seed));
            CHECK_FINITE(worley_noise_2d(x, y, seed));
            CHECK_FINITE(value_noise_3d(x, y, x, seed));
            CHECK_FINITE(gradient_noise_3d(x, y, y, seed));
            CHECK_FINITE(simplex_noise_3d(x, y, x + y, seed));
        }
    }
}

MGTK_TEST(noise_output_ranges_are_sane) {
    // Gradient and simplex noise should be zero-mean-ish and stay within
    // roughly [-1,1]. A badly scaled gradient table shows up here as a mean
    // that has drifted or a range that has blown out.
    const Stats g = sample_grid(
        [](float x, float y) { return gradient_noise_2d(x, y, 4242u); }, 200, 0.05f, -5.0f);
    CHECK_EQ(g.non_finite, 0);
    CHECK_MSG(std::fabs(g.mean) < 0.15, "gradient noise is not centred on zero");
    CHECK(g.min_value > -1.6);
    CHECK(g.max_value < 1.6);
    CHECK_MSG(g.max_value - g.min_value > 0.5, "gradient noise has too little range");

    const Stats s = sample_grid(
        [](float x, float y) { return simplex_noise_2d(x, y, 4242u); }, 200, 0.05f, -5.0f);
    CHECK_EQ(s.non_finite, 0);
    CHECK_MSG(std::fabs(s.mean) < 0.15, "simplex noise is not centred on zero");
    CHECK(s.min_value > -1.3);
    CHECK(s.max_value < 1.3);
    CHECK_MSG(s.max_value - s.min_value > 0.5, "simplex noise has too little range");

    // Value noise is [0,1].
    const Stats v = sample_grid(
        [](float x, float y) { return value_noise_2d(x, y, 4242u); }, 200, 0.05f, -5.0f);
    CHECK_EQ(v.non_finite, 0);
    CHECK(v.min_value >= -1e-6);
    CHECK(v.max_value <= 1.0 + 1e-6);
    CHECK_MSG(v.max_value - v.min_value > 0.5, "value noise has too little range");

    // Worley is a normalised distance, so also [0,1].
    const Stats w = sample_grid(
        [](float x, float y) { return worley_noise_2d(x, y, 4242u); }, 200, 0.05f, -5.0f);
    CHECK_EQ(w.non_finite, 0);
    CHECK(w.min_value >= -1e-6);
    CHECK(w.max_value <= 1.0 + 1e-6);
}

MGTK_TEST(noise_worley_edge_is_non_negative) {
    // The second-nearest minus nearest distance can only be >= 0.
    for (int j = 0; j < 60; ++j) {
        for (int i = 0; i < 60; ++i) {
            const float x = static_cast<float>(i) * 0.137f;
            const float y = static_cast<float>(j) * 0.211f;
            const float e = worley_edge_2d(x, y, 555u);
            CHECK_FINITE(e);
            CHECK(e >= -1e-6f);
        }
    }
}

MGTK_TEST(noise_jitter_zero_gives_a_regular_grid) {
    // With no jitter, feature points sit at cell centres, so the pattern is
    // exactly periodic with period 1.
    for (int i = 0; i < 40; ++i) {
        const float x = static_cast<float>(i) * 0.23f;
        const float y = static_cast<float>(i) * 0.11f;
        const float a = worley_noise_2d(x, y, 3u, 0.0f);
        const float b = worley_noise_2d(x + 1.0f, y + 1.0f, 3u, 0.0f);
        CHECK_NEAR(a, b, 1e-5f);
    }
}

MGTK_TEST(noise_fbm_is_deterministic_and_bounded) {
    FbmParams params;
    params.octaves = 5;
    params.lacunarity = 2.0f;
    params.gain = 0.5f;

    for (int i = 0; i < 30; ++i) {
        const float x = static_cast<float>(i) * 0.31f;
        const float y = static_cast<float>(i) * -0.17f;

        const float a = fbm_2d(x, y, 12u, params);
        const float b = fbm_2d(x, y, 12u, params);
        CHECK_NEAR(a, b, 0.0f);
        CHECK_FINITE(a);
        CHECK(a >= -1.2f && a <= 1.2f);

        const float r = ridged_fbm_2d(x, y, 12u, params);
        CHECK_FINITE(r);
        CHECK(r >= -1.2f && r <= 1.2f);

        const float t = turbulence_2d(x, y, 12u, params);
        CHECK_FINITE(t);
        CHECK(t >= -1.2f && t <= 1.2f);
    }
}

MGTK_TEST(noise_fbm_octaves_add_detail) {
    // More octaves must add *high-frequency* energy. The obvious metric --
    // total variation -- is useless here: it is dominated by the base octave
    // and barely moves as octaves are added, so a test built on it would pass
    // even if the octave loop never advanced its frequency.
    //
    // The second difference is the right probe. For a component of amplitude A
    // and frequency f it scales as A*f^2, so summed over octaves it captures
    // exactly the quantity of interest. With the standard gain 0.5 /
    // lacunarity 2 pairing, (A*f)^2 is constant across octaves, which makes the
    // curvature grow roughly linearly in the octave count.
    const auto curvature = [](int octaves, float dt, int samples) {
        FbmParams p;
        p.octaves = octaves;
        double acc = 0.0;
        float a = fbm_2d(0.0f, 0.0f, 1u, p);
        float b = fbm_2d(dt, 0.0f, 1u, p);
        for (int i = 2; i <= samples; ++i) {
            const float c = fbm_2d(static_cast<float>(i) * dt, 0.0f, 1u, p);
            const double d = static_cast<double>(c) - 2.0 * b + a;
            acc += d * d;
            a = b;
            b = c;
        }
        return std::sqrt(acc / samples);
    };

    const double one = curvature(1, 0.002f, 10000);
    const double four = curvature(4, 0.002f, 10000);
    const double eight = curvature(8, 0.002f, 10000);

    CHECK_FINITE(one);
    CHECK_FINITE(four);
    CHECK_FINITE(eight);

    CHECK_MSG(four > one * 2.0, "four octaves carried no more detail than one");
    CHECK_MSG(eight > four * 2.0, "eight octaves carried no more detail than four");

    // And the sequence has to be monotone, not merely larger at the ends.
    double previous = one;
    for (int octaves : {2, 3, 5, 6}) {
        const double c = curvature(octaves, 0.002f, 10000);
        CHECK_MSG(c > previous, "curvature decreased as octaves were added");
        previous = c;
    }
}

MGTK_TEST(noise_fbm_extreme_parameters_are_safe) {
    // Users drag sliders to their limits, and a preset can carry any value.
    for (int octaves : {-5, 0, 1, 10, 100}) {
        for (float lacunarity : {0.0f, 1.0f, 1.01f, 8.0f, 100.0f}) {
            for (float gain : {-1.0f, 0.0f, 0.5f, 0.95f, 5.0f}) {
                FbmParams p;
                p.octaves = octaves;
                p.lacunarity = lacunarity;
                p.gain = gain;

                const float v = fbm_2d(1.5f, 2.5f, 3u, p);
                CHECK_FINITE(v);
                CHECK(std::fabs(v) < 10.0f);

                CHECK_FINITE(ridged_fbm_2d(1.5f, 2.5f, 3u, p));
                CHECK_FINITE(turbulence_2d(1.5f, 2.5f, 3u, p));
                CHECK_FINITE(fbm_3d(1.5f, 2.5f, 0.5f, 3u, p));
            }
        }
    }
}

MGTK_TEST(noise_fbm_helpers_remap_into_unit_range) {
    FbmParams p;
    p.octaves = 4;

    for (int i = 0; i < 200; ++i) {
        const float x = static_cast<float>(i) * 0.13f;
        const float y = static_cast<float>(i) * 0.07f;

        const float a = fbm_2d_01(x, y, 5u, p);
        CHECK_FINITE(a);
        CHECK(a >= 0.0f && a <= 1.0f);

        const float b = fbm_3d_01(x, y, 0.5f, 5u, p);
        CHECK_FINITE(b);
        CHECK(b >= 0.0f && b <= 1.0f);
    }
}

MGTK_TEST(noise_decorrelated_across_octaves) {
    // If the octave seed were not mixed, all octaves would sample the same
    // lattice and the field would show strong grid alignment. Checking that two
    // nearby sample positions differ is a cheap proxy for "the field is not
    // trivially self-similar".
    FbmParams p;
    p.octaves = 6;

    std::set<int> quantised;
    for (int i = 0; i < 500; ++i) {
        const float x = static_cast<float>(i) * 0.031f;
        const float v = fbm_2d(x, x * 1.7f, 21u, p);
        quantised.insert(static_cast<int>(std::floor(v * 100.0f)));
    }
    CHECK_MSG(quantised.size() > 40,
              "fBm produced too few distinct values -- octaves may be correlated");
}
