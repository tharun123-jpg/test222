// =============================================================================
//  mgtk/image.cpp -- sampling, alpha handling and rectangle helpers
// =============================================================================
#include "mgtk/image.hpp"

namespace mgtk {

namespace {

// Resolve one axis into a valid index. Returns false if the coordinate falls
// outside a transparent-mode image.
inline bool resolve_axis(int& v, int extent, WrapMode mode, bool& outside) {
    if (extent <= 0) {
        outside = true;
        return false;
    }
    if (v >= 0 && v < extent) return true;

    switch (mode) {
        case WrapMode::Clamp:
            v = clamp(v, 0, extent - 1);
            return true;
        case WrapMode::Repeat:
            v = imod(v, extent);
            return true;
        case WrapMode::Mirror: {
            const int period = 2 * extent;
            int m = imod(v, period);
            if (m >= extent) m = period - 1 - m;
            v = m;
            return true;
        }
        case WrapMode::Transparent:
        case WrapMode::Count:
        default:
            outside = true;
            return false;
    }
}

}  // namespace

bool ImageView::resolve(int& x, int& y, WrapMode mode) const {
    bool outside = false;
    const bool okx = resolve_axis(x, width_, mode, outside);
    if (!okx) return false;
    const bool oky = resolve_axis(y, height_, mode, outside);
    if (!oky) return false;
    return !outside;
}

Float4 ImageView::get(int x, int y, WrapMode mode) const {
    if (empty()) return Float4{};
    if (!resolve(x, y, mode)) return Float4{};  // transparent outside
    return at(x, y);
}

Float4 ImageView::sample_bilinear(float x, float y, WrapMode mode) const {
    if (empty()) return Float4{};

    // Coordinates are pixel indices: pixel (i,j) *is* the point (i,j), so an
    // integer coordinate lands exactly on a pixel centre with no shift. The
    // "half texel" offset belongs at the boundary with the host, not here.
    const float fx = x;
    const float fy = y;

    const float x0f = std::floor(fx);
    const float y0f = std::floor(fy);
    const float tx = fx - x0f;
    const float ty = fy - y0f;

    int x0 = static_cast<int>(x0f);
    int y0 = static_cast<int>(y0f);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    if (mode == WrapMode::Clamp) {
        // Fast path: clamp indices once, no per-tap branch.
        const int cx0 = clamp(x0, 0, width_ - 1);
        const int cx1 = clamp(x1, 0, width_ - 1);
        const int cy0 = clamp(y0, 0, height_ - 1);
        const int cy1 = clamp(y1, 0, height_ - 1);

        const Float4 p00 = at(cx0, cy0);
        const Float4 p10 = at(cx1, cy0);
        const Float4 p01 = at(cx0, cy1);
        const Float4 p11 = at(cx1, cy1);

        const Float4 top = lerp(p00, p10, tx);
        const Float4 bot = lerp(p01, p11, tx);
        return lerp(top, bot, ty);
    }

    // General path. Each of the four taps is resolved independently, because
    // WrapMode::Transparent is allowed to *reject* a tap: it leaves the index
    // untouched and reports failure, so a tap outside the frame must not be
    // read. Accumulating weights rather than assuming all four contribute is
    // what keeps this in bounds and gives transparent-outside-edges its
    // characteristic soft falloff at the border.
    const float wx[2] = {1.0f - tx, tx};
    const float wy[2] = {1.0f - ty, ty};
    const int xs[2] = {x0, x1};
    const int ys[2] = {y0, y1};

    float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            int sx = xs[i];
            int sy = ys[j];
            bool outside = false;
            if (!resolve_axis(sx, width_, mode, outside)) continue;
            if (!resolve_axis(sy, height_, mode, outside)) continue;

            const Float4 p = at(sx, sy);
            const float wgt = wx[i] * wy[j];
            acc[0] += p.r * wgt;
            acc[1] += p.g * wgt;
            acc[2] += p.b * wgt;
            acc[3] += p.a * wgt;
        }
    }
    return Float4{acc[0], acc[1], acc[2], acc[3]};
}

namespace {

// Catmull-Rom basis weights (a = -0.5).
inline void catmull_rom_weights(float t, float w[4]) {
    const float t2 = t * t;
    const float t3 = t2 * t;
    w[0] = 0.5f * (-t3 + 2.0f * t2 - t);
    w[1] = 0.5f * (3.0f * t3 - 5.0f * t2 + 2.0f);
    w[2] = 0.5f * (-3.0f * t3 + 4.0f * t2 + t);
    w[3] = 0.5f * (t3 - t2);
}

}  // namespace

Float4 ImageView::sample_bicubic(float x, float y, WrapMode mode) const {
    if (empty()) return Float4{};

    const float fx = x;
    const float fy = y;

    const int ix = static_cast<int>(std::floor(fx));
    const int iy = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(ix);
    const float ty = fy - static_cast<float>(iy);

    float wx[4];
    float wy[4];
    catmull_rom_weights(tx, wx);
    catmull_rom_weights(ty, wy);

    float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    for (int j = 0; j < 4; ++j) {
        const int sy = iy - 1 + j;
        float rowacc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (int i = 0; i < 4; ++i) {
            const int sx = ix - 1 + i;
            int cx = sx;
            int cy = sy;
            if (!resolve(cx, cy, mode)) continue;  // transparent mode: skip tap
            const Float4 p = at(cx, cy);
            const float w = wx[i];
            rowacc[0] += p.r * w;
            rowacc[1] += p.g * w;
            rowacc[2] += p.b * w;
            rowacc[3] += p.a * w;
        }
        out[0] += rowacc[0] * wy[j];
        out[1] += rowacc[1] * wy[j];
        out[2] += rowacc[2] * wy[j];
        out[3] += rowacc[3] * wy[j];
    }

    return Float4{out[0], out[1], out[2], out[3]};
}

// ---------------------------------------------------------------------------
//  Alpha handling
// ---------------------------------------------------------------------------
void unpremultiply(ImageView img) {
    if (img.empty()) return;
    for (int y = 0; y < img.height(); ++y) {
        Float4* p = img.row(y);
        for (int x = 0; x < img.width(); ++x) {
            const float a = p[x].a;
            if (a > kEpsilon && a < 1.0f - kEpsilon) {
                const float inv = 1.0f / a;
                p[x].r *= inv;
                p[x].g *= inv;
                p[x].b *= inv;
            } else if (a <= kEpsilon) {
                // Fully transparent pixels carry no meaningful colour. Leaving
                // them non-zero would leak colour into blurs and glows.
                p[x].r = 0.0f;
                p[x].g = 0.0f;
                p[x].b = 0.0f;
            }
        }
    }
}

void premultiply(ImageView img) {
    if (img.empty()) return;
    for (int y = 0; y < img.height(); ++y) {
        Float4* p = img.row(y);
        for (int x = 0; x < img.width(); ++x) {
            const float a = p[x].a;
            p[x].r *= a;
            p[x].g *= a;
            p[x].b *= a;
        }
    }
}

// ---------------------------------------------------------------------------
//  Rectangles
// ---------------------------------------------------------------------------
Rect intersect(const Rect& a, const Rect& b) {
    Rect r;
    r.x0 = max2(a.x0, b.x0);
    r.y0 = max2(a.y0, b.y0);
    r.x1 = min2(a.x1, b.x1);
    r.y1 = min2(a.y1, b.y1);
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    return r;
}

Rect expand_and_clip(const Rect& r, int amount, int width, int height) {
    if (amount < 0) amount = 0;
    Rect out;
    out.x0 = max2(0, r.x0 - amount);
    out.y0 = max2(0, r.y0 - amount);
    out.x1 = min2(width, r.x1 + amount);
    out.y1 = min2(height, r.y1 + amount);
    if (out.x1 < out.x0) out.x1 = out.x0;
    if (out.y1 < out.y0) out.y1 = out.y0;
    return out;
}

}  // namespace mgtk
