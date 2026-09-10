// =============================================================================
//  mgtk/noise.cpp -- hash-based procedural noise
//
//  Nothing here uses a global permutation table, which means:
//    * any 32-bit seed works, with no table reshuffling,
//    * the generators are re-entrant and therefore safe to call from as many
//      threads as AE's multi-frame renderer wants to throw at them.
// =============================================================================
#include "mgtk/noise.hpp"

namespace mgtk {

namespace {

// ---------------------------------------------------------------------------
//  Gradient tables
// ---------------------------------------------------------------------------
inline Vec2 grad2(uint32_t h) {
    // 8 evenly spaced directions -- enough variety, and cheap to fold in.
    const uint32_t idx = h & 7u;
    constexpr float kInvSqrt2 = 0.70710678f;
    switch (idx) {
        case 0: return { 1.0f,  0.0f};
        case 1: return {-1.0f,  0.0f};
        case 2: return { 0.0f,  1.0f};
        case 3: return { 0.0f, -1.0f};
        case 4: return { kInvSqrt2,  kInvSqrt2};
        case 5: return {-kInvSqrt2,  kInvSqrt2};
        case 6: return { kInvSqrt2, -kInvSqrt2};
        default:return {-kInvSqrt2, -kInvSqrt2};
    }
}

inline float grad3_dot(uint32_t h, float x, float y, float z) {
    // The 12 classic edge-midpoint gradients.
    const uint32_t idx = h % 12u;
    switch (idx) {
        case 0:  return  x + y;
        case 1:  return -x + y;
        case 2:  return  x - y;
        case 3:  return -x - y;
        case 4:  return  x + z;
        case 5:  return -x + z;
        case 6:  return  x - z;
        case 7:  return -x - z;
        case 8:  return  y + z;
        case 9:  return -y + z;
        case 10: return  y - z;
        default: return -y - z;
    }
}

inline uint32_t coord_hash(int32_t x, int32_t y, uint32_t seed) {
    return hash_combine(hash_combine(hash_u32(static_cast<uint32_t>(x)),
                                     hash_u32(static_cast<uint32_t>(y))),
                        seed);
}

inline uint32_t coord_hash(int32_t x, int32_t y, int32_t z, uint32_t seed) {
    return hash_combine(hash_3i(x, y, z), seed);
}

// Quintic fade, C2 continuous -- avoids the visible lattice creases you get
// with the 3t^2-2t^3 fade.
inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

}  // namespace

// ---------------------------------------------------------------------------
//  Value noise
// ---------------------------------------------------------------------------
float value_noise_2d(float x, float y, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float tx = fade(x - fx);
    const float ty = fade(y - fy);

    const float v00 = hash_to_unit(coord_hash(ix,     iy,     seed));
    const float v10 = hash_to_unit(coord_hash(ix + 1, iy,     seed));
    const float v01 = hash_to_unit(coord_hash(ix,     iy + 1, seed));
    const float v11 = hash_to_unit(coord_hash(ix + 1, iy + 1, seed));

    const float a = lerp(v00, v10, tx);
    const float b = lerp(v01, v11, tx);
    return lerp(a, b, ty);
}

float value_noise_3d(float x, float y, float z, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const int iz = static_cast<int>(fz);
    const float tx = fade(x - fx);
    const float ty = fade(y - fy);
    const float tz = fade(z - fz);

    float c[2][2][2];
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
                c[k][j][i] = hash_to_unit(
                    coord_hash(ix + i, iy + j, iz + k, seed));

    const float x00 = lerp(c[0][0][0], c[0][0][1], tx);
    const float x10 = lerp(c[0][1][0], c[0][1][1], tx);
    const float x01 = lerp(c[1][0][0], c[1][0][1], tx);
    const float x11 = lerp(c[1][1][0], c[1][1][1], tx);

    const float y0 = lerp(x00, x10, ty);
    const float y1 = lerp(x01, x11, ty);
    return lerp(y0, y1, tz);
}

// ---------------------------------------------------------------------------
//  Gradient (Perlin) noise
// ---------------------------------------------------------------------------
float gradient_noise_2d(float x, float y, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const float dx = x - fx;
    const float dy = y - fy;
    const float tx = fade(dx);
    const float ty = fade(dy);

    const Vec2 g00 = grad2(coord_hash(ix,     iy,     seed));
    const Vec2 g10 = grad2(coord_hash(ix + 1, iy,     seed));
    const Vec2 g01 = grad2(coord_hash(ix,     iy + 1, seed));
    const Vec2 g11 = grad2(coord_hash(ix + 1, iy + 1, seed));

    const float n00 = g00.x * dx          + g00.y * dy;
    const float n10 = g10.x * (dx - 1.0f) + g10.y * dy;
    const float n01 = g01.x * dx          + g01.y * (dy - 1.0f);
    const float n11 = g11.x * (dx - 1.0f) + g11.y * (dy - 1.0f);

    const float a = lerp(n00, n10, tx);
    const float b = lerp(n01, n11, tx);
    // Scale so the output spans roughly [-1,1].
    return lerp(a, b, ty) * 1.41421356f;
}

float gradient_noise_3d(float x, float y, float z, uint32_t seed) {
    const float fx = std::floor(x);
    const float fy = std::floor(y);
    const float fz = std::floor(z);
    const int ix = static_cast<int>(fx);
    const int iy = static_cast<int>(fy);
    const int iz = static_cast<int>(fz);
    const float dx = x - fx;
    const float dy = y - fy;
    const float dz = z - fz;
    const float tx = fade(dx);
    const float ty = fade(dy);
    const float tz = fade(dz);

    float n[2][2][2];
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                const float ox = dx - static_cast<float>(i);
                const float oy = dy - static_cast<float>(j);
                const float oz = dz - static_cast<float>(k);
                n[k][j][i] = grad3_dot(coord_hash(ix + i, iy + j, iz + k, seed),
                                       ox, oy, oz);
            }

    const float x00 = lerp(n[0][0][0], n[0][0][1], tx);
    const float x10 = lerp(n[0][1][0], n[0][1][1], tx);
    const float x01 = lerp(n[1][0][0], n[1][0][1], tx);
    const float x11 = lerp(n[1][1][0], n[1][1][1], tx);

    const float y0 = lerp(x00, x10, ty);
    const float y1 = lerp(x01, x11, ty);
    return lerp(y0, y1, tz) * 1.1547f;
}

// ---------------------------------------------------------------------------
//  Simplex noise
// ---------------------------------------------------------------------------
namespace {
constexpr float kF2 = 0.3660254037844386f;   // 0.5*(sqrt(3)-1)
constexpr float kG2 = 0.2113248654051871f;   // (3-sqrt(3))/6
constexpr float kF3 = 0.3333333333333333f;   // 1/3
constexpr float kG3 = 0.1666666666666667f;   // 1/6
}  // namespace

float simplex_noise_2d(float x, float y, uint32_t seed) {
    const float s = (x + y) * kF2;
    const int i = static_cast<int>(std::floor(x + s));
    const int j = static_cast<int>(std::floor(y + s));
    const float t = static_cast<float>(i + j) * kG2;

    const float x0 = x - (static_cast<float>(i) - t);
    const float y0 = y - (static_cast<float>(j) - t);

    // Which of the two triangles in the skewed cell are we in?
    const int i1 = (x0 > y0) ? 1 : 0;
    const int j1 = (x0 > y0) ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + kG2;
    const float y1 = y0 - static_cast<float>(j1) + kG2;
    const float x2 = x0 - 1.0f + 2.0f * kG2;
    const float y2 = y0 - 1.0f + 2.0f * kG2;

    auto corner = [&](int ci, int cj, float cx, float cy) {
        float att = 0.5f - cx * cx - cy * cy;
        if (att <= 0.0f) return 0.0f;
        att *= att;
        const Vec2 g = grad2(coord_hash(i + ci, j + cj, seed));
        return att * att * (g.x * cx + g.y * cy);
    };

    const float n = corner(0, 0, x0, y0) +
                    corner(i1, j1, x1, y1) +
                    corner(1, 1, x2, y2);
    return n * 70.0f;
}

float simplex_noise_3d(float x, float y, float z, uint32_t seed) {
    const float s = (x + y + z) * kF3;
    const int i = static_cast<int>(std::floor(x + s));
    const int j = static_cast<int>(std::floor(y + s));
    const int k = static_cast<int>(std::floor(z + s));
    const float t = static_cast<float>(i + j + k) * kG3;

    const float x0 = x - (static_cast<float>(i) - t);
    const float y0 = y - (static_cast<float>(j) - t);
    const float z0 = z - (static_cast<float>(k) - t);

    // Rank the coordinates to work out which of the six tetrahedra we are in.
    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0)      { i1=1; j1=0; k1=0; i2=1; j2=1; k2=0; }
        else if (x0 >= z0) { i1=1; j1=0; k1=0; i2=1; j2=0; k2=1; }
        else               { i1=0; j1=0; k1=1; i2=1; j2=0; k2=1; }
    } else {
        if (y0 < z0)       { i1=0; j1=0; k1=1; i2=0; j2=1; k2=1; }
        else if (x0 < z0)  { i1=0; j1=1; k1=0; i2=0; j2=1; k2=1; }
        else               { i1=0; j1=1; k1=0; i2=1; j2=1; k2=0; }
    }

    const float x1 = x0 - static_cast<float>(i1) + kG3;
    const float y1 = y0 - static_cast<float>(j1) + kG3;
    const float z1 = z0 - static_cast<float>(k1) + kG3;
    const float x2 = x0 - static_cast<float>(i2) + 2.0f * kG3;
    const float y2 = y0 - static_cast<float>(j2) + 2.0f * kG3;
    const float z2 = z0 - static_cast<float>(k2) + 2.0f * kG3;
    const float x3 = x0 - 1.0f + 3.0f * kG3;
    const float y3 = y0 - 1.0f + 3.0f * kG3;
    const float z3 = z0 - 1.0f + 3.0f * kG3;

    auto corner = [&](int ci, int cj, int ck, float cx, float cy, float cz) {
        float att = 0.6f - cx * cx - cy * cy - cz * cz;
        if (att <= 0.0f) return 0.0f;
        att *= att;
        return att * att * grad3_dot(coord_hash(i + ci, j + cj, k + ck, seed),
                                     cx, cy, cz);
    };

    const float n = corner(0, 0, 0, x0, y0, z0) +
                    corner(i1, j1, k1, x1, y1, z1) +
                    corner(i2, j2, k2, x2, y2, z2) +
                    corner(1, 1, 1, x3, y3, z3);
    return n * 32.0f;
}

// ---------------------------------------------------------------------------
//  Worley / cellular
// ---------------------------------------------------------------------------
namespace {

// Returns (nearest, second-nearest) distances.
void worley_distances_2d(float x, float y, uint32_t seed, float jitter,
                         float& d1, float& d2) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));

    d1 = 1.0e9f;
    d2 = 1.0e9f;

    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int cx = ix + i;
            const int cy = iy + j;
            const uint32_t h = coord_hash(cx, cy, seed);

            // Offset the feature point within its cell.
            const float ox = 0.5f + (hash_to_unit(h) - 0.5f) * jitter;
            const float oy = 0.5f + (hash_to_unit(mix32(h)) - 0.5f) * jitter;

            const float dx = static_cast<float>(cx) + ox - x;
            const float dy = static_cast<float>(cy) + oy - y;
            const float d = std::sqrt(dx * dx + dy * dy);

            if (d < d1) {
                d2 = d1;
                d1 = d;
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
}

}  // namespace

float worley_noise_2d(float x, float y, uint32_t seed, float jitter) {
    float d1, d2;
    worley_distances_2d(x, y, seed, clamp(jitter, 0.0f, 1.0f), d1, d2);
    return saturate(d1);
}

float worley_edge_2d(float x, float y, uint32_t seed, float jitter) {
    float d1, d2;
    worley_distances_2d(x, y, seed, clamp(jitter, 0.0f, 1.0f), d1, d2);
    return saturate(d2 - d1);
}

float worley_noise_3d(float x, float y, float z, uint32_t seed, float jitter) {
    jitter = clamp(jitter, 0.0f, 1.0f);
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const int iz = static_cast<int>(std::floor(z));

    float d1 = 1.0e9f;
    for (int k = -1; k <= 1; ++k) {
        for (int j = -1; j <= 1; ++j) {
            for (int i = -1; i <= 1; ++i) {
                const uint32_t h = coord_hash(ix + i, iy + j, iz + k, seed);
                const float ox = 0.5f + (hash_to_unit(h) - 0.5f) * jitter;
                const float oy = 0.5f + (hash_to_unit(mix32(h)) - 0.5f) * jitter;
                const float oz = 0.5f + (hash_to_unit(mix32(mix32(h))) - 0.5f) * jitter;

                const float dx = static_cast<float>(ix + i) + ox - x;
                const float dy = static_cast<float>(iy + j) + oy - y;
                const float dz = static_cast<float>(iz + k) + oz - z;
                const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d < d1) d1 = d;
            }
        }
    }
    return saturate(d1);
}

// ---------------------------------------------------------------------------
//  fBm family
// ---------------------------------------------------------------------------
namespace {

inline void effective_octaves(const FbmParams& p, int& octaves, float& lacunarity,
                              float& gain) {
    octaves = clamp(p.octaves, 1, 10);
    lacunarity = clamp(p.lacunarity, 1.01f, 8.0f);
    gain = clamp(p.gain, 0.05f, 0.95f);
}

}  // namespace

float fbm_2d(float x, float y, uint32_t seed, const FbmParams& p) {
    int octaves;
    float lac, gain;
    effective_octaves(p, octaves, lac, gain);

    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = p.frequency;
    uint32_t s = seed;

    for (int i = 0; i < octaves; ++i) {
        sum += amp * gradient_noise_2d(x * freq, y * freq, s);
        norm += amp;
        amp *= gain;
        freq *= lac;
        s = mix32(s + 0x9e3779b9u);  // decorrelate octaves
    }
    return norm > kEpsilon ? sum / norm : 0.0f;
}

float fbm_3d(float x, float y, float z, uint32_t seed, const FbmParams& p) {
    int octaves;
    float lac, gain;
    effective_octaves(p, octaves, lac, gain);

    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = p.frequency;
    uint32_t s = seed;

    for (int i = 0; i < octaves; ++i) {
        sum += amp * gradient_noise_3d(x * freq, y * freq, z * freq, s);
        norm += amp;
        amp *= gain;
        freq *= lac;
        s = mix32(s + 0x9e3779b9u);
    }
    return norm > kEpsilon ? sum / norm : 0.0f;
}

float ridged_fbm_2d(float x, float y, uint32_t seed, const FbmParams& p) {
    int octaves;
    float lac, gain;
    effective_octaves(p, octaves, lac, gain);

    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = p.frequency;
    uint32_t s = seed;

    for (int i = 0; i < octaves; ++i) {
        const float n = gradient_noise_2d(x * freq, y * freq, s);
        // 1 - |n| produces creases at the zero crossings.
        sum += amp * (1.0f - std::fabs(n));
        norm += amp;
        amp *= gain;
        freq *= lac;
        s = mix32(s + 0x9e3779b9u);
    }
    return norm > kEpsilon ? (sum / norm) * 2.0f - 1.0f : 0.0f;
}

float ridged_fbm_3d(float x, float y, float z, uint32_t seed, const FbmParams& p) {
    int octaves;
    float lac, gain;
    effective_octaves(p, octaves, lac, gain);

    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = p.frequency;
    uint32_t s = seed;

    for (int i = 0; i < octaves; ++i) {
        const float n = gradient_noise_3d(x * freq, y * freq, z * freq, s);
        sum += amp * (1.0f - std::fabs(n));
        norm += amp;
        amp *= gain;
        freq *= lac;
        s = mix32(s + 0x9e3779b9u);
    }
    return norm > kEpsilon ? (sum / norm) * 2.0f - 1.0f : 0.0f;
}

float turbulence_2d(float x, float y, uint32_t seed, const FbmParams& p) {
    int octaves;
    float lac, gain;
    effective_octaves(p, octaves, lac, gain);

    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = p.frequency;
    uint32_t s = seed;

    for (int i = 0; i < octaves; ++i) {
        sum += amp * std::fabs(gradient_noise_2d(x * freq, y * freq, s));
        norm += amp;
        amp *= gain;
        freq *= lac;
        s = mix32(s + 0x9e3779b9u);
    }
    return norm > kEpsilon ? (sum / norm) * 2.0f - 1.0f : 0.0f;
}

float turbulence_3d(float x, float y, float z, uint32_t seed, const FbmParams& p) {
    int octaves;
    float lac, gain;
    effective_octaves(p, octaves, lac, gain);

    float sum = 0.0f;
    float amp = 1.0f;
    float norm = 0.0f;
    float freq = p.frequency;
    uint32_t s = seed;

    for (int i = 0; i < octaves; ++i) {
        sum += amp * std::fabs(gradient_noise_3d(x * freq, y * freq, z * freq, s));
        norm += amp;
        amp *= gain;
        freq *= lac;
        s = mix32(s + 0x9e3779b9u);
    }
    return norm > kEpsilon ? (sum / norm) * 2.0f - 1.0f : 0.0f;
}

}  // namespace mgtk
