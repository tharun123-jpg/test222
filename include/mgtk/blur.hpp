// =============================================================================
//  mgtk/blur.hpp -- separable blur primitives
//
//  Why box-blur triplets instead of a true Gaussian convolution?
//
//  A glow with a 200px radius on a 4K frame, done as a direct convolution, is
//  O(pixels * radius) -- roughly 8.3 billion multiply-adds per pass. Three box
//  passes give a visually indistinguishable result (the central limit theorem
//  is doing the work for us) at O(pixels), independent of radius. That is the
//  difference between a live preview and a coffee break.
// =============================================================================
#pragma once

#include "mgtk/image.hpp"

namespace mgtk {

// ---------------------------------------------------------------------------
//  Single-pass box blur along one axis.
//
//  `radius` may be fractional: the trailing sample is weighted by the
//  fractional part, which keeps a slowly animating radius from popping
//  between integer widths.
// ---------------------------------------------------------------------------
//  ImageView is passed by value: it is a four-word descriptor, and taking it by
//  value keeps `dst` mutable without an awkward non-const reference to a
//  temporary view.
void box_blur_h(ImageView src, ImageView dst, float radius, WrapMode mode);
void box_blur_v(ImageView src, ImageView dst, float radius, WrapMode mode);

// ---------------------------------------------------------------------------
//  Gaussian approximation via three box passes.
//
//  `scratch` is optional. Pass the same Image object back on repeated calls to
//  reuse the allocation; leave it null and the function allocates internally.
//  `sigma` <= 0 is a no-op.
// ---------------------------------------------------------------------------
void gaussian_blur(Image& img, float sigma_x, float sigma_y,
                   WrapMode mode = WrapMode::Clamp, Image* scratch = nullptr);

// Convenience overload -- same sigma on both axes.
inline void gaussian_blur(Image& img, float sigma,
                          WrapMode mode = WrapMode::Clamp, Image* scratch = nullptr) {
    gaussian_blur(img, sigma, sigma, mode, scratch);
}

// ---------------------------------------------------------------------------
//  Directional ("motion") blur, sampled along `length` pixels at `angle_deg`.
//  Used for the anamorphic streak on the glow effect and for the smear on
//  slit-scan.
// ---------------------------------------------------------------------------
void directional_blur(Image& img, float angle_deg, float length,
                      int samples = 12, WrapMode mode = WrapMode::Clamp,
                      Image* scratch = nullptr);

// ---------------------------------------------------------------------------
//  Multi-scale (mip) helpers used by the glow stack.
//
//  `downsample_box` halves the resolution with a 2x2 box filter (proper
//  anti-aliasing, unlike nearest sampling). `upsample_bilinear` scales back up
//  and adds into `dst` with `weight`, which is how the accumulated bloom is
//  composited without a separate accumulation buffer.
// ---------------------------------------------------------------------------
void downsample_box(const Image& src, Image& dst);
void upsample_bilinear(const Image& src, Image& dst, float weight);

// ---------------------------------------------------------------------------
//  General bilinear resize. Used for the preview-quality path and for
//  re-fitting buffers when AE hands us a different working resolution.
// ---------------------------------------------------------------------------
void resize_bilinear(const Image& src, Image& dst, int new_width, int new_height,
                     WrapMode mode = WrapMode::Clamp);

}  // namespace mgtk
