# Operator ABI changelog

`VIVID_OPERATOR_ABI_VERSION` (in `app/src/operator_api/types.h`) is bumped whenever the
operator-facing C ABI changes in an incompatible way. It exists to **catch stale dylibs during
hot-reload** — it is not a cross-version compatibility promise. The host refuses to load an
operator package whose declared `abi` does not match.

**Current: v11.**

| Version | Change |
|--------:|--------|
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
