// =============================================================================
//  mgtk/blur.cpp
// =============================================================================
#include "mgtk/blur.hpp"

namespace mgtk {

namespace {

// Integer radius that a fractional radius decomposes into: the kernel is
// [r_int full taps] plus possibly one partial tap at each end.
struct RadiusSplit {
    int whole = 0;
    float frac = 0.0f;
};

inline RadiusSplit split_radius(float radius) {
    RadiusSplit s;
    if (!(radius > 0.0f)) return s;
    s.whole = static_cast<int>(std::floor(radius));
    s.frac = radius - static_cast<float>(s.whole);
    return s;
}

// Accumulate the wrapped index for a given axis position.
inline int wrap_index(int v, int extent, WrapMode mode) {
    if (extent <= 0) return 0;
    if (v >= 0 && v < extent) return v;
    switch (mode) {
        case WrapMode::Repeat:
            return imod(v, extent);
        case WrapMode::Mirror: {
            const int period = 2 * extent;
            int m = imod(v, period);
            if (m >= extent) m = period - 1 - m;
            return m;
        }
        case WrapMode::Clamp:
        case WrapMode::Transparent:
        case WrapMode::Count:
        default:
            return clamp(v, 0, extent - 1);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
//  Horizontal box blur
// ---------------------------------------------------------------------------
void box_blur_h(ImageView src, ImageView dst, float radius, WrapMode mode) {
    if (src.empty() || dst.empty()) return;

    const int w = src.width();
    const int h = std::min(src.height(), dst.height());
    const RadiusSplit rs = split_radius(radius);
    const int r = rs.whole;

    if (r == 0 && rs.frac <= 0.0f) {
        // Nothing to do beyond an optional copy.
        for (int y = 0; y < h; ++y) {
            const Float4* s = src.row(y);
            Float4* d = dst.row(y);
            for (int x = 0; x < w; ++x) d[x] = s[x];
        }
        return;
    }

    // Kernel normalisation. The window holds (2r+1) full taps plus, when the
    // radius is fractional, two partial taps at the ends contributing `frac`
    // each -- so the denominator is (2r+1) + 2*frac, NOT (2r+1) + 2. Getting
    // this wrong darkens or brightens the image by a constant factor, which is
    // exactly the kind of bug that survives a visual review.
    const float kernel_weight = static_cast<float>(2 * r + 1) + 2.0f * rs.frac;
    const float inv_count = 1.0f / kernel_weight;

    for (int y = 0; y < h; ++y) {
        const Float4* s = src.row(y);
        Float4* d = dst.row(y);

        // `sum` tracks only the full-weight core of the window, [x-r, x+r].
        // The two fractional end taps are added fresh for each output rather
        // than being folded into the running sum.
        //
        // Folding them in was the obvious thing to do and it is wrong: as the
        // window slides one pixel, the tap that leaves the core becomes a
        // fractional tap and the tap that enters the core stops being one, so
        // the real weight change is (frac-1) and (1-frac), not -1 and +1.
        // Treating it as the latter leaks energy, which shows up as a blur that
        // silently brightens the image.
        Float4 sum{};
        for (int i = -r; i <= r; ++i) {
            sum += s[wrap_index(i, w, mode)];
        }

        for (int x = 0; x < w; ++x) {
            Float4 v = sum;
            if (rs.frac > 0.0f) {
                const Float4 lo = s[wrap_index(x - r - 1, w, mode)];
                const Float4 hi = s[wrap_index(x + r + 1, w, mode)];
                v += (lo + hi) * rs.frac;
            }
            d[x] = v * inv_count;

            // Slide the core one pixel to the right.
            sum += s[wrap_index(x + r + 1, w, mode)] - s[wrap_index(x - r, w, mode)];
        }
    }
}

// ---------------------------------------------------------------------------
//  Vertical box blur
// ---------------------------------------------------------------------------
void box_blur_v(ImageView src, ImageView dst, float radius, WrapMode mode) {
    if (src.empty() || dst.empty()) return;

    const int w = std::min(src.width(), dst.width());
    const int h = src.height();
    const RadiusSplit rs = split_radius(radius);
    const int r = rs.whole;

    if (r == 0 && rs.frac <= 0.0f) {
        for (int y = 0; y < h; ++y) {
            const Float4* s = src.row(y);
            Float4* d = dst.row(y);
            for (int x = 0; x < w; ++x) d[x] = s[x];
        }
        return;
    }

    // Kernel normalisation. The window holds (2r+1) full taps plus, when the
    // radius is fractional, two partial taps at the ends contributing `frac`
    // each -- so the denominator is (2r+1) + 2*frac, NOT (2r+1) + 2. Getting
    // this wrong darkens or brightens the image by a constant factor, which is
    // exactly the kind of bug that survives a visual review.
    const float kernel_weight = static_cast<float>(2 * r + 1) + 2.0f * rs.frac;
    const float inv_count = 1.0f / kernel_weight;

    for (int x = 0; x < w; ++x) {
        // Same structure as the horizontal pass: `sum` is the core window and
        // the fractional taps are added per output. See the comment there.
        Float4 sum{};
        for (int i = -r; i <= r; ++i) {
            sum += src.at(x, wrap_index(i, h, mode));
        }

        for (int y = 0; y < h; ++y) {
            Float4 v = sum;
            if (rs.frac > 0.0f) {
                v += (src.at(x, wrap_index(y - r - 1, h, mode)) +
                      src.at(x, wrap_index(y + r + 1, h, mode))) * rs.frac;
            }
            dst.at(x, y) = v * inv_count;

            sum += src.at(x, wrap_index(y + r + 1, h, mode)) -
                   src.at(x, wrap_index(y - r, h, mode));
        }
    }
}

// ---------------------------------------------------------------------------
//  Gaussian via three box passes
//
//  Box widths are chosen so that the composite variance matches the requested
//  sigma (Kutskir's derivation). For sigma <= 0 we do nothing at all, which
//  keeps a zeroed radius parameter genuinely free.
// ---------------------------------------------------------------------------
void gaussian_blur(Image& img, float sigma_x, float sigma_y,
                   WrapMode mode, Image* scratch) {
    if (img.empty()) return;

    Image local;
    Image& tmp = (scratch != nullptr) ? *scratch : local;
    tmp.resize(img.width(), img.height());

    const auto run_axis = [&](bool horizontal, float sigma) {
        if (sigma <= 0.0f) return;

        // Three boxes whose combined variance equals sigma^2.
        constexpr int n = 3;
        const float w_ideal = std::sqrt((12.0f * sigma * sigma / n) + 1.0f);
        int wl = static_cast<int>(std::floor(w_ideal));
        if (wl % 2 == 0) --wl;
        if (wl < 1) wl = 1;
        const int wu = wl + 2;

        const float m_ideal = (12.0f * sigma * sigma - static_cast<float>(n * wl * wl) -
                               4.0f * static_cast<float>(n * wl) - 3.0f * n) /
                              (-4.0f * static_cast<float>(wl) - 4.0f);
        const int m = clamp(static_cast<int>(std::round(m_ideal)), 0, n);

        const int widths[n] = {m > 0 ? wl : wu, m > 1 ? wl : wu, m > 2 ? wl : wu};

        for (int pass = 0; pass < n; ++pass) {
            const float radius = static_cast<float>(widths[pass] - 1) * 0.5f;
            if (radius <= 0.0f) continue;

            if (horizontal) {
                box_blur_h(img.view(), tmp.view(), radius, mode);
            } else {
                box_blur_v(img.view(), tmp.view(), radius, mode);
            }
            img.swap(tmp);
        }
    };

    run_axis(true, sigma_x);
    run_axis(false, sigma_y);
}

// ---------------------------------------------------------------------------
//  Directional blur
// ---------------------------------------------------------------------------
void directional_blur(Image& img, float angle_deg, float length,
                      int samples, WrapMode mode, Image* scratch) {
    if (img.empty() || length <= 0.0f || samples < 2) return;

    samples = clamp(samples, 2, 128);

    Image local;
    Image& tmp = (scratch != nullptr) ? *scratch : local;
    tmp.resize(img.width(), img.height());

    const float radians = angle_deg * kDegToRad;
    const float dx = std::cos(radians);
    const float dy = std::sin(radians);
    const float half = length * 0.5f;
    const float inv_samples = 1.0f / static_cast<float>(samples);

    // Read from the source, write the averaged result into tmp.
    for (int y = 0; y < img.height(); ++y) {
        Float4* out = tmp.row(y);
        for (int x = 0; x < img.width(); ++x) {
            Float4 acc{};
            for (int s = 0; s < samples; ++s) {
                const float t = (static_cast<float>(s) + 0.5f) * inv_samples;
                const float offset = t * 2.0f * half - half;
                acc += img.view().sample_bilinear(
                    static_cast<float>(x) + dx * offset,
                    static_cast<float>(y) + dy * offset, mode);
            }
            out[x] = acc * inv_samples;
        }
    }
    img.swap(tmp);
}

// ---------------------------------------------------------------------------
//  Mip helpers
// ---------------------------------------------------------------------------
void downsample_box(const Image& src, Image& dst) {
    const int sw = src.width();
    const int sh = src.height();
    if (sw <= 0 || sh <= 0) {
        dst.resize(0, 0);
        return;
    }

    // Round up so that odd dimensions do not lose the last row/column.
    const int dw = std::max(1, (sw + 1) / 2);
    const int dh = std::max(1, (sh + 1) / 2);
    dst.resize(dw, dh);

    for (int y = 0; y < dh; ++y) {
        Float4* d = dst.row(y);
        const int sy0 = y * 2;
        const int sy1 = std::min(sy0 + 1, sh - 1);
        const Float4* r0 = src.row(sy0);
        const Float4* r1 = src.row(sy1);

        for (int x = 0; x < dw; ++x) {
            const int sx0 = x * 2;
            const int sx1 = std::min(sx0 + 1, sw - 1);
            d[x] = (r0[sx0] + r0[sx1] + r1[sx0] + r1[sx1]) * 0.25f;
        }
    }
}

void upsample_bilinear(const Image& src, Image& dst, float weight) {
    const int dw = dst.width();
    const int dh = dst.height();
    const int sw = src.width();
    const int sh = src.height();
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;

    // Map destination pixel centres onto source pixel centres. Using
    // (d + 0.5) * ratio - 0.5 rather than a naive scale keeps the magnified
    // image centred, which matters when the mip chain is offset.
    const float rx = static_cast<float>(sw) / static_cast<float>(dw);
    const float ry = static_cast<float>(sh) / static_cast<float>(dh);

    for (int y = 0; y < dh; ++y) {
        Float4* d = dst.row(y);
        const float sy = (static_cast<float>(y) + 0.5f) * ry - 0.5f;
        const int y0 = static_cast<int>(std::floor(sy));
        const float ty = sy - static_cast<float>(y0);
        const int cy0 = clamp(y0, 0, sh - 1);
        const int cy1 = clamp(y0 + 1, 0, sh - 1);
        const Float4* r0 = src.row(cy0);
        const Float4* r1 = src.row(cy1);

        for (int x = 0; x < dw; ++x) {
            const float sx = (static_cast<float>(x) + 0.5f) * rx - 0.5f;
            const int x0 = static_cast<int>(std::floor(sx));
            const float tx = sx - static_cast<float>(x0);
            const int cx0 = clamp(x0, 0, sw - 1);
            const int cx1 = clamp(x0 + 1, 0, sw - 1);

            const Float4 top = lerp(r0[cx0], r0[cx1], tx);
            const Float4 bot = lerp(r1[cx0], r1[cx1], tx);
            const Float4 v = lerp(top, bot, ty);
            d[x] += v * weight;
        }
    }
}

void resize_bilinear(const Image& src, Image& dst, int new_width, int new_height,
                     WrapMode mode) {
    if (new_width <= 0 || new_height <= 0) {
        dst.resize(0, 0);
        return;
    }
    if (src.empty()) {
        dst.assign(new_width, new_height, Float4{});
        return;
    }

    const float rx = static_cast<float>(src.width()) / static_cast<float>(new_width);
    const float ry = static_cast<float>(src.height()) / static_cast<float>(new_height);

    // Guard against aliasing when minifying: fall back to supersampling.
    const bool minifying = (rx > 1.5f) || (ry > 1.5f);

    Image tmp(new_width, new_height);
    const ImageView sv = src.view();

    for (int y = 0; y < new_height; ++y) {
        Float4* d = tmp.row(y);
        for (int x = 0; x < new_width; ++x) {
            // Centre-aligned mapping. Destination pixel x sits at index x, and
            // the source position of that pixel's centre is
            //     (x + 0.5) * ratio - 0.5
            // The two half-pixel terms are what make the mapping symmetric:
            // without them the result is top-left aligned, which still passes an
            // identity resize but shifts an upscale by a quarter of a pixel at
            // each end.
            const float sx_centre = (static_cast<float>(x) + 0.5f) * rx - 0.5f;
            const float sy_centre = (static_cast<float>(y) + 0.5f) * ry - 0.5f;

            if (!minifying) {
                d[x] = sv.sample_bilinear(sx_centre, sy_centre, mode);
            } else {
                // 2x2 supersample on the quarter points of the destination
                // pixel's source footprint. Sampling once per output pixel
                // while minifying would alias badly: fine detail would shimmer
                // as the buffer is resized.
                Float4 acc{};
                for (int j = 0; j < 2; ++j) {
                    for (int i = 0; i < 2; ++i) {
                        acc += sv.sample_bilinear(
                            sx_centre + (static_cast<float>(i) - 0.5f) * rx * 0.5f,
                            sy_centre + (static_cast<float>(j) - 0.5f) * ry * 0.5f,
                            mode);
                    }
                }
                d[x] = acc * 0.25f;
            }
        }
    }
    dst.swap(tmp);
}

}  // namespace mgtk
