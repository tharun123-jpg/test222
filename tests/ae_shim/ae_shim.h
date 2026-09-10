// =============================================================================
//  tests/ae_shim/ae_shim.h -- a working stand-in for the After Effects SDK
//                             headers.
//
//  THIS IS NOT THE AFTER EFFECTS SDK. It is a test double that declares the
//  small subset of AE's API that src/ae/ touches, with the same names, field
//  layouts and calling conventions, so that the plug-in glue can be compiled,
//  linked and *exercised* on a machine that does not have After Effects
//  installed.
//
//  What that buys:
//
//    * `make check-glue` compiles every line of src/ae/ and all eight plug-in
//      translation units, so a typo or a type error is caught here rather than
//      in the user's first SDK build.
//    * `tools/gen_pipl` can walk the real effect registry and emit the PiPL
//      resource files, which is what keeps the out-flags in those files in
//      step with what PF_Cmd_GLOBAL_SETUP reports.
//    * tests/test_ae_glue.cpp drives a fake host through the full SmartFX
//      handshake and compares what comes out of an AE world against what the
//      core produces directly.
//
//  The real build uses the real headers: `make plugins AE_SDK_ROOT=...` puts
//  the SDK's include paths first, and this directory is not on the path at all.
//
//  Field layouts below were taken from the SDK's own bindings (the
//  after-effects-sys crate, which is generated directly from AE_Effect.h), so
//  a plug-in written against these structures compiles against the real ones
//  as long as it only uses what is declared here.
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
//  Fundamental types
// ---------------------------------------------------------------------------
typedef int32_t        A_long;
typedef uint32_t       A_u_long;
typedef int16_t        A_short;
typedef uint16_t       A_u_short;
typedef char           A_char;
typedef unsigned char  A_u_char;
typedef int32_t        A_int32;
typedef void*          PF_Handle;
typedef void*          PF_ProgPtr;
typedef A_long         PF_ParamIndex;
typedef A_long         PF_Err;
typedef A_long         PF_Fixed;     // 16.16 fixed point
typedef float          PF_FpShort;
typedef double         PF_FpLong;
typedef float          PF_FPLong;
typedef A_u_char       PF_Boolean;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#define PF_MAX_CHAN8  255
#define PF_MAX_CHAN16 32768

// ---------------------------------------------------------------------------
//  Errors
// ---------------------------------------------------------------------------
enum {
    PF_Err_NONE = 0,
    PF_Err_OUT_OF_MEMORY = -8252,
    PF_Err_INTERRUPT = -8253,
    PF_Err_BAD_CALLBACK_PARAM = -8261,
    PF_Err_PARAMETER = -8262
};

// ---------------------------------------------------------------------------
//  Geometry
// ---------------------------------------------------------------------------
typedef struct {
    A_long top;
    A_long left;
    A_long bottom;
    A_long right;
} PF_LRect;

typedef struct {
    A_short top;
    A_short left;
    A_short bottom;
    A_short right;
} PF_Rect;

typedef struct {
    A_long num;
    A_long den;
} PF_RationalScale;

union PF_UnionableRect {
    PF_Rect  r;
    PF_LRect lr;
};

// ---------------------------------------------------------------------------
//  Pixels
// ---------------------------------------------------------------------------
typedef struct {
    A_u_char alpha;
    A_u_char red;
    A_u_char green;
    A_u_char blue;
} PF_Pixel8;

typedef struct {
    A_u_short alpha;
    A_u_short red;
    A_u_short green;
    A_u_short blue;
} PF_Pixel16;

typedef struct {
    PF_FpShort alpha;
    PF_FpShort red;
    PF_FpShort green;
    PF_FpShort blue;
} PF_PixelFloat;

typedef PF_Pixel8 PF_Pixel;
typedef PF_Pixel* PF_PixelPtr;

enum PF_PixelFormat {
    PF_PixelFormat_ARGB32 = 32,
    PF_PixelFormat_ARGB64 = 64,
    PF_PixelFormat_ARGB128 = 128
};

// ---------------------------------------------------------------------------
//  World
// ---------------------------------------------------------------------------
enum {
    PF_WorldFlag_NONE = 0,
    PF_WorldFlag_DEEP = 1L << 0,
    PF_WorldFlag_WRITEABLE = 1L << 1
};
typedef A_long PF_WorldFlags;

typedef struct PF_LayerDef {
    void*            reserved0;
    void*            reserved1;
    PF_WorldFlags    world_flags;
    PF_PixelPtr      data;
    A_long           rowbytes;
    A_long           width;
    A_long           height;
    PF_UnionableRect extent_hint;
    void*            platform_ref;
    A_long           reserved_long1;
    void*            reserved_long4;
    PF_RationalScale pix_aspect_ratio;
    void*            reserved_long2;
    A_long           origin_x;
    A_long           origin_y;
    A_long           reserved_long3;
    A_long           dephault;
} PF_LayerDef;

typedef PF_LayerDef PF_EffectWorld;

// ---------------------------------------------------------------------------
//  Parameter definitions
// ---------------------------------------------------------------------------
typedef A_long PF_ParamValue;

enum PF_Precision {
    PF_Precision_INTEGER = 0,
    PF_Precision_TENTHS,
    PF_Precision_HUNDREDTHS,
    PF_Precision_THOUSANDTHS
};
typedef A_short PF_ValueDisplayFlags;
enum {
    PF_ValueDisplayFlag_NONE = 0,
    PF_ValueDisplayFlag_PERCENT = 1,
    PF_ValueDisplayFlag_PIXEL = 2
};
typedef A_u_long PF_FSliderFlags;
enum { PF_FSliderFlag_NONE = 0, PF_FSliderFlag_WANT_PHASE = 1 };

typedef struct {
    PF_FpLong            value;
    PF_FpLong            phase;
    A_char               value_desc[32];
    PF_FpShort           valid_min;
    PF_FpShort           valid_max;
    PF_FpShort           slider_min;
    PF_FpShort           slider_max;
    PF_FpShort           dephault;
    PF_Precision         precision;
    PF_ValueDisplayFlags display_flags;
    PF_FSliderFlags      fs_flags;
    PF_FpShort           curve_tolerance;
    PF_Boolean           useExponent;
    PF_FpShort           exponent;
} PF_FloatSliderDef;

typedef struct {
    PF_Fixed value;
    PF_Fixed dephault;
    PF_Fixed valid_min;
    PF_Fixed valid_max;
} PF_AngleDef;

typedef struct {
    PF_ParamValue value;
    PF_Boolean    dephault;
    A_char        reserved;
    A_short       reserved1;
    union {
        const A_char* nameptr;
    } u;
} PF_CheckBoxDef;

// The real PF_ColorDef holds a union of 8/16/float pixels. Only the 8-bit
// member is used by the glue (and by the SDK's own PF_ADD_COLOR macro), so the
// shim models exactly that.
typedef struct {
    PF_Pixel8 value;
    PF_Pixel8 dephault;
} PF_ColorDef;

typedef struct {
    PF_Fixed   x_value;
    PF_Fixed   y_value;
    A_char     reserved[3];
    PF_Boolean restrict_bounds;
    PF_Fixed   x_dephault;
    PF_Fixed   y_dephault;
} PF_PointDef;

typedef struct {
    PF_ParamValue value;
    A_short       num_choices;
    A_short       dephault;
    union {
        const A_char* namesptr;
    } u;
} PF_PopupDef;

typedef struct {
    PF_ParamValue value;
    union {
        const A_char* namesptr;
    } u;
} PF_ButtonDef;

union PF_ParamDefUnion {
    PF_LayerDef       ld;
    PF_AngleDef       ad;
    PF_CheckBoxDef    bd;
    PF_ColorDef       cd;
    PF_PointDef       td;
    PF_PopupDef       pd;
    PF_FloatSliderDef fs_d;
    PF_ButtonDef      button_d;
};

enum PF_ParamType {
    PF_Param_LAYER = 0,
    PF_Param_SLIDER,
    PF_Param_FIX_SLIDER,
    PF_Param_FLOAT_SLIDER,
    PF_Param_ANGLE,
    PF_Param_CHECKBOX,
    PF_Param_COLOR,
    PF_Param_POINT,
    PF_Param_POPUP,
    PF_Param_ARBITRARY_DATA,
    PF_Param_PATH,
    PF_Param_GROUP_START,
    PF_Param_GROUP_END,
    PF_Param_BUTTON,
    PF_Param_POINT_3D
};

typedef A_long PF_ParamFlags;
enum {
    PF_ParamFlag_NONE = 0,
    PF_ParamFlag_CANNOT_TIME_VARY = 1L << 0,
    PF_ParamFlag_CANNOT_INTERP = 1L << 1,
    PF_ParamFlag_SUPERVISE = 1L << 4
};

typedef A_long PF_ParamUIFlags;
enum {
    PF_PUI_NONE = 0,
    PF_PUI_TOPIC = 1,
    PF_PUI_CONTROL = 2,
    PF_PUI_STD_CONTROL_ONLY = 4,
    PF_PUI_DISABLED = 32
};

typedef A_long PF_ChangeFlags;

typedef struct PF_ParamDef {
    // The anonymous union the SDK calls `uu`: either the parameter's id, or the
    // change flags while handling a supervision callback.
    union {
        A_long          id;
        PF_ChangeFlags  change_flags;
    } uu;
    PF_ParamUIFlags  ui_flags;
    A_short          ui_width;
    A_short          ui_height;
    PF_ParamType     param_type;
    A_char           name[32];
    PF_ParamFlags    flags;
    A_long           unused;
    PF_ParamDefUnion u;
} PF_ParamDef;

// ---------------------------------------------------------------------------
//  In / out blocks
// ---------------------------------------------------------------------------
typedef A_long PF_Field;
typedef A_long PF_ChannelMask;
enum {
    PF_ChannelMask_ALPHA = 1,
    PF_ChannelMask_RED = 2,
    PF_ChannelMask_GREEN = 4,
    PF_ChannelMask_BLUE = 8,
    PF_ChannelMask_ARGB = 15
};

enum {
    PF_LayerDefault_NONE = 0,
    PF_LayerDefault_MYSELF = -1
};

typedef A_long PF_Quality;

typedef struct PF_InData {
    void*          inter;              // PF_InteractCallbacks, unused by MGTK
    void*          utils;
    PF_ProgPtr     effect_ref;
    PF_Quality     quality;
    A_long         version;
    A_long         serial_num;
    A_long         appl_id;
    A_long         num_params;
    A_long         reserved;
    A_long         what_cpu;
    A_long         what_fpu;
    A_long         current_time;
    A_long         time_step;
    A_long         total_time;
    A_long         local_time_step;
    A_u_long       time_scale;
    PF_Field       field;
    PF_Fixed       shutter_angle;
    A_long         width;
    A_long         height;
    PF_Rect        extent_hint;
    A_long         output_origin_x;
    A_long         output_origin_y;
    PF_RationalScale downsample_x;
    PF_RationalScale downsample_y;
    PF_RationalScale pixel_aspect_ratio;
    A_long         in_flags;
    PF_Handle      global_data;
    PF_Handle      sequence_data;
    PF_Handle      frame_data;
    A_long         start_sampL;
    A_long         dur_sampL;
    A_long         total_sampL;
    void*          src_snd;
    void*          pica_basicP;
    A_long         pre_effect_source_origin_x;
    A_long         pre_effect_source_origin_y;
    PF_Fixed       shutter_phase;
} PF_InData;

// The numeric values below are NOT guesses: they were read out of Adobe's own
// headers via the after-effects-sys crate, which is a direct bindgen of
// AE_Effect.h. They matter more than usual here, because these are the numbers
// that have to be written as literals into the PiPL resource files -- see
// tools/gen_pipl.cpp. PF_OutFlag* are C enums in the SDK, not #defines, so a
// .r file containing `PF_OutFlag_USE_OUTPUT_EXTENT` would preprocess to 0.
typedef A_u_long PF_OutFlags;
enum {
    PF_OutFlag_NONE = 0,
    PF_OutFlag_NON_PARAM_VARY = 4,               // the effect varies per frame
    PF_OutFlag_USE_OUTPUT_EXTENT = 64,           // honours extent_hint / result_rect
    PF_OutFlag_PIX_INDEPENDENT = 1024,           // output pixel depends only on input pixel
    PF_OutFlag_DEEP_COLOR_AWARE = 33554432       // handles 32-bit float
};

typedef A_u_long PF_OutFlags2;
enum {
    PF_OutFlag2_NONE = 0,
    PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG = 8,
    PF_OutFlag2_DOESNT_NEED_EMPTY_PIXELS = 64,
    PF_OutFlag2_SUPPORTS_SMART_RENDER = 1024,
    PF_OutFlag2_FLOAT_COLOR_AWARE = 4096,
    PF_OutFlag2_AUTOMATIC_WIDE_TIME_INPUT = 131072
};

typedef struct PF_OutData {
    A_u_long    my_version;
    A_char      name[32];
    PF_Handle   global_data;
    A_long      num_params;
    PF_Handle   sequence_data;
    A_long      flat_sdata_size;
    PF_Handle   frame_data;
    A_long      width;
    A_long      height;
    struct {
        A_long v;
        A_long h;
    } origin;
    PF_OutFlags  out_flags;
    A_char       return_msg[256];
    A_long       start_sampL;
    A_long       dur_sampL;
    void*        dest_snd;
    PF_OutFlags2 out_flags2;
} PF_OutData;

// ---------------------------------------------------------------------------
//  Command selectors
// ---------------------------------------------------------------------------
enum PF_Cmd {
    PF_Cmd_NONE = 0,
    PF_Cmd_ABOUT,
    PF_Cmd_GLOBAL_SETUP,
    PF_Cmd_PARAMS_SETUP,
    PF_Cmd_SEQUENCE_SETUP,
    PF_Cmd_SEQUENCE_RESETUP,
    PF_Cmd_SEQUENCE_FLATTEN,
    PF_Cmd_SEQUENCE_SETDOWN,
    PF_Cmd_FRAME_SETUP,
    PF_Cmd_RENDER,
    PF_Cmd_FRAME_SETDOWN,
    PF_Cmd_GLOBAL_SETDOWN,
    PF_Cmd_DO_DIALOG,
    PF_Cmd_UPDATE_PARAMS_UI,
    PF_Cmd_USER_CHANGED_PARAM,
    PF_Cmd_SMART_PRE_RENDER,
    PF_Cmd_SMART_RENDER
};

typedef struct {
    PF_ParamIndex param_index;
} PF_UserChangedParamExtra;

// ---------------------------------------------------------------------------
//  SmartFX
// ---------------------------------------------------------------------------
typedef struct {
    PF_LRect       rect;
    PF_Field       field;
    PF_ChannelMask channel_mask;
    PF_Boolean     preserve_rgb_of_zero_alpha;
    char           unused[3];
    A_long         reserved[4];
} PF_RenderRequest;

typedef struct {
    PF_LRect          result_rect;
    PF_LRect          max_result_rect;
    PF_RationalScale  par;
    PF_Boolean        solid;
    PF_Boolean        reservedB[3];
    A_long            ref_width;
    A_long            ref_height;
    A_long            reserved[6];
} PF_CheckoutResult;

typedef A_short PF_RenderOutputFlags;
enum {
    PF_RenderOutputFlag_RETURNS_EXTRA_PIXELS = 1,
    PF_RenderOutputFlag_GPU_RENDER_POSSIBLE = 2
};

typedef struct {
    PF_RenderRequest     output_request;
    A_short              bitdepth;
    const void*          gpu_data;
    A_long               what_gpu;
    A_u_long             device_index;
} PF_PreRenderInput;

typedef void (*PF_DeletePreRenderDataFunc)(void* pre_render_data);

typedef struct {
    PF_LRect                  result_rect;
    PF_LRect                  max_result_rect;
    PF_Boolean                solid;
    PF_Boolean                reserved;
    PF_RenderOutputFlags      flags;
    void*                     pre_render_data;
    PF_DeletePreRenderDataFunc delete_pre_render_data_func;
} PF_PreRenderOutput;

typedef struct {
    PF_Err (*checkout_layer)(PF_ProgPtr effect_ref, PF_ParamIndex index, A_long checkout_idL,
                             const PF_RenderRequest* req, A_long what_time, A_long time_step,
                             A_u_long time_scale, PF_CheckoutResult* checkout_result);
    PF_Err (*GuidMixInPtr)(PF_ProgPtr effect_ref, A_u_long buf_sizeLu, const void* buf);
} PF_PreRenderCallbacks;

typedef struct {
    PF_PreRenderInput*   input;
    PF_PreRenderOutput*  output;
    PF_PreRenderCallbacks* cb;
} PF_PreRenderExtra;

typedef struct {
    PF_RenderRequest output_request;
    A_short          bitdepth;
    void*            pre_render_data;
    const void*      gpu_data;
    A_long           what_gpu;
    A_u_long         device_index;
} PF_SmartRenderInput;

typedef struct {
    PF_Err (*checkout_layer_pixels)(PF_ProgPtr effect_ref, A_long checkout_idL,
                                    PF_EffectWorld** pixels);
    PF_Err (*checkin_layer_pixels)(PF_ProgPtr effect_ref, A_long checkout_idL);
    PF_Err (*checkout_output)(PF_ProgPtr effect_ref, PF_EffectWorld** output);
} PF_SmartRenderCallbacks;

typedef struct {
    PF_SmartRenderInput*    input;
    PF_SmartRenderCallbacks* cb;
} PF_SmartRenderExtra;

// ---------------------------------------------------------------------------
//  Suites
// ---------------------------------------------------------------------------
#define kPFWorldSuite "PF World Suite"
#define kPFWorldSuiteVersion2 2

typedef struct {
    PF_Err (*PF_GetPixelFormat)(PF_EffectWorld* world, PF_PixelFormat* format);
} PF_WorldSuite2;

#define kPFColorParamSuite "PF Color Param Suite"
#define kPFColorParamSuiteVersion1 1

typedef struct {
    PF_Err (*PF_GetColor)(PF_ProgPtr effect_ref, PF_ParamDef* param, PF_PixelFloat* color);
} PF_ColorParamSuite1;

// ---------------------------------------------------------------------------
//  Callbacks and macros
// ---------------------------------------------------------------------------
PF_Err shim_acquire_suite(PF_InData* in_data, PF_OutData* out_data, const char* name,
                          A_long version, void** suite);
PF_Err shim_release_suite(PF_InData* in_data, const char* name, A_long version);
PF_Err shim_add_param(PF_InData* in_data, A_long index, PF_ParamDef* def);
PF_Err shim_checkout_param(PF_InData* in_data, A_long index, A_long time, A_long time_step,
                           A_u_long time_scale, PF_ParamDef* def);
PF_Err shim_checkin_param(PF_InData* in_data, PF_ParamDef* def);
PF_Err shim_progress(PF_InData* in_data, A_long current, A_long total);
PF_Err shim_abort(PF_InData* in_data);

#define AEFX_AcquireSuite(in_data, out_data, name, version, msg, suite) \
    shim_acquire_suite((in_data), (out_data), (name), (version), (void**)(suite))
#define AEFX_ReleaseSuite(in_data, name, version) \
    shim_release_suite((in_data), (name), (version))

#define PF_ADD_PARAM(in_data, index, def) shim_add_param((in_data), (index), (def))
#define PF_CHECKOUT_PARAM(in_data, index, time, step, scale, def) \
    shim_checkout_param((in_data), (index), (time), (step), (scale), (def))
#define PF_CHECKIN_PARAM(in_data, def) shim_checkin_param((in_data), (def))
#define PF_PROGRESS(in_data, current, total) shim_progress((in_data), (current), (total))
#define PF_ABORT(in_data) shim_abort(in_data)

#define AEFX_CLR_STRUCT(x) std::memset(&(x), 0, sizeof(x))
#define PF_STRCPY(dst, src) \
    std::strncpy((dst), (src), sizeof(dst) - 1)
#define FLOAT2FIX(x) ((PF_Fixed)((x) * 65536.0))

union PF_UnionableLRect {
    PF_LRect* lr;
    PF_Rect* r;
};
void shim_union_lrect(const PF_LRect* src, PF_LRect* dst);
#define UnionLRect(src, dst) shim_union_lrect((src), (dst))

// ---------------------------------------------------------------------------
//  Versioning and handles
// ---------------------------------------------------------------------------
#define PF_Stage_DEVELOP 0
#define PF_Stage_ALPHA 1
#define PF_Stage_BETA 2
#define PF_Stage_RELEASE 3
#define PF_Stage_NONE 4

#define PF_VERSION(major, minor, bugfix, stage, build)                            \
    ((A_u_long)((((A_u_long)(major)) << 19) | (((A_u_long)(minor)) << 15) |       \
                (((A_u_long)(bugfix)) << 11) | (((A_u_long)(stage)) << 9) |       \
                ((A_u_long)(build))))

#if defined(_WIN32)
#define DllExport extern "C" __declspec(dllexport)
#else
#define DllExport extern "C" __attribute__((visibility("default")))
#endif

// Handles: AE treats a sequence-data handle as an opaque block of bytes that it
// can write to disk verbatim. The shim uses malloc, which is exactly what the
// host does on modern platforms.
#define PF_NEW_HANDLE(size) std::malloc(static_cast<size_t>(size))
#define PF_DISPOSE_HANDLE(handle) std::free(handle)
#define PF_LOCK_HANDLE(handle) (handle)
#define PF_UNLOCK_HANDLE(handle) ((void)0)
