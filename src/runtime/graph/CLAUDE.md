# Graph Subsystem

## Purpose

This directory contains the graph compiler and both execution engines. It transforms the declarative `Graph` data model (nodes, connections, parameters) into a live `CompiledGraph` that the frame and audio executors process each tick.

## Key Files

| File | Role |
|------|------|
| `graph.h/cpp` | `Graph` — pure data model (nodes, connections, params). Serialized to/from JSON. Not execution state. |
| `graph_compiler.h/cpp` | 7-pass compiler: `Graph` + `OperatorRegistry` → `CompiledGraph` |
| `graph_compiler_init.cpp` | Frame and audio state initialization helpers used by Pass 1 |
| `graph_compiler_planning.cpp` | Lane execution strategy planner (Passes 4c–4d) |
| `graph_compiler_reload.cpp` | Incremental reload: preserves operator state across recompiles |
| `graph_compiler_internal.h` | Shared constants and helpers across compiler files |
| `compiled_graph.h` | `CompiledGraph` — the compiled execution state shared read-only by both executors |
| `frame_executor.h/cpp` | `FrameExecutor` — processes frame-cadence nodes in topo order each tick |
| `audio_executor.h/cpp` | `AudioExecutor` — processes audio-cadence nodes in the real-time audio callback |
| `lane_state.h` | `LaneStateService` — identity-keyed per-lane persistent state storage |
| `lane_types.h` | Lane set types, provenance, execution strategy enums |
| `cadence_types.h` | `Cadence` enum (Frame, Audio) and `BridgeKind` |
| `snapshot_types.h` | `ParamSnapshot`, `AnalysisSnapshot`, `LaneSnapshot` — cross-cadence data structs |
| `graph_snapshot_builder.h/cpp` | Builds the read-only `GraphSnapshot` consumed by the UI |
| `subgraph_module.h/cpp` | Subgraph expansion and module instance management |
| `port_type_registry.cpp` | Runtime custom port type registration |

## How It's Organized

### Graph Compiler

`GraphCompiler::compile()` runs 7 passes that progressively build the `CompiledGraph`:

1. **Pass 1 — Create nodes.** Instantiates `CompiledNode` for each `NodeDef`. Loads operators via the registry, determines cadence from the descriptor, initializes frame-side and audio-side state. Missing operators get placeholder nodes with diagnostic info.

2. **Pass 2 — Resolve edges.** Converts `ConnectionDef`s into `CompiledEdge`s. Resolves port indices, detects type mismatches, classifies bridge kind for cross-cadence edges, and builds the adjacency graph.

3. **Pass 2.6 — Lane-set propagation.** Walks the graph in topo order propagating lane-set provenance through edges. Determines which nodes see multi-lane inputs and what their effective lane count is.

4. **Pass 4 — Audio channel negotiation.** Four sub-passes: (a) explicit channel counts from descriptors, (b) propagation via audio Direct edges, (c) audio lane execution strategy via planner, (d) frame lane execution strategy via planner.

5. **Pass 5 — Audio buffer allocation.** Pre-allocates per-node planar audio buffers based on negotiated channel counts.

6. **Pass 6 — Partition edges.** Separates edges into frame Direct, audio Direct, and Snapshot categories for the two executors.

7. **Pass 7 — Finalize.** Error summary, diagnostics, dropped connection reporting.

Topology changes always trigger a full recompile. The `CompiledGraph` is never mutated during execution — it is shared read-only between both executors.

### Frame Executor

`FrameExecutor::tick()` walks `frame_order` (the topo-sorted frame-cadence nodes) once per frame. For each node it sets up the lane context, copies wire values from upstream outputs to downstream inputs, calls `process_frame()` or `process_gpu()`, and propagates lane data. GPU nodes are dispatched via a callback to the GPU context.

### Audio Executor

`AudioExecutor` runs on the real-time audio thread. `build()` pre-computes lane lift groups — sets of `InstancePerLane` operators that need per-lane cloned instances for multi-lane audio processing. The audio callback walks `audio_order`, processes each node (or each lane instance for lifted nodes), and writes to the output device buffer. Real-time constraints apply: no allocation, no locking, no blocking.

Cross-cadence data flows through `AudioFrameBridge`: frame→audio via `ParamSnapshot` (atomic index swap), audio→frame via `AnalysisSnapshot`. `LaneSnapshot` carries lane data in both directions using fixed-size 64-element structs to avoid audio-thread heap allocation.

### Lane State

`LaneStateService` provides per-lane persistent state keyed by `(node_idx, lane_id)`. Operators access it via the `vivid_lane_state(ctx, lane_id, T)` macro. Lane IDs can be positional (rebuilt per-graph) or identity-bearing (stable across reordering). The service handles allocation, lookup, and retirement of lane state.

## Relationships

- **Upstream:** `Graph` (data model), `OperatorRegistry` (type→loader mapping)
- **Downstream:** `RuntimeCore` owns the compiler and frame executor; `AudioEngine` owns the audio executor
- **Cross-cutting:** `AudioFrameBridge` connects the two executors without either blocking

## See Also

- `docs/runtime/graph.md` — `Graph` data model and JSON schema
- `docs/runtime/runtime_core.md` — how `RuntimeCore` orchestrates compilation and frame ticks
- `docs/runtime/audio_engine.md` — audio device lifecycle and bridge details
- `docs/ARCHITECTURE.md` §5.4 (dual-cadence model), §5.5 (bridges), §5.9 (lanes)
