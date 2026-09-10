// =============================================================================
//  mgtk/math.hpp -- scalar/vector helpers, easing, and deterministic hashing
//
//  Pure C++17. No Adobe headers, no platform headers, no allocation.
//  Every function here is a candidate for auto-vectorisation; keep the bodies
//  branch-free where you can.
// =============================================================================
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace mgtk {

// ---------------------------------------------------------------------------
//  Floating-point literals & constants
// ---------------------------------------------------------------------------
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr float kHalfPi = 1.57079632679489661923f;
inline constexpr float kDegToRad = kPi / 180.0f;
inline constexpr float kRadToDeg = 180.0f / kPi;
inline constexpr float kEpsilon = 1.0e-8f;

// ---------------------------------------------------------------------------
//  Basic scalar helpers
// ---------------------------------------------------------------------------
template <typename T>
inline T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

template <typename T>
inline T min2(T a, T b) { return a < b ? a : b; }
template <typename T>
inline T max2(T a, T b) { return a > b ? a : b; }
template <typename T>
inline T min3(T a, T b, T c) { return min2(a, min2(b, c)); }
template <typename T>
inline T max3(T a, T b, T c) { return max2(a, max2(b, c)); }

inline float saturate(float v) { return clamp(v, 0.0f, 1.0f); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float fract(float v) { return v - std::floor(v); }

// Smooth Hermite interpolation; t is clamped to [0,1].
inline float smoothstep(float t) {
    t = saturate(t);
    return t * t * (3.0f - 2.0f * t);
}

// 5th-order smootherstep -- C2 continuous, used for high-quality easing.
inline float smootherstep(float t) {
    t = saturate(t);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float smoothstep(float edge0, float edge1, float x) {
    const float d = edge1 - edge0;
    if (std::fabs(d) < kEpsilon) return x < edge0 ? 0.0f : 1.0f;
    return smoothstep((x - edge0) / d);
}

inline float smootherstep(float edge0, float edge1, float x) {
    const float d = edge1 - edge0;
    if (std::fabs(d) < kEpsilon) return x < edge0 ? 0.0f : 1.0f;
    return smootherstep((x - edge0) / d);
}

// Remap x from [inMin,inMax] into [outMin,outMax] without clamping.
inline float remap(float x, float inMin, float inMax, float outMin, float outMax) {
    const float d = inMax - inMin;
    if (std::fabs(d) < kEpsilon) return outMin;
    return outMin + (x - inMin) * (outMax - outMin) / d;
}

// Remap and clamp to the output range.
inline float remap_clamped(float x, float inMin, float inMax, float outMin, float outMax) {
    return lerp(outMin, outMax, saturate(remap(x, inMin, inMax, 0.0f, 1.0f)));
}

// Integer modulo that always returns a non-negative result for positive m.
inline int imod(int a, int m) {
    if (m <= 0) return 0;
    const int r = a % m;
    return r < 0 ? r + m : r;
}

// Floating-point modulo that always returns a non-negative result for m > 0.
inline float fmod_positive(float a, float m) {
    if (m <= 0.0f) return 0.0f;
    const float r = std::fmod(a, m);
    return r < 0.0f ? r + m : r;
}

// Mirror a coordinate into [0, extent). Used for edge-extend sampling.
inline float mirror_coord(float v, float extent) {
    if (extent <= 0.0f) return 0.0f;
    const float period = 2.0f * extent;
    float m = fmod_positive(v, period);
    if (m >= extent) m = period - m;
    return m;
}

// ---------------------------------------------------------------------------
//  Log / exponential helpers
// ---------------------------------------------------------------------------
inline float exp2f_fast(float x) { return std::exp2(x); }

// Safe pow: never returns NaN for a negative or zero base.
inline float safe_pow(float base, float e) {
    const float b = std::fabs(base) < kEpsilon ? (base < 0.0f ? -kEpsilon : kEpsilon) : base;
    return std::pow(b, e);
}

// The classic "ratio" exponent: ratio > 1 brightens, < 1 darkens, 1 is identity.
inline float apply_ratio(float v, float ratio) {
    if (std::fabs(ratio - 1.0f) < kEpsilon) return v;
    if (ratio <= kEpsilon) return 0.0f;
    if (v <= 0.0f) return 0.0f;
    return std::pow(v, 1.0f / std::max(ratio, kEpsilon));
}

// ---------------------------------------------------------------------------
//  Colour-space helpers (linear <-> sRGB)
// ---------------------------------------------------------------------------
inline float srgb_to_linear(float c) {
    if (c <= 0.0f) return 0.0f;
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb(float c) {
    if (c <= 0.0f) return 0.0f;
    return (c <= 0.0031308f) ? (c * 12.92f) : (1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f);
}

// Rec.709 / sRGB luma weights.
inline float luma(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ---------------------------------------------------------------------------
//  2D vector
// ---------------------------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
inline Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { a.x += b.x; a.y += b.y; return a; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { a.x -= b.x; a.y -= b.y; return a; }
inline Vec2& operator*=(Vec2& a, float s) { a.x *= s; a.y *= s; return a; }

inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length_sq(Vec2 v) { return dot(v, v); }
inline float length(Vec2 v) { return std::sqrt(length_sq(v)); }

inline Vec2 normalize(Vec2 v) {
    const float len = length(v);
    return len > kEpsilon ? v / len : Vec2{0.0f, 0.0f};
}

inline Vec2 lerp(Vec2 a, Vec2 b, float t) {
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)};
}

inline Vec2 rotate(Vec2 v, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// Angle in radians of a vector, atan2(y, x).
inline float angle_of(Vec2 v) { return std::atan2(v.y, v.x); }

// ---------------------------------------------------------------------------
//  Deterministic integer hashing
//
//  Every stochastic effect in the toolkit draws its randomness from these, so
//  a given frame always renders identically -- a hard requirement for AE's
//  disk cache and for multi-frame rendering to produce the same result as a
//  single-threaded render.
// ---------------------------------------------------------------------------

// 32-bit mixing finaliser (murmur3).
inline uint32_t mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

inline uint32_t hash_u32(uint32_t x) { return mix32(x + 0x9e3779b9u); }

inline uint32_t hash_combine(uint32_t a, uint32_t b) {
    return mix32(a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2)));
}

inline uint32_t hash_2i(int32_t x, int32_t y) {
    return hash_combine(hash_u32(static_cast<uint32_t>(x)),
                        hash_u32(static_cast<uint32_t>(y)));
}

inline uint32_t hash_3i(int32_t x, int32_t y, int32_t z) {
    return hash_combine(hash_2i(x, y), hash_u32(static_cast<uint32_t>(z)));
}

// Uniform float in [0,1).
inline float hash_to_unit(uint32_t h) {
    // 24 bits of mantissa precision -> exact, reproducible floats.
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

inline float rand_2i(int32_t x, int32_t y) { return hash_to_unit(hash_2i(x, y)); }
inline float rand_3i(int32_t x, int32_t y, int32_t z) { return hash_to_unit(hash_3i(x, y, z)); }

// Uniform float in [-1,1).
inline float rand_signed_2i(int32_t x, int32_t y) { return rand_2i(x, y) * 2.0f - 1.0f; }
inline float rand_signed_3i(int32_t x, int32_t y, int32_t z) { return rand_3i(x, y, z) * 2.0f - 1.0f; }

// ---------------------------------------------------------------------------
//  Easing curves (shared by the core effects and by the AEGP keyframe tools)
// ---------------------------------------------------------------------------
enum class Easing : int {
    Linear = 0,
    SineIn,
    SineOut,
    SineInOut,
    QuadIn,
    QuadOut,
    QuadInOut,
    CubicIn,
    CubicOut,
    CubicInOut,
    QuartIn,
    QuartOut,
    QuartInOut,
    QuintIn,
    QuintOut,
    QuintInOut,
    ExpoIn,
    ExpoOut,
    ExpoInOut,
    CircIn,
    CircOut,
    CircInOut,
    BackIn,
    BackOut,
    BackInOut,
    ElasticIn,
    ElasticOut,
    ElasticInOut,
    BounceIn,
    BounceOut,
    BounceInOut,
    Count  // sentinel -- keep last
};

// Evaluate an easing curve. t is clamped to [0,1]; the result is NOT clamped.
float evaluate_easing(Easing e, float t);

// Human-readable name of an easing curve (used to populate AE popups).
const char* easing_name(Easing e);

// ---------------------------------------------------------------------------
//  Overshoot / anticipation generator
//
//  Produces the classic "settle" curve used for overshoot animation. Returns a
//  value where 0 -> rest, 1 -> settled, and the curve may exceed 1 in the
//  middle when `overshoot` is non-zero, or dip below 0 at the start when
//  `anticipation` is non-zero.
// ---------------------------------------------------------------------------
float overshoot_curve(float t, float overshoot, float anticipation);

// ---------------------------------------------------------------------------
//  Ballistic bounce, expressed as a series of decaying parabolic arcs.
//
//  `bounces`  -- number of visible bounces (>= 1)
//  `decay`    -- how quickly bounce height falls off (0..1, 1 == no decay)
//  Returns 0 at t<=0 and 1 at t>=1.
// ---------------------------------------------------------------------------
float bounce_curve(float t, int bounces, float decay);

// ---------------------------------------------------------------------------
//  Wiggle
//
//  A smooth, band-limited pseudo-random signal built from summed sinusoids with
//  irrational frequency ratios. Unlike pure value-noise, the result has a
//  controllable, perceptually flat frequency spectrum, which is what makes AE's
//  own wiggle() look organic. Deterministic for a given seed.
// ---------------------------------------------------------------------------
float wiggle_1d(float time, float frequency, float seed);
float wiggle_2d_x(float time, float frequency, float seed);
float wiggle_2d_y(float time, float frequency, float seed);

}  // namespace mgtk
