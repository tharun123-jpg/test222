// =============================================================================
//  mgtk/image.hpp -- the pixel container every effect operates on
//
//  Design notes
//  ------------
//  * Internally everything is 32-bit float, linear-ish RGBA, interleaved.
//    `Float4` is 16-byte aligned so the compiler emits vector loads/stores.
//  * `Image` owns its pixels; `ImageView` is a non-owning window used for
//    history buffers and sub-rect processing.
//  * When alpha is present, pixels are UNPREMULTIPLIED inside the core. The AE
//    glue layer handles the premultiply/unpremultiply round-trip, because AE
//    hands effects premultiplied pixels but almost every creative operation
//    wants straight alpha (otherwise blurs and displacements bleed black).
//  * All conversions to/from AE's 8- and 16-bit formats live in the glue layer
//    so this header stays buildable with nothing but a C++ compiler.
// =============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "mgtk/math.hpp"

namespace mgtk {

// ---------------------------------------------------------------------------
//  Pixel type
// ---------------------------------------------------------------------------
struct alignas(16) Float4 {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

inline Float4 make_rgba(float r, float g, float b, float a = 1.0f) {
    return Float4{r, g, b, a};
}

inline Float4 operator+(Float4 x, Float4 y) {
    return {x.r + y.r, x.g + y.g, x.b + y.b, x.a + y.a};
}
inline Float4 operator-(Float4 x, Float4 y) {
    return {x.r - y.r, x.g - y.g, x.b - y.b, x.a - y.a};
}
inline Float4 operator*(Float4 x, float s) {
    return {x.r * s, x.g * s, x.b * s, x.a * s};
}
inline Float4 operator*(float s, Float4 x) { return x * s; }
inline Float4 operator*(Float4 x, Float4 y) {
    return {x.r * y.r, x.g * y.g, x.b * y.b, x.a * y.a};
}
inline Float4 operator/(Float4 x, float s) {
    const float inv = (std::fabs(s) > kEpsilon) ? (1.0f / s) : 0.0f;
    return x * inv;
}
inline Float4& operator+=(Float4& x, Float4 y) {
    x.r += y.r; x.g += y.g; x.b += y.b; x.a += y.a;
    return x;
}
inline Float4& operator*=(Float4& x, float s) {
    x.r *= s; x.g *= s; x.b *= s; x.a *= s;
    return x;
}

inline Float4 lerp(Float4 a, Float4 b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t),
            lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

inline Float4 saturate4(Float4 v) {
    return {saturate(v.r), saturate(v.g), saturate(v.b), saturate(v.a)};
}

inline Float4 max4(Float4 a, Float4 b) {
    return {max2(a.r, b.r), max2(a.g, b.g), max2(a.b, b.b), max2(a.a, b.a)};
}

inline Float4 min4(Float4 a, Float4 b) {
    return {min2(a.r, b.r), min2(a.g, b.g), min2(a.b, b.b), min2(a.a, b.a)};
}

inline float luminance(Float4 v) { return luma(v.r, v.g, v.b); }

// ---------------------------------------------------------------------------
//  Boundary handling for sampling
// ---------------------------------------------------------------------------
enum class WrapMode : int {
    Clamp = 0,   // extend edge pixels  (AE's default behaviour)
    Repeat,      // tile
    Mirror,      // reflect
    Transparent, // black / zero alpha outside the frame
    Count
};

// ---------------------------------------------------------------------------
//  ImageView -- a non-owning, strided window over pixels
// ---------------------------------------------------------------------------
class ImageView {
public:
    ImageView() = default;

    ImageView(Float4* data, int width, int height, int stride = -1)
        : data_(data), width_(width), height_(height),
          stride_(stride < 0 ? width : stride) {}

    bool empty() const { return data_ == nullptr || width_ <= 0 || height_ <= 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }

    Float4* data() { return data_; }
    const Float4* data() const { return data_; }

    Float4* row(int y) {
        return data_ + static_cast<std::ptrdiff_t>(y) * stride_;
    }
    const Float4* row(int y) const {
        return data_ + static_cast<std::ptrdiff_t>(y) * stride_;
    }

    // Unchecked element access -- callers must have already clamped.
    Float4& at(int x, int y) {
        return data_[static_cast<std::ptrdiff_t>(y) * stride_ + x];
    }
    const Float4& at(int x, int y) const {
        return data_[static_cast<std::ptrdiff_t>(y) * stride_ + x];
    }

    // Range-checked access; out-of-bounds resolves according to `mode`.
    Float4 get(int x, int y, WrapMode mode = WrapMode::Clamp) const;

    // Bilinear sample in *pixel-index* coordinates: pixel (i,j) is the point
    // (i,j), so an integer coordinate returns that pixel exactly.
    //
    // The convention is worth stating because the alternative is just as
    // common. After Effects itself describes a layer point (i+0.5, j+0.5) as
    // the centre of pixel (i,j) -- an "edge at the integer" space. Everything
    // in this library works in index space instead, because the effects reason
    // about pixel offsets and stencil widths, and in index space a displacement
    // of `d` is exactly `d` pixels with no half-pixel term to forget. The glue
    // layer does the single conversion at the boundary with the host.
    Float4 sample_bilinear(float x, float y, WrapMode mode = WrapMode::Clamp) const;

    // Catmull-Rom bicubic sample, same convention. Sharper than bilinear for
    // rotations and free-form warps; used wherever the effect is transform-like.
    Float4 sample_bicubic(float x, float y, WrapMode mode = WrapMode::Clamp) const;

    void fill(Float4 c) {
        for (int y = 0; y < height_; ++y) {
            Float4* p = row(y);
            for (int x = 0; x < width_; ++x) p[x] = c;
        }
    }

private:
    // Resolve a single coordinate pair into bounds per `mode`, returning false
    // if the sample lies entirely outside a transparent-mode image.
    bool resolve(int& x, int& y, WrapMode mode) const;

    Float4* data_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    int stride_ = 0;
};

// ---------------------------------------------------------------------------
//  Image -- an owning, contiguous image buffer
// ---------------------------------------------------------------------------
class Image {
public:
    Image() = default;

    Image(int width, int height)
        : width_(width < 0 ? 0 : width), height_(height < 0 ? 0 : height) {
        pixels_.resize(static_cast<std::size_t>(width_) *
                       static_cast<std::size_t>(height_));
    }

    explicit Image(Float4 fill_value, int width, int height) : Image(width, height) {
        for (auto& p : pixels_) p = fill_value;
    }

    void resize(int width, int height) {
        width_ = width < 0 ? 0 : width;
        height_ = height < 0 ? 0 : height;
        // vector::resize rather than assign: growing value-initialises only the
        // new elements, and shrinking then growing cannot resurrect stale
        // pixels. Effects reuse scratch buffers on every frame, and an assign
        // here would mean re-zeroing the whole buffer each time for nothing.
        pixels_.resize(static_cast<std::size_t>(width_) *
                       static_cast<std::size_t>(height_));
    }

    void assign(int width, int height, Float4 fill_value) {
        resize(width, height);
        for (auto& p : pixels_) p = fill_value;
    }

    void clear() {
        // std::fill rather than memset: Float4 has default member initialisers,
        // so it is not trivially copyable and blanket-clearing it is undefined
        // behaviour (and the compiler is right to say so).
        std::fill(pixels_.begin(), pixels_.end(), Float4{});
    }

    bool empty() const { return width_ <= 0 || height_ <= 0; }
    int width() const { return width_; }
    int height() const { return height_; }
    std::size_t pixel_count() const { return pixels_.size(); }

    Float4* data() { return pixels_.data(); }
    const Float4* data() const { return pixels_.data(); }

    ImageView view() { return ImageView(pixels_.data(), width_, height_); }
    ImageView view() const {
        return ImageView(const_cast<Float4*>(pixels_.data()), width_, height_);
    }

    Float4* row(int y) { return pixels_.data() + static_cast<std::ptrdiff_t>(y) * width_; }
    const Float4* row(int y) const {
        return pixels_.data() + static_cast<std::ptrdiff_t>(y) * width_;
    }

    Float4& at(int x, int y) {
        return pixels_[static_cast<std::ptrdiff_t>(y) * width_ + x];
    }
    const Float4& at(int x, int y) const {
        return pixels_[static_cast<std::ptrdiff_t>(y) * width_ + x];
    }

    Float4 get(int x, int y, WrapMode mode = WrapMode::Clamp) const {
        return view().get(x, y, mode);
    }
    Float4 sample_bilinear(float x, float y, WrapMode mode = WrapMode::Clamp) const {
        return view().sample_bilinear(x, y, mode);
    }
    Float4 sample_bicubic(float x, float y, WrapMode mode = WrapMode::Clamp) const {
        return view().sample_bicubic(x, y, mode);
    }

    void fill(Float4 c) { view().fill(c); }

    // Swap contents with another image. Used for ping-pong buffers -- avoids
    // the allocation and copy of assigning a whole image.
    void swap(Image& other) noexcept {
        pixels_.swap(other.pixels_);
        std::swap(width_, other.width_);
        std::swap(height_, other.height_);
    }

private:
    std::vector<Float4> pixels_;
    int width_ = 0;
    int height_ = 0;
};

// ---------------------------------------------------------------------------
//  Alpha handling
//
//  AE gives effects premultiplied pixels by default. Almost every creative
//  operation in this toolkit wants straight alpha, so the glue converts on the
//  way in and back on the way out. These helpers are public because the unit
//  tests exercise them and because custom integrations may need them.
// ---------------------------------------------------------------------------
void unpremultiply(ImageView img);
void premultiply(ImageView img);

// ---------------------------------------------------------------------------
//  Region-of-interest helpers
// ---------------------------------------------------------------------------
struct Rect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;  // exclusive
    int y1 = 0;  // exclusive

    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

inline Rect full_rect(const Image& img) { return Rect{0, 0, img.width(), img.height()}; }

// Intersect two rectangles; returns an empty rect when they do not overlap.
Rect intersect(const Rect& a, const Rect& b);

// Grow a rectangle by `amount` pixels in every direction, then clip it to the
// bounds of an image. Used by the SmartFX pre-render pass to work out how much
// input an effect needs beyond the output it was asked for.
Rect expand_and_clip(const Rect& r, int amount, int width, int height);

}  // namespace mgtk
