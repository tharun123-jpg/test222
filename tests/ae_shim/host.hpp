// =============================================================================
//  tests/ae_shim/host.hpp -- a stand-in After Effects host.
//
//  This is the other half of the test double: ae_shim.h declares what the
//  plug-in sees, and this drives the plug-in the way AE's render pipeline does
//  -- GlobalSetup, ParamsSetup, SequenceSetup, then either the SmartFX pair or
//  the legacy Render -- over a synthetic layer.
//
//  It exists so that the glue layer can be *run* in this repository's test
//  suite instead of only being compiled. The checks it enables are the ones
//  most likely to be wrong before the first real AE launch: the SmartFX
//  checkout protocol, the premultiply/unpremultiply round trip at every bit
//  depth, and whether a parameter id in the registry matches the one the
//  effect reads back.
// =============================================================================
#pragma once

#include "ae_shim.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "mgtk/image.hpp"

namespace aetest {

// A PF_EffectWorld plus the storage it points at. The storage is deliberately
// given a row stride larger than width * pixel size, because AE pads rows and
// an effect that assumes tightly packed pixels works in the test and then
// tears in production.
struct OwnedWorld {
    std::vector<unsigned char> bytes;
    PF_EffectWorld world{};

    void allocate(int width, int height, PF_PixelFormat format, int padding_bytes);
    void write_from(const mgtk::Image& src, PF_PixelFormat format);
    void read_into(mgtk::Image& dst, PF_PixelFormat format) const;
};

// The layer as it exists at a given time. A host that hands back the same
// pixels for every time makes a time-displacement effect untestable -- it would
// correctly produce the input -- so the fake host synthesises a moving element.
mgtk::Image frame_at(const mgtk::Image& base, A_long time, A_long time_scale);

// The fake host. One instance drives one plug-in instance.
class Host {
public:
    Host(int width, int height, PF_PixelFormat format);

    // ---- layer contents -------------------------------------------------
    void set_input(const mgtk::Image& src);
    void get_output(mgtk::Image& dst) const;
    PF_EffectWorld& input_world() { return input_.world; }
    PF_EffectWorld& output_world() { return output_.world; }

    // ---- parameters -----------------------------------------------------
    // Parameters are addressed by their disk id, exactly as the effect
    // addresses them, so a mismatch between the registry and the effect shows
    // up as a wrong result rather than as a compile error.
    void set_float(A_long id, double value);
    void set_popup(A_long id, A_long value);
    void set_checkbox(A_long id, bool value);
    void set_angle(A_long id, double degrees);
    void set_point(A_long id, double x, double y);
    void set_color(A_long id, float r, float g, float b);
    void set_defaults_from_registration();

    // ---- driving the plug-in -------------------------------------------
    using Entry = PF_Err (*)(PF_Cmd, PF_InData*, PF_OutData*, PF_ParamDef*[],
                             PF_LayerDef*, void*);

    PF_Err send(Entry entry, PF_Cmd cmd, void* extra = nullptr);

    // Runs the whole handshake the way AE would.
    PF_Err setup(Entry entry);
    PF_Err render_smart(Entry entry);
    PF_Err render_legacy(Entry entry);

    // Layer contents at an arbitrary composition time, so that an effect which
    // asks for a past frame gets a different one. Without this, slit scan and
    // feedback echo would be untestable: both would correctly return the input.
    PF_EffectWorld& world_at(A_long time, A_long checkout_id);
    void note_checkout_time(A_long checkout_id, A_long time);
    A_long checkout_time(A_long checkout_id) const;

    // What PF_CHECKOUT_PARAM returns for the input layer in the legacy render
    // path, where there is no pre-render to declare the checkout in.
    PF_ParamDef legacy_layer_at(A_long time);

    PF_InData& in_data() { return in_data_; }
    PF_OutData& out_data() { return out_data_; }
    const std::vector<PF_ParamDef>& registered() const { return registered_; }
    std::vector<PF_ParamDef>& registered_mut() { return registered_; }
    const std::vector<PF_ParamDef>& param_values() const { return param_values_; }

    // The host counts progress callbacks so a test can tell the difference
    // between an effect that polled for cancellation and one that ignored it.
    void note_progress() { ++progress_calls_; }
    bool should_abort() const {
        return abort_after_ >= 0 && progress_calls_ >= abort_after_;
    }

    // Progress and abort bookkeeping, so a test can assert the effect asked
    // the host before giving up.
    int progress_calls() const { return progress_calls_; }
    void set_abort_after(int calls) { abort_after_ = calls; }

private:
    void refresh_param_array();

    PF_InData in_data_{};
    PF_OutData out_data_{};
    OwnedWorld input_;
    OwnedWorld output_;
    PF_PixelFormat format_;
    int width_ = 0;
    int height_ = 0;

    mgtk::Image base_;                      // the layer as set by the test
    // Keyed by checkout id. Held through a unique_ptr because an OwnedWorld
    // owns a byte buffer that PF_EffectWorld points into: moving one would
    // invalidate the world's own data pointer.
    std::map<A_long, std::unique_ptr<OwnedWorld>> time_worlds_;
    std::map<A_long, A_long> checkout_times_;   // checkout id -> composition time

    std::vector<PF_ParamDef> registered_;   // what ParamsSetup added
    std::vector<PF_ParamDef> param_values_; // by disk id, what checkout returns
    std::vector<PF_ParamDef*> param_ptrs_;  // params[] as the entry point sees it
    PF_ParamDef input_param_{};             // params[0], the layer

    int progress_calls_ = 0;
    int abort_after_ = -1;
};

// A few synthetic layers used across the glue tests. They are deliberately
// awkward: semi-transparent edges, values above 1.0 and a zero-alpha region all
// appear, because those are the pixels that catch premultiply mistakes.
// Values stay inside [0,1]: an 8-bit AE world cannot hold anything else, and a
// test that compares against an unclamped source is testing the host.
mgtk::Image test_layer_gradient(int width, int height);

// Highlights above 1.0, for the 32-bit float path only. Losing these is the
// whole reason float rendering exists, so one layer has to contain them.
mgtk::Image test_layer_hdr(int width, int height);
// Alpha ramps from `min_alpha` to 1.0. The floor exists on purpose: a fully
// transparent pixel has no defined straight-alpha colour, and a test that
// compares one is testing the host, not the effect.
mgtk::Image test_layer_alpha_ramp(int width, int height, float min_alpha = 0.15f);

}  // namespace aetest
