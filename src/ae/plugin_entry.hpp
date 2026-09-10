// =============================================================================
//  src/ae/plugin_entry.hpp -- the exported entry point.
//
//  AE looks up the symbol named in the PiPL's "Entry Point" property. For an
//  effect that symbol is EffectMain, and it must be exported with C linkage
//  and the platform's default calling convention, which is what DllExport
//  (from AE_Effect.h) does.
//
//  A plug-in translation unit is then two lines:
//
//      #include "mgtk/ae/plugin_entry.hpp"
//      MGTK_EFFECT_ENTRY(feedback_echo_spec)
//
//  Everything else is shared, which is the whole reason the eight effects can
//  afford to be eight separate binaries rather than one.
// =============================================================================
#pragma once

#include "AE_Effect.h"

#include "effect_spec.hpp"

namespace mgtk {
namespace ae {

// Declared here rather than in the header the plug-ins include so that the
// entry point's signature stays the only thing a plug-in TU depends on.
// Defined in dispatcher.cpp.
PF_Err dispatch(const EffectSpec& spec, PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                PF_ParamDef* params[], PF_LayerDef* output, void* extra);

}  // namespace ae
}  // namespace mgtk

#define MGTK_EFFECT_ENTRY(spec_accessor)                                                 \
    extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data,                \
                                           PF_OutData* out_data, PF_ParamDef* params[],   \
                                           PF_LayerDef* output, void* extra) {            \
        return mgtk::ae::dispatch(mgtk::ae::spec_accessor(), cmd, in_data, out_data,      \
                                  params, output, extra);                                 \
    }
