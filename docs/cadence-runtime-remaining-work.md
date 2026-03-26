# Cadence-Aware Runtime: Remaining Work

Tracks outstanding items from the cadence-aware runtime migration. The core architecture is complete and merged to master. These are deferred items that expand coverage or add UX.

## Completed

- [x] CompiledGraph as sole runtime authority (Scheduler, AudioEngine consolidated)
- [x] FrameExecutor / AudioExecutor / CadenceBridge integrated
- [x] 11 control operators migrated to dual-cadence
- [x] AudioExecutor audit: auto-dup, custom ports, channel_index, channel offsets, string ptrs, recording overrun
- [x] alternate, spread_noise, stack migrated to dual-cadence (no bridge infrastructure needed — CadenceBridge already handles cross-cadence spreads; AudioExecutor already routes audio-to-audio spreads)

- [x] ChildOp audio-context forwarding: `process_audio()` method added to `ChildOp<T>`, `modulated_gain` migrated to dual-cadence

## Operator Timing Redesign (15 operators)

These operators cannot be made audio-capable mechanically. Their timing models assume frame-rate `dt` (~16ms) and break at audio-rate `dt` (~20µs). Each needs individual design work.

### Time-accumulating operators

| Operator | Issue | Fix pattern |
|----------|-------|-------------|
| `mseg` | Breakpoint envelope timing compresses ~800x | External phase input or cadence-scaled duration |
| `path_animate` | Speed parameter assumes frame-rate traversal | Explicit phase input or cadence-aware speed |
| `random` | Trigger edge detection meaningless at 48kHz | Phase-driven RNG state advancement |
| `random_sh` | Timed mode accumulates phase at control rate | Phase-driven redesign with explicit rate semantics |

### Beat/sequence operators

| Operator | Issue | Fix pattern |
|----------|-------|-------------|
| `arpeggiator` | Gate transitions need phase-driven timing | Refactor to beat-phase wrap model (like `state_machine`) |
| `chord_progression` | Same | Same |
| `drum_sequencer` | Same | Same |
| `euclidean` | Same | Same |
| `note_pattern` | Same | Same |
| `pattern_seq` | Same | Same |
| `sequencer` | Same | Same |
| `step_seq` | Same | Same |
| `tracker` | Same | Same |

**Reference implementation:** `state_machine` uses beat-phase wraps for all timing decisions, making it naturally cadence-agnostic. This is the target pattern.

- [x] Cadence selection UI: per-node override in NodeDef, GraphCompiler reads it, inspector selector (auto/frame/audio cycle), MCP set_cadence_override command, cadence_capability exposed in inspect_graph
