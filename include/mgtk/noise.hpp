// =============================================================================
//  mgtk/noise.hpp -- procedural noise used by the warp / glow / halftone stack
//
//  All generators are hash-based and stateless: the value at a coordinate
//  depends only on that coordinate and the seed. That means multi-threaded
//  rendering produces bit-identical output to a single-threaded render, and a
//  frame cached by AE will match a frame rendered fresh.
// =============================================================================
#pragma once

#include "mgtk/math.hpp"

namespace mgtk {

// ---------------------------------------------------------------------------
//  Value noise -- smooth interpolation between hashed lattice values.
//  Cheap, and adequate wherever it is subsequently blurred or warped.
// ---------------------------------------------------------------------------
float value_noise_2d(float x, float y, uint32_t seed);
float value_noise_3d(float x, float y, float z, uint32_t seed);

// ---------------------------------------------------------------------------
//  Gradient (Perlin-style) noise -- classic "clouds" look, zero mean.
//  Returns roughly [-1,1].
// ---------------------------------------------------------------------------
float gradient_noise_2d(float x, float y, uint32_t seed);
float gradient_noise_3d(float x, float y, float z, uint32_t seed);

// ---------------------------------------------------------------------------
//  Simplex noise -- fewer directional artefacts than Perlin and cheaper in 3D.
//  Returns roughly [-1,1].
// ---------------------------------------------------------------------------
float simplex_noise_2d(float x, float y, uint32_t seed);
float simplex_noise_3d(float x, float y, float z, uint32_t seed);

// ---------------------------------------------------------------------------
//  Worley / cellular noise. Returns the distance to the nearest feature point,
//  normalised to roughly [0,1]. `jitter` of 0 gives a regular grid, 1 gives
//  fully scattered points.
// ---------------------------------------------------------------------------
float worley_noise_2d(float x, float y, uint32_t seed, float jitter = 1.0f);
float worley_noise_3d(float x, float y, float z, uint32_t seed, float jitter = 1.0f);

// Second-nearest distance minus nearest: the classic way to get cell borders.
float worley_edge_2d(float x, float y, uint32_t seed, float jitter = 1.0f);

// ---------------------------------------------------------------------------
//  Fractal Brownian motion -- the workhorse of the Fractal Warp effect.
// ---------------------------------------------------------------------------
struct FbmParams {
    int octaves = 4;          // 1..10
    float lacunarity = 2.0f;  // frequency multiplier per octave
    float gain = 0.5f;        // amplitude multiplier per octave
    float frequency = 1.0f;   // base frequency
};

float fbm_2d(float x, float y, uint32_t seed, const FbmParams& p);
float fbm_3d(float x, float y, float z, uint32_t seed, const FbmParams& p);

// Ridged variant -- takes the absolute value at each octave, producing sharp
// creases that read as lightning/marble veins.
float ridged_fbm_2d(float x, float y, uint32_t seed, const FbmParams& p);
float ridged_fbm_3d(float x, float y, float z, uint32_t seed, const FbmParams& p);

// Turbulence -- sums absolute noise; a cloudier, higher-contrast variant.
float turbulence_2d(float x, float y, uint32_t seed, const FbmParams& p);
float turbulence_3d(float x, float y, float z, uint32_t seed, const FbmParams& p);

// ---------------------------------------------------------------------------
//  Convenience: sample fBm and map the result into [0,1], which is what most
//  effects actually want.
// ---------------------------------------------------------------------------
inline float fbm_2d_01(float x, float y, uint32_t seed, const FbmParams& p) {
    return saturate(fbm_2d(x, y, seed, p) * 0.5f + 0.5f);
}

inline float fbm_3d_01(float x, float y, float z, uint32_t seed, const FbmParams& p) {
    return saturate(fbm_3d(x, y, z, seed, p) * 0.5f + 0.5f);
}

}  // namespace mgtk
