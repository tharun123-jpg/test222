// =============================================================================
//  tests/ae_shim/host.cpp -- implementation of the fake AE host and of the
//                            callbacks an effect calls back into.
// =============================================================================
#include "host.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "mgtk/math.hpp"

namespace aetest {
namespace {

// The callbacks the effects use (PF_CHECKOUT_PARAM, PF_PROGRESS, ...) receive
// a PF_InData and nothing else, so the host they belong to has to be reachable
// from there. AE smuggles the same information in in_data->effect_ref; this
// uses in_data->global_data, which is otherwise unused by MGTK effects.
Host* g_host = nullptr;

Host* host_from(PF_InData* in_data) {
    return in_data != nullptr ? static_cast<Host*>(in_data->global_data) : nullptr;
}

constexpr int kMaxParamId = 256;

PF_Err world_suite_get_format(PF_EffectWorld* world, PF_PixelFormat* format) {
    if (world == nullptr || format == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    // AE tracks a world's bit depth outside PF_EffectWorld, in the render
    // request. The shim has to remember it somewhere, and `dephault` is a
    // reserved field the SDK never reads, so the test host parks the format
    // there. Guessing from rowbytes would be wrong: AE pads rows.
    switch (world->dephault) {
        case PF_PixelFormat_ARGB64: *format = PF_PixelFormat_ARGB64; break;
        case PF_PixelFormat_ARGB128: *format = PF_PixelFormat_ARGB128; break;
        default: *format = PF_PixelFormat_ARGB32; break;
    }
    return PF_Err_NONE;
}

PF_Err color_suite_get_color(PF_ProgPtr, PF_ParamDef* param, PF_PixelFloat* color) {
    if (param == nullptr || color == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    color->alpha = 1.0f;
    color->red = static_cast<PF_FpShort>(param->u.cd.value.red) / PF_MAX_CHAN8;
    color->green = static_cast<PF_FpShort>(param->u.cd.value.green) / PF_MAX_CHAN8;
    color->blue = static_cast<PF_FpShort>(param->u.cd.value.blue) / PF_MAX_CHAN8;
    return PF_Err_NONE;
}

PF_Err checkout_layer_slot(PF_ProgPtr, PF_ParamIndex, A_long checkout_id,
                           const PF_RenderRequest* req, A_long what_time, A_long, A_u_long,
                           PF_CheckoutResult* result) {
    if (g_host == nullptr || result == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    g_host->note_checkout_time(checkout_id, what_time);
    const PF_EffectWorld& in = g_host->world_at(what_time, checkout_id);
    result->result_rect.left = 0;
    result->result_rect.top = 0;
    result->result_rect.right = in.width;
    result->result_rect.bottom = in.height;
    result->max_result_rect = result->result_rect;
    result->solid = FALSE;
    result->ref_width = in.width;
    result->ref_height = in.height;
    (void)req;
    return PF_Err_NONE;
}

PF_Err checkout_layer_pixels_slot(PF_ProgPtr, A_long checkout_id, PF_EffectWorld** pixels) {
    if (g_host == nullptr || pixels == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    // The pixel checkout must hand back the frame that was declared in
    // pre-render, not the current frame: that is the contract SmartFX is built
    // on, and getting it wrong would make every time-displacement effect
    // silently render the present.
    *pixels = &g_host->world_at(g_host->checkout_time(checkout_id),
                                checkout_id);
    return PF_Err_NONE;
}

PF_Err checkin_layer_pixels_slot(PF_ProgPtr, A_long) { return PF_Err_NONE; }

PF_Err checkout_output_slot(PF_ProgPtr, PF_EffectWorld** output) {
    if (g_host == nullptr || output == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    *output = &g_host->output_world();
    return PF_Err_NONE;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Callbacks the plug-in uses
//
// These are declared in the global namespace by ae_shim.h -- that is what the
// glue calls -- so they are defined here, outside aetest.
// ---------------------------------------------------------------------------
}  // namespace aetest

// The callback bodies live in aetest's anonymous namespace; these pull them
// into scope for the global-namespace definitions below.
using aetest::Host;
using aetest::host_from;

PF_Err shim_acquire_suite(PF_InData*, PF_OutData*, const char* name, A_long version,
                          void** suite) {
    static PF_WorldSuite2 world_suite{&aetest::world_suite_get_format};
    static PF_ColorParamSuite1 color_suite{&aetest::color_suite_get_color};

    if (std::strcmp(name, kPFWorldSuite) == 0 && version == kPFWorldSuiteVersion2) {
        *suite = &world_suite;
        return PF_Err_NONE;
    }
    if (std::strcmp(name, kPFColorParamSuite) == 0 && version == kPFColorParamSuiteVersion1) {
        *suite = &color_suite;
        return PF_Err_NONE;
    }
    *suite = nullptr;
    return PF_Err_BAD_CALLBACK_PARAM;
}

PF_Err shim_release_suite(PF_InData*, const char*, A_long) { return PF_Err_NONE; }

PF_Err shim_add_param(PF_InData* in_data, A_long, PF_ParamDef* def) {
    Host* host = host_from(in_data);
    if (host == nullptr || def == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    host->registered_mut().push_back(*def);
    return PF_Err_NONE;
}

PF_Err shim_checkout_param(PF_InData* in_data, A_long index, A_long time, A_long, A_u_long,
                           PF_ParamDef* def) {
    Host* host = host_from(in_data);
    if (host == nullptr || def == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    if (index == 0) {
        // The implicit input layer. In the legacy render path an effect reaches
        // its own past frames this way, so the time has to be honoured.
        *def = host->legacy_layer_at(time);
        return PF_Err_NONE;
    }
    const std::vector<PF_ParamDef>& values = host->param_values();
    if (index < 0 || index >= static_cast<A_long>(values.size())) return PF_Err_PARAMETER;
    *def = values[static_cast<size_t>(index)];
    return PF_Err_NONE;
}

PF_Err shim_checkin_param(PF_InData*, PF_ParamDef*) { return PF_Err_NONE; }

PF_Err shim_progress(PF_InData* in_data, A_long, A_long) {
    Host* host = host_from(in_data);
    if (host != nullptr) host->note_progress();
    return PF_Err_NONE;
}

PF_Err shim_abort(PF_InData* in_data) {
    Host* host = host_from(in_data);
    return host != nullptr && host->should_abort() ? 1 : 0;
}

void shim_union_lrect(const PF_LRect* src, PF_LRect* dst) {
    if (src == nullptr || dst == nullptr) return;
    if (src->left < dst->left) dst->left = src->left;
    if (src->top < dst->top) dst->top = src->top;
    if (src->right > dst->right) dst->right = src->right;
    if (src->bottom > dst->bottom) dst->bottom = src->bottom;
}

namespace aetest {

// ---------------------------------------------------------------------------
//  OwnedWorld
// ---------------------------------------------------------------------------
void OwnedWorld::allocate(int width, int height, PF_PixelFormat format, int padding_bytes) {
    int pixel_size = 4;
    switch (format) {
        case PF_PixelFormat_ARGB64: pixel_size = 8; break;
        case PF_PixelFormat_ARGB128: pixel_size = 16; break;
        default: pixel_size = 4; break;
    }
    const int rowbytes = width * pixel_size + padding_bytes;
    bytes.assign(static_cast<size_t>(rowbytes) * static_cast<size_t>(height), 0);

    world = PF_EffectWorld{};
    world.world_flags = (format == PF_PixelFormat_ARGB64) ? PF_WorldFlag_DEEP : 0;
    world.data = reinterpret_cast<PF_PixelPtr>(bytes.data());
    world.rowbytes = rowbytes;
    world.width = width;
    world.height = height;
    // See world_suite_get_format: the shim stores the bit depth here.
    world.dephault = static_cast<A_long>(format);
}

void OwnedWorld::write_from(const mgtk::Image& src, PF_PixelFormat format) {
    const int w = std::min(src.width(), static_cast<int>(world.width));
    const int h = std::min(src.height(), static_cast<int>(world.height));
    for (int y = 0; y < h; ++y) {
        unsigned char* row = bytes.data() + static_cast<size_t>(y) * world.rowbytes;
        const mgtk::Float4* in = src.row(y);
        for (int x = 0; x < w; ++x) {
            // AE worlds are premultiplied; the host does the premultiply, the
            // same way AE does before handing a layer to an effect.
            const mgtk::Float4 c = in[x];
            const float r = c.r * c.a;
            const float g = c.g * c.a;
            const float b = c.b * c.a;
            if (format == PF_PixelFormat_ARGB128) {
                PF_PixelFloat* out = reinterpret_cast<PF_PixelFloat*>(row) + x;
                out->alpha = c.a;
                out->red = r;
                out->green = g;
                out->blue = b;
            } else if (format == PF_PixelFormat_ARGB64) {
                PF_Pixel16* out = reinterpret_cast<PF_Pixel16*>(row) + x;
                out->alpha = static_cast<A_u_short>(mgtk::saturate(c.a) * PF_MAX_CHAN16 + 0.5f);
                out->red = static_cast<A_u_short>(mgtk::saturate(r) * PF_MAX_CHAN16 + 0.5f);
                out->green = static_cast<A_u_short>(mgtk::saturate(g) * PF_MAX_CHAN16 + 0.5f);
                out->blue = static_cast<A_u_short>(mgtk::saturate(b) * PF_MAX_CHAN16 + 0.5f);
            } else {
                PF_Pixel8* out = reinterpret_cast<PF_Pixel8*>(row) + x;
                out->alpha = static_cast<A_u_char>(mgtk::saturate(c.a) * PF_MAX_CHAN8 + 0.5f);
                out->red = static_cast<A_u_char>(mgtk::saturate(r) * PF_MAX_CHAN8 + 0.5f);
                out->green = static_cast<A_u_char>(mgtk::saturate(g) * PF_MAX_CHAN8 + 0.5f);
                out->blue = static_cast<A_u_char>(mgtk::saturate(b) * PF_MAX_CHAN8 + 0.5f);
            }
        }
    }
}

void OwnedWorld::read_into(mgtk::Image& dst, PF_PixelFormat format) const {
    const int w = static_cast<int>(world.width);
    const int h = static_cast<int>(world.height);
    dst.resize(w, h);
    for (int y = 0; y < h; ++y) {
        const unsigned char* row = bytes.data() + static_cast<size_t>(y) * world.rowbytes;
        mgtk::Float4* out = dst.row(y);
        for (int x = 0; x < w; ++x) {
            float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
            if (format == PF_PixelFormat_ARGB128) {
                const PF_PixelFloat* in = reinterpret_cast<const PF_PixelFloat*>(row) + x;
                a = in->alpha; r = in->red; g = in->green; b = in->blue;
            } else if (format == PF_PixelFormat_ARGB64) {
                const PF_Pixel16* in = reinterpret_cast<const PF_Pixel16*>(row) + x;
                a = in->alpha / static_cast<float>(PF_MAX_CHAN16);
                r = in->red / static_cast<float>(PF_MAX_CHAN16);
                g = in->green / static_cast<float>(PF_MAX_CHAN16);
                b = in->blue / static_cast<float>(PF_MAX_CHAN16);
            } else {
                const PF_Pixel8* in = reinterpret_cast<const PF_Pixel8*>(row) + x;
                a = in->alpha / static_cast<float>(PF_MAX_CHAN8);
                r = in->red / static_cast<float>(PF_MAX_CHAN8);
                g = in->green / static_cast<float>(PF_MAX_CHAN8);
                b = in->blue / static_cast<float>(PF_MAX_CHAN8);
            }
            // Undo the premultiply so the comparison in the tests is against
            // straight-alpha values, which is what the core works in.
            if (a > 0.0f && a < 1.0f) { r /= a; g /= a; b /= a; }
            out[x] = mgtk::Float4{r, g, b, a};
        }
    }
}

// ---------------------------------------------------------------------------
//  Host
// ---------------------------------------------------------------------------
Host::Host(int width, int height, PF_PixelFormat format)
    : format_(format), width_(width), height_(height) {
    // A padding of four bytes is enough to break any code that assumes rows are
    // contiguous, and is what AE commonly hands out.
    input_.allocate(width, height, format, 4);
    output_.allocate(width, height, format, 4);

    param_values_.assign(kMaxParamId, PF_ParamDef{});
    for (PF_ParamDef& def : param_values_) {
        AEFX_CLR_STRUCT(def);
        def.param_type = PF_Param_FLOAT_SLIDER;
    }
    input_param_ = PF_ParamDef{};
    AEFX_CLR_STRUCT(input_param_);
    input_param_.param_type = PF_Param_LAYER;
    input_param_.u.ld = input_.world;

    std::memset(&in_data_, 0, sizeof(in_data_));
    in_data_.effect_ref = reinterpret_cast<PF_ProgPtr>(this);
    in_data_.global_data = this;
    in_data_.current_time = 0;
    in_data_.time_step = 1;
    in_data_.time_scale = 24;
    in_data_.width = width;
    in_data_.height = height;
    in_data_.num_params = 0;
    in_data_.quality = 1;

    std::memset(&out_data_, 0, sizeof(out_data_));
    out_data_.width = width;
    out_data_.height = height;

    g_host = this;
}

void Host::set_input(const mgtk::Image& src) {
    base_ = src;
    input_.write_from(src, format_);
}

void Host::note_checkout_time(A_long checkout_id, A_long time) {
    checkout_times_[checkout_id] = time;
}

A_long Host::checkout_time(A_long checkout_id) const {
    auto it = checkout_times_.find(checkout_id);
    return it == checkout_times_.end() ? in_data_.current_time : it->second;
}

PF_EffectWorld& Host::world_at(A_long time, A_long checkout_id) {
    if (time == in_data_.current_time) return input_.world;
    auto it = time_worlds_.find(checkout_id);
    if (it == time_worlds_.end()) {
        std::unique_ptr<OwnedWorld> fresh(new OwnedWorld());
        fresh->allocate(width_, height_, format_, 4);
        it = time_worlds_.emplace(checkout_id, std::move(fresh)).first;
    }
    it->second->write_from(frame_at(base_, time, in_data_.time_scale), format_);
    return it->second->world;
}

PF_ParamDef Host::legacy_layer_at(A_long time) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    def.param_type = PF_Param_LAYER;
    def.uu.id = 0;
    def.u.ld = world_at(time, 4096 + time);
    return def;
}

void Host::get_output(mgtk::Image& dst) const { output_.read_into(dst, format_); }

void Host::set_float(A_long id, double value) {
    if (id <= 0 || id >= kMaxParamId) return;
    param_values_[static_cast<size_t>(id)].param_type = PF_Param_FLOAT_SLIDER;
    param_values_[static_cast<size_t>(id)].u.fs_d.value = value;
}

void Host::set_popup(A_long id, A_long value) {
    if (id <= 0 || id >= kMaxParamId) return;
    param_values_[static_cast<size_t>(id)].param_type = PF_Param_POPUP;
    param_values_[static_cast<size_t>(id)].u.pd.value = value;
}

void Host::set_checkbox(A_long id, bool value) {
    if (id <= 0 || id >= kMaxParamId) return;
    param_values_[static_cast<size_t>(id)].param_type = PF_Param_CHECKBOX;
    param_values_[static_cast<size_t>(id)].u.bd.value = value ? TRUE : FALSE;
}

void Host::set_angle(A_long id, double degrees) {
    if (id <= 0 || id >= kMaxParamId) return;
    param_values_[static_cast<size_t>(id)].param_type = PF_Param_ANGLE;
    param_values_[static_cast<size_t>(id)].u.ad.value = FLOAT2FIX(degrees);
}

void Host::set_point(A_long id, double x, double y) {
    if (id <= 0 || id >= kMaxParamId) return;
    param_values_[static_cast<size_t>(id)].param_type = PF_Param_POINT;
    param_values_[static_cast<size_t>(id)].u.td.x_value = FLOAT2FIX(x);
    param_values_[static_cast<size_t>(id)].u.td.y_value = FLOAT2FIX(y);
}

void Host::set_color(A_long id, float r, float g, float b) {
    if (id <= 0 || id >= kMaxParamId) return;
    PF_ParamDef& def = param_values_[static_cast<size_t>(id)];
    def.param_type = PF_Param_COLOR;
    def.u.cd.value.alpha = PF_MAX_CHAN8;
    def.u.cd.value.red = static_cast<A_u_char>(mgtk::saturate(r) * PF_MAX_CHAN8 + 0.5f);
    def.u.cd.value.green = static_cast<A_u_char>(mgtk::saturate(g) * PF_MAX_CHAN8 + 0.5f);
    def.u.cd.value.blue = static_cast<A_u_char>(mgtk::saturate(b) * PF_MAX_CHAN8 + 0.5f);
}

void Host::set_defaults_from_registration() {
    // AE is the one that remembers parameter defaults, so the host copies them
    // out of the defs the effect registered. A test that runs the effect
    // without calling this gets zeroed parameters, which is itself a useful
    // check (it is what a project saved by an older build looks like).
    for (const PF_ParamDef& def : registered_) {
        const A_long id = def.uu.id;
        if (id <= 0 || id >= kMaxParamId) continue;
        PF_ParamDef& slot = param_values_[static_cast<size_t>(id)];
        // Preserve the id, which shim_checkout_param does not need but which
        // makes a mismatch visible when debugging a failing test.
        slot.uu.id = id;
        switch (def.param_type) {
            case PF_Param_FLOAT_SLIDER:
                slot.param_type = PF_Param_FLOAT_SLIDER;
                slot.u.fs_d.value = def.u.fs_d.dephault;
                break;
            case PF_Param_POPUP:
                slot.param_type = PF_Param_POPUP;
                slot.u.pd.value = def.u.pd.dephault;
                break;
            case PF_Param_CHECKBOX:
                slot.param_type = PF_Param_CHECKBOX;
                slot.u.bd.value = def.u.bd.dephault;
                break;
            case PF_Param_COLOR:
                slot.param_type = PF_Param_COLOR;
                slot.u.cd.value = def.u.cd.dephault;
                break;
            case PF_Param_POINT:
                slot.param_type = PF_Param_POINT;
                slot.u.td.x_value = FLOAT2FIX(static_cast<double>(def.u.td.x_dephault) / 65536.0 *
                                              width_ / 100.0);
                slot.u.td.y_value = FLOAT2FIX(static_cast<double>(def.u.td.y_dephault) / 65536.0 *
                                              height_ / 100.0);
                break;
            case PF_Param_ANGLE:
                slot.param_type = PF_Param_ANGLE;
                slot.u.ad.value = def.u.ad.dephault;
                break;
            default:
                break;
        }
    }
}

void Host::refresh_param_array() {
    param_ptrs_.clear();
    input_param_.u.ld = input_.world;
    param_ptrs_.push_back(&input_param_);
    for (const PF_ParamDef& def : registered_) {
        const A_long id = def.uu.id;
        if (id > 0 && id < kMaxParamId) param_ptrs_.push_back(&param_values_[static_cast<size_t>(id)]);
    }
}

PF_Err Host::send(Entry entry, PF_Cmd cmd, void* extra) {
    if (entry == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    in_data_.global_data = this;
    in_data_.num_params = static_cast<A_long>(registered_.size()) + 1;
    // AE keeps in_data->sequence_data pointing at whatever the effect most
    // recently stored in out_data, including after a project load.
    in_data_.sequence_data = out_data_.sequence_data;

    PF_ParamDef** params = param_ptrs_.empty() ? nullptr : param_ptrs_.data();
    return entry(cmd, &in_data_, &out_data_, params, &output_.world, extra);
}

PF_Err Host::setup(Entry entry) {
    // Without the default parameter values registered first, ParamsSetup would
    // have nothing to report and the checks below would be vacuous.
    registered_.clear();
    PF_Err err = send(entry, PF_Cmd_GLOBAL_SETUP);
    if (err != PF_Err_NONE) return err;
    err = send(entry, PF_Cmd_PARAMS_SETUP);
    if (err != PF_Err_NONE) return err;
    set_defaults_from_registration();
    refresh_param_array();
    err = send(entry, PF_Cmd_SEQUENCE_SETUP);
    if (err != PF_Err_NONE) return err;
    in_data_.sequence_data = out_data_.sequence_data;
    return PF_Err_NONE;
}

PF_Err Host::render_smart(Entry entry) {
    refresh_param_array();
    in_data_.sequence_data = out_data_.sequence_data;

    PF_PreRenderInput pre_input{};
    pre_input.output_request.rect.left = 0;
    pre_input.output_request.rect.top = 0;
    pre_input.output_request.rect.right = width_;
    pre_input.output_request.rect.bottom = height_;
    pre_input.output_request.channel_mask = PF_ChannelMask_ARGB;
    pre_input.bitdepth = (format_ == PF_PixelFormat_ARGB128) ? 32
                        : (format_ == PF_PixelFormat_ARGB64) ? 16 : 8;

    PF_PreRenderOutput pre_output{};
    PF_PreRenderCallbacks pre_cb{&checkout_layer_slot, nullptr};
    PF_PreRenderExtra pre_extra{&pre_input, &pre_output, &pre_cb};

    PF_Err err = send(entry, PF_Cmd_SMART_PRE_RENDER, &pre_extra);
    if (err != PF_Err_NONE) return err;
    if (pre_output.result_rect.right <= pre_output.result_rect.left ||
        pre_output.result_rect.bottom <= pre_output.result_rect.top) {
        // The effect declared an empty output, which for these effects means it
        // has no work to do at all. That is legal and there is no render to run.
        return PF_Err_NONE;
    }

    PF_SmartRenderInput render_input{};
    render_input.output_request = pre_input.output_request;
    render_input.bitdepth = pre_input.bitdepth;
    PF_SmartRenderCallbacks render_cb{&checkout_layer_pixels_slot, &checkin_layer_pixels_slot,
                                      &checkout_output_slot};
    PF_SmartRenderExtra render_extra{&render_input, &render_cb};

    err = send(entry, PF_Cmd_SMART_RENDER, &render_extra);
    if (err != PF_Err_NONE) return err;
    in_data_.sequence_data = out_data_.sequence_data;
    return PF_Err_NONE;
}

PF_Err Host::render_legacy(Entry entry) {
    refresh_param_array();
    in_data_.sequence_data = out_data_.sequence_data;
    input_param_.u.ld = input_.world;
    const PF_Err err = send(entry, PF_Cmd_RENDER);
    in_data_.sequence_data = out_data_.sequence_data;
    return err;
}

// ---------------------------------------------------------------------------
//  Test layers
// ---------------------------------------------------------------------------
namespace {

float hash01(int x, int y, int seed) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u +
                 static_cast<uint32_t>(seed) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xffffffu) / static_cast<float>(0x1000000);
}

}  // namespace

mgtk::Image frame_at(const mgtk::Image& base, A_long time, A_long time_scale) {
    mgtk::Image out = base;
    if (out.empty() || time == 0) return out;
    const float seconds = static_cast<float>(time) /
                          static_cast<float>(time_scale > 0 ? time_scale : 24);
    // A per-row rotation plus a slow colour drift: deterministic, obviously
    // time-dependent, and cheap. A host that returned the same frame for every
    // time would make slit scan and feedback echo untestable, because both
    // would correctly produce the input.
    const int w = out.width();
    const int shift = static_cast<int>(std::fabs(seconds) * 7.0f) % (w > 0 ? w : 1);
    const float tint = std::fmod(std::fabs(seconds), 4.0f) * 0.25f;
    for (int y = 0; y < out.height(); ++y) {
        mgtk::Float4* row = out.row(y);
        std::vector<mgtk::Float4> copy(row, row + w);
        for (int x = 0; x < w; ++x) {
            const int sx = (x + shift + y) % (w > 0 ? w : 1);
            row[x] = copy[static_cast<size_t>(sx)];
            row[x].b = mgtk::clamp(row[x].b + tint, 0.0f, 4.0f);
        }
    }
    return out;
}

mgtk::Image test_layer_gradient(int width, int height) {
    mgtk::Image img;
    img.resize(width, height);
    const float w = static_cast<float>(width > 1 ? width - 1 : 1);
    const float h = static_cast<float>(height > 1 ? height - 1 : 1);
    for (int y = 0; y < height; ++y) {
        mgtk::Float4* row = img.row(y);
        for (int x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / w;
            const float v = static_cast<float>(y) / h;
            row[x] = mgtk::Float4{0.15f + 0.70f * u, 0.10f + 0.60f * v,
                                  0.05f + 0.80f * hash01(x, y, 7), 1.0f};
        }
    }
    return img;
}

mgtk::Image test_layer_hdr(int width, int height) {
    mgtk::Image img = test_layer_gradient(width, height);
    for (int y = 0; y < height; ++y) {
        mgtk::Float4* row = img.row(y);
        for (int x = 0; x < width; ++x) {
            // A soft highlight well above 1.0 in the middle of the frame.
            const float dx = static_cast<float>(x) - width * 0.5f;
            const float dy = static_cast<float>(y) - height * 0.5f;
            const float d2 = (dx * dx + dy * dy) /
                             static_cast<float>(width * width + height * height);
            row[x].r += 1.6f * std::exp(-d2 * 18.0f);
            row[x].g += 1.2f * std::exp(-d2 * 18.0f);
        }
    }
    return img;
}

mgtk::Image test_layer_alpha_ramp(int width, int height, float min_alpha) {
    mgtk::Image img;
    img.resize(width, height);
    const float w = static_cast<float>(width > 1 ? width - 1 : 1);
    for (int y = 0; y < height; ++y) {
        mgtk::Float4* row = img.row(y);
        for (int x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / w;
            // Near-transparent at the left edge, opaque at the right. Blurring
            // premultiplied pixels across this ramp is exactly what produces
            // the dark halo this suite is meant to catch.
            row[x] = mgtk::Float4{0.9f, 0.4f, 0.15f,
                                  min_alpha + (1.0f - min_alpha) * u};
        }
    }
    return img;
}

}  // namespace aetest
