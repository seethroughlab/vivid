# Advanced API Features

## ChildOp\<T\> — Owned Child Operators

Embed an operator as a persistent member variable inside another operator. Frame-cadence only.

```cpp
#include "operator_api/child_op.h"
#include "control/lfo/lfo.h"

struct ModulatedGain : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "ModulatedGain";
    static constexpr bool kTimeDependent = true;

    vivid::ChildOp<LFO> lfo;
    vivid::Param<float> depth{"depth", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&depth);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        lfo.set_param("frequency", 2.0f);
        lfo.process(ctx);  // inherits time/frame from parent
        float mod = lfo.output("value");
        ctx->output_values[0] = ctx->input_values[0] * (1.0f - depth.value * mod);
    }
};
```

### ChildOp API
| Method | Description |
|--------|-------------|
| `set_param(name, value)` | Set child param by name |
| `set_param(index, value)` | Set child param by index |
| `set_input(name, value)` | Set child float input |
| `set_input_lane_data(name, data, length)` | Set child lane input |
| `process(parent_ctx)` | Run child (inherits time/frame) |
| `output(name)` | Read child float output |
| `output_lane_data(name)` | Read child lane output data |
| `output_lane_length(name)` | Read child lane output length |
| `op()` | Direct access to underlying operator instance |

### Choosing the right composition surface

Use `ChildOp<T>` when the host privately owns a fixed internal helper such as an LFO, Envelope, or Smooth. If a value needs to travel through the graph or fan out across many voices/elements, expose an ordinary port and rely on lanes for multiplicity. If host-local behavior must become visible to the rest of the graph, expose an explicit output instead of leaking the internal mechanism.

## Custom Port Types

Typed opaque data for passing complex payloads between operators (e.g. media streams, 3D scene fragments). Two transports are available:

- `VIVID_PORT_TRANSPORT_CUSTOM_VALUE` — small structs (≤256 bytes) copied by value
- `VIVID_PORT_TRANSPORT_CUSTOM_REF` — opaque pointer via shared handle registry (any size)

```cpp
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"

VIVID_DECLARE_CUSTOM_REF_TYPE(vivid::MediaStreamV1,
                              "com.example.media_stream_v1",
                              "MediaStreamV1",
                              false);

// Producer
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back(VIVID_CUSTOM_REF_PORT("media_stream", VIVID_PORT_OUTPUT, vivid::MediaStreamV1));
}

void process_frame(const VividFrameContext* ctx) override {
    auto* stream = static_cast<vivid::MediaStreamV1*>(ctx->custom_outputs[0]);
    // write stream data...
}

// Consumer
void collect_ports(std::vector<VividPortDescriptor>& out) override {
    out.push_back(VIVID_CUSTOM_REF_PORT("media_stream", VIVID_PORT_INPUT, vivid::MediaStreamV1));
}

void process_frame(const VividFrameContext* ctx) override {
    auto* stream = static_cast<const vivid::MediaStreamV1*>(ctx->custom_inputs[0]);
    if (stream) { /* read stream */ }
}

// Optionally export the type metadata directly from this dylib
VIVID_DESCRIBE_REF_TYPE(vivid::MediaStreamV1)
```

Type safety: `VIVID_DECLARE_CUSTOM_*_TYPE(...)` gives the type a stable namespaced id, and `vivid_port_type<T>()` derives the custom port token from that id. The runtime rejects connections between incompatible custom types.

## Native Note Protocol

Inside the graph every note stream uses `VividNoteBuffer` — a buffer of
timestamped per-note events keyed by stable `note_id`. External MIDI 1.0 and
MPE live at the I/O boundary (`MidiInput` and `MidiClip`); inside the graph
everything speaks this native protocol. Use `MidiClip` for MIDI file playback.

```cpp
#include "operator_api/note_types.h"

enum VividNoteEventType {
    VIVID_NOTE_ON         = 0,  // value = velocity 0..1
    VIVID_NOTE_OFF        = 1,  // value unused
    VIVID_NOTE_PITCH_BEND = 2,  // value = signed semitones
    VIVID_NOTE_PRESSURE   = 3,  // value = 0..1
    VIVID_NOTE_TIMBRE     = 4,  // value = 0..1
};

struct VividNoteEvent {
    uint8_t  type;                  // VividNoteEventType
    uint8_t  note_number;           // 0..127, meaningful for ON/OFF
    uint16_t reserved;
    uint32_t frame_offset_samples;  // sample offset within buffer
    uint64_t note_id;               // stable across all events for this note
    float    value;                 // see comments above per event type
};

struct VividNoteBuffer {
    VividNoteEvent events[64];      // VIVID_NOTE_BUFFER_CAPACITY
    uint32_t       count;
};
```

Use `VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer)`
to declare a note port. Emission helpers in
`operators/shared/sequencer/note_helpers.h`:
`vivid_sequencers::note_on/off/pitch_bend/pressure/timbre` plus
`next_note_id()` for fresh note_id allocation. Re-triggering the same MIDI
pitch produces a fresh `note_id`, so legato retriggers and same-pitch
overlap allocate distinct synth voices.

## Input Events (Mouse/Keyboard)

```cpp
#include "operator_api/input_state.h"

void process_frame(const VividFrameContext* ctx) override {
    const VividInputState* input = vivid_input(ctx);
    if (!input) return;

    // Current mouse position (normalized [0,1] texture coords)
    float mx = input->mouse_x;
    float my = input->mouse_y;

    // Process events
    for (uint32_t i = 0; i < input->event_count; i++) {
        const VividInputEvent& e = input->events[i];
        if (e.type == VIVID_INPUT_MOUSE_BUTTON && e.action == 1) {
            // mouse click at (e.mouse_x, e.mouse_y)
        }
    }
}
```

## Cross-Cadence AV Sync

Movie operators synchronize audio and video across cadences using standard scalar ports and the cadence bridge:

- **MovieFileAudio** (audio thread) — decodes audio, outputs `time` and `duration` scalar ports
- **MovieFileIn** (GPU thread) — receives `audio_time` scalar input via the cadence bridge, seeks video to matching frame

Shared decode/audio extraction libraries live in `operators/shared/` (`movie_decode`, `movie_audio`). No custom port types are needed — the cadence bridge handles the audio→control rate conversion automatically.

See `gpu/movie_file_in` and `audio/movie_file_audio` for the canonical implementation.

## Shared Handle Service

Process-wide handle lifecycle management for `CUSTOM_REF` payloads:

```cpp
const VividSharedHandleService* svc = ctx->shared_handles;
uint64_t id = svc->create("my_type", payload_ptr, generation);
svc->retain(id);
VividSharedHandleEntry entry = svc->resolve(id);
svc->release(id);
svc->invalidate(id, new_generation);
```

## GPU Types (gpu_types.h)

Structured GPU resource types commonly used with `CUSTOM_REF` ports:

- `VividGpuBuffer` — GPU buffer with usage flags
- `VividComputeBuffer` — Compute buffer with element count/stride
- `VividMesh` — Vertex + optional index buffer with topology
- `VividVertexAttribute` — Vertex attribute layout

## main_thread_update

Optional override for non-audio-thread work (file I/O, AVFoundation decoding, ring buffer pre-fill):

```cpp
void main_thread_update(double time) override {
    // Called on main thread before each frame
    // File params are synced before this call
}
```

## Custom Thumbnails and Inspectors

Operators can provide custom visual thumbnails (node graph previews) and custom inspector panels.

### Thumbnail API

Override `draw_thumbnail()` and add `VIVID_THUMBNAIL(ClassName)` after your struct definition:

```cpp
#include "operator_api/thumbnail.h"

void draw_thumbnail(const VividThumbnailContext* ctx) override {
    VividDrawAPI& d = const_cast<VividDrawAPI&>(ctx->draw);
    void* o = d.opaque;
    if (!o) return;  // no draw backend available

    float w = ctx->thumbnail_logical_width;
    float h = ctx->thumbnail_logical_height;
    d.draw_rect(o, 0, 0, w, h, {0.1f, 0.1f, 0.1f, 1.0f});
    d.draw_text(o, 4, 4, "Hello", {1, 1, 1, 1}, 1.0f);
}

VIVID_THUMBNAIL(MyOp)
```

**VividThumbnailContext key fields:**

| Field | Type | Description |
|-------|------|-------------|
| `draw` | `VividDrawAPI` | 2D draw API (check `draw.opaque` before use) |
| `thumbnail_logical_width/height` | `uint32_t` | Graph-space dimensions for draw coordinates |
| `param_values` / `param_count` | `float*`, `uint32_t` | Current param values |
| `output_values` / `output_count` | `float*`, `uint32_t` | Current output values |
| `device`, `queue`, `command_encoder` | WebGPU handles | For GPU-accelerated thumbnails |
| `thumbnail_texture` / `thumbnail_texture_view` | WebGPU handles | Target texture for GPU rendering |
| `source_output_texture` / `source_output_texture_view` | WebGPU handles | Operator's own output texture (GPU ops) |

Use `prepare_instance_assets()` for expensive one-time CPU prep — never do heavy work in `draw_thumbnail()`.

### VividDrawAPI Methods

The 2D draw API is shared by thumbnails and inspectors:

```cpp
void  draw_rect(opaque, x, y, w, h, color);
void  draw_rounded_rect(opaque, x, y, w, h, radius, color);
void  draw_text(opaque, x, y, text, color, scale);
void  draw_line(opaque, x1, y1, x2, y2, thickness, color);
float text_width(opaque, text, scale);
float line_height(opaque);
void  push_clip_rect(opaque, x, y, w, h);
void  pop_clip_rect(opaque);
```

Higher-level helpers are available in `draw_ui_helpers.h`:
```cpp
#include "operator_api/draw_ui_helpers.h"
// vivid::draw_ui::draw_panel, draw_section_header, draw_value_badge,
// draw_button, draw_tab_strip, draw_text_row, draw_grid_cell, etc.
```

### Custom Inspector

Override `draw_inspector()` and add `VIVID_INSPECTOR(ClassName)` or `VIVID_INSPECTOR_FULL_MODE(ClassName)`:

```cpp
void draw_inspector(VividInspectorContext* ctx) override {
    VividDrawAPI& d = ctx->draw;
    void* o = d.opaque;
    float x = ctx->content_x, y = ctx->content_y, w = ctx->content_width;

    d.draw_text(o, x, y, "Custom section", ctx->theme.bright_text, 1.0f);
    ctx->consumed_height = 20.0f;  // report how much vertical space you used
}

VIVID_INSPECTOR(MyOp)          // STANDARD: core draws params first, then your draw_inspector
// or: VIVID_INSPECTOR_FULL_MODE(MyOp)  // FULL: you handle the entire inspector
```

**Inspector modes:**
- `VIVID_INSPECTOR_STANDARD` — core renders standard param sliders first, your `draw_inspector()` draws below
- `VIVID_INSPECTOR_FULL` — your `draw_inspector()` handles the entire inspector area

**VividInspectorContext key fields:**

| Field | Type | Description |
|-------|------|-------------|
| `content_x/y`, `content_width` | `float` | Layout bounds (scroll-adjusted) |
| `draw` | `VividDrawAPI` | 2D draw API |
| `commands` | `VividInspectorCommandAPI` | `set_param(name, value)`, `set_string_param(name, value)` |
| `theme` | `VividInspectorTheme` | Colors: `bg`, `accent`, `dim_text`, `bright_text`, `separator`, etc. |
| `mouse` | `VividInspectorMouse` | `x`, `y`, `left_down`, `left_clicked`, etc. |
| `param_values` / `output_values` | `float*` | Current values |
| `consumed_height` | `float` | Write-back: total height your drawing consumed |
| `wants_keyboard` | `int` | Write-back: set 1 to request keyboard focus |

## Canonical Examples

Reference operators for advanced patterns — study these when implementing specific capabilities.

| Pattern | Example Operator(s) |
|---|---|
| ChildOp\<T\> composites | `control/modulated_gain` |
| Custom value ports | `control/step_counter`, `control/sample_hold` (use `VIVID_CUSTOM_VALUE_PORT`) |
| Custom ref ports | `control/drum_kit`, `audio/sampler` (use `VIVID_CUSTOM_REF_PORT`) |
| MIDI input | `control/midi_input`, `control/midi_clip` |
| File drop params | `gpu/texture_loader`, `gpu/lut_apply`, `gpu/svg_render` |
| Input events (mouse/keyboard) | `control/mouse`, `control/keyboard` |
| Cross-cadence AV sync | `gpu/movie_file_in`, `audio/movie_file_audio` |
| GPU compute buffers | `gpu/texture_analysis` |
| Custom thumbnails | `control/envelope`, `control/clock`, `control/smooth` |
| Audio analysis / FFT | `control/fft_analysis`, `audio/audio_analysis` |

## Lane Behavior and Identity-Bearing Lane Sets

### Declaring Lane Behavior

```cpp
static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;
```

If omitted, the operator defaults to `VIVID_LANE_POINTWISE`.

### Behavior Classes

- **Pointwise** (default): processes one lane at a time. The runtime may create N instances for multi-lane inputs (lane lifting). Most operators are pointwise.
- **Structural**: creates, reshapes, or filters lane sets. Outputs get a fresh lane-set provenance. Example: voice allocator, collection generator.
- **Reduction**: collapses many lanes into fewer. Example: voice mixer, sum.
- **Kernel**: reads the full lane set with cross-lane access. Not lane-lifted; runs as a single instance with full lane data. Example: lane smoothing, FFT-bin interpolation.

### Per-Lane Persistent State

Audio operators can use `vivid_lane_state()` for persistent state keyed by lane identity (not positional index):

```cpp
struct Voice { double phase; float current_freq; bool was_gated; };
uint32_t lid = /* lane_id from upstream allocator */;
Voice& v = *vivid_lane_state(ctx, lid, Voice);
// v.phase, v.current_freq, etc. survive across callbacks for this lane_id
```

The state is zero-initialized on first access and stable until the lane_id is retired.

### Identity Allocation and Retirement

Structural operators that manage voice lifecycle call:
```cpp
uint32_t new_id = ctx->allocate_lane_id_fn(ctx->lane_state_service);  // fresh identity
ctx->retire_lane_id_fn(ctx->lane_state_service, old_id);              // deferred cleanup
```

Lane IDs are monotonic `uint32_t` values. Retirement triggers deferred cleanup on the next frame tick.

## Module Authoring (`.vivid-module.json`)

Modules are encapsulated subgraph instruments that appear as single nodes with curated exposed controls. A module definition lives in a `.vivid-module.json` file, typically inside a package directory.

### Exposed Controls

Exposed params carry the same metadata as normal operator params:

| Field | Description |
|-------|-------------|
| `type` | Param type (float, int, bool, choice) |
| `default`, `min`, `max` | Value range |
| `choices` | Choice labels (for enum/choice params) |
| `group`, `section` | Inspector grouping |
| `display_hint` | Layout hint (`"knob"`, `"xy_pad"`, `"color"`, etc.) |
| `semantic` | Semantic tag (`"frequency_hz"`, `"normalized"`, etc.) |
| `description` | Human-readable description |

Module instances use the flatten-before-compile execution model — internal nodes are part of graph truth at runtime. No separate execution path exists for modules.

### Module Factory Presets

Declare factory presets in the module definition under `module.presets`. These appear in the preset UI the same way normal operator factory presets do. Preset values are remapped through the module's exposed-param bindings during recall.

### Declaring Modulation Sources and Destinations

Modules can declare named modulation sources and destinations for module-local modulation. Authors specify these in the module definition:

- **Sources**: internal signals the user can assign to destinations (e.g., `env2`, `lfo1`, `macro1`, `velocity`)
- **Destinations**: exposed or internal params the user can target (e.g., `filter_cutoff`, `brightness`, `wt_position`)

Users create assignments via the inspector or MCP tools (`add_mod_assignment`). Each assignment has:
- `amount` — modulation depth
- `polarity` — `"unipolar"` (0..amount) or `"bipolar"` (-amount..+amount)
- `curve` — `"linear"` in V1

Assignments are lowered into ordinary graph routing at compile time (additive mix: `base_value + source * amount`).

### Performance Page Metadata

Exposed params can carry performance-surface metadata to curate a live performance page:

| Field | Description |
|-------|-------------|
| `performance_page` | Page name for grouping (e.g., `"Macros"`, `"Expression"`) |
| `performance_order` | Sort order within the page |
| `performance_role` | Built-in role: `macro`, `mod_wheel`, `expression`, `aftertouch`, `xy_x`, `xy_y` |

Performance controls are ordinary exposed params — presets, session clips, and modulation still apply to them.

### Asset-Bound Params

File or string params can declare an `asset_kind` to bind them to the asset library. See `data_driven.md` for the `asset_kind` field on data-driven operator params. The same annotation works on module exposed params — the UI will browse the asset library for that kind when the user edits the param.
