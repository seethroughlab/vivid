# Lanes Architecture Follow-Up Audit

The lane architecture is strongly implemented in the semantic/runtime core, and the main float-lane API plus tool-facing strings are now in good shape. The remaining gap is mostly newcomer-facing clarity: the repo still does not fully read as if lanes were there from the beginning, and the spread residue that remains is now concentrated in comments, helper descriptions, tests/fixtures, and string-array naming asymmetry.

## Findings

### 1. Contributor-facing narration still contains spread-era language

The main remaining problem is now how parts of the repo explain themselves, not how the runtime works.

Representative references:
- [`mcp/vivid_opdev_mcp.py`](../mcp/vivid_opdev_mcp.py)
- [`src/operator_api/types.h`](../src/operator_api/types.h)
- [`src/operator_api/embedded_op.h`](../src/operator_api/embedded_op.h)
- [`src/runtime/audio_executor.cpp`](../src/runtime/audio_executor.cpp)
- [`src/runtime/frame_executor.cpp`](../src/runtime/frame_executor.cpp)

The runtime model is lane-native, but the repo still explains some lane-bearing transport as “spread.” That makes the codebase feel migrated rather than fully born-lane-native.

### 2. Public naming asymmetry remains around string collections

Float lane transport is much cleaner now, but string collections still preserve the old vocabulary.

Representative references:
- [`src/operator_api/types.h`](../src/operator_api/types.h)

The current state is asymmetrical:
- `VIVID_PORT_LANE_ARRAY` is correct and established
- `VIVID_PORT_STRING_SPREAD` and related naming still remain

That leaves one visible inconsistency in the public model.

### 3. Tests, fixtures, and some operator comments still reveal migration history

This is lower-severity residue, but it still matters for newcomer perception.

Representative references:
- [`tests/operators/spread_source_op.cpp`](../tests/operators/spread_source_op.cpp)
- [`tests/operators/spread_sink_op.cpp`](../tests/operators/spread_sink_op.cpp)
- [`tests/test_cross_cadence_spread.cpp`](../tests/test_cross_cadence_spread.cpp)
- [`operators/control/fft_analysis/fft_analysis.cpp`](../operators/control/fft_analysis/fft_analysis.cpp)
- [`operators/control/repeat/repeat.cpp`](../operators/control/repeat/repeat.cpp)

This is mostly readability residue. It will not confuse the compiler, but it can still confuse contributors exploring the repo.

## What Improved

Since the earlier clean-break audit, several important surfaces are now materially better.

- The main float-lane API is lane-native:
  - [`src/operator_api/types.h`](../src/operator_api/types.h) uses `VIVID_PORT_LANE_ARRAY`
  - [`src/operator_api/types.h`](../src/operator_api/types.h) uses `input_lanes`
  - [`src/operator_api/types.h`](../src/operator_api/types.h) uses `output_lanes`
- Tool-facing float type strings now use `lane_array`:
  - [`src/runtime/control_server.cpp`](../src/runtime/control_server.cpp)
  - [`src/ui/node_graph_util.h`](../src/ui/node_graph_util.h)
- Authoring docs improved materially:
  - [`mcp/opdev_docs/core_api.md`](../mcp/opdev_docs/core_api.md)
  - [`mcp/opdev_docs/control_domain.md`](../mcp/opdev_docs/control_domain.md)
  - [`mcp/opdev_docs/audio_domain.md`](../mcp/opdev_docs/audio_domain.md)
  - [`docs/ARCHITECTURE-GUARDRAILS.md`](./ARCHITECTURE-GUARDRAILS.md)
- The semantic core remains strong:
  - [`src/runtime/lane_types.h`](../src/runtime/lane_types.h)
  - [`src/runtime/graph_compiler.cpp`](../src/runtime/graph_compiler.cpp)
  - [`src/runtime/compiled_graph.h`](../src/runtime/compiled_graph.h)

## Direct Answers

- **How well did we implement `docs/lanes-architecture.md`?**  
  Pretty well. The semantic/runtime side is strong and the main float-lane API now aligns closely with the architecture.

- **Will it seem to a newcomer as if Vivid was built with lanes from the beginning?**  
  Not fully. The core implementation is close, but surrounding commentary and fixture naming still reveal migration history.

- **Is all spread cruft gone?**  
  No. The remaining cruft is mostly narrative and naming residue, not execution-model residue.

## Recommended Remaining Cleanup

1. **Finish comment and helper-description cleanup**  
   Replace spread-era commentary where it describes lane-bearing transport.

2. **Decide whether to keep or rename string spread terminology**  
   Either keep it intentionally and document the distinction, or rename it for conceptual symmetry.

3. **Modernize tests and fixtures**  
   Rename fixture/test helpers that still describe lane transport as spreads, and update operator comments that still narrate lane arrays as spreads.

Caution:
- Do not rename unrelated uses like stereo spread, spatial spread, or other domain-specific meanings of `spread`.

## Assumptions

- This is a new standalone report, not an edit to the prior audit docs.
- The document is focused on present-state evaluation, not future phase planning.
- The overall conclusion is that the lane architecture is substantially implemented; the remaining gap is newcomer-facing clarity and naming consistency.
