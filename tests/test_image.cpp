// =============================================================================
//  tests/test_image.cpp -- image container, sampling and alpha handling
// =============================================================================
#include <vector>

#include "mgtk/image.hpp"
#include "test_framework.hpp"

using namespace mgtk;

namespace {

// Build a small image where every pixel encodes its own coordinates, so that a
// sampling bug shows up as a wildly wrong value rather than as a small
// numerical difference.
Image make_coordinate_image(int w, int h) {
    Image img(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            img.at(x, y) = Float4{static_cast<float>(x),
                                  static_cast<float>(y),
                                  static_cast<float>(x + y), 1.0f};
        }
    }
    return img;
}

Image make_constant(int w, int h, Float4 c) {
    Image img(w, h);
    img.fill(c);
    return img;
}

}  // namespace

MGTK_TEST(image_dimensions_and_default_state) {
    Image empty;
    CHECK(empty.empty());
    CHECK_EQ(empty.width(), 0);
    CHECK_EQ(empty.height(), 0);

    Image img(7, 5);
    CHECK(!img.empty());
    CHECK_EQ(img.width(), 7);
    CHECK_EQ(img.height(), 5);
    CHECK_EQ(static_cast<int>(img.pixel_count()), 35);

    // Freshly allocated pixels must be transparent black, never uninitialised.
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            CHECK_NEAR(img.at(x, y).r, 0.0f, 0.0f);
            CHECK_NEAR(img.at(x, y).a, 0.0f, 0.0f);
        }
    }
}

// ImageView::get() indexes by integer pixel coordinate, so it is unaffected by
// the sampling convention -- but it is the other place where a wrap mode can go
// wrong, so all four are pinned here.
MGTK_TEST(image_get_with_clamp_wrap) {
    const Image img = make_coordinate_image(4, 3);
    const ImageView v = img.view();

    // In-bounds reads are exact.
    CHECK_NEAR(v.get(2, 1, WrapMode::Clamp).r, 2.0f, 0.0f);
    CHECK_NEAR(v.get(2, 1, WrapMode::Clamp).g, 1.0f, 0.0f);

    // Clamp: everything outside becomes the nearest edge pixel.
    CHECK_NEAR(v.get(-5, 0, WrapMode::Clamp).r, 0.0f, 0.0f);
    CHECK_NEAR(v.get(99, 0, WrapMode::Clamp).r, 3.0f, 0.0f);
    CHECK_NEAR(v.get(0, -5, WrapMode::Clamp).g, 0.0f, 0.0f);
    CHECK_NEAR(v.get(0, 99, WrapMode::Clamp).g, 2.0f, 0.0f);

    // Repeat: the coordinate wraps around.
    CHECK_NEAR(v.get(4, 0, WrapMode::Repeat).r, 0.0f, 0.0f);
    CHECK_NEAR(v.get(-1, 0, WrapMode::Repeat).r, 3.0f, 0.0f);
    CHECK_NEAR(v.get(0, 3, WrapMode::Repeat).g, 0.0f, 0.0f);

    // Mirror, integer-index flavour: this is OpenGL's GL_MIRRORED_REPEAT, the
    // standard for pixel access. Reflection happens about the pixel *centres*,
    // so the edge pixel appears twice in the reflected sequence:
    //   ... 3 2 1 0 | 0 1 2 3 | 3 2 1 0 ...
    // Hence 4 -> 3, 5 -> 2, 7 -> 0, and -1 -> 0.
    //
    // Note this deliberately differs from mgtk::mirror_coord(), which reflects
    // *continuous* coordinates about the edges and is therefore the right tool
    // for sampling positions rather than pixel indices. Mixing the two up is a
    // classic half-pixel bug, so both conventions are pinned here.
    CHECK_NEAR(v.get(4, 0, WrapMode::Mirror).r, 3.0f, 0.0f);
    CHECK_NEAR(v.get(5, 0, WrapMode::Mirror).r, 2.0f, 0.0f);
    CHECK_NEAR(v.get(7, 0, WrapMode::Mirror).r, 0.0f, 0.0f);
    CHECK_NEAR(v.get(-1, 0, WrapMode::Mirror).r, 0.0f, 0.0f);
    CHECK_NEAR(v.get(-2, 0, WrapMode::Mirror).r, 1.0f, 0.0f);

    // And the continuous-coordinate counterpart, for contrast.
    CHECK_NEAR(mirror_coord(4.0f, 4.0f), 4.0f, 1e-5f);
    CHECK_NEAR(mirror_coord(5.0f, 4.0f), 3.0f, 1e-5f);
    CHECK_NEAR(mirror_coord(-1.0f, 4.0f), 1.0f, 1e-5f);

    // Transparent: outside is empty, not an edge pixel.
    CHECK_NEAR(v.get(-1, 0, WrapMode::Transparent).a, 0.0f, 0.0f);
    CHECK_NEAR(v.get(999, 0, WrapMode::Transparent).a, 0.0f, 0.0f);
}

MGTK_TEST(image_bilinear_hits_pixel_centres_exactly) {
    const Image img = make_coordinate_image(8, 8);
    const ImageView v = img.view();

    // In pixel-index coordinates pixel (i,j) is the point (i,j), so sampling at
    // an integer must reproduce that pixel exactly. Getting the convention
    // wrong shows up here as a value half a pixel off.
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const Float4 s = v.sample_bilinear(static_cast<float>(x),
                                               static_cast<float>(y));
            CHECK_NEAR(s.r, static_cast<float>(x), 1e-4f);
            CHECK_NEAR(s.g, static_cast<float>(y), 1e-4f);
        }
    }
}

MGTK_TEST(image_bilinear_blends_between_neighbours) {
    Image img = make_constant(4, 4, Float4{0.0f, 0.0f, 0.0f, 1.0f});
    ImageView v = img.view();
    v.at(1, 1) = Float4{1.0f, 0.0f, 0.0f, 1.0f};
    v.at(2, 1) = Float4{0.0f, 1.0f, 0.0f, 1.0f};

    // Pixel centres are the integers, so 1.5 sits exactly halfway between
    // pixels 1 and 2.
    const Float4 mid = v.sample_bilinear(1.5f, 1.0f);
    CHECK_NEAR(mid.r, 0.5f, 1e-4f);
    CHECK_NEAR(mid.g, 0.5f, 1e-4f);

    // A quarter of the way from pixel 1 towards pixel 2.
    const Float4 quarter = v.sample_bilinear(1.25f, 1.0f);
    CHECK_NEAR(quarter.r, 0.75f, 1e-4f);
    CHECK_NEAR(quarter.g, 0.25f, 1e-4f);
}

MGTK_TEST(image_bicubic_hits_pixel_centres_exactly) {
    const Image img = make_coordinate_image(10, 10);
    const ImageView v = img.view();

    // Catmull-Rom is an interpolating spline: at the knots it must reproduce
    // the sample values exactly. A non-interpolating kernel here would mean the
    // warp effects subtly soften the whole image even at zero displacement.
    for (int y = 1; y < 9; ++y) {
        for (int x = 1; x < 9; ++x) {
            const Float4 s = v.sample_bicubic(static_cast<float>(x),
                                              static_cast<float>(y));
            CHECK_NEAR(s.r, static_cast<float>(x), 1e-3f);
            CHECK_NEAR(s.g, static_cast<float>(y), 1e-3f);
        }
    }
}

MGTK_TEST(image_sampling_never_produces_nan) {
    const Image img = make_coordinate_image(5, 5);
    const ImageView v = img.view();

    const float probes[] = {-1e6f, -100.0f, -0.5f, 0.0f, 2.5f, 4.5f, 100.0f, 1e6f};
    for (WrapMode mode : {WrapMode::Clamp, WrapMode::Repeat, WrapMode::Mirror,
                          WrapMode::Transparent}) {
        for (float px : probes) {
            for (float py : probes) {
                const Float4 bl = v.sample_bilinear(px, py, mode);
                CHECK_FINITE(bl.r);
                CHECK_FINITE(bl.g);
                CHECK_FINITE(bl.a);

                const Float4 bc = v.sample_bicubic(px, py, mode);
                CHECK_FINITE(bc.r);
                CHECK_FINITE(bc.g);
                CHECK_FINITE(bc.a);
            }
        }
    }
}

MGTK_TEST(image_unpremultiply_and_premultiply_round_trip) {
    Image img(2, 1);
    img.at(0, 0) = Float4{0.5f, 0.25f, 0.125f, 0.5f};   // premultiplied
    img.at(1, 0) = Float4{0.25f, 0.25f, 0.25f, 1.0f};

    const Image original = img;

    unpremultiply(img.view());
    // Straight colour: the 0.5 alpha pixel doubles.
    CHECK_NEAR(img.at(0, 0).r, 1.0f, 1e-5f);
    CHECK_NEAR(img.at(0, 0).g, 0.5f, 1e-5f);
    CHECK_NEAR(img.at(0, 0).a, 0.5f, 1e-5f);
    // The opaque pixel is untouched.
    CHECK_NEAR(img.at(1, 0).r, 0.25f, 1e-5f);

    premultiply(img.view());
    for (int x = 0; x < 2; ++x) {
        CHECK_NEAR(img.at(x, 0).r, original.at(x, 0).r, 1e-5f);
        CHECK_NEAR(img.at(x, 0).g, original.at(x, 0).g, 1e-5f);
        CHECK_NEAR(img.at(x, 0).a, original.at(x, 0).a, 1e-5f);
    }
}

MGTK_TEST(image_unpremultiply_clears_transparent_pixels) {
    // A fully transparent pixel still carrying colour is the classic source of
    // black fringes around a blurred matte.
    Image img(1, 1);
    img.at(0, 0) = Float4{0.9f, 0.1f, 0.4f, 0.0f};

    unpremultiply(img.view());

    CHECK_NEAR(img.at(0, 0).r, 0.0f, 0.0f);
    CHECK_NEAR(img.at(0, 0).g, 0.0f, 0.0f);
    CHECK_NEAR(img.at(0, 0).b, 0.0f, 0.0f);
    CHECK_NEAR(img.at(0, 0).a, 0.0f, 0.0f);
}

MGTK_TEST(image_swap_is_cheap_and_correct) {
    Image a = make_constant(2, 2, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    Image b = make_constant(3, 1, Float4{2.0f, 2.0f, 2.0f, 1.0f});

    const Float4* a_data = a.data();
    const Float4* b_data = b.data();

    a.swap(b);

    CHECK_EQ(a.width(), 3);
    CHECK_EQ(a.height(), 1);
    CHECK_EQ(b.width(), 2);
    CHECK_EQ(b.height(), 2);

    // The underlying buffers must have been exchanged, not copied.
    CHECK(a.data() == b_data);
    CHECK(b.data() == a_data);

    CHECK_NEAR(a.at(0, 0).r, 2.0f, 0.0f);
    CHECK_NEAR(b.at(0, 0).r, 1.0f, 0.0f);
}

MGTK_TEST(image_rect_intersect_and_expand) {
    const Rect a{0, 0, 10, 10};
    const Rect b{5, 5, 20, 20};

    const Rect i = intersect(a, b);
    CHECK_EQ(i.x0, 5);
    CHECK_EQ(i.y0, 5);
    CHECK_EQ(i.x1, 10);
    CHECK_EQ(i.y1, 10);

    // Disjoint rectangles produce an empty, well-formed rect rather than a
    // negative-width one.
    const Rect none = intersect(Rect{0, 0, 5, 5}, Rect{10, 10, 20, 20});
    CHECK(none.empty());
    CHECK(none.width() >= 0);
    CHECK(none.height() >= 0);

    // Expanding must clip to the image, never run off the edge.
    const Rect grown = expand_and_clip(Rect{2, 2, 8, 8}, 5, 10, 10);
    CHECK_EQ(grown.x0, 0);
    CHECK_EQ(grown.y0, 0);
    CHECK_EQ(grown.x1, 10);
    CHECK_EQ(grown.y1, 10);

    const Rect grown2 = expand_and_clip(Rect{4, 4, 6, 6}, 1, 10, 10);
    CHECK_EQ(grown2.x0, 3);
    CHECK_EQ(grown2.x1, 7);
}
