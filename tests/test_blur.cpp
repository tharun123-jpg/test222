// =============================================================================
//  tests/test_blur.cpp -- blur primitives and mip helpers
// =============================================================================
#include <cmath>

#include "mgtk/blur.hpp"
#include "test_framework.hpp"

using namespace mgtk;

namespace {

Image constant_image(int w, int h, Float4 c) {
    Image img(w, h);
    img.fill(c);
    return img;
}

// A deterministic pseudo-random image. Uses the toolkit's own hash so that the
// tests do not depend on <random>'s implementation-defined output.
Image noise_image(int w, int h, uint32_t seed) {
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.at(x, y) = Float4{rand_2i(static_cast<int>(seed), x * 71 + y),
                                  rand_2i(static_cast<int>(seed) + 1, x * 31 + y * 17),
                                  rand_2i(static_cast<int>(seed) + 2, x + y * 5),
                                  1.0f};
        }
    }
    return img;
}

double channel_sum(const Image& img, int channel) {
    double total = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const Float4 p = img.at(x, y);
            total += (channel == 0) ? p.r : (channel == 1 ? p.g : p.b);
        }
    }
    return total;
}

double variance(const Image& img) {
    double mean = 0.0;
    const double n = static_cast<double>(img.pixel_count());
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) mean += img.at(x, y).r;
    }
    mean /= n;

    double acc = 0.0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const double d = img.at(x, y).r - mean;
            acc += d * d;
        }
    }
    return acc / n;
}

bool images_nearly_equal(const Image& a, const Image& b, float tol) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const Float4 p = a.at(x, y);
            const Float4 q = b.at(x, y);
            if (std::fabs(p.r - q.r) > tol) return false;
            if (std::fabs(p.g - q.g) > tol) return false;
            if (std::fabs(p.b - q.b) > tol) return false;
            if (std::fabs(p.a - q.a) > tol) return false;
        }
    }
    return true;
}

}  // namespace

MGTK_TEST(blur_box_preserves_a_constant_image) {
    // With edge clamping a uniform field must stay uniform: if the kernel is
    // not normalised, a constant image would drift brighter or darker.
    for (float radius : {1.0f, 2.5f, 7.0f, 0.3f}) {
        Image img = constant_image(32, 24, Float4{0.4f, 0.6f, 0.8f, 1.0f});
        Image tmp(32, 24);

        box_blur_h(img.view(), tmp.view(), radius, WrapMode::Clamp);
        img.swap(tmp);
        box_blur_v(img.view(), tmp.view(), radius, WrapMode::Clamp);
        img.swap(tmp);

        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                CHECK_NEAR(img.at(x, y).r, 0.4f, 1e-3f);
                CHECK_NEAR(img.at(x, y).g, 0.6f, 1e-3f);
                CHECK_NEAR(img.at(x, y).b, 0.8f, 1e-3f);
            }
        }
    }
}

MGTK_TEST(blur_box_with_zero_radius_is_a_copy) {
    const Image src = noise_image(16, 16, 7);
    Image dst(16, 16);

    box_blur_h(src.view(), dst.view(), 0.0f, WrapMode::Clamp);
    CHECK(images_nearly_equal(src, dst, 1e-6f));

    Image dst2(16, 16);
    box_blur_v(src.view(), dst2.view(), 0.0f, WrapMode::Clamp);
    CHECK(images_nearly_equal(src, dst2, 1e-6f));
}

MGTK_TEST(blur_box_conserves_energy_under_wrap) {
    // A wrapped box blur is a normalised circulant convolution, so the total of
    // every channel must be preserved exactly. This catches both a kernel
    // normalisation error and an off-by-one in the sliding window.
    const Image src = noise_image(24, 17, 11);
    const double src_sum = channel_sum(src, 0);

    for (float radius : {1.0f, 3.0f, 3.5f, 8.0f}) {
        Image img = src;
        Image tmp(24, 17);

        box_blur_h(img.view(), tmp.view(), radius, WrapMode::Repeat);
        img.swap(tmp);
        box_blur_v(img.view(), tmp.view(), radius, WrapMode::Repeat);
        img.swap(tmp);

        const double out_sum = channel_sum(img, 0);
        // Tolerance scales with the number of pixels: this is float accumulation
        // drift, not algorithmic error.
        const double tol = 1e-2 * static_cast<double>(img.pixel_count());
        CHECK_NEAR(out_sum, src_sum, tol);
    }
}

MGTK_TEST(blur_box_reduces_variance) {
    const Image src = noise_image(32, 32, 3);
    const double v0 = variance(src);

    Image img = src;
    Image tmp(32, 32);
    box_blur_h(img.view(), tmp.view(), 3.0f, WrapMode::Clamp);
    img.swap(tmp);
    box_blur_v(img.view(), tmp.view(), 3.0f, WrapMode::Clamp);
    img.swap(tmp);

    const double v1 = variance(img);
    CHECK_MSG(v1 < v0, "blur did not reduce variance");
    CHECK_FINITE(v1);
}

MGTK_TEST(blur_gaussian_preserves_constant) {
    for (float sigma : {0.5f, 2.0f, 9.0f, 25.0f}) {
        Image img = constant_image(40, 30, Float4{0.25f, 0.5f, 0.75f, 1.0f});
        gaussian_blur(img, sigma, WrapMode::Clamp);

        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                CHECK_NEAR(img.at(x, y).r, 0.25f, 2e-3f);
                CHECK_NEAR(img.at(x, y).g, 0.5f, 2e-3f);
                CHECK_NEAR(img.at(x, y).b, 0.75f, 2e-3f);
            }
        }
    }
}

MGTK_TEST(blur_gaussian_with_zero_sigma_changes_nothing) {
    const Image src = noise_image(20, 20, 5);
    Image img = src;
    gaussian_blur(img, 0.0f, WrapMode::Clamp);
    CHECK(images_nearly_equal(src, img, 0.0f));
}

MGTK_TEST(blur_gaussian_is_stronger_than_a_single_box) {
    // Three box passes approximate a Gaussian, so the result should be smoother
    // (lower variance) than one box pass of the same width.
    const Image src = noise_image(48, 48, 9);

    Image gauss = src;
    gaussian_blur(gauss, 3.0f, WrapMode::Clamp);

    Image single = src;
    Image tmp(48, 48);
    box_blur_h(single.view(), tmp.view(), 3.0f, WrapMode::Clamp);
    single.swap(tmp);
    box_blur_v(single.view(), tmp.view(), 3.0f, WrapMode::Clamp);
    single.swap(tmp);

    CHECK_MSG(variance(gauss) < variance(single),
              "the three-pass Gaussian should smooth more than one box pass");
}

MGTK_TEST(blur_gaussian_is_separable) {
    // Blurring X then Y in two separate calls must match one combined call.
    const Image src = noise_image(32, 24, 13);

    Image combined = src;
    gaussian_blur(combined, 4.0f, 2.0f, WrapMode::Clamp, nullptr);

    Image split = src;
    gaussian_blur(split, 4.0f, 0.0f, WrapMode::Clamp, nullptr);
    gaussian_blur(split, 0.0f, 2.0f, WrapMode::Clamp, nullptr);

    CHECK(images_nearly_equal(combined, split, 1e-4f));
}

MGTK_TEST(blur_gaussian_rejects_a_non_positive_sigma_gracefully) {
    const Image src = noise_image(8, 8, 1);
    Image img = src;

    gaussian_blur(img, -5.0f, WrapMode::Clamp);
    CHECK(images_nearly_equal(src, img, 1e-9f));

    gaussian_blur(img, 0.0f, -3.0f, WrapMode::Clamp, nullptr);
    CHECK(images_nearly_equal(src, img, 1e-9f));
}

MGTK_TEST(blur_directional_preserves_constant_and_energy) {
    Image img = constant_image(24, 24, Float4{0.5f, 0.5f, 0.5f, 1.0f});
    directional_blur(img, 30.0f, 12.0f, 16, WrapMode::Clamp);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            CHECK_NEAR(img.at(x, y).r, 0.5f, 1e-3f);
        }
    }

    // A zero-length streak is a no-op.
    const Image src = noise_image(20, 20, 4);
    Image same = src;
    directional_blur(same, 45.0f, 0.0f, 8, WrapMode::Clamp);
    CHECK(images_nearly_equal(src, same, 1e-9f));
}

MGTK_TEST(blur_directional_is_deterministic) {
    const Image src = noise_image(20, 20, 8);
    Image a = src;
    Image b = src;
    directional_blur(a, 37.0f, 15.0f, 12, WrapMode::Clamp);
    directional_blur(b, 37.0f, 15.0f, 12, WrapMode::Clamp);
    CHECK(images_nearly_equal(a, b, 0.0f));
}

MGTK_TEST(blur_downsample_halves_and_averages) {
    Image src = constant_image(9, 7, Float4{0.3f, 0.3f, 0.3f, 1.0f});
    Image dst;

    downsample_box(src, dst);

    // Odd dimensions round up rather than truncating the last row or column.
    CHECK_EQ(dst.width(), 5);
    CHECK_EQ(dst.height(), 4);

    for (int y = 0; y < dst.height(); ++y) {
        for (int x = 0; x < dst.width(); ++x) {
            CHECK_NEAR(dst.at(x, y).r, 0.3f, 1e-5f);
        }
    }

    Image zero;
    Image empty_src;
    downsample_box(empty_src, zero);
    CHECK(zero.empty());
}

MGTK_TEST(blur_upsample_accumulates_with_weight) {
    Image src = constant_image(2, 2, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    Image dst = constant_image(8, 8, Float4{0.0f, 0.0f, 0.0f, 1.0f});

    upsample_bilinear(src, dst, 0.5f);

    // Every destination pixel should have received 1.0 * 0.5.
    for (int y = 0; y < dst.height(); ++y) {
        for (int x = 0; x < dst.width(); ++x) {
            CHECK_NEAR(dst.at(x, y).r, 0.5f, 1e-4f);
        }
    }
}

MGTK_TEST(blur_resize_identity_and_content) {
    const Image src = noise_image(16, 16, 21);

    // Resizing to the same dimensions must be (nearly) lossless.
    Image same;
    resize_bilinear(src, same, 16, 16);
    CHECK_EQ(same.width(), 16);
    CHECK_EQ(same.height(), 16);
    CHECK(images_nearly_equal(src, same, 1e-4f));

    // A constant image survives any resize.
    Image flat = constant_image(8, 8, Float4{0.6f, 0.2f, 0.1f, 1.0f});
    Image bigger;
    resize_bilinear(flat, bigger, 40, 25);
    for (int y = 0; y < bigger.height(); ++y) {
        for (int x = 0; x < bigger.width(); ++x) {
            CHECK_NEAR(bigger.at(x, y).r, 0.6f, 5e-3f);
        }
    }

    // Degenerate target sizes must not crash or produce garbage.
    Image degenerate;
    resize_bilinear(src, degenerate, 0, 10);
    CHECK(degenerate.empty());

    Image from_empty;
    resize_bilinear(Image{}, from_empty, 4, 4);
    CHECK_EQ(from_empty.width(), 4);
    CHECK_NEAR(from_empty.at(0, 0).a, 0.0f, 0.0f);
}

MGTK_TEST(blur_handles_degenerate_sizes_without_crashing) {
    // Empty images in, empty images out: no division by zero, no reads past the
    // end of a zero-length buffer.
    Image empty;
    Image dst;
    box_blur_h(empty.view(), dst.view(), 4.0f, WrapMode::Clamp);
    box_blur_v(empty.view(), dst.view(), 4.0f, WrapMode::Clamp);
    gaussian_blur(empty, 3.0f, WrapMode::Clamp);
    directional_blur(empty, 0.0f, 50.0f, 8, WrapMode::Clamp);
    CHECK(empty.empty());

    // A single-pixel image is the smallest interesting case.
    Image one(1, 1);
    one.at(0, 0) = Float4{0.5f, 0.5f, 0.5f, 1.0f};
    gaussian_blur(one, 5.0f, WrapMode::Clamp);
    CHECK_NEAR(one.at(0, 0).r, 0.5f, 1e-4f);
    CHECK_FINITE(one.at(0, 0).r);

    // A one-row image: the vertical pass has nothing to average over.
    Image row(8, 1);
    row.fill(Float4{0.3f, 0.3f, 0.3f, 1.0f});
    gaussian_blur(row, 3.0f, WrapMode::Clamp);
    CHECK_NEAR(row.at(4, 0).r, 0.3f, 1e-4f);
}
