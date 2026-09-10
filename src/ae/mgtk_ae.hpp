// =============================================================================
//  src/ae/mgtk_ae.hpp -- the bridge between the pure-C++ core and After
//                        Effects.
//
//  Everything in this header is only compiled when building the actual
//  plug-ins (`make plugins AE_SDK_ROOT=...`), because it is the only part of
//  the project that needs Adobe's headers. The core library and the entire
//  test suite build without them.
//
//  The bridge does four jobs:
//
//    1. reads parameters out of AE's PF_ParamDef union into the plain structs
//       the core consumes,
//    2. converts PF_EffectWorld pixels (premultiplied ARGB, 8/16/32 bits per
//       channel, padded rowbytes) into mgtk::Image (straight alpha, float,
//       tightly packed) and back,
//    3. runs the SmartFX two-pass protocol (pre-render checkouts, then render),
//    4. reports progress and honours the user's cancel.
//
//  Nothing here knows about any particular effect; the eight plug-ins in
//  src/ae/plugins/ are thin, and the table that drives them lives in
//  src/ae/registry.cpp.
//
//  There is deliberately no dependency on the SDK's Util/ "suite handler"
//  templates: the scoper below acquires what it needs through
//  AEFX_AcquireSuite / AEFX_ReleaseSuite, which are plain macros in
//  AE_Effect.h and are therefore available in every SDK version.
// =============================================================================
#pragma once

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"

#include "mgtk/effects.hpp"

namespace mgtk {
namespace ae {

// ---------------------------------------------------------------------------
//  Suite scoping
// ---------------------------------------------------------------------------
// RAII wrapper around AEFX_AcquireSuite / AEFX_ReleaseSuite. Holding a suite
// pointer past the command that acquired it is a classic source of crashes,
// so the pointer never escapes a scope that owns its lifetime.
class SuiteScopeBase {
public:
    SuiteScopeBase(PF_InData* in_data, PF_OutData* out_data, const char* name,
                   A_long version)
        : in_data_(in_data), out_data_(out_data), name_(name), version_(version) {}
    ~SuiteScopeBase();

    SuiteScopeBase(const SuiteScopeBase&) = delete;
    SuiteScopeBase& operator=(const SuiteScopeBase&) = delete;

    bool ok() const { return suite_ != nullptr; }

protected:
    PF_InData* in_data_ = nullptr;
    PF_OutData* out_data_ = nullptr;
    const char* name_ = nullptr;
    A_long version_ = 0;
    void* suite_ = nullptr;
};

template <typename SuiteT>
class SuiteScope : public SuiteScopeBase {
public:
    SuiteScope(PF_InData* in_data, PF_OutData* out_data, const char* name, A_long version)
        : SuiteScopeBase(in_data, out_data, name, version) {
        void* acquired = nullptr;
        const PF_Err err = AEFX_AcquireSuite(in_data, out_data, name, version,
                                             "MGTK could not acquire a required suite",
                                             &acquired);
        if (err == PF_Err_NONE) suite_ = acquired;
    }

    SuiteT* operator->() const { return static_cast<SuiteT*>(suite_); }
};

// ---------------------------------------------------------------------------
//  Parameter access
// ---------------------------------------------------------------------------
// A checked-out effect parameter.
//
// PF_CHECKOUT_PARAM must be balanced by PF_CHECKIN_PARAM on every path,
// including error paths: an unbalanced checkout keeps a reference to a cached
// frame alive, which shows up as AE running out of memory several hundred
// frames into a render. Bundling the two into one object means the check-in
// cannot be forgotten.
//
// The same class is used from PF_Cmd_RENDER and PF_Cmd_SMART_RENDER. SmartFX
// does not pass the parameter array at all, so checking out is the only way to
// read a non-layer parameter there; doing it that way everywhere keeps one
// code path instead of two.
class Param {
public:
    Param(PF_InData* in_data, PF_OutData* out_data, A_long index);
    ~Param();

    Param(const Param&) = delete;
    Param& operator=(const Param&) = delete;

    bool ok() const { return ok_; }

    // The union member has to match the type the parameter was registered as;
    // AE does not type-check this for you. Each accessor is named after the
    // parameter type it expects, so a mismatch is visible at the call site.
    float slider() const;         // PF_Param_FLOAT_SLIDER
    float angle_degrees() const;  // PF_Param_ANGLE (fixed-point degrees)
    A_long popup() const;         // PF_Param_POPUP
    bool checkbox() const;        // PF_Param_CHECKBOX
    // PF_Param_COLOR. `fallback` is returned when the host cannot supply
    // PF_ColorParamSuite1, which is the only way to read a colour without
    // depending on how this SDK generation lays out PF_ColorDef.
    Float4 color(Float4 fallback) const;
    Vec2 point() const;           // PF_Param_POINT, in layer pixels

private:
    PF_InData* in_data_ = nullptr;
    PF_OutData* out_data_ = nullptr;
    PF_ParamDef def_;
    bool ok_ = false;
};

// ---------------------------------------------------------------------------
//  Pixel format
// ---------------------------------------------------------------------------
// PF_WorldSuite2 is the only supported way to ask what is actually in a
// world's buffer. (The docs are explicit that effects must be able to handle
// 8, 16 and 32 bit worlds, and that AE hands out worlds whose depth it decided
//  based on what the effect claimed -- so guessing is not an option.)
class WorldSuite : public SuiteScopeBase {
public:
    WorldSuite(PF_InData* in_data, PF_OutData* out_data);

    PF_Err format_of(PF_EffectWorld& world, PF_PixelFormat& format) const;
};

// ---------------------------------------------------------------------------
//  World <-> Image
// ---------------------------------------------------------------------------
// AE hands effects premultiplied pixels; the core works in straight alpha, so
// the conversion has to undo and redo the premultiply. That is not a detail:
// blurring premultiplied pixels drags the matte into the colour and produces
// the classic dark halo around anything semi-transparent.
//
// `input_is_linear` comes back set for 32-bit float worlds, which AE keeps in
// the project's linear working space. Tone-sensitive effects use it to judge
// thresholds in a perceptual space, so that a Threshold of 1.0 means the same
// thing in an 8-bit project as it does in a 32-bit one.
PF_Err world_to_image(const WorldSuite& ws, PF_EffectWorld& world, const AlphaPolicy policy,
                      Image& dst, bool* input_is_linear);
PF_Err image_to_world(const WorldSuite& ws, const Image& src, const AlphaPolicy policy,
                      PF_EffectWorld& world);

// ---------------------------------------------------------------------------
//  Host services
// ---------------------------------------------------------------------------
// A RenderContext wired to PF_PROGRESS / PF_ABORT. `user_data` is the
// PF_InData pointer.
RenderContext make_context(PF_InData* in_data);

// Copies `text` into out_data->return_msg, truncating safely. PF_STRCPY is
// provided by AE but does not bound the destination, and return_msg is a
// fixed 256-byte field, so this does the bounds check.
void set_return_message(PF_OutData* out_data, const char* text);

}  // namespace ae
}  // namespace mgtk
