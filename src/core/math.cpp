// =============================================================================
//  mgtk/math.cpp -- implementation of the easing / procedural curves
// =============================================================================
#include "mgtk/math.hpp"

namespace mgtk {

// ---------------------------------------------------------------------------
//  Easing curves
//
//  These are the standard Penner-style equations. Note that Back/Elastic/Bounce
//  intentionally leave the [0,1] range in the middle -- the caller is expected
//  to have sampled at t values that make sense, and to clamp afterwards if it
//  needs a bounded result.
// ---------------------------------------------------------------------------
namespace {

constexpr float kBackC1 = 1.70158f;
constexpr float kBackC2 = kBackC1 * 1.525f;
constexpr float kBackC3 = kBackC1 + 1.0f;
constexpr float kElasticC4 = kTwoPi / 3.0f;
constexpr float kElasticC5 = kTwoPi / 4.5f;

float bounce_out(float t) {
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    if (t < 1.0f / d1) {
        return n1 * t * t;
    } else if (t < 2.0f / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

}  // namespace

float evaluate_easing(Easing e, float t) {
    t = clamp(t, 0.0f, 1.0f);

    switch (e) {
        case Easing::Linear:      return t;

        case Easing::SineIn:      return 1.0f - std::cos(t * kHalfPi);
        case Easing::SineOut:     return std::sin(t * kHalfPi);
        case Easing::SineInOut:   return -(std::cos(kPi * t) - 1.0f) * 0.5f;

        case Easing::QuadIn:      return t * t;
        case Easing::QuadOut:     return 1.0f - (1.0f - t) * (1.0f - t);
        case Easing::QuadInOut:
            return t < 0.5f ? 2.0f * t * t
                            : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);

        case Easing::CubicIn:     return t * t * t;
        case Easing::CubicOut: {  const float u = 1.0f - t; return 1.0f - u * u * u; }
        case Easing::CubicInOut:
            return t < 0.5f ? 4.0f * t * t * t
                            : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);

        case Easing::QuartIn:     return t * t * t * t;
        case Easing::QuartOut: {  const float u = 1.0f - t; return 1.0f - u * u * u * u; }
        case Easing::QuartInOut: {
            if (t < 0.5f) return 8.0f * t * t * t * t;
            const float u = 1.0f - t;
            return 1.0f - 8.0f * u * u * u * u;
        }

        case Easing::QuintIn:     return t * t * t * t * t;
        case Easing::QuintOut: {  const float u = 1.0f - t; return 1.0f - u * u * u * u * u; }
        case Easing::QuintInOut: {
            if (t < 0.5f) return 16.0f * t * t * t * t * t;
            const float u = 1.0f - t;
            return 1.0f - 16.0f * u * u * u * u * u;
        }

        case Easing::ExpoIn:
            return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
        case Easing::ExpoOut:
            return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case Easing::ExpoInOut: {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) * 0.5f
                            : (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) * 0.5f;
        }

        case Easing::CircIn:
            return 1.0f - std::sqrt(std::max(0.0f, 1.0f - t * t));
        case Easing::CircOut: {
            const float u = t - 1.0f;
            return std::sqrt(std::max(0.0f, 1.0f - u * u));
        }
        case Easing::CircInOut: {
            if (t < 0.5f) {
                const float s = 2.0f * t;
                return (1.0f - std::sqrt(std::max(0.0f, 1.0f - s * s))) * 0.5f;
            }
            const float s = 2.0f * t - 2.0f;
            return (std::sqrt(std::max(0.0f, 1.0f - s * s)) + 1.0f) * 0.5f;
        }

        case Easing::BackIn: {
            return kBackC3 * t * t * t - kBackC1 * t * t;
        }
        case Easing::BackOut: {
            const float u = t - 1.0f;
            return 1.0f + kBackC3 * u * u * u + kBackC1 * u * u;
        }
        case Easing::BackInOut: {
            if (t < 0.5f) {
                const float s = 2.0f * t;
                return 0.5f * (s * s * ((kBackC2 + 1.0f) * s - kBackC2));
            }
            const float s = 2.0f * t - 2.0f;
            return 0.5f * (s * s * ((kBackC2 + 1.0f) * s + kBackC2) + 2.0f);
        }

        case Easing::ElasticIn: {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return -std::pow(2.0f, 10.0f * t - 10.0f) *
                   std::sin((t * 10.0f - 10.75f) * kElasticC4);
        }
        case Easing::ElasticOut: {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            return std::pow(2.0f, -10.0f * t) *
                       std::sin((t * 10.0f - 0.75f) * kElasticC4) + 1.0f;
        }
        case Easing::ElasticInOut: {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            if (t < 0.5f) {
                return -(std::pow(2.0f, 20.0f * t - 10.0f) *
                         std::sin((20.0f * t - 11.125f) * kElasticC5)) * 0.5f;
            }
            return (std::pow(2.0f, -20.0f * t + 10.0f) *
                    std::sin((20.0f * t - 11.125f) * kElasticC5)) * 0.5f + 1.0f;
        }

        case Easing::BounceIn:    return 1.0f - bounce_out(1.0f - t);
        case Easing::BounceOut:   return bounce_out(t);
        case Easing::BounceInOut:
            return t < 0.5f ? (1.0f - bounce_out(1.0f - 2.0f * t)) * 0.5f
                            : bounce_out(2.0f * t - 1.0f) * 0.5f + 0.5f;

        case Easing::Count:
        default:                  return t;
    }
}

const char* easing_name(Easing e) {
    switch (e) {
        case Easing::Linear:      return "Linear";
        case Easing::SineIn:      return "Sine In";
        case Easing::SineOut:     return "Sine Out";
        case Easing::SineInOut:   return "Sine In/Out";
        case Easing::QuadIn:      return "Quad In";
        case Easing::QuadOut:     return "Quad Out";
        case Easing::QuadInOut:   return "Quad In/Out";
        case Easing::CubicIn:     return "Cubic In";
        case Easing::CubicOut:    return "Cubic Out";
        case Easing::CubicInOut:  return "Cubic In/Out";
        case Easing::QuartIn:     return "Quart In";
        case Easing::QuartOut:    return "Quart Out";
        case Easing::QuartInOut:  return "Quart In/Out";
        case Easing::QuintIn:     return "Quint In";
        case Easing::QuintOut:    return "Quint Out";
        case Easing::QuintInOut:  return "Quint In/Out";
        case Easing::ExpoIn:      return "Expo In";
        case Easing::ExpoOut:     return "Expo Out";
        case Easing::ExpoInOut:   return "Expo In/Out";
        case Easing::CircIn:      return "Circ In";
        case Easing::CircOut:     return "Circ Out";
        case Easing::CircInOut:   return "Circ In/Out";
        case Easing::BackIn:      return "Back In";
        case Easing::BackOut:     return "Back Out";
        case Easing::BackInOut:   return "Back In/Out";
        case Easing::ElasticIn:   return "Elastic In";
        case Easing::ElasticOut:  return "Elastic Out";
        case Easing::ElasticInOut:return "Elastic In/Out";
        case Easing::BounceIn:    return "Bounce In";
        case Easing::BounceOut:   return "Bounce Out";
        case Easing::BounceInOut: return "Bounce In/Out";
        case Easing::Count:
        default:                  return "Unknown";
    }
}

// ---------------------------------------------------------------------------
//  Overshoot ("settle") curve
//
//  Analytic step response of a damped harmonic oscillator, renormalised so that
//  f(0) == 0 and f(1) == 1 exactly. Low damping -> visible overshoot and ring;
//  high damping -> a smooth, monotone approach. This is what makes "Overshoot
//  Amount" feel like a physical spring rather than an arbitrary curve.
// ---------------------------------------------------------------------------
float overshoot_curve(float t, float overshoot, float anticipation) {
    t = clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f && anticipation <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    const float k = saturate(overshoot);
    // damping coefficient and angular frequency
    const float d = lerp(9.0f, 1.15f, k);
    const float w = lerp(0.0f, 11.0f, k);

    auto response = [&](float x) {
        if (w < kEpsilon) {
            return 1.0f - std::exp(-d * x);           // critically damped
        }
        // x'' + 2*d*x' + (d^2 + w^2)*x = 0, unit step input
        return 1.0f - std::exp(-d * x) *
                          (std::cos(w * x) + (d / w) * std::sin(w * x));
    };

    const float denom = response(1.0f);
    float v = (std::fabs(denom) < kEpsilon) ? t : response(t) / denom;

    // Anticipation: an early dip below zero, exactly zero at both endpoints.
    if (anticipation > 0.0f) {
        const float dip = anticipation * 0.35f *
                          std::sin(kPi * std::pow(t, 0.6f)) *
                          (1.0f - t);
        v -= dip;
    }
    return v;
}

// ---------------------------------------------------------------------------
//  Bounce curve
//
//  A ball falls from height 1 to height 0, then rebounds to a decaying sequence
//  of heights. Returns 1 - height, so the result runs 0 -> 1 as the ball
//  settles. Bounce durations follow the physical sqrt(2h/g) relationship, which
//  is why this reads as "real" gravity rather than as an arbitrary ease.
// ---------------------------------------------------------------------------
float bounce_curve(float t, int bounces, float decay) {
    t = clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    bounces = clamp(bounces, 1, 32);
    decay = clamp(decay, 0.05f, 1.0f);

    // Build the arc durations: [fall, up+down, up+down, ...]
    float durations[33];
    int arcs = 0;
    durations[arcs++] = 1.0f;  // initial fall from height 1
    for (int i = 1; i < bounces; ++i) {
        const float h = std::pow(decay, static_cast<float>(i));
        durations[arcs++] = 2.0f * std::sqrt(h);
    }

    float total = 0.0f;
    for (int i = 0; i < arcs; ++i) total += durations[i];
    if (total < kEpsilon) return t;

    // Locate the arc containing t (in normalised time).
    float remaining = t * total;
    int arc = 0;
    while (arc < arcs - 1 && remaining > durations[arc]) {
        remaining -= durations[arc];
        ++arc;
    }

    const float u = durations[arc] > kEpsilon ? remaining / durations[arc] : 0.0f;

    float height;
    if (arc == 0) {
        // Straight fall: height goes 1 -> 0 following a parabola.
        height = 1.0f - u * u;
    } else {
        // Rebound arc: height rises to h and returns to 0.
        const float h = std::pow(decay, static_cast<float>(arc));
        const float v = 2.0f * u - 1.0f;          // -1 .. 1
        height = h * (1.0f - v * v);
    }
    return 1.0f - saturate(height);
}

// ---------------------------------------------------------------------------
//  Wiggle
//
//  Sum of octaves of a sinusoid with a fixed seed-derived phase. Using a
//  slightly inharmonic frequency ratio (2.13 rather than 2.0) avoids the
//  buzzy, obviously-periodic quality of a pure harmonic stack. The result is
//  smooth and differentiable, which means the derivative AE computes for a
//  keyframe velocity is a finite, sensible number.
// ---------------------------------------------------------------------------
namespace {

// Two octaves, not the five you might expect from a noise generator.
//
// This is a band-limiting decision. Wiggle is sampled once per frame, so any
// component above the frame rate's Nyquist limit (fps/2) aliases: it stops
// reading as movement and starts reading as per-frame jitter. With the classic
// 2.13x octave ratio, N octaves put the top component at 2.13^(N-1) times the
// requested frequency:
//
//   2 octaves -> 2.1x   alias-free up to requested frequency fps/4.3
//   3 octaves -> 4.5x   alias-free up to requested frequency fps/9.1
//   5 octaves -> 20.5x  alias-free up to requested frequency fps/41
//
// At 24 fps that makes five octaves unsafe above a requested frequency of 0.6,
// which is far too restrictive to ship. Two octaves keep wiggle(2) coherent at
// 24 fps (top component 4.3 Hz against a 12 Hz Nyquist limit) while still
// having enough high-frequency detail to read as organic rather than as a
// plain sine. Callers wanting more texture can raise `frequency`, which scales
// every octave together.
constexpr int kOctaves = 2;
constexpr float kBase = 2.13f;  // inharmonic ratio

float wiggle_core(float time, float frequency, uint32_t seed, uint32_t octave_offset) {
    float total = 0.0f;
    float norm = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;

    for (int i = 0; i < kOctaves; ++i) {
        const uint32_t h = hash_u32(seed + octave_offset * 8191u +
                                    static_cast<uint32_t>(i) * 131u);
        const float phase = hash_to_unit(h) * kTwoPi;
        total += amp * std::sin(kTwoPi * freq * frequency * time + phase);
        norm += amp;
        amp *= 0.5f;
        freq *= kBase;
    }
    return norm > kEpsilon ? (total / norm) : 0.0f;
}

}  // namespace

float wiggle_1d(float time, float frequency, float seed) {
    const uint32_t s = hash_u32(static_cast<uint32_t>(seed * 65536.0f) ^ 0xB5297A4Du);
    return wiggle_core(time, frequency, s, 0u);
}

float wiggle_2d_x(float time, float frequency, float seed) {
    const uint32_t s = hash_u32(static_cast<uint32_t>(seed * 65536.0f) ^ 0x68E31DA4u);
    return wiggle_core(time, frequency, s, 1u);
}

float wiggle_2d_y(float time, float frequency, float seed) {
    const uint32_t s = hash_u32(static_cast<uint32_t>(seed * 65536.0f) ^ 0xB5297A4Du);
    return wiggle_core(time, frequency, s, 3u);
}

}  // namespace mgtk
