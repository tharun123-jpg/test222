// ===========================================================================
//  motiongraphicstoolkit_slitscan.r
//
//  GENERATED FILE -- do not edit. Edit src/ae/registry.cpp and run
//  `make pipl` (or ./build/gen_pipl resources/pipl) instead.
//
//  The out-flag values below are literals on purpose: the PF_OutFlag_*
//  names are C enums, so writing them here would expand to 0 and AE would
//  refuse to load the plug-in. They are printed from the same constants
//  PF_Cmd_GLOBAL_SETUP assigns, which is what keeps the two in step.
//
//  Resource files are stored big-endian: one .r serves both platforms, and
//  Windows converts it with pipltool.exe as part of the build.
// ===========================================================================
#include "AEConfig.h"
#include "AE_EffectVers.h"

resource 'PiPL' (16000) {
    {
        Kind { AEEffect },
        Name { "MGTK Slit Scan" },
        Category { "Motion Graphics Toolkit" },
#ifdef AE_OS_WIN
        AE_Effect_Windows_Entry_Point { "EffectMain" },
#else
        AE_Effect_Mac_Entry_Point { "EffectMain" },
#endif
        AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
        AE_Effect_Version { 525825 },
        AE_Effect_Info_Flags { 0 },
        AE_Effect_Global_OutFlags { 0x02000044 },
        AE_Effect_Global_OutFlags_2 { 0x00001400 },
        AE_Effect_Match_Name { "MotionGraphicsToolkit_SlitScan" },
        AE_Reserved_Info { 0 }
    }
};
