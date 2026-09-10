// =============================================================================
//  mgtk/effects.hpp -- the eight effects, their parameters, and their entry
//                     points.
//
//  Everything in this header is pure C++17. The After Effects glue layer
//  translates AE parameters into these structs, converts AE's world buffers
//  into `mgtk::Image`, calls `apply_*`, and converts the result back. Keeping
//  the boundary here means every algorithm is unit-testable without After
//  Effects, which is the only sane way to develop pixel code.
// =============================================================================
#pragma once

#include <vector>

#include "mgtk/blend.hpp"
#include "mgtk/blur.hpp"
#include "mgtk/image.hpp"
#include "mgtk/math.hpp"
#include "mgtk/noise.hpp"

namespace mgtk {

// ---------------------------------------------------------------------------
//  Render context
//
//  Deliberately free of std::function: the AE glue passes plain function
//  pointers into its own C++ objects, so nothing allocates on the render path.
// ---------------------------------------------------------------------------
struct RenderContext {
    float frame = 0.0f;    // current frame number (may be fractional)
    float fps = 24.0f;     // composition frame rate
    float seconds = 0.0f;  // frame / fps, precomputed

    // True when AE handed us linear-light pixels, which it does in 32-bit
    // float projects. Tone-sensitive effects (glow threshold, halftone, pixel
    // sort) linearise their tonal maths when this is set, so that a given
    // Threshold value produces the same *visual* result in an 8-bit project
    // and a 32-bit one. Spatial effects ignore it.
    bool input_linear = false;

    void* user_data = nullptr;
    void (*progress)(void* user_data, float fraction) = nullptr;
    bool (*abort_requested)(void* user_data) = nullptr;

    void report(float fraction) const {
        if (progress) progress(user_data, saturate(fraction));
    }
    bool aborted() const {
        return abort_requested != nullptr && abort_requested(user_data);
    }
};

// Quality presets shared by the effects that have a mip / sample-count knob.
enum class Quality : int {
    Draft = 0,   // fastest, meant for scrubbing
    Good,        // the default
    Best,        // slowest, for final render
    Count
};

const char* quality_name(Quality q);
int quality_count();
Quality quality_from_index(int index);

// How an effect should treat straight vs premultiplied source alpha. The glue
// uses this to decide whether to unpremultiply before and premultiply after.
enum class AlphaPolicy : int {
    // Convert to straight alpha, run the effect, convert back. Correct for
    // anything that blurs, samples off-pixel, or blends colours.
    Straight = 0,
    // Leave the pixels as AE gave them. Correct for effects that only reshape
    // geometry or operate per-pixel without cross-sampling.
    PassThrough,
    // Straight alpha for colour, but the alpha result is the original alpha
    // multiplied by a coverage term (used by halftone and pixel-sort, which
    // should not punch holes in a layer's matte).
    StraightKeepSourceAlpha
};

// ===========================================================================
//  1. FEEDBACK ECHO
// ===========================================================================
enum class EchoBlendMode : int {
    Normal = 0, Add, Screen, Lighten, Darken, Difference, Overlay, Count
};

struct FeedbackEchoParams {
    // How much of the accumulated feedback survives into the next frame.
    // 0 -> no echo at all, 1 -> infinitely persistent.
    float echo_amount = 0.80f;

    // Shapes how aggressively the echo is attenuated on every step. The
    // per-step retention is pow(echo_amount, decay_curve), so 1.0 is neutral,
    // values below 1 make the tail persist for longer, and values above 1 make
    // it drop away quickly once it starts to fade.
    float decay_curve = 1.0f;

    EchoBlendMode blend = EchoBlendMode::Normal;

    // Transform applied to the feedback buffer on every step. A scale slightly
    // above or below 1 produces the classic infinite-zoom tunnel.
    float scale = 1.0f;
    float rotation_deg = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    Vec2 center{0.5f, 0.5f};  // normalised, 0.5,0.5 == layer centre

    // Hue rotation applied to the feedback each step, in degrees. Non-zero
    // values make the trail cycle through the colour wheel.
    float hue_shift_deg = 0.0f;

    // Multiplies the feedback's brightness each step.
    float brightness = 1.0f;

    // Saturation of the feedback each step (1 == unchanged).
    float saturation = 1.0f;

    // 1 = feedback only, 0 = source only. Useful as a "how much echo" master.
    float mix = 1.0f;

    // Sampling outside the buffer.
    WrapMode wrap = WrapMode::Clamp;

    // Clear the accumulator (driven by a parameter button in the UI).
    bool reset = false;
};

// `feedback` is read-modify-written: it carries the accumulator between frames
// and must persist in the effect's sequence data. `dst` receives the frame that
// AE will display.
void apply_feedback_echo(const Image& src, Image& feedback, Image& dst,
                         const FeedbackEchoParams& p, const RenderContext& ctx);

// ===========================================================================
//  2. CHROMATIC SPLIT
// ===========================================================================
enum class ChromaMode : int { Radial = 0, Linear, Zoom, Spin, Barrel, Count };
enum class ChromaQuality : int { Bilinear = 0, Bicubic, Count };

struct ChromaticSplitParams {
    ChromaMode mode = ChromaMode::Radial;

    // Master separation in pixels. Negative values invert the direction.
    float amount = 12.0f;

    // Per-channel multipliers. Leaving green at 0 and pushing red/blue apart is
    // the classic lens look; equal-and-opposite is more of a creative choice.
    float amount_r = 1.0f;
    float amount_g = 0.0f;
    float amount_b = -1.0f;

    Vec2 center{0.5f, 0.5f};  // normalised
    float angle_deg = 0.0f;    // used by Linear mode
    float radial_bias = 1.0f;  // >1 pushes the split towards the edges

    // Shape of the falloff from the centre. 1 is linear.
    float falloff = 1.0f;

    // Adds a barrel-style radial warp on top of the split, in pixels.
    float barrel = 0.0f;

    ChromaQuality quality = ChromaQuality::Bilinear;
    WrapMode wrap = WrapMode::Clamp;

    // 1 leaves the effect untouched, 0 has no effect at all.
    float mix = 1.0f;
};

void apply_chromatic_split(const Image& src, Image& dst,
                           const ChromaticSplitParams& p, const RenderContext& ctx);

// ===========================================================================
//  3. ANAMORPHIC GLOW
// ===========================================================================
struct AnamorphicGlowParams {
    // Bright pass
    float threshold = 1.0f;
    float knee = 0.25f;
    float intensity = 1.0f;

    // Radial (bloom) component
    float radius = 60.0f;

    // Streak component
    float streak_length = 300.0f;
    float streak_angle_deg = 0.0f;
    int streak_count = 1;          // 1..8; >1 builds a star
    float streak_intensity = 0.6f;
    int streak_samples = 16;

    // Tints
    Float4 glow_tint{1.0f, 1.0f, 1.0f, 1.0f};
    float glow_tint_amount = 0.0f;
    Float4 streak_tint{0.35f, 0.6f, 1.0f, 1.0f};
    float streak_tint_amount = 0.0f;

    // Splits the glow's colour channels apart for a lens-like fringe.
    float rgb_separation = 0.0f;

    // Composite
    BlendMode blend = BlendMode::Add;
    float mix = 1.0f;

    Quality quality = Quality::Good;

    // When true, the glow is confined to the source's alpha, so it cannot
    // spill outside a layer's matte.
    bool preserve_alpha = true;
};

void apply_anamorphic_glow(const Image& src, Image& dst,
                           const AnamorphicGlowParams& p, const RenderContext& ctx);

// ===========================================================================
//  4. FRACTAL WARP
// ===========================================================================
enum class WarpMode : int {
    Displace = 0,      // pure noise displacement
    Swirl,             // rotational
    Pinch,             // radial squeeze/bulge
    DisplaceSwirl,     // both
    DomainWarp,        // warped noise (the "liquid" look)
    Count
};

struct FractalWarpParams {
    WarpMode mode = WarpMode::DisplaceSwirl;

    float amount = 40.0f;      // displacement in pixels
    float noise_scale = 0.005f;  // cycles per pixel
    int octaves = 4;
    float lacunarity = 2.0f;
    float gain = 0.5f;
    float evolution = 0.0f;    // animates the noise field (seconds)
    uint32_t seed = 0u;

    float swirl_amount = 0.5f;  // turns
    float swirl_radius = 0.5f;  // normalised

    float pinch = 0.0f;         // -1 pinch, +1 bulge

    float center_x = 0.5f;
    float center_y = 0.5f;

    WrapMode wrap = WrapMode::Clamp;
    ChromaQuality quality = ChromaQuality::Bilinear;
    float mix = 1.0f;
};

void apply_fractal_warp(const Image& src, Image& dst,
                        const FractalWarpParams& p, const RenderContext& ctx);

// ===========================================================================
//  5. KALEIDOSCOPE
// ===========================================================================
enum class KaleidoMode : int {
    Mirror = 0,        // reflect every other wedge
    Rotate,            // rotate the wedge N times, no reflection
    MirrorRotate,      // both, gives a mandala
    Quilt,             // 90-degree mirrored tiling
    Count
};

struct KaleidoscopeParams {
    KaleidoMode mode = KaleidoMode::MirrorRotate;
    int segments = 6;             // 2..64
    float rotation_deg = 0.0f;
    Vec2 center{0.5f, 0.5f};     // normalised
    float scale = 1.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float radial_fade = 0.0f;     // 0..1, dims the outer part of each wedge
    ChromaQuality quality = ChromaQuality::Bilinear;
    float mix = 1.0f;
};

void apply_kaleidoscope(const Image& src, Image& dst,
                        const KaleidoscopeParams& p, const RenderContext& ctx);

// ===========================================================================
//  6. HALFTONE PRO
// ===========================================================================
enum class HalftonePattern : int {
    Dots = 0, Lines, Cross, DiamondGrid, Squares, Concentric, Count
};
enum class HalftoneDotShape : int {
    Round = 0, Ellipse, Square, Diamond, Cross, Count
};
enum class HalftoneColorMode : int {
    Monochrome = 0, RGB, CMYK, Count
};

struct HalftoneParams {
    HalftonePattern pattern = HalftonePattern::Dots;
    HalftoneDotShape dot_shape = HalftoneDotShape::Round;
    HalftoneColorMode color_mode = HalftoneColorMode::Monochrome;

    float cell_size = 8.0f;        // pixels
    float angle_deg = 15.0f;       // screen angle
    float angle_spread_deg = 30.0f;  // angle offset between channels in colour modes

    float threshold = 0.0f;        // -1..1, shifts the tonal midpoint
    float contrast = 1.0f;         // 0..4
    float gamma = 1.0f;            // tonal response

    float anti_alias = 1.0f;       // px of edge softness

    Float4 ink{0.0f, 0.0f, 0.0f, 1.0f};
    Float4 paper{1.0f, 1.0f, 1.0f, 1.0f};
    float ink_amount = 1.0f;

    float line_width = 1.0f;       // for Lines / Cross patterns

    bool invert = false;
    BlendMode blend = BlendMode::Normal;
    float mix = 1.0f;
};

void apply_halftone(const Image& src, Image& dst,
                    const HalftoneParams& p, const RenderContext& ctx);

// ===========================================================================
//  7. SLIT SCAN
// ===========================================================================
enum class SlitScanMode : int {
    TimeSlice = 0,   // each strip comes from a different frame
    TimeBlend,       // averaging across time, weighted
    TimeDisplace,    // displacement driven by motion between frames
    TimeEcho,        // ordered trail of frames
    Count
};
enum class SlitDirection : int { Horizontal = 0, Vertical, Radial, Angular, Count };

struct SlitScanParams {
    SlitScanMode mode = SlitScanMode::TimeSlice;
    SlitDirection direction = SlitDirection::Horizontal;

    // How many frames of history the effect consumes. The AE glue checks out
    // this many past frames before calling in.
    int frames = 12;

    float slice_width = 4.0f;    // px per time step
    float offset = 0.0f;         // shifts the strip pattern
    float falloff = 0.0f;        // 0 = hard edges between strips, 1 = smooth
    float angle_deg = 0.0f;      // for Angular direction
    Vec2 center{0.5f, 0.5f};

    bool mirror = false;         // ping-pong the frame index
    float intensity = 1.0f;      // for TimeDisplace
    BlendMode blend = BlendMode::Normal;
    float mix = 1.0f;
};

// `history` must be ordered newest-first: history[0] is the current frame and
// history[i] is i frames earlier. A history shorter than `p.frames` is fine --
// the effect falls back to the oldest entry it has.
void apply_slit_scan(const Image& src, const std::vector<Image>& history, Image& dst,
                     const SlitScanParams& p, const RenderContext& ctx);

// ===========================================================================
//  8. PIXEL SORT
// ===========================================================================
enum class SortKey : int { Brightness = 0, Hue, Saturation, Red, Green, Blue, Random, Count };
enum class SortAxis : int { Horizontal = 0, Vertical, Count };
enum class SortOrder : int { Ascending = 0, Descending, Count };

struct PixelSortParams {
    SortKey key = SortKey::Brightness;
    SortAxis axis = SortAxis::Horizontal;
    SortOrder order = SortOrder::Descending;

    float threshold_low = 0.15f;
    float threshold_high = 1.0f;

    int max_length = 200;      // px; the longest run that may be sorted
    float randomness = 0.0f;   // 0 = deterministic, 1 = shuffled

    // 0..1: stretches each sorted run across its full span instead of moving
    // pixels. Gives the "smeared" datamosh look.
    float stretch = 0.0f;

    // Confines sorting to fully opaque pixels, which stops the effect from
    // mangling soft mattes.
    bool use_alpha_mask = false;

    BlendMode blend = BlendMode::Normal;
    float mix = 1.0f;
};

void apply_pixel_sort(const Image& src, Image& dst,
                      const PixelSortParams& p, const RenderContext& ctx);

// ---------------------------------------------------------------------------
//  Shared helpers used by more than one effect
// ---------------------------------------------------------------------------

// Rotate hue by `degrees`, keeping luminance roughly constant. Used by the
// echo's colour shift.
Float4 rotate_hue(Float4 c, float degrees);

// Scale saturation about the pixel's luminance.
Float4 adjust_saturation(Float4 c, float saturation);

// Apply a colour tint, blended by `amount` in [0,1].
Float4 apply_tint(Float4 c, Float4 tint, float amount);

// Map a working-space value to a perceptual (sRGB-encoded) value, for effects
// whose parameters are tonal -- thresholds, halftone levels, sort keys.
//
// The point is that "Threshold = 1.0" should mean "white" whether the project
// is 8-bit (where AE hands us gamma-encoded values) or 32-bit float (where it
// hands us linear values). Judging thresholds in the working space would make
// the same parameter value behave completely differently between the two.
// Only the *decision* is made perceptually; colour arithmetic stays in the
// working space, so adding glow light stays physically correct.
inline float to_perceptual(float v, bool input_linear) {
    return input_linear ? linear_to_srgb(v) : v;
}

inline float from_perceptual(float v, bool input_linear) {
    return input_linear ? srgb_to_linear(v) : v;
}

// Build the normalised coordinate frame for an effect: maps a pixel to a
// centre-relative coordinate in units where the layer's half-diagonal is 1.
struct RadialFrame {
    float cx = 0.0f;
    float cy = 0.0f;
    float inv_half_diag = 1.0f;

    // `center_norm` is the user's normalised centre: (0.5, 0.5) means the middle
    // of the layer, (0,0) and (1,1) mean its outer corners. The conversion to
    // pixel-index space therefore subtracts a half pixel -- without it the
    // default centre would sit half a pixel right and down of true centre.
    static RadialFrame make(int width, int height, Vec2 center_norm);
    float radius_at(float x, float y) const;
};

}  // namespace mgtk
