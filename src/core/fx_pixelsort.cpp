// =============================================================================
//  mgtk/fx_pixelsort.cpp -- Pixel Sort
//
//  The datamosh / glitch effect. Within each row (or column), runs of pixels
//  whose sort key falls inside a threshold window are sorted; everything else
//  is left alone. Tuning the window is what turns the look from "gently
//  rearranged" to "hard glitch".
//
//  Two details that separate a usable implementation from a naive one:
//
//   * Randomness perturbs each key by a fraction of *that run's* own key
//     range, not by an absolute amount. An absolute perturbation would flatten
//     low-contrast runs completely while doing nothing to high-contrast ones.
//
//   * Sorting is done on (key, pixel) pairs copied out of the line, so no
//     pixel's key can change under the sort's feet. Sorting indices with a
//     comparator that re-reads the buffer is a classic source of subtle bugs
//     when the buffer is also the destination.
// =============================================================================
#include <algorithm>
#include <cmath>

#include "mgtk/effects.hpp"

namespace mgtk {

namespace {

struct SortEntry {
    float key = 0.0f;
    Float4 px;
    bool in_run = false;
};

// Hue in [0,1). Returns 0 for greys, which keeps achromatic pixels grouped at
// the start of an ascending sort instead of scattering them.
inline float hue_of(const Float4& c) {
    const float mx = max3(c.r, c.g, c.b);
    const float mn = min3(c.r, c.g, c.b);
    const float chroma = mx - mn;
    if (chroma < kEpsilon) return 0.0f;

    float h;
    if (mx == c.r) {
        h = (c.g - c.b) / chroma;
    } else if (mx == c.g) {
        h = (c.b - c.r) / chroma + 2.0f;
    } else {
        h = (c.r - c.g) / chroma + 4.0f;
    }
    h /= 6.0f;
    if (h < 0.0f) h += 1.0f;
    return h;
}

// HSV saturation: 0 for black, 1 for a fully saturated colour.
inline float saturation_of(const Float4& c) {
    const float mx = max3(c.r, c.g, c.b);
    if (mx < kEpsilon) return 0.0f;
    const float mn = min3(c.r, c.g, c.b);
    return (mx - mn) / mx;
}

inline float key_of(SortKey key, const Float4& c, bool linear) {
    switch (key) {
        case SortKey::Brightness:
            return to_perceptual(luma(c.r, c.g, c.b), linear);
        case SortKey::Hue:
            return hue_of(c);
        case SortKey::Saturation:
            return saturation_of(c);
        case SortKey::Red:
            return to_perceptual(c.r, linear);
        case SortKey::Green:
            return to_perceptual(c.g, linear);
        case SortKey::Blue:
            return to_perceptual(c.b, linear);
        case SortKey::Random:
            // The per-position perturbation below provides the entropy; the
            // base key is a constant so that runs of equal keys are shuffled
            // rather than ordered.
            return 0.0f;
        case SortKey::Count:
        default:
            return to_perceptual(luma(c.r, c.g, c.b), linear);
    }
}

// Sorts one run of entries in place and writes the result back into the line.
void process_run(std::vector<SortEntry>& line, std::size_t start, std::size_t end,
                 const PixelSortParams& p, bool descending, uint32_t line_index) {
    if (end <= start + 1) return;

    // -----------------------------------------------------------------------
    //  Key perturbation
    // -----------------------------------------------------------------------
    if (p.randomness > kEpsilon) {
        // Find the run's own key range so the perturbation is proportional.
        float lo = line[start].key;
        float hi = line[start].key;
        for (std::size_t i = start + 1; i < end; ++i) {
            lo = min2(lo, line[i].key);
            hi = max2(hi, line[i].key);
        }
        const float span = std::max(hi - lo, 1.0e-4f);
        const float jitter = p.randomness * span * 0.5f;

        // Deterministic per (line, position): the same frame always sorts the
        // same way, which is what keeps AE's disk cache and multi-frame
        // rendering consistent with a fresh render.
        for (std::size_t i = start; i < end; ++i) {
            const int32_t l = static_cast<int32_t>(line_index);
            const int32_t pos = static_cast<int32_t>(i);
            line[i].key += rand_signed_2i(l, pos) * jitter;
        }
    }

    // -----------------------------------------------------------------------
    //  Sort. std::stable_sort keeps equal keys in their original order, which
    //  avoids large flat areas swapping their pixels around for no reason.
    // -----------------------------------------------------------------------
    std::stable_sort(line.begin() + static_cast<std::ptrdiff_t>(start),
                     line.begin() + static_cast<std::ptrdiff_t>(end),
                     [descending](const SortEntry& a, const SortEntry& b) {
                         return descending ? (a.key > b.key) : (a.key < b.key);
                     });

    // -----------------------------------------------------------------------
    //  Stretch: pull every pixel in the run towards the run's leading value.
    //  At 1.0 the run becomes a flat bar, which is the heavy "smear" look.
    // -----------------------------------------------------------------------
    if (p.stretch > kEpsilon) {
        const Float4 lead = line[start].px;
        const float t = saturate(p.stretch);
        for (std::size_t i = start; i < end; ++i) {
            line[i].px = lerp(line[i].px, lead, t);
        }
    }
}

}  // namespace

void apply_pixel_sort(const Image& src, Image& dst, const PixelSortParams& p,
                      const RenderContext& ctx) {
    const int w = src.width();
    const int h = src.height();
    if (w <= 0 || h <= 0) {
        dst.resize(0, 0);
        return;
    }

    // Work on a copy so that anything outside a sorted run passes through
    // untouched, including all alpha values.
    if (dst.width() != w || dst.height() != h) {
        dst = src;
    } else {
        for (int y = 0; y < h; ++y) {
            std::copy(src.row(y), src.row(y) + w, dst.row(y));
        }
    }

    const bool linear = ctx.input_linear;
    const bool descending = (p.order == SortOrder::Descending);
    const float lo = std::min(p.threshold_low, p.threshold_high);
    const float hi = std::max(p.threshold_low, p.threshold_high);
    const int max_length = std::max(1, p.max_length);
    const bool axis_horizontal = (p.axis == SortAxis::Horizontal);
    const float mix = saturate(p.mix);
    const bool identity_mix = (mix >= 1.0f);

    const int line_count = axis_horizontal ? h : w;
    const int line_length = axis_horizontal ? w : h;

    std::vector<SortEntry> line(static_cast<std::size_t>(line_length));

    for (int li = 0; li < line_count; ++li) {
        if ((li & 15) == 0 && ctx.aborted()) return;

        // -------------------------------------------------------------------
        //  Gather the line
        // -------------------------------------------------------------------
        for (int i = 0; i < line_length; ++i) {
            const int x = axis_horizontal ? i : li;
            const int y = axis_horizontal ? li : i;
            const Float4 px = dst.at(x, y);

            SortEntry e;
            e.px = px;
            e.key = key_of(p.key, px, linear);

            // A pixel joins a run when its key is inside the window and, if
            // asked, when it is opaque. Excluding soft mattes stops the effect
            // from dragging colour out of feathered edges.
            const bool alpha_ok = !p.use_alpha_mask || (px.a > 0.5f);
            e.in_run = alpha_ok && (e.key >= lo) && (e.key <= hi);
            line[static_cast<std::size_t>(i)] = e;
        }

        // -------------------------------------------------------------------
        //  Find and process runs, subject to max_length
        // -------------------------------------------------------------------
        int run_start = -1;
        for (int i = 0; i <= line_length; ++i) {
            const bool in_run =
                (i < line_length) && line[static_cast<std::size_t>(i)].in_run;

            if (!in_run) {
                // End of a run (or the end of the line).
                if (run_start >= 0) {
                    process_run(line, static_cast<std::size_t>(run_start),
                                static_cast<std::size_t>(i), p, descending,
                                static_cast<uint32_t>(li));
                    run_start = -1;
                }
                continue;
            }

            if (run_start < 0) {
                run_start = i;
                continue;
            }

            if ((i - run_start) >= max_length) {
                // The run hit its length cap: process the block we have and
                // start the next one *at this pixel*. Consuming the pixel that
                // triggered the cap -- rather than restarting one past it --
                // is what keeps every pixel in a capped row inside some block;
                // dropping it leaves an unsorted speck at the head of every
                // block, which is exactly the sort of one-pixel artefact that
                // survives review and shows up on a client's 4K display.
                // This is what gives the effect its characteristic "sorted
                // blocks" rather than full rows.
                process_run(line, static_cast<std::size_t>(run_start),
                            static_cast<std::size_t>(i), p, descending,
                            static_cast<uint32_t>(li));
                run_start = i;
            }
        }

        // -------------------------------------------------------------------
        //  Write the line back, honouring the mix amount
        // -------------------------------------------------------------------
        for (int i = 0; i < line_length; ++i) {
            const int x = axis_horizontal ? i : li;
            const int y = axis_horizontal ? li : i;

            if (identity_mix) {
                dst.at(x, y) = line[static_cast<std::size_t>(i)].px;
            } else {
                dst.at(x, y) = lerp(src.at(x, y), line[static_cast<std::size_t>(i)].px, mix);
            }
        }
    }

    ctx.report(1.0f);
}

}  // namespace mgtk
