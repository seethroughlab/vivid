# Inner/Outer Loop Test Plan

This document defines manual test procedures for inner/outer loop iteration:

- Inner loop: parameter tweaking in a running graph
- Outer loop: operator source edits with hot-reload in a running graph

Use this plan with `docs/testing/MANUAL-TEST-CATALOG.md`. That catalog defines broad functional coverage; this document focuses on iteration speed and reliability.

## Test Preconditions

- Build: Debug and RelWithDebInfo
- Platform: macOS 14+ (Apple Silicon and Intel where available)
- Launch Vivid from `build/` with operator hot-reload enabled
- Ensure audio output and GPU rendering are both active
- Keep a terminal visible to watch compile/reload logs

## Evidence to Capture

Record for each test case:

- `Result`: Pass / Fail
- `Graph`: filename or test graph name
- `Operator`: type/name (outer-loop tests)
- `Latency`: rough observed delay for update/reload (milliseconds or qualitative: instant/<1s/>1s)
- `Notes`: errors, screenshots, and relevant log lines

## Inner Loop: Parameter Tweaking

## Workflow Definition

Expected inner-loop flow:

1. User changes a parameter (slider, XY pad, color, typed input).
2. Runtime applies the value immediately.
3. Visual and/or audio output responds continuously during interaction.
4. Final value persists in graph state.

## Recommended Test Graphs

- GPU-heavy graph: a chain such as noise -> bloom -> composite
- Audio-heavy graph: oscillator/synth -> effects -> AudioOut
- Mixed graph: control drives both audio and GPU parameters

## Checklist

### IL-1 Slider Real-Time Response

- Steps:
  1. Select a frequently changing parameter (e.g., gain, frequency, blend amount).
  2. Drag slowly across full range, then quickly scrub back and forth.
- Pass criteria:
  - Output updates continuously while dragging (no visible/audible stepping beyond expected quantization).
  - UI remains responsive; no noticeable lag spikes.
- Fail criteria:
  - Delayed updates, frozen UI, or output changes only on mouse release.

### IL-2 XY Pad Dual-Parameter Response

- Steps:
  1. Drag diagonally and circularly across the full XY pad area.
  2. Pause at corners and edges.
- Pass criteria:
  - Both mapped parameters update simultaneously and correctly.
  - Values clamp cleanly at bounds without jitter.
- Fail criteria:
  - Axis inversion, one axis not updating, or erratic jitter.

### IL-3 Color Picker Propagation

- Steps:
  1. Change color across distinct hues and brightness values.
  2. Confirm downstream GPU nodes are affected.
- Pass criteria:
  - Visual result changes after each confirmed color update.
  - No stale frame remains after color change.
- Fail criteria:
  - Delayed propagation, incorrect color mapping, or required extra interaction.

### IL-4 Typed Numeric Input Validation

- Steps:
  1. Enter valid in-range value.
  2. Enter out-of-range value.
  3. Enter invalid text.
- Pass criteria:
  - Valid values apply immediately.
  - Out-of-range values clamp or reject with stable UI.
  - Invalid text does not corrupt parameter state.
- Fail criteria:
  - UI breakage, NaN/invalid state persistence, or mismatch between displayed and actual value.

### IL-5 Continuous Drag Coalescing

- Steps:
  1. Perform a long continuous slider drag.
  2. Undo once.
- Pass criteria:
  - Single undo returns to pre-drag value.
- Fail criteria:
  - Multiple undos required for one continuous drag.

## Outer Loop: Operator Editing + Hot-Reload

## Workflow Definition

Expected outer-loop flow:

1. User edits operator source (`.cpp`, plus `.wgsl` where applicable).
2. File watcher detects save.
3. Hot-reload build runs automatically.
4. Runtime swaps to new operator build without restarting Vivid.
5. Graph keeps state (params, wires, node layout).

## Domain Coverage Matrix

- Control operator reload
- Audio operator reload
- GPU operator reload

At least one operator per domain must be tested each run.

## Checklist

### OL-1 Control Operator Hot-Reload

- Steps:
  1. Load graph with a control operator that has visible downstream effect.
  2. Edit a small behavior detail in operator `.cpp`.
  3. Save and observe reload.
- Pass criteria:
  - Recompile/reload completes without app restart.
  - New behavior is visible in graph output.
- Fail criteria:
  - No reload trigger, stale behavior, or runtime instability.

### OL-2 Audio Operator Hot-Reload

- Steps:
  1. Load graph routing an audio operator to AudioOut.
  2. Change operator logic in `.cpp` (e.g., gain scaling constant).
  3. Save and monitor output.
- Pass criteria:
  - Reload succeeds and audible behavior updates.
  - Audio remains stable (no prolonged dropouts/crash).
- Fail criteria:
  - Reload breaks audio path or requires full restart to recover.

### OL-3 GPU Operator Hot-Reload

- Steps:
  1. Load graph with a GPU operator chain.
  2. Edit `.cpp` or associated shader behavior.
  3. Save and observe render output.
- Pass criteria:
  - Reload occurs and render reflects change.
  - No persistent black frame or validation spam.
- Fail criteria:
  - Shader/operator change ignored or render pipeline enters bad state.

### OL-4 Syntax Error Handling

- Steps:
  1. Introduce a deliberate syntax error in operator `.cpp`.
  2. Save to trigger hot-reload.
  3. Fix error and save again.
- Pass criteria:
  - Error is surfaced clearly.
  - Operator stays on last good version during failed compile.
  - After fix, reload recovers automatically.
- Fail criteria:
  - Crash, silent failure, or operator removed permanently after a transient error.

### OL-5 Missing Include Handling

- Steps:
  1. Add an invalid `#include` path.
  2. Save; then restore valid include and save again.
- Pass criteria:
  - Compile failure is reported with actionable diagnostics.
  - Last good operator continues running; fixed version reloads successfully.
- Fail criteria:
  - Graph node becomes unrecoverable or runtime remains stuck after fix.

### OL-6 Linked Package Operator Reload

- Steps:
  1. `vivid link` a local package.
  2. Use linked operator in graph.
  3. Edit operator in linked source directory and save.
- Pass criteria:
  - File watcher detects linked-path change.
  - Reload compiles from linked source and graph updates.
- Fail criteria:
  - Watcher misses linked edits or rebuild targets wrong source.

### OL-7 State Preservation Across Reload

- Steps:
  1. Arrange custom node layout and parameter values.
  2. Trigger successful hot-reload on an operator in use.
  3. Inspect graph state after reload.
- Pass criteria:
  - Parameter values, wire connections, and node positions are unchanged.
- Fail criteria:
  - Any state resets unexpectedly due to reload.

## Exit Criteria

Inner/outer loop testing is complete when:

- This document is present and current.
- All IL/OL checklist cases have at least one recorded pass on current `master`.
- Any failures have linked issues or follow-up tasks.
