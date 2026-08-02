# Operator ABI changelog

`VIVID_OPERATOR_ABI_VERSION` (in `app/src/operator_api/types.h`) is bumped whenever the
operator-facing C ABI changes. The host loads any operator declaring an ABI in
`[VIVID_OPERATOR_ABI_MIN_LOADABLE, VIVID_OPERATOR_ABI_VERSION]` — a **range**, checked in
`app/src/gpu/operator_loader.cpp`. **Never narrow that to an exact match**: every installed dylib
built at an older-but-additive ABI would be orphaned at a stroke.

**Current: v15.** The runtime loads any operator in **[v11, v15]** — every change since v11 has
been purely additive (new fields appended to the END of the context structs, or new enum values
appended at the numeric end), so an older dylib still finds every field it reads at the same offset.
`VIVID_OPERATOR_ABI_MIN_LOADABLE` is the floor; bump it only for a change that is *not* additive.

| Version | Change |
|--------:|--------|
| **v15** | ADR-0047. `VividPortTransport` gains `NOTE_STREAM` (8u) and `CONTROL_SIGNAL` (9u), appended at the numeric end (retired 2u/4u slots are never reused). A note/control operator can now declare its output port's real stream type instead of a fake `AUDIO_BUFFER` shim. Additive — a v14 operator never sets these, and audio routing stays flag-driven, so nothing changes for existing ops. The host dispatches transport by explicit equality (no catch-all), so the new values fall through to the same handling as before unless explicitly routed. |
| **v14** | Operator-drawn cell thumbnails + an audio-role hint. Two OPTIONAL exports (`vivid_draw_thumbnail(instance, VividThumbnailContext*)` and `uint32_t vivid_audio_role(void)`) the host `dlsym`s — absent ⇒ opt out, so nothing breaks for a dylib that omits them. `VividOperatorDescriptor` gains `audio_role` appended at the END, but the host fills its OWN descriptor from a virtual (`declared_audio_role`), so it never reads the field out of an older dylib's shorter struct. `VividThumbnailContext` is a new host→op struct (draw API + param snapshot + accent). Lets a loaded audio dylib declare itself a generator / note-effect / modulator and draw its own session-grid cell preview. |
| **v13** | ADR-0022. `VividAudioContext` gains `control_out` / `control_out_capacity` so a modulator operator (LFO / envelope / random) can emit a normalized 0..1 control signal, one sample per frame. Additive — a v12 operator never touches it. Polarity lives on the control EDGE, not the source. |
| **v12** | ADR-0015. `VividAudioContext` gains `note_out` / `note_out_capacity` / `note_out_count` so a note-effect operator (arpeggiator / chord / transpose) can emit notes. Additive — a v11 operator never touches it. |
| **v11** | Audio operator API. `VividAudioContext` gains `note_events` / `note_event_count` (`VividNoteEvent`) so instrument operators are MIDI-driven. Roles: *effect* = audio-in → out; *generator* = params/transport → out; *instrument* = note_events → out. Additive. |
| **v10** | Removed `VividOperatorDescriptor.lane_behavior` and the `VividLaneBehavior` enum / `VIVID_LANE_*` constants. Operators now declare multiplicity via `static constexpr VividMultiplicityBehavior kMultiplicityBehavior` (defaults to `Map`). |
| **v9**  | Removed `ctx.lane_set_id` from `VividFrameContext` / `VividAudioContext` (lane-set provenance retired; per-element identity remains via `ctx.lane_id` + `vivid_lane_state`). |
| **v8**  | Removed the operator-facing lane C-API (`VividLaneView` / `VividLaneOutput` / `VividStringLaneView` / `VividStringLaneOutput` + `ctx` `input_lanes` / `output_lanes` / `*_string_lanes`). Operators use the value API (`ctx->values` / `value_outputs`). |
| **v7**  | Retired the `VIVID_PORT_LANE_ARRAY` / `VIVID_PORT_STRING_LANES` port types (+ transport variants). Port arity is declared via `VividPortDescriptor.multiplicity`. |
| **v6**  | Value model, phase 1: `VividOperatorDescriptor.multiplicity_behavior` + `VividPortDescriptor.{value_type,multiplicity}`. |
| **v5**  | `VividPortDescriptor.gpu_texture_format`. |
| **v4**  | `VividInspectorCommandAPI.{begin_undo_group,end_undo_group}`. |

The value model that superseded the lane system (the vocabulary, the old→new mapping, identity
semantics per behavior) is documented in `docs/runtime/value-model.md`.
