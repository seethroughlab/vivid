#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when operator-facing C ABI changes in incompatible ways. */
#define VIVID_OPERATOR_ABI_VERSION 6u

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
    VIVID_DISPLAY_HIDDEN  = 4,   // param exists but is not drawn by standard inspector
} VividDisplayHint;

// Channel kinds — reflect the logical data type on a port.
typedef uint32_t VividPortType;

#define VIVID_PORT_FLOAT          0u  // float value (control_float/int/bool all route identically)
#define VIVID_PORT_AUDIO          1u  // audio sample buffer
#define VIVID_PORT_SPREAD         2u  // variable-length float array
#define VIVID_PORT_STRING         3u  // UTF-8 string
#define VIVID_PORT_STRING_SPREAD  4u  // variable-length string array
#define VIVID_PORT_TEXTURE        5u  // WGPUTextureView

typedef enum VividPortDirection {
    VIVID_PORT_INPUT  = 0,
    VIVID_PORT_OUTPUT = 1,
} VividPortDirection;

typedef enum VividPortTransport {
    VIVID_PORT_TRANSPORT_SCALAR        = 0, // float-like main-thread copy
    VIVID_PORT_TRANSPORT_AUDIO_BUFFER  = 1, // audio sample buffers
    VIVID_PORT_TRANSPORT_SPREAD        = 2, // float spread copy
    VIVID_PORT_TRANSPORT_STRING        = 3, // string copy
    VIVID_PORT_TRANSPORT_STRING_SPREAD = 4, // string spread copy
    VIVID_PORT_TRANSPORT_TEXTURE       = 5, // GPU texture/view routing
    VIVID_PORT_TRANSPORT_CUSTOM_VALUE  = 6, // memcpy-by-value snapshot
    VIVID_PORT_TRANSPORT_CUSTOM_REF    = 7, // opaque shared-handle/reference
} VividPortTransport;

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
    VividPortTransport transport;
    uint32_t           payload_size; // 0 for built-in types
    const char*        type_name;    // C++ type name, NULL for built-ins
    uint8_t            channels;     // 0=auto, 1=mono, 2=stereo, etc.
    float              default_value;// default for VIVID_PORT_FLOAT inputs
    const char*        stable_type_id; // stable namespaced id for custom types, NULL for built-ins
    const char*        semantic_tag;   // e.g. "beat_phase", "gate", "trigger", "midi", NULL if unset
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
    float**   input_buffers;    // [port_idx][sample] — planar multi-channel: ch c at [c * buffer_size]
    float**   output_buffers;   // [port_idx][sample] — planar multi-channel: ch c at [c * buffer_size]
    uint32_t  buffer_size;      // 256
    uint32_t  sample_rate;      // 48000
    // Per-port channel counts (resolved by runtime)
    const uint8_t* input_channel_counts;   // [port_idx] — NULL when all mono
    const uint8_t* output_channel_counts;  // [port_idx] — NULL when all mono
    // Cross-domain inputs from control
    VividSpreadPort*  input_spreads;
    VividSpreadPort*  output_spreads;
    void**            custom_inputs;       // [custom_input_ordinal] — opaque custom-type inputs
    uint32_t          custom_input_count;  // number of custom-transport input ports
    const char**      input_string_values;
    float*            input_float_values;   // [float_input_ordinal] — CV inputs from control domain
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
    VividSpreadPort* input_spreads;    // [spread_port_ordinal], NULL if none
    VividSpreadPort* output_spreads;   // [spread_port_ordinal], NULL if none
    void**     custom_inputs;          // [custom_input_ordinal], NULL if none
    uint32_t   custom_input_count;     // number of custom-transport input ports
    void**     custom_outputs;         // [custom_output_ordinal], NULL if none
    uint32_t   custom_output_count;    // number of custom-transport output ports
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

// ---------------------------------------------------------------------------
// Inspector context — optional custom inspector rendering (draw_inspector)
// ---------------------------------------------------------------------------

typedef enum VividInspectorMode {
    VIVID_INSPECTOR_STANDARD = 0,  // core draws standard params first, operator draws below
    VIVID_INSPECTOR_FULL     = 1,  // operator handles entire inspector (no standard params)
} VividInspectorMode;

typedef struct VividColor { float r, g, b, a; } VividColor;

typedef struct VividInspectorDrawAPI {
    void* opaque;  // Renderer2D* — operators must not dereference
    void  (*draw_rect)(void*, float x, float y, float w, float h, VividColor);
    void  (*draw_rounded_rect)(void*, float x, float y, float w, float h, float radius, VividColor);
    void  (*draw_text)(void*, float x, float y, const char* text, VividColor, float scale);
    void  (*draw_line)(void*, float x1, float y1, float x2, float y2, float thickness, VividColor);
    float (*text_width)(void*, const char* text, float scale);
    float (*line_height)(void*);
    void  (*push_clip_rect)(void*, float x, float y, float w, float h);
    void  (*pop_clip_rect)(void*);
} VividInspectorDrawAPI;

typedef struct VividInspectorCommandAPI {
    void* opaque;  // scoped to node_id by the core
    void (*set_param)(void*, const char* param_name, float value);
    void (*set_string_param)(void*, const char* param_name, const char* value);
} VividInspectorCommandAPI;

typedef struct VividInspectorTheme {
    VividColor bg, accent, dim_text, bright_text, separator;
    VividColor dark_bg, slider_fill, slider_track;
    float corner_radius;
} VividInspectorTheme;

typedef struct VividInspectorMouse {
    float x, y;           // relative to inspector content area origin
    float prev_x, prev_y;
    int left_down, left_clicked, left_released, right_clicked, shift_down;
} VividInspectorMouse;

typedef struct VividInspectorKeyEvent {
    int key;     // GLFW key code
    int action;  // 0=release, 1=press, 2=repeat
    int mods;    // bitmask
} VividInspectorKeyEvent;

typedef struct VividInspectorContext {
    // Layout (absolute screen coords, scroll-adjusted)
    float content_x, content_y, content_width;

    // Drawing & commands
    VividInspectorDrawAPI    draw;
    VividInspectorCommandAPI commands;
    VividInspectorTheme      theme;

    // Operator state (read-only)
    const float*       param_values;       uint32_t param_count;
    const float*       output_values;      uint32_t output_count;
    const char* const* string_param_values; uint32_t string_param_count;

    // Input
    VividInspectorMouse             mouse;
    const VividInspectorKeyEvent*   key_events;      uint32_t key_event_count;
    const uint32_t*                 char_events;     uint32_t char_event_count;

    double time;

    // Return: operator writes total height consumed
    float consumed_height;
    // Return: operator writes 1 if it wants keyboard focus
    int   wants_keyboard;
} VividInspectorContext;

typedef void     (*VividDrawInspectorFn)(void* instance, VividInspectorContext* ctx);
typedef uint32_t (*VividInspectorModeFn)(void);

// Optional main-thread update hook for operators that need non-audio-thread work
// (e.g. AVFoundation decoding, file I/O, ring buffer pre-fill)
typedef void (*VividMainThreadUpdateFn)(void* instance, double time,
                                        const char** file_param_values,
                                        uint32_t file_param_count);

#ifdef __cplusplus
}
#endif
