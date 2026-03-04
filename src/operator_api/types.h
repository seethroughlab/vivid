#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

typedef enum VividDomain {
    VIVID_DOMAIN_CONTROL = 0,
    VIVID_DOMAIN_AUDIO   = 1,
    VIVID_DOMAIN_GPU     = 2,
} VividDomain;

typedef enum VividParamType {
    VIVID_PARAM_FLOAT = 0,
    VIVID_PARAM_INT   = 1,
    VIVID_PARAM_BOOL  = 2,
    VIVID_PARAM_FILE  = 3,
} VividParamType;

typedef enum VividDisplayHint {
    VIVID_DISPLAY_DEFAULT = 0,   // full-width slider (current behavior)
    VIVID_DISPLAY_KNOB    = 1,   // circular knob widget
    VIVID_DISPLAY_XY_PAD  = 2,   // 2D axis pad (pair consecutive x/y params)
    VIVID_DISPLAY_COLOR   = 3,   // color swatch + popup (triple consecutive r/g/b params)
} VividDisplayHint;

typedef enum VividPortType {
    VIVID_PORT_CONTROL_FLOAT  = 0,
    VIVID_PORT_CONTROL_INT    = 1,
    VIVID_PORT_CONTROL_BOOL   = 2,
    VIVID_PORT_AUDIO_FLOAT    = 3,
    VIVID_PORT_CONTROL_SPREAD = 4,
    VIVID_PORT_GPU_TEXTURE    = 5,
    VIVID_PORT_DATA           = 6,  // package-defined opaque pointer type
} VividPortType;

typedef enum VividPortDirection {
    VIVID_PORT_INPUT  = 0,
    VIVID_PORT_OUTPUT = 1,
} VividPortDirection;

// ---------------------------------------------------------------------------
// Descriptors
// ---------------------------------------------------------------------------

typedef struct VividParamDescriptor {
    const char*    name;
    VividParamType type;
    float          default_value;
    float          min_value;
    float          max_value;
    const char**   choice_labels;   /* NULL if not an enum, else array of choice_count strings */
    uint32_t       choice_count;    /* 0 for regular params */
    const char*    default_string;  /* default value for VIVID_PARAM_FILE, NULL otherwise */

    /* inspector layout metadata */
    const char*       group;              /* NULL = ungrouped */
    VividDisplayHint  display_hint;       /* VIVID_DISPLAY_DEFAULT if unset */
    uint8_t           layout_columns;     /* 0 = full-width (1 column), 2 = half-width, etc. */
    uint8_t           layout_column_index;/* 0-based column position within a multi-column row */
} VividParamDescriptor;

typedef struct VividPortDescriptor {
    const char*        name;
    VividPortType      type;
    VividPortDirection direction;
    const char*        data_type;  // non-NULL when type == VIVID_PORT_DATA (e.g. "gpu_scene")
} VividPortDescriptor;

typedef struct VividOperatorDescriptor {
    const char*               name;
    VividDomain               domain;
    uint32_t                  param_count;
    const VividParamDescriptor* params;
    uint32_t                  port_count;
    const VividPortDescriptor*  ports;
    int                       time_dependent;  // 1 if operator reads ctx->time, 0 otherwise
} VividOperatorDescriptor;

// ---------------------------------------------------------------------------
// Input events — mouse, keyboard, scroll for interactive operators
// ---------------------------------------------------------------------------

typedef enum VividInputEventType {
    VIVID_INPUT_MOUSE_MOVE   = 0,
    VIVID_INPUT_MOUSE_BUTTON = 1,
    VIVID_INPUT_MOUSE_SCROLL = 2,
    VIVID_INPUT_KEY          = 3,
    VIVID_INPUT_CHAR         = 4,
} VividInputEventType;

typedef struct VividInputEvent {
    VividInputEventType type;
    float    mouse_x, mouse_y;     /* normalized [0,1] texture coords */
    int      button;               /* 0=left, 1=right, 2=middle */
    int      action;               /* 0=release, 1=press, 2=repeat */
    float    scroll_dx, scroll_dy;
    int      key;                  /* GLFW key code */
    int      scancode;
    uint32_t codepoint;            /* Unicode (CHAR events) */
    int      modifiers;            /* bitmask: 1=shift, 2=ctrl, 4=alt, 8=super */
} VividInputEvent;

typedef struct VividInputState {
    const VividInputEvent* events;
    uint32_t event_count;
    float mouse_x, mouse_y;    /* current position (normalized [0,1]) */
    int   buttons_held;        /* bitmask: bit 0=left, 1=right, 2=middle */
    int   modifiers;
} VividInputState;

// ---------------------------------------------------------------------------
// Spread port — variable-length float array
// ---------------------------------------------------------------------------

typedef struct VividSpreadPort {
    float*   data;      // pointer to spread data
    uint32_t length;    // current number of floats
    uint32_t capacity;  // allocated size (for output ports)
} VividSpreadPort;

// ---------------------------------------------------------------------------
// Process context — passed each tick
// ---------------------------------------------------------------------------

typedef struct VividProcessContext {
    double    time;
    double    delta_time;
    uint64_t  frame;
    float*    param_values;   // indexed by param descriptor order
    float*    input_values;   // indexed by input port order (VIVID_PORT_INPUT only)
    float*    output_values;  // indexed by output port order (VIVID_PORT_OUTPUT only)
    void*     gpu;            // VividGpuState* for GPU operators, NULL otherwise
    void*     audio;          // VividAudioState* for audio operators, NULL otherwise
    VividSpreadPort* input_spreads;   // [input_port_idx], NULL if none
    VividSpreadPort* output_spreads;  // [output_port_idx], NULL if none
    const char** file_param_values;   // indexed by file param order, NULL if none
    uint32_t     file_param_count;
    uint32_t  preferred_tex_width;   // operator writes non-zero to request resize
    uint32_t  preferred_tex_height;  // 0 = no preference (keep current)
    void*     input;          // VividInputState* for GPU operators when UI hidden, NULL otherwise
} VividProcessContext;

// ---------------------------------------------------------------------------
// Function pointer typedefs (dlopen entry points)
// ---------------------------------------------------------------------------

typedef const VividOperatorDescriptor* (*VividDescriptorFn)(void);
typedef void*  (*VividCreateFn)(void);
typedef void   (*VividDestroyFn)(void* instance);
typedef void   (*VividProcessFn)(void* instance, const VividProcessContext* ctx);

// ---------------------------------------------------------------------------
// Thumbnail context — optional custom thumbnail rendering
// ---------------------------------------------------------------------------

typedef struct VividThumbnailContext {
    uint8_t*  pixels;         // RGBA8 buffer (pre-allocated, row-major)
    uint32_t  width;          // 140
    uint32_t  height;         // 88
    uint32_t  stride;         // width * 4
    double    time;
    float*    output_values;
    uint32_t  output_count;
    float*    param_values;
    uint32_t  param_count;
} VividThumbnailContext;

typedef void (*VividDrawThumbnailFn)(void* instance, const VividThumbnailContext* ctx);

// Optional main-thread update hook for operators that need non-audio-thread work
// (e.g. AVFoundation decoding, file I/O, ring buffer pre-fill)
typedef void (*VividMainThreadUpdateFn)(void* instance, double time,
                                        const char** file_param_values,
                                        uint32_t file_param_count);

#ifdef __cplusplus
}
#endif
