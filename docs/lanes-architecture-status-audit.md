# Lanes Architecture Status Audit

The semantic/runtime/compiler architecture is strongly aligned with [lanes-architecture.md](./lanes-architecture.md). The lane-native core is real. The repo still does not fully feel clean-slate to a newcomer, and the remaining spread residue is now mostly comments, UI/debug labels, tests, and incidental contributor-facing wording rather than architectural structure.

## Findings

### 1. `lanes-architecture.md` is substantially implemented in the runtime and public API

At the model/compiler/runtime level, the architecture is mostly real.

Representative references:
- [src/operator_api/types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h)
- [src/operator_api/types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:72)
- [src/runtime/control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp:96)
- [src/ui/node_graph_util.h](/Users/jeff/Developer/vivid/src/ui/node_graph_util.h:198)
- [src/runtime/lane_types.h](/Users/jeff/Developer/vivid/src/runtime/lane_types.h)
- [src/runtime/graph_compiler.cpp](/Users/jeff/Developer/vivid/src/runtime/graph_compiler.cpp)
- [src/runtime/compiled_graph.h](/Users/jeff/Developer/vivid/src/runtime/compiled_graph.h)

The important pieces are in place:
- the architecture is mostly real at the model/compiler/runtime level
- float and string lanes are present in the public type system
- the lane-native vocabulary now reaches the control server and UI port-type surfaces

### 2. The repo still does not fully read as “lanes from day one”

The remaining problem is mostly narration rather than architecture.

Representative references:
- [operators/control/string_select/string_select.cpp](/Users/jeff/Developer/vivid/operators/control/string_select/string_select.cpp:10)
- [operators/control/alternate/alternate.cpp](/Users/jeff/Developer/vivid/operators/control/alternate/alternate.cpp:7)
- [operators/control/sequencer/sequencer.cpp](/Users/jeff/Developer/vivid/operators/control/sequencer/sequencer.cpp:92)
- [src/runtime/graph_compiler.cpp](/Users/jeff/Developer/vivid/src/runtime/graph_compiler.cpp:38)
- [src/runtime/compiled_graph.h](/Users/jeff/Developer/vivid/src/runtime/compiled_graph.h:141)
- [src/ui/node_graph_draw.cpp](/Users/jeff/Developer/vivid/src/ui/node_graph_draw.cpp:657)

Contributor-facing narration still uses spread terminology in important places. These are now mostly comments, helper descriptions, or debug/UI text, but they still give a newcomer the feeling of a migration rather than a clean-slate system.

### 3. Spread cruft is reduced, but not gone

The remaining cruft is no longer structural, but it still exists and can still confuse contributors.

Representative references:
- [tests/test_string_ports.cpp](/Users/jeff/Developer/vivid/tests/test_string_ports.cpp:63)
- [tests/test_graph_compiler_init.cpp](/Users/jeff/Developer/vivid/tests/test_graph_compiler_init.cpp:271)
- [tests/test_operator_creator.cpp](/Users/jeff/Developer/vivid/tests/test_operator_creator.cpp:713)
- [docs/runtime/audio_engine.md](/Users/jeff/Developer/vivid/docs/runtime/audio_engine.md:31)
- [operators/control/midi_input/midi_input.cpp](/Users/jeff/Developer/vivid/operators/control/midi_input/midi_input.cpp:190)
- [operators/control/tracker/tracker.cpp](/Users/jeff/Developer/vivid/operators/control/tracker/tracker.cpp:252)

Spread cruft is now mostly wording cruft. It no longer defines the architecture, but it is still present and still worth cleaning up.

## What Landed Well

Several important pieces are now materially real:

- string-lane naming is real in the public/runtime-facing surfaces:
  - [src/operator_api/types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h:72)
  - [src/runtime/control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp:96)
  - [src/ui/node_graph_util.h](/Users/jeff/Developer/vivid/src/ui/node_graph_util.h:198)
- the lane fixture rename mostly landed:
  - [tests/operators/lane_source_op.cpp](/Users/jeff/Developer/vivid/tests/operators/lane_source_op.cpp:5)
  - [tests/operators/lane_sink_op.cpp](/Users/jeff/Developer/vivid/tests/operators/lane_sink_op.cpp:5)
  - [tests/operators/identity_lane_source_op.cpp](/Users/jeff/Developer/vivid/tests/operators/identity_lane_source_op.cpp:15)
- the lane docs now say the stronger architecture:
  - [docs/lanes-architecture.md](/Users/jeff/Developer/vivid/docs/lanes-architecture.md)
  - [docs/lanes-implementation.md](/Users/jeff/Developer/vivid/docs/lanes-implementation.md)

## Direct Answers

- **How well did we do at implementing `docs/lanes-architecture.md`?**  
  Pretty well. The semantic/runtime/compiler model is strongly aligned with it.

- **Will it seem to a newcomer as if we built Vivid with lanes from the beginning?**  
  Not fully. The core model is close, but comments, debug labels, and some tests still reveal migration history.

- **Is all spread cruft gone?**  
  No. It is now mostly comment/UI/debug/test wording cruft rather than architectural cruft.

## Recommended Remaining Cleanup

1. **Finish contributor-facing wording cleanup**  
   Remove spread-era language from operator comments, runtime comments, and helper descriptions that describe lane-bearing transport.

2. **Fix UI/debug residue**  
   Update labels like `string spread` to `string lanes`.

3. **Finish test/doc wording cleanup**  
   Update remaining spread-oriented assertions, comments, and runtime-doc references that still teach old vocabulary.

Caution:
- Keep legitimate domain uses like stereo spread or spatial spread untouched.

## Assumptions

- This is a new standalone audit note, not an edit to the lane docs themselves.
- The architecture is mostly implemented; the remaining issue is clarity and polish.
- The key distinction is:
  - architectural alignment is mostly achieved
  - newcomer clean-slate perception is not yet fully achieved
  - spread cruft is reduced to wording-level residue, not eliminated
