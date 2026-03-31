# Lanes Post-Clean-Break Audit

Lane semantics and runtime implementation are strong. The clean break is much closer than before, and the float-lane public API plus tool-facing type strings improved significantly. The remaining gap is mostly contributor-facing language and a smaller set of leftover `spread` terms, not the lane execution model itself.

## Findings

1. **The docs and authoring guidance are still not consistently lane-first**

   Representative references:
   - [core_api.md](/Users/jeff/Developer/vivid/mcp/opdev_docs/core_api.md:74)
   - [control_domain.md](/Users/jeff/Developer/vivid/mcp/opdev_docs/control_domain.md:24)
   - [audio_domain.md](/Users/jeff/Developer/vivid/mcp/opdev_docs/audio_domain.md:29)
   - [vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py:59)
   - [vivid-runtime-architecture.md](/Users/jeff/Developer/vivid/docs/vivid-runtime-architecture.md:134)
   - [ARCHITECTURE-GUARDRAILS.md](/Users/jeff/Developer/vivid/docs/ARCHITECTURE-GUARDRAILS.md:9)

   Conclusion: the implementation is lane-native, but the onboarding path still contains enough `spread` language to feel transitional.

2. **`spread` is no longer the main float-lane product term, but it still survives in adjacent API and naming surfaces**

   Representative references:
   - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:70)
   - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:72)
   - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:82)
   - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:191)

   The important split is:
   - `VIVID_PORT_LANE_ARRAY` is correct
   - `VIVID_PORT_STRING_SPREAD` still exists
   - some comments still describe lane arrays as spreads

   Conclusion: the main rename landed, but the code still reads like a partial conceptual cleanup rather than a totally clean-slate naming model.

3. **There is still contributor-facing residue in comments, fixtures, and test helpers**

   Representative references:
   - [spread_source_op.cpp](/Users/jeff/Developer/vivid/tests/operators/spread_source_op.cpp:1)
   - [spread_sink_op.cpp](/Users/jeff/Developer/vivid/tests/operators/spread_sink_op.cpp:1)
   - [test_cross_cadence_spread.cpp](/Users/jeff/Developer/vivid/tests/test_cross_cadence_spread.cpp:39)
   - [tile.cpp](/Users/jeff/Developer/vivid/operators/control/tile/tile.cpp:5)
   - [stack.cpp](/Users/jeff/Developer/vivid/operators/control/stack/stack.cpp:6)
   - [frame_executor.cpp](/Users/jeff/Developer/vivid/src/runtime/frame_executor.cpp:124)

   Conclusion: these are mostly readability and onboarding residue, not architectural problems, but they still make the repo feel post-migration rather than clean-slate.

## What Improved

- The main float-lane ABI is lane-native:
  - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:70)
  - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:239)
  - [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:287)
- Tool-facing float lane-array strings are much better:
  - [control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp:94)
  - [control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp:2319)
  - [node_graph_util.h](/Users/jeff/Developer/vivid/src/ui/node_graph_util.h:196)
- Key helper and internal names improved:
  - [child_op.h](/Users/jeff/Developer/vivid/src/operator_api/child_op.h:72)
  - [snapshot_types.h](/Users/jeff/Developer/vivid/src/runtime/snapshot_types.h:17)
- The semantic core remains strong:
  - [lane_types.h](/Users/jeff/Developer/vivid/src/runtime/lane_types.h)
  - [graph_compiler.cpp](/Users/jeff/Developer/vivid/src/runtime/graph_compiler.cpp:17)
  - [compiled_graph.h](/Users/jeff/Developer/vivid/src/runtime/compiled_graph.h:137)

## Direct Answers

- **How well did we do at implementing `docs/lanes-architecture.md`?**
  Pretty well at the semantic and runtime level. The compiler, provenance model, execution strategy model, and main float-lane ABI now line up with the architecture much more closely.

- **Will it seem to a newcomer as if we built Vivid with lanes from the beginning?**
  Not fully. The core headers and runtime feel much closer, but the docs and contributor-facing language still make the repo feel post-migration.

- **Is all spread cruft gone?**
  No. The remaining cruft is mostly docs, string-array naming, comments, test fixtures, and a smaller set of leftover helper and runtime descriptions.

## Recommended Remaining Cleanup

1. **Finish the documentation cleanup**

   Make opdev docs, runtime docs, and contributor guardrails consistently lane-first.

2. **Decide how far to take string-array naming**

   Either keep `STRING_SPREAD` intentionally and document why, or rename it for symmetry with `LANE_ARRAY`.

3. **Clean up readability residue**

   Rename or modernize comments, fixtures, and helper descriptions that still describe lane-bearing transport as spreads.

Caution:
- do **not** rename unrelated domain uses such as stereo spread or spatial spread params
- focus only on lane-bearing transport and model terminology

## Strategic Surfaces

Strongly lane-native semantic and model surfaces:
- `VIVID_PORT_LANE_ARRAY`
- `VividLanePort`
- `input_lanes` / `output_lanes`
- `LaneBehavior`
- `LaneExecutionStrategy`

Remaining spread-era surfaces that still matter:
- `VIVID_PORT_STRING_SPREAD`
- comments and docs that still call lane arrays `spread`
- test and helper names like `SpreadSourceOp` and `SpreadSinkOp`

The important distinction is:
- the semantic and model surfaces are now strongly lane-native
- tool and API surfaces improved materially
- the remaining residue is mostly documentation and readability related

## Audit Summary

Validated by current audit:
- the semantic execution model is lane-native
- the main float-lane ABI is lane-named
- control-server and UI float type strings now say `lane_array`
- important helper and internal names like `LaneSnapshot` and `set_input_lane_data` landed

Still not fully clean for newcomers:
- docs that still explain lanes through spread vocabulary
- remaining string-array and helper naming asymmetry
- comments and fixtures that still read as spread-era holdovers

## Bottom Line

The semantic implementation is strong. The float-lane clean break is mostly there. The remaining gap is contributor-facing clarity, not architectural correctness.
