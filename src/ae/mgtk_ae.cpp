// =============================================================================
//  src/ae/mgtk_ae.cpp -- implementation of the After Effects bridge.
//
//  See mgtk_ae.hpp for the design notes. Everything in here is mechanical:
//  AE's parameter and pixel formats on one side, mgtk::Image on the other.
// =============================================================================
#include "mgtk_ae.hpp"

#include <cstring>

namespace mgtk {
namespace ae {

// ---------------------------------------------------------------------------
//  Suite scoping
// ---------------------------------------------------------------------------
SuiteScopeBase::~SuiteScopeBase() {
    if (suite_ != nullptr && in_data_ != nullptr) {
        // Release is best-effort: there is nothing useful to do with a failure
        // this late, and throwing out of a destructor inside AE's call stack
        // would be far worse than leaking a suite reference count.
        AEFX_ReleaseSuite(in_data_, name_, version_);
        suite_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
//  Param
// ---------------------------------------------------------------------------
Param::Param(PF_InData* in_data, PF_OutData* out_data, A_long index)
    : in_data_(in_data), out_data_(out_data) {
    AEFX_CLR_STRUCT(def_);
    if (in_data == nullptr) return;

    const PF_Err err = PF_CHECKOUT_PARAM(in_data, index, in_data->current_time,
                                         in_data->time_step, in_data->time_scale,
                                         &def_);
    ok_ = (err == PF_Err_NONE);
}

Param::~Param() {
    if (ok_ && in_data_ != nullptr) {
        PF_CHECKIN_PARAM(in_data_, &def_);
    }
}

float Param::slider() const {
    if (!ok_) return 0.0f;
    return static_cast<float>(def_.u.fs_d.value);
}

float Param::angle_degrees() const {
    if (!ok_) return 0.0f;
    // PF_Param_ANGLE is carried as a PF_Fixed: 16 bits of integer degrees and
    // 16 bits of fraction. Users may type more than one revolution, so no
    // wrap-around is applied here.
    return static_cast<float>(def_.u.ad.value) / 65536.0f;
}

A_long Param::popup() const {
    if (!ok_) return 0;
    return static_cast<A_long>(def_.u.pd.value);
}

bool Param::checkbox() const {
    if (!ok_) return false;
    return def_.u.bd.value != 0;
}

Float4 Param::color(Float4 fallback) const {
    if (!ok_) return fallback;

    // Colours are read through PF_ColorParamSuite1 rather than by poking at
    // PF_ColorDef's union directly. Two reasons:
    //
    //   * it returns a PF_PixelFloat, so a colour picked in a 32-bit project
    //     keeps its precision instead of being rounded to 8 bits;
    //   * PF_ColorDef's member is a union whose layout has changed between SDK
    //     generations, and the suite is the interface Adobe documented as
    //     stable for exactly this purpose.
    //
    // If the host does not provide the suite (some third-party hosts do not),
    // the parameter's default is used. That is a graceful degradation rather
    // than an error: a slightly wrong tint is better than a failed render.
    SuiteScope<PF_ColorParamSuite1> color_suite(in_data_, out_data_,
                                                kPFColorParamSuite,
                                                kPFColorParamSuiteVersion1);
    if (color_suite.ok()) {
        PF_PixelFloat pf{0.0f, 0.0f, 0.0f, 1.0f};
        // PF_GetColor's second argument is non-const in the SDK even though it
        // only reads it, so the const_cast is the documented way to call it.
        const PF_Err err = color_suite->PF_GetColor(in_data_->effect_ref,
                                                    const_cast<PF_ParamDef*>(&def_), &pf);
        if (err == PF_Err_NONE) {
            return Float4{static_cast<float>(pf.red), static_cast<float>(pf.green),
                          static_cast<float>(pf.blue), 1.0f};
        }
    }
    return fallback;
}

Vec2 Param::point() const {
    if (!ok_) return Vec2{0.0f, 0.0f};
    // Point values arrive in layer pixel coordinates with 16 bits of fraction.
    // (The point's *default*, set at registration, is in percent -- that
    // asymmetry is an AE quirk; by the time we read it, it is in pixels.)
    return Vec2{static_cast<float>(def_.u.td.x_value) / 65536.0f,
                static_cast<float>(def_.u.td.y_value) / 65536.0f};
}

// ---------------------------------------------------------------------------
//  WorldSuite
// ---------------------------------------------------------------------------
WorldSuite::WorldSuite(PF_InData* in_data, PF_OutData* out_data)
    : SuiteScopeBase(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2) {
    void* acquired = nullptr;
    const PF_Err err = AEFX_AcquireSuite(in_data, out_data, kPFWorldSuite,
                                         kPFWorldSuiteVersion2,
                                         "MGTK could not acquire PF_WorldSuite2",
                                         &acquired);
    if (err == PF_Err_NONE) suite_ = acquired;
}

PF_Err WorldSuite::format_of(PF_EffectWorld& world, PF_PixelFormat& format) const {
    if (suite_ == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    return static_cast<PF_WorldSuite2*>(suite_)->PF_GetPixelFormat(&world, &format);
}

// ---------------------------------------------------------------------------
//  World <-> Image
// ---------------------------------------------------------------------------
namespace {

// 8-bit and 16-bit AE pixels are integers with a fixed "one" value; 16-bit uses
// 32768 rather than 65535 because Adobe's 16-bit pipeline treats the top bit as
// headroom for highlights produced by prior effects.
constexpr float kOne8 = 255.0f;
constexpr float kOne16 = 32768.0f;

inline Float4 unpremultiply(Float4 c) {
    if (c.a <= 0.0f) {
        // Fully transparent pixels keep their colour (AE stores it, and
        // zeroing it here would make any later composite lose the pixels'
        // contribution). Only the matte is zero, so leave RGB alone.
        return Float4{c.r, c.g, c.b, 0.0f};
    }
    if (c.a >= 1.0f) return c;
    const float inv = 1.0f / c.a;
    return Float4{c.r * inv, c.g * inv, c.b * inv, c.a};
}

inline Float4 premultiply(Float4 c) {
    return Float4{c.r * c.a, c.g * c.a, c.b * c.a, c.a};
}

inline uint8_t* row_ptr(void* data, A_long rowbytes, int y) {
    return reinterpret_cast<uint8_t*>(data) + static_cast<ptrdiff_t>(y) * rowbytes;
}

}  // namespace

PF_Err world_to_image(const WorldSuite& ws, PF_EffectWorld& world, const AlphaPolicy policy,
                      Image& dst, bool* input_is_linear) {
    if (!ws.ok()) return PF_Err_BAD_CALLBACK_PARAM;
    if (world.data == nullptr) return PF_Err_BAD_CALLBACK_PARAM;

    PF_PixelFormat format = PF_PixelFormat_ARGB32;
    const PF_Err err = ws.format_of(world, format);
    if (err != PF_Err_NONE) return err;

    const int w = world.width;
    const int h = world.height;
    dst.resize(w, h);
    if (w <= 0 || h <= 0) return PF_Err_NONE;

    const bool straight = (policy != AlphaPolicy::PassThrough);

    switch (format) {
        case PF_PixelFormat_ARGB32: {
            for (int y = 0; y < h; ++y) {
                const PF_Pixel8* src = reinterpret_cast<const PF_Pixel8*>(
                    row_ptr(world.data, world.rowbytes, y));
                Float4* out = dst.row(y);
                for (int x = 0; x < w; ++x) {
                    Float4 c{src[x].red / kOne8, src[x].green / kOne8,
                             src[x].blue / kOne8, src[x].alpha / kOne8};
                    out[x] = straight ? unpremultiply(c) : c;
                }
            }
            break;
        }
        case PF_PixelFormat_ARGB64: {
            for (int y = 0; y < h; ++y) {
                const PF_Pixel16* src = reinterpret_cast<const PF_Pixel16*>(
                    row_ptr(world.data, world.rowbytes, y));
                Float4* out = dst.row(y);
                for (int x = 0; x < w; ++x) {
                    Float4 c{src[x].red / kOne16, src[x].green / kOne16,
                             src[x].blue / kOne16, src[x].alpha / kOne16};
                    out[x] = straight ? unpremultiply(c) : c;
                }
            }
            break;
        }
        case PF_PixelFormat_ARGB128: {
            for (int y = 0; y < h; ++y) {
                const PF_PixelFloat* src = reinterpret_cast<const PF_PixelFloat*>(
                    row_ptr(world.data, world.rowbytes, y));
                Float4* out = dst.row(y);
                for (int x = 0; x < w; ++x) {
                    Float4 c{static_cast<float>(src[x].red), static_cast<float>(src[x].green),
                             static_cast<float>(src[x].blue), static_cast<float>(src[x].alpha)};
                    out[x] = straight ? unpremultiply(c) : c;
                }
            }
            break;
        }
        default:
            return PF_Err_BAD_CALLBACK_PARAM;
    }

    if (input_is_linear != nullptr) {
        // 32-bit float projects keep their working space linear, which is the
        // whole point of the deep-colour pipeline. 8- and 16-bit projects hand
        // over gamma-encoded values. Tone-sensitive effects need to know which
        // they are looking at so that a Threshold of 1.0 means "white" either
        // way -- see to_perceptual() in mgtk/effects.hpp.
        *input_is_linear = (format == PF_PixelFormat_ARGB128);
    }
    return PF_Err_NONE;
}

PF_Err image_to_world(const WorldSuite& ws, const Image& src, const AlphaPolicy policy,
                      PF_EffectWorld& world) {
    if (!ws.ok()) return PF_Err_BAD_CALLBACK_PARAM;
    if (world.data == nullptr) return PF_Err_BAD_CALLBACK_PARAM;

    PF_PixelFormat format = PF_PixelFormat_ARGB32;
    const PF_Err err = ws.format_of(world, format);
    if (err != PF_Err_NONE) return err;

    const bool straight = (policy != AlphaPolicy::PassThrough);
    const int w = (src.width() < world.width) ? src.width() : world.width;
    const int h = (src.height() < world.height) ? src.height() : world.height;
    if (w <= 0 || h <= 0) return PF_Err_NONE;

    switch (format) {
        case PF_PixelFormat_ARGB32: {
            for (int y = 0; y < h; ++y) {
                PF_Pixel8* out = reinterpret_cast<PF_Pixel8*>(
                    row_ptr(world.data, world.rowbytes, y));
                const Float4* in = src.row(y);
                for (int x = 0; x < w; ++x) {
                    Float4 c = straight ? premultiply(in[x]) : in[x];
                    out[x].alpha = static_cast<A_u_char>(saturate(c.a) * kOne8 + 0.5f);
                    out[x].red = static_cast<A_u_char>(saturate(c.r) * kOne8 + 0.5f);
                    out[x].green = static_cast<A_u_char>(saturate(c.g) * kOne8 + 0.5f);
                    out[x].blue = static_cast<A_u_char>(saturate(c.b) * kOne8 + 0.5f);
                }
            }
            break;
        }
        case PF_PixelFormat_ARGB64: {
            for (int y = 0; y < h; ++y) {
                PF_Pixel16* out = reinterpret_cast<PF_Pixel16*>(
                    row_ptr(world.data, world.rowbytes, y));
                const Float4* in = src.row(y);
                for (int x = 0; x < w; ++x) {
                    Float4 c = straight ? premultiply(in[x]) : in[x];
                    out[x].alpha = static_cast<A_u_short>(saturate(c.a) * kOne16 + 0.5f);
                    out[x].red = static_cast<A_u_short>(saturate(c.r) * kOne16 + 0.5f);
                    out[x].green = static_cast<A_u_short>(saturate(c.g) * kOne16 + 0.5f);
                    out[x].blue = static_cast<A_u_short>(saturate(c.b) * kOne16 + 0.5f);
                }
            }
            break;
        }
        case PF_PixelFormat_ARGB128: {
            for (int y = 0; y < h; ++y) {
                PF_PixelFloat* out = reinterpret_cast<PF_PixelFloat*>(
                    row_ptr(world.data, world.rowbytes, y));
                const Float4* in = src.row(y);
                for (int x = 0; x < w; ++x) {
                    // No clamping in float: highlights above 1.0 are legal and
                    // are part of what makes a 32-bit project worth using.
                    Float4 c = straight ? premultiply(in[x]) : in[x];
                    out[x].alpha = c.a;
                    out[x].red = c.r;
                    out[x].green = c.g;
                    out[x].blue = c.b;
                }
            }
            break;
        }
        default:
            return PF_Err_BAD_CALLBACK_PARAM;
    }
    return PF_Err_NONE;
}

// ---------------------------------------------------------------------------
//  Host services
// ---------------------------------------------------------------------------
namespace {

void progress_trampoline(void* user_data, float fraction) {
    PF_InData* in_data = static_cast<PF_InData*>(user_data);
    if (in_data == nullptr) return;
    // PF_PROGRESS takes (current, total) on a 0..total scale. A zero total
    // makes AE fall back to the abort-only prompt, so always pass a real total.
    const A_long current = static_cast<A_long>(saturate(fraction) * 1000.0f);
    PF_PROGRESS(in_data, current, 1000);
}

bool abort_trampoline(void* user_data) {
    PF_InData* in_data = static_cast<PF_InData*>(user_data);
    if (in_data == nullptr) return false;
    return PF_ABORT(in_data) != 0;
}

}  // namespace

RenderContext make_context(PF_InData* in_data) {
    RenderContext ctx;
    ctx.user_data = in_data;
    ctx.progress = progress_trampoline;
    ctx.abort_requested = abort_trampoline;

    if (in_data != nullptr) {
        ctx.frame = static_cast<float>(in_data->current_time) /
                    static_cast<float>(in_data->time_step > 0 ? in_data->time_step : 1);
        const float time_scale =
            static_cast<float>(in_data->time_scale > 0 ? in_data->time_scale : 1);
        ctx.seconds = static_cast<float>(in_data->current_time) / time_scale;
        const float step_seconds =
            static_cast<float>(in_data->time_step) / time_scale;
        ctx.fps = (step_seconds > 0.0f) ? (1.0f / step_seconds) : 24.0f;
    }
    return ctx;
}

void set_return_message(PF_OutData* out_data, const char* text) {
    if (out_data == nullptr || text == nullptr) return;
    const size_t capacity = sizeof(out_data->return_msg) - 1;
    std::strncpy(out_data->return_msg, text, capacity);
    out_data->return_msg[capacity] = '\0';
}

}  // namespace ae
}  // namespace mgtk
