#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when operator-facing C ABI changes in incompatible ways. */
#define VIVID_OPERATOR_ABI_VERSION 9u

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
    VIVID_PARAM_TEXT  = 4,
} VividParamType;

typedef enum VividDisplayHint {
    VIVID_DISPLAY_DEFAULT = 0,   // full-width slider (current behavior)
    VIVID_DISPLAY_KNOB    = 1,   // circular knob widget
    VIVID_DISPLAY_XY_PAD  = 2,   // 2D axis pad (pair consecutive x/y params)
    VIVID_DISPLAY_COLOR   = 3,   // color swatch + popup (triple consecutive r/g/b params)
} VividDisplayHint;

// Channel kinds — reflect runtime routing mechanisms.
// ABI v9: collapsed from 15 values to 7.
typedef enum VividPortType {
    VIVID_PORT_FLOAT          = 0,  // float value (control_float/int/bool all route identically)
    VIVID_PORT_AUDIO          = 1,  // audio sample buffer
    VIVID_PORT_SPREAD         = 2,  // variable-length float array
    VIVID_PORT_STRING         = 3,  // UTF-8 string
    VIVID_PORT_STRING_SPREAD  = 4,  // variable-length string array
    VIVID_PORT_TEXTURE        = 5,  // WGPUTextureView
    VIVID_PORT_HANDLE         = 6,  // typed opaque pointer (void*), type-safe via handle_type_id
} VividPortType;

// Backward-compatible aliases (deprecated, remove in v10)
#define VIVID_PORT_CONTROL_FLOAT         VIVID_PORT_FLOAT
#define VIVID_PORT_CONTROL_INT           VIVID_PORT_FLOAT
#define VIVID_PORT_CONTROL_BOOL          VIVID_PORT_FLOAT
#define VIVID_PORT_AUDIO_FLOAT           VIVID_PORT_AUDIO
#define VIVID_PORT_CONTROL_SPREAD        VIVID_PORT_SPREAD
#define VIVID_PORT_GPU_TEXTURE           VIVID_PORT_TEXTURE
#define VIVID_PORT_CONTROL_STRING        VIVID_PORT_STRING
#define VIVID_PORT_CONTROL_STRING_SPREAD VIVID_PORT_STRING_SPREAD
#define VIVID_PORT_DATA                  VIVID_PORT_HANDLE
#define VIVID_PORT_GPU_BUFFER            VIVID_PORT_HANDLE
#define VIVID_PORT_GPU_MESH              VIVID_PORT_HANDLE
#define VIVID_PORT_GPU_COMPUTE           VIVID_PORT_HANDLE
#define VIVID_PORT_MEDIA_STREAM          VIVID_PORT_HANDLE
#define VIVID_PORT_MEDIA_CLOCK           VIVID_PORT_HANDLE
#define VIVID_PORT_MIDI                  VIVID_PORT_HANDLE

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
    const char*    default_string;  /* default value for VIVID_PARAM_FILE/TEXT, NULL otherwise */

    /* inspector layout metadata */
    const char*       group;              /* NULL = ungrouped */
    VividDisplayHint  display_hint;       /* VIVID_DISPLAY_DEFAULT if unset */
    uint8_t           layout_columns;     /* 0 = full-width (1 column), 2 = half-width, etc. */
    uint8_t           layout_column_index;/* 0-based column position within a multi-column row */

    /* optional semantic metadata (for tooling/introspection) */
    const char*       semantic_tag;       /* e.g. "frequency_hz", "gate", "color_rgba" */
    const char*       semantic_shape;     /* e.g. "scalar", "vec2", "color", "event" */
    const char*       semantic_unit;      /* e.g. "Hz", "s", "dB" */
    const char*       semantic_intent;    /* free-form hint, e.g. "input_gain" */
} VividParamDescriptor;

typedef struct VividPortDescriptor {
    const char*        name;
    VividPortType      type;
    VividPortDirection direction;
    uint32_t           handle_type_id;  // non-zero when type == VIVID_PORT_HANDLE (FNV-1a of C++ type)
} VividPortDescriptor;

typedef struct VividOperatorDescriptor {
    const char*               name;
    VividDomain               domain;
    uint32_t                  param_count;
    const VividParamDescriptor* params;
    uint32_t                  port_count;
    const VividPortDescriptor*  ports;
    int                       time_dependent;  // 1 if operator reads ctx->time, 0 otherwise
    int                       has_process_audio; // 1 if operator inherits AudioOperatorBase
    int                       has_process_gpu;   // 1 if operator inherits GpuOperatorBase
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

typedef struct VividStringSpreadPort {
    const char** data;    // pointer to array of UTF-8 string pointers
    uint32_t length;      // current number of strings
    uint32_t capacity;    // allocated size (for output ports)
} VividStringSpreadPort;

typedef struct VividSharedHandleEntry {
    const char* type;
    uint64_t generation;
    void* payload;
    uint8_t valid;
} VividSharedHandleEntry;

typedef struct VividSharedHandleService {
    uint64_t (*create)(const char* type, void* payload, uint64_t generation);
    uint8_t  (*retain)(uint64_t id);
    uint8_t  (*release)(uint64_t id);
    uint8_t  (*invalidate)(uint64_t id, uint64_t generation);
    VividSharedHandleEntry (*resolve)(uint64_t id);
} VividSharedHandleService;

// ---------------------------------------------------------------------------
// Audio process context — passed to audio operators on the audio thread
// ---------------------------------------------------------------------------

typedef struct VividAudioContext {
    double    time;
    double    delta_time;
    uint64_t  frame;
    float*    param_values;
    // Audio-specific
    float**   input_buffers;    // [port_idx][sample]
    float**   output_buffers;   // [port_idx][sample]
    uint32_t  buffer_size;      // 256
    uint32_t  sample_rate;      // 48000
    // Cross-domain inputs from control
    VividSpreadPort*  input_spreads;
    VividSpreadPort*  output_spreads;
    void**            input_handles;
    uint32_t          input_handle_count;
    const char**      input_string_values;
    const char**      file_param_values;
    uint32_t          file_param_count;
    const VividSharedHandleService* shared_handles;
} VividAudioContext;

// ---------------------------------------------------------------------------
// Control process context — passed to control operators on the main thread
// ---------------------------------------------------------------------------

typedef struct VividProcessContext {
    // ---- Runtime-provided: read-only inputs --------------------------------
    double    time;
    double    delta_time;
    uint64_t  frame;
    float*    param_values;   // indexed by param descriptor order
    float*    input_values;   // indexed by input port order (VIVID_PORT_INPUT only)
    float*    output_values;  // indexed by output port order (VIVID_PORT_OUTPUT only)
    VividSpreadPort* input_spreads;   // [spread_port_ordinal], NULL if none
    VividSpreadPort* output_spreads;  // [spread_port_ordinal], NULL if none
    void**     input_handles;         // [handle_port_ordinal], NULL if none
    uint32_t   input_handle_count;    // number of HANDLE input ports
    void**     output_handles;        // [handle_port_ordinal], NULL if none
    uint32_t   output_handle_count;   // number of HANDLE output ports
    const char** input_string_values;   // [string_port_ordinal]
    const char** output_string_values;  // [string_port_ordinal]
    VividStringSpreadPort* input_string_spreads;   // [string_spread_port_ordinal], NULL if none
    VividStringSpreadPort* output_string_spreads;  // [string_spread_port_ordinal], NULL if none
    const char** file_param_values;   // indexed by file param order, NULL if none
    uint32_t     file_param_count;
    void*     input;          // VividInputState* for interactive operators, NULL otherwise
    const VividSharedHandleService* shared_handles; // runtime-owned process-wide handle service

    // ---- Operator write-back: operator sets these during process() ---------
    // The runtime reads them after process() returns and acts accordingly.
    // preferred_tex_*: set to request a texture reallocation next frame.
    // Leave as 0 to keep the current size (no action taken).
    uint32_t  preferred_tex_width;
    uint32_t  preferred_tex_height;
} VividProcessContext;

// Forward declaration — full definition in gpu_operator.h (requires WebGPU types)
struct VividGpuContext;

// ---------------------------------------------------------------------------
// Function pointer typedefs (dlopen entry points)
// ---------------------------------------------------------------------------

typedef const VividOperatorDescriptor* (*VividDescriptorFn)(void);
typedef uint32_t (*VividAbiVersionFn)(void);
typedef void*  (*VividCreateFn)(void);
typedef void   (*VividDestroyFn)(void* instance);
typedef void   (*VividProcessFn)(void* instance, VividProcessContext* ctx);
typedef void   (*VividProcessAudioFn)(void* instance, VividAudioContext* ctx);
typedef void   (*VividProcessGpuFn)(void* instance, struct VividGpuContext* ctx);

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
