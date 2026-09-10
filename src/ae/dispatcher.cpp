// =============================================================================
//  src/ae/dispatcher.cpp -- the command selector loop every MGTK effect shares.
//
//  Each plug-in in src/ae/plugins/ is a two-line translation unit that names
//  its EffectSpec; everything else -- parameter registration, sequence data,
//  the SmartFX two-pass protocol, the legacy render path -- lives here and is
//  driven by the table in registry.cpp.
//
//  Why every effect supports SmartFX rather than only PF_Cmd_RENDER:
//
//    * 32-bit float projects. AE will not hand an effect a 32-bit float world
//      through the legacy path at all; without PF_OutFlag2_SUPPORTS_SMART_RENDER
//      the effect silently loses float precision in the project that most wants
//      it.
//    * The two effects that read frames other than the current one can declare
//      exactly which frames they need instead of forcing AE to widen every
//      input's time range.
// =============================================================================
#include "effect_spec.hpp"
#include "mgtk_ae.hpp"

#include "mgtk/version.hpp"

#include <cstring>
#include <new>
#include <vector>

namespace mgtk {
namespace ae {

// Implemented in param_table.cpp.
PF_Err setup_params(PF_InData* in_data, PF_OutData* out_data, const EffectSpec& spec);

namespace {

// ---------------------------------------------------------------------------
//  SmartFX checkout ids
// ---------------------------------------------------------------------------
// Chosen by the effect, must be positive and unique within one pre-render.
constexpr A_long kCheckoutInput = 1;
constexpr A_long kCheckoutHistoryBase = 100;
// A ceiling on how many past frames an effect may ask for. Every one of them is
// a full render of the layer at an earlier time, so this is a performance
// guard rail, not a correctness one.
constexpr int kMaxHistoryFrames = 24;

// ---------------------------------------------------------------------------
//  Sequence data
// ---------------------------------------------------------------------------
// The feedback accumulator has to survive between frames and be saved with the
// project, which in AE means living in the effect's sequence data.
//
// It is stored as one flat block: a small POD header followed by RGBA floats.
// Because there are no nested handles, the block is already in its
// serialisable form, so PF_Cmd_SEQUENCE_FLATTEN is never needed and the effect
// does not have to claim PF_OutFlag_SEQUENCE_DATA_NEEDS_FLATTENING. (That flag
// exists for sequence data holding handles of its own, and the two-phase
// flatten/unflatten dance it implies is the usual source of bugs here.)
constexpr A_long kSequenceMagic = 0x4D47544B;  // 'MGTK'
constexpr A_long kSequenceVersion = 1;
constexpr A_long kAccumulatorChannels = 4;

struct SequenceHeader {
    A_long magic;
    A_long version;
    A_long width;
    A_long height;
    A_long channels;
    A_long byte_size;  // total block size, header included
};

A_long sequence_bytes(int width, int height) {
    const A_long pixels = static_cast<A_long>(width) * static_cast<A_long>(height);
    return static_cast<A_long>(sizeof(SequenceHeader)) +
           pixels * kAccumulatorChannels * static_cast<A_long>(sizeof(float));
}

SequenceHeader* locked_header(PF_Handle handle) {
    return reinterpret_cast<SequenceHeader*>(PF_LOCK_HANDLE(handle));
}

float* header_pixels(SequenceHeader* header) {
    return reinterpret_cast<float*>(header + 1);
}

// Allocates the accumulator so it can hold a `width` x `height` frame, or
// replaces a block that is the wrong size or was left over from a different
// effect. Sets *reset when the accumulator was (re)initialised.
//
// The size is carried in the header rather than asked of the handle: a fresh
// allocation is used whenever the geometry changes, so no resize primitive is
// involved at all. That matters because size-changing handle operations are
// the part of the host API that differs most between SDK generations, and the
// accumulator only ever needs two sizes in practice (the layer it was built
// for, and nothing).
PF_Err ensure_sequence(PF_InData* in_data, PF_Handle* handle, int width, int height,
                       bool* reset) {
    if (handle == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    *reset = false;
    if (width <= 0 || height <= 0) return PF_Err_NONE;

    const A_long needed = sequence_bytes(width, height);

    bool usable = false;
    if (*handle != NULL) {
        const SequenceHeader* existing = locked_header(*handle);
        if (existing != nullptr) usable = (existing->magic == kSequenceMagic);
        PF_UNLOCK_HANDLE(*handle);
    }

    if (usable) {
        SequenceHeader* header = locked_header(*handle);
        usable = header->version == kSequenceVersion && header->byte_size == needed &&
                 header->width == width && header->height == height &&
                 header->channels == kAccumulatorChannels;
        PF_UNLOCK_HANDLE(*handle);
    }

    if (!usable) {
        // A layer whose size changed invalidates whatever was accumulated: the
        // old frame cannot be resampled into the new one without inventing
        // pixels, and keeping the top-left corner produces a visible tear.
        // Starting clean is both cheaper and more honest.
        if (*handle != NULL) {
            PF_DISPOSE_HANDLE(*handle);
            *handle = NULL;
        }
        *handle = PF_NEW_HANDLE(needed);
        if (*handle == NULL) return PF_Err_OUT_OF_MEMORY;

        SequenceHeader* header = locked_header(*handle);
        if (header == nullptr) return PF_Err_OUT_OF_MEMORY;
        header->magic = kSequenceMagic;
        header->version = kSequenceVersion;
        header->width = width;
        header->height = height;
        header->channels = kAccumulatorChannels;
        header->byte_size = needed;
        const A_long pixels = static_cast<A_long>(width) * static_cast<A_long>(height);
        std::memset(header_pixels(header), 0,
                    static_cast<size_t>(pixels) * kAccumulatorChannels * sizeof(float));
        PF_UNLOCK_HANDLE(*handle);
        *reset = true;
    }

    (void)in_data;
    return PF_Err_NONE;
}

// Read the accumulator into an Image. Copies because the sequence block is
// locked only briefly: holding a lock across the whole render would block any
// other effect instance AE happens to be running.
void read_accumulator(PF_Handle handle, Image& dst) {
    if (handle == NULL) {
        dst.resize(0, 0);
        return;
    }
    SequenceHeader* header = locked_header(handle);
    if (header == nullptr || header->magic != kSequenceMagic) {
        PF_UNLOCK_HANDLE(handle);
        dst.resize(0, 0);
        return;
    }
    dst.resize(header->width, header->height);
    const float* pixels = header_pixels(header);
    for (int y = 0; y < header->height; ++y) {
        Float4* row = dst.row(y);
        const float* src = pixels + static_cast<size_t>(y) * header->width * kAccumulatorChannels;
        for (int x = 0; x < header->width; ++x) {
            row[x] = Float4{src[0], src[1], src[2], src[3]};
            src += kAccumulatorChannels;
        }
    }
    PF_UNLOCK_HANDLE(handle);
}

void write_accumulator(PF_Handle handle, const Image& src) {
    if (handle == NULL) return;
    SequenceHeader* header = locked_header(handle);
    if (header == nullptr || header->magic != kSequenceMagic) {
        PF_UNLOCK_HANDLE(handle);
        return;
    }
    const int w = (src.width() < header->width) ? src.width() : header->width;
    const int h = (src.height() < header->height) ? src.height() : header->height;
    float* pixels = header_pixels(header);
    for (int y = 0; y < h; ++y) {
        const Float4* row = src.row(y);
        float* dst = pixels + static_cast<size_t>(y) * header->width * kAccumulatorChannels;
        for (int x = 0; x < w; ++x) {
            dst[0] = row[x].r;
            dst[1] = row[x].g;
            dst[2] = row[x].b;
            dst[3] = row[x].a;
            dst += kAccumulatorChannels;
        }
    }
    PF_UNLOCK_HANDLE(handle);
}

// ---------------------------------------------------------------------------
//  Parameter marshalling
// ---------------------------------------------------------------------------
// The state buffer is raw storage sized by the spec, but the effect's struct is
// trivially copyable, so a byte-wise copy is exactly an object copy. Doing it
// this way keeps the dispatcher free of `new`/`delete` on the render path.
void fill_state(const EffectSpec& spec, PF_InData* in_data, PF_OutData* out_data,
                std::vector<unsigned char>& state) {
    state.resize(spec.state_size);
    if (spec.defaults != nullptr) spec.defaults(state.data());
    if (spec.read != nullptr) spec.read(in_data, out_data, state.data());
}

// ---------------------------------------------------------------------------
//  History checkouts
// ---------------------------------------------------------------------------
// How many past frames an effect wants. Read straight from the frame-count
// parameter so that pre-render and render agree; if the parameter cannot be
// read, fall back to one frame of history, which degrades slit scan into a
// pass-through rather than into garbage.
int history_frame_count(PF_InData* in_data, PF_OutData* out_data, A_long frame_param) {
    if (frame_param < 0) return 1;
    Param p(in_data, out_data, frame_param);
    if (!p.ok()) return 1;
    A_long frames = static_cast<A_long>(p.slider());
    if (frames < 1) frames = 1;
    if (frames > kMaxHistoryFrames) frames = kMaxHistoryFrames;
    return static_cast<int>(frames);
}

}  // namespace

// ---------------------------------------------------------------------------
//  PF_Cmd_SMART_PRE_RENDER
// ---------------------------------------------------------------------------
// The effect declares what it needs and how big its output will be. Two rules
// drive everything here:
//
//   * Every layer input the effect might possibly want has to be checked out
//     now. Render cannot ask for anything new.
//   * max_result_rect must not depend on the render request. These effects are
//     all size-preserving, so the content bounds are the layer bounds,
//     whatever rectangle AE happened to ask for.
PF_Err pre_render(const EffectSpec& spec, PF_InData* in_data, PF_OutData* out_data,
                  PF_PreRenderExtra* extra) {
    if (extra == nullptr || extra->input == nullptr || extra->output == nullptr ||
        extra->cb == nullptr) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    // Ask for the whole layer rather than the requested rectangle.
    //
    // A handful of these effects are genuinely global: the radial modes of the
    // chromatic split, the kaleidoscope's wedge geometry and the glow's streak
    // pass all need a coordinate frame anchored to the layer, not to whatever
    // sub-rectangle AE decided to request. Requesting the full layer keeps
    // pixel coordinates and layer coordinates identical, which removes an
    // entire class of off-by-an-origin bugs from the core effects (they have
    // no notion of an origin at all).
    //
    // The cost is that AE cannot crop the work to a region of interest. That
    // is a deliberate trade: correctness of the coordinate frame first, and
    // AE's own caching still avoids recomputing frames that have not changed.
    PF_RenderRequest request = extra->input->output_request;
    request.rect.left = 0;
    request.rect.top = 0;
    request.rect.right = in_data->width;
    request.rect.bottom = in_data->height;
    request.channel_mask = PF_ChannelMask_ARGB;

    PF_CheckoutResult result;
    AEFX_CLR_STRUCT(result);
    PF_Err err = extra->cb->checkout_layer(in_data->effect_ref, 0, kCheckoutInput, &request,
                                           in_data->current_time, in_data->time_step,
                                           in_data->time_scale, &result);
    if (err != PF_Err_NONE) return err;

    // The current frame is available from `result`. Past frames, if any, each
    // need their own checkout with their own id and time.
    if (spec.input == InputKind::History) {
        const int frames = history_frame_count(in_data, out_data, spec.history_frames_param);
        for (int i = 1; i <= frames; ++i) {
            PF_CheckoutResult past;
            AEFX_CLR_STRUCT(past);
            const A_long when = in_data->current_time - static_cast<A_long>(i) * in_data->time_step;
            err = extra->cb->checkout_layer(in_data->effect_ref, 0,
                                            kCheckoutHistoryBase + i, &request, when,
                                            in_data->time_step, in_data->time_scale, &past);
            // The first frame of a composition has no history before it. That
            // is normal, not an error: the effect falls back to the oldest
            // frame it was actually given.
            if (err != PF_Err_NONE) break;
            UnionLRect(&past.result_rect, &result.result_rect);
        }
    }

    // These effects never grow the layer, so the content bounds are the layer
    // bounds regardless of which frames were checked out.
    extra->output->max_result_rect.left = 0;
    extra->output->max_result_rect.top = 0;
    extra->output->max_result_rect.right = in_data->width;
    extra->output->max_result_rect.bottom = in_data->height;
    extra->output->result_rect = extra->output->max_result_rect;
    extra->output->solid = FALSE;
    // The output is the whole layer even when AE asked for less, so it has to
    // say so.
    extra->output->flags = PF_RenderOutputFlag_RETURNS_EXTRA_PIXELS;
    extra->output->pre_render_data = nullptr;
    extra->output->delete_pre_render_data_func = nullptr;
    return PF_Err_NONE;
}

namespace {

// Converts a checked-out world into an Image using the world suite. The suite
// is acquired once per render rather than per world: PICA acquisition is not
// free and a history effect can convert two dozen worlds in one frame.
PF_Err convert_in(const WorldSuite& ws, PF_EffectWorld& world, const EffectSpec& spec,
                  Image& dst, bool* linear) {
    return world_to_image(ws, world, spec.alpha_policy, dst, linear);
}

}  // namespace

// ---------------------------------------------------------------------------
//  The render step
// ---------------------------------------------------------------------------
// Shared by the SmartFX and legacy paths. By this point `src` and `dst` are
// mgtk images, so the two paths differ only in how they obtained them.
PF_Err run_effect(const EffectSpec& spec, PF_InData* in_data, PF_OutData* out_data,
                  const Image& src, Image& dst, Image* feedback,
                  const std::vector<Image>* history, bool linear) {
    std::vector<unsigned char> state;
    fill_state(spec, in_data, out_data, state);

    EffectInputs inputs;
    inputs.src = &src;
    inputs.feedback = feedback;
    inputs.history = history;

    RenderContext ctx = make_context(in_data);
    ctx.input_linear = linear;

    dst.resize(src.width(), src.height());
    if (spec.apply == nullptr) return PF_Err_BAD_CALLBACK_PARAM;

    spec.apply(inputs, dst, state.data(), ctx);

    if (PF_ABORT(in_data) != 0) return PF_Err_INTERRUPT;
    return PF_Err_NONE;
}

// ---------------------------------------------------------------------------
//  PF_Cmd_SMART_RENDER
// ---------------------------------------------------------------------------
PF_Err smart_render(const EffectSpec& spec, PF_InData* in_data, PF_OutData* out_data,
                    PF_SmartRenderExtra* extra) {
    if (extra == nullptr || extra->cb == nullptr) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld* input_world = nullptr;
    PF_Err err = extra->cb->checkout_layer_pixels(in_data->effect_ref, kCheckoutInput,
                                                  &input_world);
    if (err != PF_Err_NONE || input_world == nullptr) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld* output_world = nullptr;
    err = extra->cb->checkout_output(in_data->effect_ref, &output_world);
    if (err != PF_Err_NONE || output_world == nullptr) {
        extra->cb->checkin_layer_pixels(in_data->effect_ref, kCheckoutInput);
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    WorldSuite ws(in_data, out_data);
    if (!ws.ok()) {
        extra->cb->checkin_layer_pixels(in_data->effect_ref, kCheckoutInput);
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    Image src;
    bool linear = false;
    err = convert_in(ws, *input_world, spec, src, &linear);

    std::vector<Image> history;
    Image feedback;
    Image* feedback_ptr = nullptr;

    if (err == PF_Err_NONE && spec.input == InputKind::History) {
        const int frames = history_frame_count(in_data, out_data, spec.history_frames_param);
        history.reserve(static_cast<size_t>(frames) + 1);
        history.push_back(Image());
        history.back() = src;  // history[0] is the current frame
        for (int i = 1; i <= frames; ++i) {
            PF_EffectWorld* past_world = nullptr;
            const PF_Err past_err = extra->cb->checkout_layer_pixels(
                in_data->effect_ref, kCheckoutHistoryBase + i, &past_world);
            if (past_err != PF_Err_NONE || past_world == nullptr) break;
            Image past;
            convert_in(ws, *past_world, spec, past, &linear);
            history.push_back(past);
            extra->cb->checkin_layer_pixels(in_data->effect_ref, kCheckoutHistoryBase + i);
        }
    }

    if (err == PF_Err_NONE && spec.input == InputKind::Feedback) {
        read_accumulator(in_data->sequence_data, feedback);
        feedback_ptr = &feedback;
    }

    Image dst;
    if (err == PF_Err_NONE) {
        err = run_effect(spec, in_data, out_data, src, dst, feedback_ptr,
                         history.empty() ? nullptr : &history, linear);
    }

    if (err == PF_Err_NONE) {
        err = image_to_world(ws, dst, spec.alpha_policy, *output_world);
    }

    if (spec.input == InputKind::Feedback && err == PF_Err_NONE) {
        // The effect wrote its new accumulator state into `feedback`; hand it
        // back to the sequence data so the next frame starts from here.
        write_accumulator(in_data->sequence_data, feedback);
    }

    extra->cb->checkin_layer_pixels(in_data->effect_ref, kCheckoutInput);
    return err;
}

// ---------------------------------------------------------------------------
//  PF_Cmd_RENDER (legacy hosts, and 8/16-bit projects)
// ---------------------------------------------------------------------------
PF_Err legacy_render(const EffectSpec& spec, PF_InData* in_data, PF_OutData* out_data,
                     PF_ParamDef* params[], PF_LayerDef* output) {
    if (params == nullptr || output == nullptr || params[0] == nullptr) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    WorldSuite ws(in_data, out_data);
    if (!ws.ok()) return PF_Err_BAD_CALLBACK_PARAM;

    Image src;
    bool linear = false;
    PF_Err err = convert_in(ws, params[0]->u.ld, spec, src, &linear);

    std::vector<Image> history;
    Image feedback;
    Image* feedback_ptr = nullptr;

    if (err == PF_Err_NONE && spec.input == InputKind::History) {
        const int frames = history_frame_count(in_data, out_data, spec.history_frames_param);
        history.reserve(static_cast<size_t>(frames) + 1);
        history.push_back(src);
        for (int i = 1; i <= frames; ++i) {
            // The legacy path has no pre-render, so a layer is checked out and
            // read in one step. params[0] is the layer the effect is applied
            // to; asking for it at an earlier time is the whole point of a
            // time-displacement effect.
            PF_ParamDef past;
            AEFX_CLR_STRUCT(past);
            const A_long when = in_data->current_time - static_cast<A_long>(i) * in_data->time_step;
            const PF_Err checkout_err = PF_CHECKOUT_PARAM(in_data, 0, when, in_data->time_step,
                                                          in_data->time_scale, &past);
            if (checkout_err != PF_Err_NONE) break;
            Image past_image;
            convert_in(ws, past.u.ld, spec, past_image, &linear);
            history.push_back(past_image);
            PF_CHECKIN_PARAM(in_data, &past);
        }
    }

    if (err == PF_Err_NONE && spec.input == InputKind::Feedback) {
        read_accumulator(in_data->sequence_data, feedback);
        feedback_ptr = &feedback;
    }

    Image dst;
    if (err == PF_Err_NONE) {
        err = run_effect(spec, in_data, out_data, src, dst, feedback_ptr,
                         history.empty() ? nullptr : &history, linear);
    }
    if (err == PF_Err_NONE) {
        err = image_to_world(ws, dst, spec.alpha_policy, *output);
    }
    if (spec.input == InputKind::Feedback && err == PF_Err_NONE) {
        write_accumulator(in_data->sequence_data, feedback);
    }
    return err;
}

// ---------------------------------------------------------------------------
//  Entry point
// ---------------------------------------------------------------------------
PF_Err dispatch_impl(const EffectSpec& spec, PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                     PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    switch (cmd) {
        case PF_Cmd_GLOBAL_SETUP: {
            out_data->my_version = PF_VERSION(MGTK_VERSION_MAJOR, MGTK_VERSION_MINOR,
                                              MGTK_VERSION_PATCH, PF_Stage_RELEASE,
                                              MGTK_BUILD_NUMBER);
            // These two assignments must produce exactly the numbers written
            // into the PiPL resource, or AE refuses to load the plug-in with
            // "the values in the pipl must correlate to the values set in the
            // plug-ins global setup call". tools/gen_pipl.cpp prints the same
            // constants, so the two cannot drift apart.
            out_data->out_flags = kOutFlags;
            if (spec.varies_per_frame) {
                out_data->out_flags |= static_cast<A_u_long>(PF_OutFlag_NON_PARAM_VARY);
            }
            out_data->out_flags2 = kOutFlags2;
            return PF_Err_NONE;
        }

        case PF_Cmd_GLOBAL_SETDOWN:
            return PF_Err_NONE;

        case PF_Cmd_PARAMS_SETUP:
            return setup_params(in_data, out_data, spec);

        case PF_Cmd_ABOUT:
            set_return_message(out_data, spec.description);
            return PF_Err_NONE;

        case PF_Cmd_SEQUENCE_SETUP:
        case PF_Cmd_SEQUENCE_RESETUP: {
            if (spec.input != InputKind::Feedback) return PF_Err_NONE;
            bool resized = false;
            return ensure_sequence(in_data, &out_data->sequence_data, in_data->width,
                                   in_data->height, &resized);
        }

        case PF_Cmd_SEQUENCE_SETDOWN:
            if (out_data->sequence_data != NULL) {
                PF_DISPOSE_HANDLE(out_data->sequence_data);
                out_data->sequence_data = NULL;
            }
            return PF_Err_NONE;

        case PF_Cmd_USER_CHANGED_PARAM: {
            // Buttons exist so the user can throw state away -- the feedback
            // accumulator in particular, which otherwise trails the layer
            // forever once it has been primed. AE does not pass the parameter
            // array here, so the pressed index comes through `extra`.
            if (spec.input != InputKind::Feedback) return PF_Err_NONE;
            if (extra == nullptr) return PF_Err_NONE;
            const auto* changed = static_cast<const PF_UserChangedParamExtra*>(extra);
            if (changed->param_index != spec.reset_param) return PF_Err_NONE;
            bool resized = false;
            // Forcing a resize is how the reset is expressed: ensure_sequence
            // zeroes the accumulator whenever the geometry or the magic does
            // not match, so poisoning the size triggers exactly that.
            if (out_data->sequence_data != NULL) {
                PF_DISPOSE_HANDLE(out_data->sequence_data);
                out_data->sequence_data = NULL;
            }
            return ensure_sequence(in_data, &out_data->sequence_data, in_data->width,
                                   in_data->height, &resized);
        }

        case PF_Cmd_SMART_PRE_RENDER:
            return pre_render(spec, in_data, out_data,
                              static_cast<PF_PreRenderExtra*>(extra));

        case PF_Cmd_SMART_RENDER:
            return smart_render(spec, in_data, out_data,
                                static_cast<PF_SmartRenderExtra*>(extra));

        case PF_Cmd_RENDER:
            return legacy_render(spec, in_data, out_data, params, output);

        default:
            // Any selector the effect does not implement must return
            // PF_Err_NONE: AE treats an error from an unknown selector as a
            // failure to load.
            return PF_Err_NONE;
    }
}

// Kept out of the header so the eight plug-in translation units stay tiny.
PF_Err dispatch(const EffectSpec& spec, PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    // AE guarantees in_data and out_data for every selector it sends, but a
    // plug-in is loaded by other hosts too, and a null dereference here would
    // be a crash inside the host with no useful diagnostic.
    if (in_data == nullptr || out_data == nullptr) return PF_Err_BAD_CALLBACK_PARAM;
    return dispatch_impl(spec, cmd, in_data, out_data, params, output, extra);
}

}  // namespace ae
}  // namespace mgtk
