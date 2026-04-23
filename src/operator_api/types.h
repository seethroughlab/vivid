#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump when operator-facing C ABI changes in incompatible ways.
   Catches stale dylibs during hot-reload — not a cross-version compatibility promise. */
#define VIVID_OPERATOR_ABI_VERSION 1u

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

// Operator kind — classifies an operator by its primary execution context.
typedef uint32_t VividOperatorKind;
#define VIVID_OP_CONTROL 0u   // main thread, frame-rate (~60 Hz)
#define VIVID_OP_AUDIO   1u   // audio thread, audio-rate (~48 kHz)
#define VIVID_OP_GPU     2u   // main thread, GPU command submission

// Lane behavior — how an operator interacts with lane multiplicity.
typedef uint32_t VividLaneBehavior;
#define VIVID_LANE_POINTWISE   0u  // processes each lane independently, preserves lane set
#define VIVID_LANE_STRUCTURAL  1u  // creates, reshapes, reorders, or filters lanes
#define VIVID_LANE_REDUCTION   2u  // collapses many lanes into fewer (often one)
#define VIVID_LANE_KERNEL      3u  // needs cross-lane access (neighborhood / full collection)

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
#define VIVID_DISPLAY_ADSR     5u  // ADSR envelope editor (4 consecutive params: A, D, S, R)
#define VIVID_DISPLAY_LFO      6u  // LFO waveform preview + enum selector (single enum param)
#define VIVID_DISPLAY_STEP_SEQ 7u  // step sequencer grid (run: count + values [+ gates])

typedef uint32_t VividParamVisibilityOp;
#define VIVID_PARAM_VIS_ALWAYS 0u  // always show the param
#define VIVID_PARAM_VIS_EQ     1u  // show when controller equals any value
#define VIVID_PARAM_VIS_NE     2u  // show when controller does not equal any value

// Channel kinds — reflect the logical data type on a port.
typedef uint32_t VividPortType;

#define VIVID_PORT_SCALAR         0u  // scalar numeric value
#define VIVID_PORT_AUDIO_BUFFER   1u  // audio sample buffer
#define VIVID_PORT_LANE_ARRAY         2u  // variable-length float array
#define VIVID_PORT_STRING         3u  // UTF-8 string
#define VIVID_PORT_STRING_LANES  4u  // variable-length string array
#define VIVID_PORT_TEXTURE        5u  // WGPUTextureView

typedef uint32_t VividPortDirection;
#define VIVID_PORT_INPUT   0u
#define VIVID_PORT_OUTPUT  1u

typedef uint32_t VividPortTransport;
#define VIVID_PORT_TRANSPORT_SIGNAL         0u  // numeric value (scalar or buffer depending on execution environment)
#define VIVID_PORT_TRANSPORT_AUDIO_BUFFER   1u  // audio sample buffers
#define VIVID_PORT_TRANSPORT_LANE_ARRAY         2u  // float lane array copy
#define VIVID_PORT_TRANSPORT_STRING         3u  // string copy
#define VIVID_PORT_TRANSPORT_STRING_LANES  4u  // string lane array copy
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
    const char*       description;        /* human-readable tooltip shown in inspector on hover */
    const char*       asset_kind;         /* optional asset kind name, e.g. "wavetable" */
    const char*       visible_when_param; /* controller param name, NULL = always visible */
    VividParamVisibilityOp visible_when_op; /* EQ/NE against visible_when_values */
    const int32_t*    visible_when_values;
    uint32_t          visible_when_value_count;
    const char*       widget_id;          /* optional namespaced compound widget id */
    uint32_t          widget_span;        /* number of primitive params claimed by widget_id */

    /* repeat-group metadata (for variadic port patterns) */
    const char*       repeat_group;      /* NULL = standalone; non-NULL = group name (e.g. "layer") */
    uint16_t          repeat_group_idx;  /* 0-based index within the repeat group */
} VividParamDescriptor;

typedef struct VividPortDescriptor {
    // C++17 default member initializers let operators use the short
    // {name, type, direction} aggregate form without -Wmissing-field-initializers.
    // Layout/ABI unchanged; every DMI value matches previous zero-init behavior.
    const char*        name             = nullptr;
    VividPortType      type             = 0;
    VividPortDirection direction        = 0;
    VividPortTransport transport        = 0;
    uint32_t           payload_size     = 0;       // 0 for built-in types
    const char*        type_name        = nullptr; // C++ type name, NULL for built-ins
    uint8_t            channels         = 0;       // 0=auto, 1=mono, 2=stereo, etc.
    float              default_value    = 0.0f;    // default for VIVID_PORT_SCALAR inputs
    const char*        stable_type_id   = nullptr; // stable namespaced id for custom types, NULL for built-ins
    const char*        semantic_tag     = nullptr; // e.g. "beat_phase", "gate", "trigger", "midi", NULL if unset
    const char*        semantic_shape   = nullptr; // e.g. "scalar", "lane_array", "audio_buffer", NULL if unset
    const char*        semantic_intent  = nullptr; // free-form hint, e.g. "per_note_gate", NULL if unset
    const char*        description      = nullptr; // human-readable tooltip shown in inspector/docs, NULL if unset

    // repeat-group metadata (for variadic port patterns)
    const char*        repeat_group     = nullptr; // NULL = standalone; non-NULL = group name (e.g. "layer")
    uint16_t           repeat_group_idx = 0;       // 0-based index within the repeat group
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

    int                       has_process_frame;     // 1 if operator implements FrameProcessable

    // Lane behavior (v3+)
    VividLaneBehavior         lane_behavior;         // POINTWISE, STRUCTURAL, REDUCTION, or KERNEL
    int                       strategy_independent;  // 1 if operator uses vivid_lane_state() for all per-lane state
} VividOperatorDescriptor;

// Derive operator kind from capability flags (replaces stored field, v18+).
static inline VividOperatorKind vivid_operator_kind(const VividOperatorDescriptor* d) {
    if (d->has_process_gpu)                              return VIVID_OP_GPU;
    if (d->has_process_audio && !d->has_process_frame)   return VIVID_OP_AUDIO;
    return VIVID_OP_CONTROL;
}

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
// Lane views (immutable inputs) and output builders (runtime-owned)
// ---------------------------------------------------------------------------

typedef struct VividLaneView {
    const float* data;       // immutable pointer to lane data
    uint32_t     length;     // number of floats
    uint32_t     lane_set_id;// provenance identifier (0 = scalar)
    uint32_t     flags;      // reserved, must be 0
} VividLaneView;

typedef struct VividLaneOutput {
    void*    handle;                                         // runtime-owned, opaque
    float*   (*resize)(void* handle, uint32_t length);       // returns writable buffer or NULL
    void     (*commit)(void* handle, uint32_t length);       // publish length elements
} VividLaneOutput;

typedef struct VividStringLaneView {
    const char* const* data; // immutable pointer to string array
    uint32_t     length;     // number of strings
    uint32_t     lane_set_id;// provenance identifier (0 = scalar)
    uint32_t     flags;      // reserved, must be 0
} VividStringLaneView;

typedef struct VividStringLaneOutput {
    void*    handle;                                         // runtime-owned, opaque
    uint8_t  (*resize)(void* handle, uint32_t length);       // returns 1 on success, 0 on failure
    void     (*set)(void* handle, uint32_t index, const char* value); // copy string into slot
    void     (*commit)(void* handle, uint32_t length);       // publish length elements
} VividStringLaneOutput;

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
    const char* node_id;
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
    const VividLaneView*  input_lanes;
    VividLaneOutput*      output_lanes;
    void**            custom_inputs;       // [custom_input_ordinal] — opaque custom-type inputs
    uint32_t          custom_input_count;  // number of custom-transport input ports
    const char**      input_string_values;
    void**            custom_outputs;       // [custom_output_ordinal] — opaque custom-type outputs
    uint32_t          custom_output_count;  // number of custom-transport output ports
    const char**      file_param_values;
    uint32_t          file_param_count;
    const VividSharedHandleService* shared_handles;

    // ---- Lane metadata (populated by audio executor) ----
    // lane_count: number of lanes this node is lifted over (1 = not lifted).
    // lane_index: which lane this invocation processes (0..lane_count-1).
    // lane_set_id: compile-time provenance (0 = scalar).
    // lane_id: stable identity token for identity-bearing lane sets (0 = positional).
    uint32_t          lane_count;
    uint32_t          lane_index;
    uint32_t          lane_set_id;
    uint32_t          lane_id;

    // ---- Per-lane persistent state service (Phase 5) ----
    // lane_state_fn: get identity-keyed state for (lane_id, byte_size).
    //   RT-safe lookup; returns pre-allocated storage or scratch for new lanes.
    void*    (*lane_state_fn)(void* service, uint32_t lane_id, uint32_t byte_size);
    void*    lane_state_service;    // opaque pointer to LaneStateService

    // ---- Lane identity allocation/retirement (for structural operators) ----
    // allocate_lane_id_fn: returns a fresh monotonic uint32_t. Audio-thread safe.
    // retire_lane_id_fn: marks lane_id for deferred cleanup (node_idx is implicit).
    uint32_t (*allocate_lane_id_fn)(void* service);
    void     (*retire_lane_id_fn)(void* service, uint32_t lane_id);

    // ---- Graph-wide metronome (read-only, always active) ----
    float     metronome_bpm;
    uint32_t  metronome_beats_per_bar;
    double    metronome_beats_elapsed;
    float     metronome_beat_phase;
    float     metronome_bar_phase;
    float     metronome_beat_ms;
} VividAudioContext;

// ---------------------------------------------------------------------------
// Frame process context — passed to frame-rate operators on the main thread
// ---------------------------------------------------------------------------

typedef struct VividFrameContext {
    // ---- Runtime-provided: read-only inputs --------------------------------
    double    time;
    double    delta_time;
    uint64_t  frame;
    const char* node_id;
    float*    param_values;   // indexed by param descriptor order
    float*    input_values;   // indexed by input port order (VIVID_PORT_INPUT only)
    float*    output_values;  // indexed by output port order (VIVID_PORT_OUTPUT only)
    const VividLaneView*  input_lanes;    // [port_ordinal], NULL if none
    VividLaneOutput*      output_lanes;   // [port_ordinal], NULL if none
    void**     custom_inputs;          // [custom_input_ordinal], NULL if none
    uint32_t   custom_input_count;     // number of custom-transport input ports
    void**     custom_outputs;         // [custom_output_ordinal], NULL if none
    uint32_t   custom_output_count;    // number of custom-transport output ports
    const char** input_string_values;   // [string_port_ordinal]
    const char** output_string_values;  // [string_port_ordinal]
    const VividStringLaneView*  input_string_lanes;   // [port_ordinal], NULL if none
    VividStringLaneOutput*      output_string_lanes;  // [port_ordinal], NULL if none
    const char** file_param_values;   // indexed by file param order, NULL if none
    uint32_t     file_param_count;
    void*     input;          // VividInputState* for interactive operators, NULL otherwise
    const VividSharedHandleService* shared_handles; // runtime-owned process-wide handle service

    // ---- Lane metadata (read-only, populated by frame executor) ----
    uint32_t  lane_count;     // runtime materialized lane count (1 = scalar)
    uint32_t  lane_index;     // current lane in LoopBased (0 = scalar or first lane)
    uint32_t  lane_set_id;    // compile-time provenance (0 = scalar)
    uint32_t  lane_id;        // stable identity token for vivid_lane_state() (0 = positional)

    // ---- Lane state service (populated for LoopBased frame nodes) ----
    void*     (*lane_state_fn)(void* service, uint32_t lane_id, uint32_t byte_size);
    void*     lane_state_service;
    uint32_t  (*allocate_lane_id_fn)(void* service);
    void      (*retire_lane_id_fn)(void* service, uint32_t lane_id);

    // ---- Graph-wide metronome (read-only, always active) ----
    float     metronome_bpm;
    uint32_t  metronome_beats_per_bar;
    double    metronome_beats_elapsed;
    float     metronome_beat_phase;
    float     metronome_bar_phase;
    float     metronome_beat_ms;

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

typedef struct VividDrawAPI {
    void* opaque;  // Renderer2D* — operators must not dereference
    void  (*draw_rect)(void*, float x, float y, float w, float h, VividColor);
    void  (*draw_rounded_rect)(void*, float x, float y, float w, float h, float radius, VividColor);
    void  (*draw_text)(void*, float x, float y, const char* text, VividColor, float scale);
    void  (*draw_line)(void*, float x1, float y1, float x2, float y2, float thickness, VividColor);
    float (*text_width)(void*, const char* text, float scale);
    float (*line_height)(void*);
    void  (*push_clip_rect)(void*, float x, float y, float w, float h);
    void  (*pop_clip_rect)(void*);
    /* Additive extensions (sequencer redesign follow-up). Operators built
       against older headers ignore these fields; host populates them. */
    void  (*draw_tri)(void*, float x0, float y0, float x1, float y1,
                      float x2, float y2, VividColor);
    void  (*draw_arc)(void*, float cx, float cy, float radius,
                      float start_angle, float end_angle,
                      float thickness, int segments, VividColor);
    /* Returns the total consumed height (pixels). */
    float (*draw_text_wrapped)(void*, float x, float y, const char* text,
                                float max_width, VividColor, float scale);
} VividDrawAPI;
typedef VividDrawAPI VividInspectorDrawAPI;

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

// ---------------------------------------------------------------------------
// Editor API — optional dedicated-editor-window surface for content-heavy
// operators (e.g. sequencers, envelopes, trackers). An operator exposes
// either a custom inspector (VIVID_INSPECTOR) or a dedicated editor
// (VIVID_EDITOR), never both: the inspector is a freeform section inside
// the node sidebar; the editor is a dedicated native window with its own
// input focus, clipboard, pointer capture, and persistent geometry. See
// operators/CLAUDE.md "Choosing a UI surface" for the decision rule.
// Discovered by optional dlsym of vivid_editor_metadata + vivid_draw_editor.
// ---------------------------------------------------------------------------

typedef struct VividEditorMetadata {
    uint32_t default_width;
    uint32_t default_height;
    uint32_t min_width;
    uint32_t min_height;
    const char* title_suffix;  // appended to node label in the window title
} VividEditorMetadata;

typedef uint32_t VividEditorEventType;
#define VIVID_EDITOR_EVENT_MOUSE_MOVE    0u
#define VIVID_EDITOR_EVENT_MOUSE_BUTTON  1u
#define VIVID_EDITOR_EVENT_MOUSE_SCROLL  2u
#define VIVID_EDITOR_EVENT_KEY           3u
#define VIVID_EDITOR_EVENT_CHAR          4u

typedef struct VividEditorEvent {
    VividEditorEventType type;
    float x, y;          /* editor-window-local pixel coordinates */
    int button;          /* 0=left, 1=right, 2=middle (MOUSE_BUTTON) */
    int action;          /* 0=release, 1=press, 2=repeat */
    float scroll_dx, scroll_dy;
    int key;             /* GLFW key code (KEY) */
    int scancode;
    uint32_t codepoint;  /* Unicode (CHAR) */
    int modifiers;       /* shift/ctrl/alt/super bitmask */
} VividEditorEvent;

// Editor-window-local pixel space. Not normalized UV, not inspector-relative.
typedef struct VividEditorMouse {
    float x, y;
    float prev_x, prev_y;
    int left_down, left_clicked, left_released, right_clicked;
    int shift_down;
} VividEditorMouse;

/* Host-service surface — additive extension to VividEditorContext. Each
 * callback is optional; operators must guard on non-null fn pointers.
 * (Phase D of the editor-UI platform plan — clipboard, cursor shape,
 * pointer capture, focus, status/tooltip.)
 */
typedef uint32_t VividCursorKind;
#define VIVID_CURSOR_DEFAULT     0u
#define VIVID_CURSOR_ARROW       1u
#define VIVID_CURSOR_IBEAM       2u
#define VIVID_CURSOR_CROSSHAIR   3u
#define VIVID_CURSOR_HAND        4u
#define VIVID_CURSOR_RESIZE_H    5u
#define VIVID_CURSOR_RESIZE_V    6u
#define VIVID_CURSOR_RESIZE_NESW 7u
#define VIVID_CURSOR_RESIZE_NWSE 8u

typedef struct VividEditorHostAPI {
    void* opaque;

    /* Clipboard — UTF-8. `get_clipboard_text` returns a read-only pointer
     * valid until the next host callback on the same window; operators
     * should copy if they need longer-lived storage. */
    const char* (*get_clipboard_text)(void* opaque);
    void        (*set_clipboard_text)(void* opaque, const char* text);

    /* Cursor shape for the current frame. Reset to DEFAULT every frame. */
    void (*set_cursor)(void* opaque, VividCursorKind kind);

    /* Pointer capture — while captured, the editor window keeps receiving
     * mouse events even when the cursor leaves its bounds. */
    void (*capture_pointer)  (void* opaque);
    void (*release_pointer)  (void* opaque);
    int  (*has_pointer_capture)(void* opaque);

    /* Focus — operator asks for keyboard focus; host reports current state. */
    void (*request_focus)(void* opaque);
    int  (*has_focus)    (void* opaque);

    /* Transient chrome. Pass nullptr to clear. */
    void (*set_status_text)(void* opaque, const char* text);
    void (*show_tooltip)   (void* opaque, const char* text);
} VividEditorHostAPI;

typedef struct VividEditorContext {
    // Surface
    float surface_width;
    float surface_height;
    float dpi_scale;

    // Drawing and commands (reused from inspector ABI)
    VividDrawAPI             draw;
    VividInspectorCommandAPI commands;
    VividInspectorTheme      theme;

    // Operator state (read-only)
    const float*       param_values;        uint32_t param_count;
    const float*       output_values;       uint32_t output_count;
    const char* const* string_param_values; uint32_t string_param_count;

    // Input (editor-local pixel coords)
    VividEditorMouse         mouse;
    const VividEditorEvent*  events;        uint32_t event_count;

    // Clock
    double time;

    // Host-writable responses
    int wants_keyboard;  // operator sets 1 to keep keyboard focus
    int request_close;   // operator sets 1 to ask the host to close the editor

    /* Host services (Phase D additive extension — clipboard, cursor,
     * pointer capture, focus, status/tooltip). All callbacks may be
     * null; operators guard before calling. */
    VividEditorHostAPI host;

    /* Introspection sink — additive extension. When non-null, each
     * widget in editor_ui.h emits one VividIntrospectWidget record
     * describing its bounds + live state. The runtime installs a
     * JSON-serializing sink around draw_editor() when an LLM / test
     * harness requests an editor tree. Operators never touch this
     * directly. */
    void (*introspect_fn)(void* sink, const struct VividIntrospectWidget* w);
    void* introspect_sink;
} VividEditorContext;

/* Flags used in VividIntrospectWidget.flags. Bitmask — any combination. */
#define VIVID_INTROSPECT_ACTIVE    0x01u  /* e.g. button's active=true */
#define VIVID_INTROSPECT_HOVERED   0x02u
#define VIVID_INTROSPECT_PRESSED   0x04u
#define VIVID_INTROSPECT_CHANGED   0x08u  /* slider/grid/text emitted change this frame */
#define VIVID_INTROSPECT_DRAGGING  0x10u
#define VIVID_INTROSPECT_FOCUSED   0x20u  /* text_field focused */
#define VIVID_INTROSPECT_VALUE     0x40u  /* toggle current=true */

/* One record per widget call per editor tick, emitted via
 * ctx.introspect_fn when the host installs a sink. `kind` is the
 * widget type slug ("button", "slider_h", "step_grid", ...). Fields
 * that don't apply to a particular widget are left zero/nullptr. */
typedef struct VividIntrospectWidget {
    const char* kind;
    float x, y, w, h;             /* bounds in editor-window-local pixels */
    const char* label;            /* nullable — button/slider/toggle label */
    float value;                  /* sliders, scalar knobs */
    float value_lo;
    float value_hi;
    int   int_value;              /* radio current, text_field cursor, etc. */
    int   rows, cols;             /* step_grid dimensions */
    int   anchor_row, anchor_col; /* step_grid selection */
    int   active_cols;            /* step_grid visible step range */
    const char* text;             /* text_field current contents */
    const char* placeholder;      /* text_field placeholder */
    unsigned flags;               /* VIVID_INTROSPECT_* bitmask */
} VividIntrospectWidget;

typedef void (*VividIntrospectFn)(void* sink, const VividIntrospectWidget* w);

typedef VividEditorMetadata (*VividEditorMetadataFn)(void);
typedef void                (*VividDrawEditorFn)(void* instance, VividEditorContext* ctx);

// Optional main-thread update hook for operators that need non-audio-thread work
// (e.g. AVFoundation decoding, file I/O, ring buffer pre-fill)
typedef void (*VividMainThreadUpdateFn)(void* instance, double time,
                                        const char** file_param_values,
                                        uint32_t file_param_count);

// Optional per-instance warmup hook for CPU-side one-time preparation that
// should happen after graph params/file params have been synced into the
// instance, but before runtime/UI code relies on first-use caches.
typedef void (*VividPrepareInstanceAssetsFn)(void* instance,
                                             const float* param_values,
                                             const char** file_param_values,
                                             uint32_t file_param_count);

// ---------------------------------------------------------------------------
// Port type compatibility helpers
// ---------------------------------------------------------------------------

static inline int vivid_is_control_type(VividPortType t) {
    return t == VIVID_PORT_SCALAR || t == VIVID_PORT_LANE_ARRAY ||
           t == VIVID_PORT_STRING || t == VIVID_PORT_STRING_LANES;
}

static inline int vivid_port_type_compatible(VividPortType a, VividPortType b) {
    if (a == b) return 1;
    return vivid_is_control_type(a) && vivid_is_control_type(b);
}

#ifdef __cplusplus
}
#endif
