#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when operator-facing C ABI changes in incompatible ways. */
#define VIVID_OPERATOR_ABI_VERSION 17u
// v17: Renamed vivid_process entry point to vivid_process_frame.
// v16: Removed deprecated `domain` field from VividOperatorDescriptor.
// v15: Cadence-aware execution model — replaced VividDomain with VividExecutionEnv + VividCadenceCapability.
// The ABI version catches stale dylibs during hot-reload — it is not a cross-version compatibility promise.

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

// Execution environment — which executor owns the node.
typedef uint32_t VividExecutionEnv;
#define VIVID_ENV_FRAME  0u   // main thread, frame-rate (~60 Hz)
#define VIVID_ENV_AUDIO  1u   // audio thread, audio-rate (~48 kHz)
#define VIVID_ENV_GPU    2u   // main thread, GPU command submission

// Cadence capability — whether a frame-env operator can be promoted to audio-rate.
typedef uint32_t VividCadenceCapability;
#define VIVID_CADENCE_FRAME_ONLY     0u  // can only run at frame rate
#define VIVID_CADENCE_AUDIO_CAPABLE  1u  // satisfies audio-safe contract, can be promoted

typedef uint32_t VividParamType;
#define VIVID_PARAM_FLOAT  0u
#define VIVID_PARAM_INT    1u
#define VIVID_PARAM_BOOL   2u
#define VIVID_PARAM_FILE   3u
#define VIVID_PARAM_TEXT   4u

typedef uint32_t VividDisplayHint;
#define VIVID_DISPLAY_DEFAULT  0u  // full-width slider (current behavior)
#define VIVID_DISPLAY_KNOB     1u  // circular knob widget
#define VIVID_DISPLAY_XY_PAD   2u  // 2D axis pad (pair consecutive x/y params)
#define VIVID_DISPLAY_COLOR    3u  // color swatch + popup (triple consecutive r/g/b params)
#define VIVID_DISPLAY_HIDDEN   4u  // param exists but is not drawn by standard inspector

// Channel kinds — reflect the logical data type on a port.
typedef uint32_t VividPortType;

#define VIVID_PORT_SIGNAL         0u  // continuous numeric value (scalar at frame cadence, per-sample buffer at audio cadence)
#define VIVID_PORT_AUDIO          1u  // audio sample buffer
#define VIVID_PORT_SPREAD         2u  // variable-length float array
#define VIVID_PORT_STRING         3u  // UTF-8 string
#define VIVID_PORT_STRING_SPREAD  4u  // variable-length string array
#define VIVID_PORT_TEXTURE        5u  // WGPUTextureView

typedef uint32_t VividPortDirection;
#define VIVID_PORT_INPUT   0u
#define VIVID_PORT_OUTPUT  1u

typedef uint32_t VividPortTransport;
#define VIVID_PORT_TRANSPORT_SIGNAL         0u  // numeric value (scalar or buffer depending on execution environment)
#define VIVID_PORT_TRANSPORT_AUDIO_BUFFER   1u  // audio sample buffers
#define VIVID_PORT_TRANSPORT_SPREAD         2u  // float spread copy
#define VIVID_PORT_TRANSPORT_STRING         3u  // string copy
#define VIVID_PORT_TRANSPORT_STRING_SPREAD  4u  // string spread copy
#define VIVID_PORT_TRANSPORT_TEXTURE        5u  // GPU texture/view routing
#define VIVID_PORT_TRANSPORT_CUSTOM_VALUE   6u  // memcpy-by-value snapshot
#define VIVID_PORT_TRANSPORT_CUSTOM_REF     7u  // opaque shared-handle/reference



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
    float              default_value;// default for VIVID_PORT_SIGNAL inputs
    const char*        stable_type_id; // stable namespaced id for custom types, NULL for built-ins
    const char*        semantic_tag;   // e.g. "beat_phase", "gate", "trigger", "midi", NULL if unset
} VividPortDescriptor;


typedef struct VividOperatorDescriptor {
    const char*               name;
    uint32_t                  param_count;
    const VividParamDescriptor* params;
    uint32_t                  port_count;
    const VividPortDescriptor*  ports;
    int                       time_dependent;    // 1 if operator reads ctx->time, 0 otherwise
    int                       has_process_audio; // 1 if operator implements AudioProcessable
    int                       has_process_gpu;   // 1 if operator implements GpuProcessable
    uint32_t                  embedded_op_slot_count;
    const struct VividEmbeddedOpSlot* embedded_op_slots;

    // Cadence-aware execution model (v15+)
    VividExecutionEnv         execution_env;        // which executor owns this operator
    VividCadenceCapability    cadence_capability;    // FRAME_ONLY or AUDIO_CAPABLE
    int                       has_process_frame;     // 1 if operator implements FrameProcessable
} VividOperatorDescriptor;

// Embedded operator slot metadata — declares which owned modulation slots
// an operator supports, and how their params map to the host's flat param namespace.
typedef struct VividEmbeddedOpSlot {
    const char* role_id;        // e.g. "envelope", "viscosity_mod"
    const char* default_type;   // e.g. "Envelope", "LFO"
    const char* param_prefix;   // e.g. "envelope_", "viscosity_mod_"
} VividEmbeddedOpSlot;

// ---------------------------------------------------------------------------
// Input events — mouse, keyboard, scroll for interactive operators
// ---------------------------------------------------------------------------

typedef uint32_t VividInputEventType;
#define VIVID_INPUT_MOUSE_MOVE    0u
#define VIVID_INPUT_MOUSE_BUTTON  1u
#define VIVID_INPUT_MOUSE_SCROLL  2u
#define VIVID_INPUT_KEY           3u
#define VIVID_INPUT_CHAR          4u

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
    // Cross-cadence inputs from frame executor
    VividSpreadPort*  input_spreads;
    VividSpreadPort*  output_spreads;
    void**            custom_inputs;       // [custom_input_ordinal] — opaque custom-type inputs
    uint32_t          custom_input_count;  // number of custom-transport input ports
    const char**      input_string_values;
    float*            input_float_values;   // [float_input_ordinal] — CV inputs from frame executor
    float*            output_float_values;  // [float_output_ordinal] — scalar float outputs
    void**            custom_outputs;       // [custom_output_ordinal] — opaque custom-type outputs
    uint32_t          custom_output_count;  // number of custom-transport output ports
    const char**      file_param_values;
    uint32_t          file_param_count;
    const VividSharedHandleService* shared_handles;
    // Auto-dup channel index: 0 for normal operators; for auto-dup mono operators
    // in multi-channel chains, identifies which channel this instance processes.
    // Operators can use this to index into spread data for per-voice modulation.
    uint8_t           channel_index;
} VividAudioContext;

// ---------------------------------------------------------------------------
// Frame process context — passed to frame-rate operators on the main thread
// ---------------------------------------------------------------------------

typedef struct VividFrameContext {
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

    // ---- Operator write-back: operator sets these during process_frame() ----
    // The runtime reads them after process_frame() returns and acts accordingly.
    // preferred_tex_*: set to request a texture reallocation next frame.
    // Leave as 0 to keep the current size (no action taken).
    uint32_t  preferred_tex_width;
    uint32_t  preferred_tex_height;

} VividFrameContext;

// Forward declaration — full definition in gpu_operator.h (requires WebGPU types)
struct VividGpuContext;

// ---------------------------------------------------------------------------
// Function pointer typedefs (dlopen entry points)
// ---------------------------------------------------------------------------

typedef const VividOperatorDescriptor* (*VividDescriptorFn)(void);
typedef uint32_t (*VividAbiVersionFn)(void);
typedef void*  (*VividCreateFn)(void);
typedef void   (*VividDestroyFn)(void* instance);
typedef void   (*VividProcessFrameFn)(void* instance, VividFrameContext* ctx);
typedef void   (*VividProcessAudioFn)(void* instance, VividAudioContext* ctx);
typedef void   (*VividProcessGpuFn)(void* instance, struct VividGpuContext* ctx);

struct VividThumbnailContext;
typedef void (*VividDrawThumbnailFn)(void* instance,
                                     const struct VividThumbnailContext* ctx);

typedef struct VividFileDropHandlerDescriptor {
    const char* label;
    const char* const* extensions;
    uint32_t extension_count;
    const char* file_param;
    int32_t priority;
    const char* description;
} VividFileDropHandlerDescriptor;

typedef const VividFileDropHandlerDescriptor* (*VividFileDropDescriptorFn)(uint32_t* count);

// ---------------------------------------------------------------------------
// Inspector context — optional custom inspector rendering (draw_inspector)
// ---------------------------------------------------------------------------

typedef uint32_t VividInspectorMode;
#define VIVID_INSPECTOR_STANDARD  0u  // core draws standard params first, operator draws below
#define VIVID_INSPECTOR_FULL      1u  // operator handles entire inspector (no standard params)

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

// ---------------------------------------------------------------------------
// Port type compatibility helpers
// ---------------------------------------------------------------------------

static inline int vivid_is_control_type(VividPortType t) {
    return t == VIVID_PORT_SIGNAL || t == VIVID_PORT_SPREAD ||
           t == VIVID_PORT_STRING || t == VIVID_PORT_STRING_SPREAD;
}

static inline int vivid_port_type_compatible(VividPortType a, VividPortType b) {
    if (a == b) return 1;
    /* SIGNAL ↔ AUDIO: audio-cadence SIGNAL ports use 1-channel buffers */
    if ((a == VIVID_PORT_SIGNAL && b == VIVID_PORT_AUDIO) ||
        (a == VIVID_PORT_AUDIO  && b == VIVID_PORT_SIGNAL)) return 1;
    return vivid_is_control_type(a) && vivid_is_control_type(b);
}

#ifdef __cplusplus
}
#endif
