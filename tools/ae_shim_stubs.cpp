// =============================================================================
//  tools/ae_shim_stubs.cpp -- link-time stubs for gen_pipl.
//
//  gen_pipl reads the effect registry, and the registry's parameter readers go
//  through mgtk::ae::Param, which calls into the host. The tool never actually
//  reads a parameter -- it only walks the table's metadata -- so the host calls
//  are satisfied here by functions that do nothing.
//
//  When gen_pipl is built against the real After Effects SDK headers these
//  stubs are not compiled at all: the real headers declare these names as the
//  SDK's own macros, and nothing here is reachable.
// =============================================================================
#include "AE_Effect.h"

PF_Err shim_acquire_suite(PF_InData*, PF_OutData*, const char*, A_long, void** suite) {
    if (suite != nullptr) *suite = nullptr;
    return PF_Err_BAD_CALLBACK_PARAM;
}

PF_Err shim_release_suite(PF_InData*, const char*, A_long) { return PF_Err_NONE; }

PF_Err shim_add_param(PF_InData*, A_long, PF_ParamDef*) { return PF_Err_NONE; }

PF_Err shim_checkout_param(PF_InData*, A_long, A_long, A_long, A_u_long, PF_ParamDef*) {
    return PF_Err_BAD_CALLBACK_PARAM;
}

PF_Err shim_checkin_param(PF_InData*, PF_ParamDef*) { return PF_Err_NONE; }

PF_Err shim_progress(PF_InData*, A_long, A_long) { return PF_Err_NONE; }

PF_Err shim_abort(PF_InData*) { return 0; }

void shim_union_lrect(const PF_LRect* src, PF_LRect* dst) {
    if (src == nullptr || dst == nullptr) return;
    *dst = *src;
}
