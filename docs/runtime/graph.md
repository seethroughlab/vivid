# Graph — Serializable Scene Description

## Overview

`Graph` (graph.h/cpp) is a pure data model — it does **not** hold operator instances or execute anything.
It is the serialized representation of the scene: what nodes exist, how they connect, and their stored parameter values.

The `RuntimeCore` and `AudioEngine` are built **from** a `Graph` at load time, and written back to a `Graph`
on save. The `Graph` is the source of truth for persistence.

## Core Structs

### `NodeDef`
```cpp
struct NodeDef {
    std::string id;          // unique node ID (user-visible, e.g. "lfo1")
    std::string type;        // operator type name (e.g. "lfo", "shape")
    std::unordered_map<std::string, float> params;
    std::unordered_map<std::string, std::string> string_params;
    float layout_x, layout_y;       // NaN = auto-layout
    uint32_t tex_width, tex_height;  // GPU resolution (0 = inherit default 1280x720)
    std::unordered_map<std::string, uint8_t> param_lock_flags; // sparse
    std::string pkg_name, pkg_version; // provenance (empty for core operators)
};
```

### `ConnectionDef`
```cpp
struct ConnectionDef {
    std::string from_node, from_port;
    std::string to_node, to_port;
    float from_min, from_max;   // remap range on source side (default 0..1)
    float to_min, to_max;       // remap range on destination side (default 0..1)
    bool clamp;
};
```
`has_remap()` returns true if any remap fields differ from their defaults.

### `MidiMappingDef`
Maps a MIDI CC to a node parameter: `node_id`, `param_name`, `cc_number` (0-127), `channel` (0=omni), `range_min/max`.

### `SessionDef`
Performance authoring state for Tracks, Clips, Scenes, and Cue Paths. Tracks own nodes and clips,
clips store per-track parameter state, scenes store track-to-clip assignments, and cue paths store
optional ordered scene progressions. Cue paths reference scenes by stable id; they do not store raw
parameters and they do not introduce a master timeline. `active_clips` tracks the visible launched
clip per track. Transition fields are parsed and round-tripped as reserved schema but are not
consulted by current launch behavior.

### `OperatorPreset`
Per-operator named preset: `name` + `params[param_name] = value` + `string_params`.
Stored in `node_presets_[node_id]`.

### `StatePresetMapping`
Maps StateMachine operator state transitions to preset recalls.
`state_machine_node` + `state_presets[state_idx][target_node_id] = preset_name`.

### `GraphContentMeta`
Graph-owned content metadata persisted in the top-level `meta` block.

```cpp
struct GraphPreviewControl {
    std::string node;
    std::string param;
    std::string label;  // optional
};

struct GraphContentMeta {
    std::string id, title, description, difficulty;
    std::vector<std::string> tags;
    std::vector<std::string> domains;
    std::vector<std::string> requires_packages;
    int featured_rank;
    int estimated_minutes;
    std::string content_kind, category, family, role, playability;
    std::vector<GraphPreviewControl> preview_controls;
};
```

This metadata is part of `Graph` itself, so ordinary `load()`, `save()`, `save_to_string()`,
and `RuntimeAPI::save_as()` preserve it without going through a separate helper path.
`preview_controls` remain compact metadata references to existing node params (`node`, `param`,
optional `label`); editor-side structured authoring should round-trip that shape directly rather
than storing a second serialized helper payload.

## `Graph` Class API

### Load / Save
```cpp
bool load(const char* path);                          // parse JSON file
bool load_from_string(const char* json, size_t len);  // parse in-memory JSON
bool save(const char* path) const;
bool save_to_string(std::string& out_json) const;
```
Uses nlohmann/json for JSON parsing. Schema version checked against `GRAPH_SCHEMA_VERSION` (currently 1).
`load_diagnostics` is populated with package version mismatches after load.

The top-level `meta` block is parsed into `GraphContentMeta`. For backward compatibility,
`meta.envs` is accepted on load as an alias for `meta.domains`, but save paths always write the
canonical `domains` field.

### Mutation
```cpp
bool add_node(id, type, params, string_params);
bool remove_node(id);
bool add_connection(from_node, from_port, to_node, to_port);
bool remove_connection(...);
bool set_connection_remap(..., from_min, from_max, to_min, to_max, clamp);
```
All mutations return false on error (e.g. duplicate ID, missing node).

### Lookup
```cpp
const NodeDef* find_node(const std::string& id) const;
NodeDef*       find_node(const std::string& id);       // mutable
```

### Viewport
```cpp
float viewport_pan_x, viewport_pan_y, viewport_zoom;  // NaN = not set
bool has_viewport() const;
void set_viewport(float px, float py, float z);
```
Viewport is UI-only state, persisted but ignored by the runtime.

## Schema Version

`GRAPH_SCHEMA_VERSION` is defined as `1` in graph.h. Graphs saved with a newer schema version
are hard-rejected on load. Graphs without a `schema_version` field are treated as version 1.

## Load Diagnostics

After `load()`, `graph.load_diagnostics` contains entries for nodes whose saved `pkg_version`
differs from the installed version. Each entry has: `node_id`, `pkg_name`, `saved_version`,
`installed_version`, `classification` (`"compatible_update"` or `"incompatible_update"`).

## JSON Format (key fields)

```json
{
  "schema_version": 3,
  "vivid_version": "1.2.3",
  "meta": {
    "title": "Wavetable Pad",
    "domains": ["audio", "control"],
    "content_kind": "instrument",
    "preview_controls": [
      { "node": "osc1", "param": "frequency", "label": "Freq" }
    ]
  },
  "nodes": [
    { "id": "lfo1", "type": "lfo", "params": {"freq": 1.5}, "x": 100, "y": 200 }
  ],
  "connections": [
    { "from": "lfo1/value", "to": "shape1/rotation" }
  ],
  "metronome": {
    "enabled": true,
    "bpm": 120.0,
    "beats_per_bar": 4
  },
  "midi_mappings": [...],
  "session": {
    "tracks": [...],
    "scenes": [...],
    "active_clips": {"track_id": "clip_id"}
  },
  "node_presets": { "lfo1": [{ "name": "slow", "params": {"freq": 0.1} }] }
}
```

`metronome` is optional graph-level transport metadata. It does not create a global timeline or a
special node; it simply provides an optional shared pulse that clocks and quantized session
launches can follow. Graphs that omit it behave as before: no shared metronome, no forced sync.

Operators opt into that pulse explicitly. Time-based operators now use one of two shared contracts:

- `rate_mode = free | external | metronome` for operators with an internal oscillator/rate
  (`LFO`, `StepSeq`, `Chorus`, `Flanger`, `Phaser`)
- `clock_source = external | metronome` for beat-phase-driven operators that still expose a
  `beat_phase` input for explicit wiring
- the same metronome snapshot is now exposed on frame, audio, and GPU operator contexts, so
  user-authored GPU operators can read graph tempo state directly too

In other words, the graph metronome adds a discoverable shared pulse without replacing external
clock wiring or forcing all temporal logic through one global source.

`quantize_clock` is still read and written for backward compatibility with older graphs and tools.
Session launch quantization uses the graph metronome.

At runtime, that transport is sampled from `RuntimeCore`'s live metronome state rather than from
the compiled graph metadata. That means:

- BPM changes retime operators immediately while preserving the current beat position
- meter changes restart the live bar at beat 0 / bar 0
- topology rebuilds and hot reloads can preserve the current live transport instead of resetting it
- graph load / new graph / snapshot apply reseed the live transport from the saved graph metadata

Shader-backed operators are persisted exactly like any other operator: a node stores the concrete
operator `type` (for example `"Blur"`), and the shader source lives in a real `.wgsl` file under
core `filters/`, a package `filters/`, or the project-local `<graph_dir>/filters/` directory.

## Lane Transport (CompiledGraph)

Lane data between compiled nodes uses `LaneBufferRef` — an intrusive-refcount reference to immutable `LaneBuffer` storage. The frame executor propagates lanes via ref sharing (zero-copy passthrough), pool-allocated buffers (remap/merge/normalization), and runtime-owned output builders.

Lane behavior controls how provenance is checked during compilation:

- `Pointwise` operators process one lane at a time and may receive one non-scalar lane lineage plus scalar broadcasts. Multiple independent non-scalar lane sets are rejected even when their current lane counts match.
- `Structural` operators create, reshape, reorder, or filter lanes and therefore allocate fresh lane-set provenance for their outputs.
- `Reduction` operators collapse lane collections back to scalar outputs.
- `Kernel` operators consume a whole lane collection or neighborhood. They may accept multiple lane-array inputs with different provenance when the operator owns the collection semantics.

Operators that read `ctx->input_lanes[...]` as whole arrays should declare `VIVID_LANE_KERNEL` instead of relying on the default pointwise behavior.

Key runtime types:
- `LaneBuffer` — CPU-backed float array with optional GPU storage-buffer backing and intrusive refcount
- `LaneBufferRef` — RAII reference wrapper (retain on copy, release on destroy, never deallocates)
- `LaneBufferPool` — pre-allocated buffer pool for frame-thread lane allocation (remap, merge, normalization)
- `BridgeLaneSlot` — pre-allocated bridge slot for cross-cadence lane data (replaces old fixed 64-element `LaneSnapshot`)

Canonical lane values live in `CompiledNode::input_lane_refs` / `output_lane_refs`. The old `input_lanes` / `output_lanes` vector fields remain as bridge injection scratch for audio→frame analysis data.

GPU lane promotion: `plan_gpu_lane_promotion()` conservatively promotes lane arrays feeding GPU consumers above `kGpuLanePromotionThreshold` (256) to GPU storage-buffer backing, with lazy CPU→GPU upload cached per frame.

Allocation policy: `kDefaultLaneCapacity` (1024) is the default initial pool-buffer size. The frame executor's `LaneBufferPool` is constructed **growable**, so remap/merge/normalization buffers that exceed the initial capacity grow on the frame thread (allocation is safe there) rather than silently truncating to an empty buffer. The audio bridge's lane slots stay no-alloc for real-time safety. `CompiledGraph::max_lane_elements` (default 16,777,216) is a nominal upper-bound field and is **not currently enforced** in the allocation path.

## Audio Lane Width Negotiation

Audio compilation tracks two related but different notions of width:

- `input_channel_counts` / `output_channel_counts` describe how many channels an operator instance processes on a given port.
- direct audio edges also carry an effective wire width that can remain lane-expanded even when the upstream node processes one lane at a time internally.

This distinction matters for lane-aware audio chains. `InstancePerLane` and `LoopBased` audio nodes execute with mono per-lane buffers, but a direct edge leaving those nodes can still semantically represent one audio stream per upstream lane. During Pass 4, the compiler re-propagates that effective wire width after lane execution planning so downstream reduction consumers receive the full lane-expanded bundle on their inputs instead of collapsing back to descriptor default mono.

## Lane State Lifecycle

`LaneStateService` stores per-node state keyed by `(node_idx, lane_id)`, but lane retirement is
handled at the lane identity level. When a note-stream helper or synth breakout path retires a
`lane_id`, the runtime clears that identity's state across every downstream node that used it, not
just the caller's own entry. This matters for long-running polyphonic graphs: oscillators, filters,
and envelopes all keep their own per-lane state, so reclaiming only the originating node would leak
voice state as notes churn.

On the frame path, retired lane IDs are swept at the start of each `FrameExecutor::tick()`. On the
audio path, retired lane IDs are swept at audio block boundaries before the next callback begins
processing node state for that block.

Lane IDs are either positional (rebuilt per-graph) or identity-bearing (the `identity_bearing`
flag on a lane set, set by the compiler when an upstream operator emits explicit `lane_ids`), which
keeps per-lane state stable across reordering/compaction within a running graph. Lane identity is
**not** preserved across a full recompile: a topology change or graph reload re-runs the compiler
and lane IDs are re-allocated from scratch, so per-lane persistent state does not survive a rebuild.

In practice, this is what lets compiled module-internal chains like pointwise lane-aware audio → reduction mixer preserve per-voice audio through flattening and route it correctly into the reducer.
