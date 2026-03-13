# Port Type Registry Review

## Purpose

This document evaluates whether the recent port-type-registry change was a good architectural move and whether it was implemented well enough for Vivid's long-term goals.

The comparison baseline is the pre-merge state where complex operator-to-operator data used `VIVID_PORT_HANDLE` plus `handle_type_id`. The current state replaces that model with:

- first-class custom port type ids
- explicit `VividPortTransport`
- runtime registration of custom type metadata
- richer introspection for tooling and MCP/control-server consumers

This review is not a migration plan. It is a technical assessment of the design quality and implementation quality of the change as it exists now.

## Executive Judgment

The change was a good idea.

It moves Vivid in the right architectural direction:

- away from a generic HANDLE bucket with side-channel typing
- toward explicit type identity
- toward explicit transport semantics
- toward package-visible metadata and introspection

The implementation is directionally solid in scheduler/runtime/tooling surfaces, but it is not yet fully mature as a long-term ecosystem foundation.

The main gaps are:

1. `type_id` stability is weaker than the code/comments imply.
2. `CUSTOM_REF` is not yet a meaningfully distinct transport on the audio path.
3. authoring and UI scaffolding still lag behind the new ABI
4. registry failure behavior is too fatal for a real package ecosystem

## What Improved

### 1. The type model is cleaner than the old HANDLE-era model

The old system treated complex data as:

- port type = `VIVID_PORT_HANDLE`
- actual identity = `handle_type_id`

That model worked, but it split "what this is" across two fields and encouraged runtime special cases.

The new model is cleaner:

- built-in types remain explicit (`FLOAT`, `SPREAD`, `STRING`, `TEXTURE`, etc.)
- custom types are first-class port types
- transport is explicit in the descriptor

This is a better abstraction for a package ecosystem because it makes port identity inspectable and comparable without relying on HANDLE-specific logic.

Relevant code:

- [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h)
- [scheduler.cpp](/Users/jeff/Developer/vivid/src/runtime/scheduler.cpp#L527)

### 2. Scheduler validation is materially better

The scheduler now validates custom wires using:

- exact custom `type`
- exact `transport`

That is a real improvement over the old HANDLE model, where wire validity depended on an extra side-channel field.

Relevant code:

- [scheduler.cpp](/Users/jeff/Developer/vivid/src/runtime/scheduler.cpp#L527)

### 3. Runtime registration is a reasonable mechanism

The loader-side `vivid_describe_custom_types()` pull model is pragmatic:

- operators expose static type metadata
- runtime registers it at load time
- wire validation and tooling can consult the registry

This is a sensible design for dlopen-loaded packages because it avoids pushing registration responsibility into arbitrary plugin initialization paths.

Relevant code:

- [operator_loader.cpp](/Users/jeff/Developer/vivid/src/runtime/operator_loader.cpp#L170)
- [port_type_registry.cpp](/Users/jeff/Developer/vivid/src/runtime/port_type_registry.cpp)

### 4. Tooling introspection improved in the right places

The control server now exposes:

- `type`
- `transport`
- `type_name`
- `payload_size`

That is much better than exposing only `"handle"` or a bare integer side-channel.

Relevant code:

- [control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp#L312)
- [control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp#L1473)
- [control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp#L2551)

## Main Findings

### Finding 1: `type_id` is not a sufficiently durable long-term package contract

The implementation currently derives custom type ids from:

- `__PRETTY_FUNCTION__` on Clang/GCC
- `__FUNCSIG__` on MSVC

and hashes that text.

Relevant code:

- [type_id.h](/Users/jeff/Developer/vivid/src/operator_api/type_id.h#L7)
- [type_id.h](/Users/jeff/Developer/vivid/src/operator_api/type_id.h#L31)

This is clever and convenient, but it is not as stable as the code comments suggest.

Why this is a problem:

- compiler-specific formatting is not a strong portability contract
- different toolchains may produce different text for the same C++ type
- future compiler upgrades may change formatting details
- package ecosystems become fragile if the type id mechanism depends on compiler string formatting

Why this has not hurt yet:

- Vivid currently runs in a relatively controlled same-platform / same-toolchain environment
- sibling repos like `../vivid-3d` are usually built against the same local toolchain

Why it matters long-term:

- Vivid wants a package ecosystem
- Windows is important
- long-term usefulness requires type ids that are explicit and intentionally stable, not incidentally stable

Assessment:

- acceptable as a short-term implementation shortcut
- weak as the final long-term contract

Recommended direction:

- move toward explicit stable type ids derived from an intentional string namespace
- example shape: `"seethroughlab.vivid.media_stream_v1"` or similar
- keep C++ type-name helpers as authoring convenience, not as the final package-facing identity

### Finding 2: `CUSTOM_REF` is not yet a truly distinct transport on the audio path

The new design introduces explicit transport classes, including:

- `VIVID_PORT_TRANSPORT_CUSTOM_VALUE`
- `VIVID_PORT_TRANSPORT_CUSTOM_REF`

That is the right abstraction.

The problem is in the audio bridge implementation.

Relevant code:

- [audio_engine.cpp](/Users/jeff/Developer/vivid/src/runtime/audio_engine.cpp#L268)
- [audio_engine.cpp](/Users/jeff/Developer/vivid/src/runtime/audio_engine.cpp#L791)
- [audio_engine.cpp](/Users/jeff/Developer/vivid/src/runtime/audio_engine.cpp#L1109)

Current behavior:

- the audio bridge snapshots custom payload bytes into `CustomPortSnapshot`
- both `CUSTOM_VALUE` and `CUSTOM_REF` follow this snapshot-copy path
- audio operators then receive a pointer to the snapshot bytes and cast it back to the original struct type

This works for the current movie/session use case because the "ref" payload structs are really small control snapshots that happen to contain ids/pointers/generation fields.

But architecturally, this means:

- `CUSTOM_REF` is not meaningfully different from `CUSTOM_VALUE` during audio delivery
- transport is partially real at validation time but blurred at runtime
- future package authors may assume reference semantics that the runtime does not actually preserve

This is the single biggest conceptual weakness in the implementation.

Important nuance:

- this does not mean the whole change was wrong
- it means the runtime still needs a sharper definition of what `CUSTOM_REF` means across threads/domains

Recommended direction:

- define `CUSTOM_VALUE` as "copyable POD snapshot payload"
- define `CUSTOM_REF` as "stable reference token / handle / indirection contract"
- make audio delivery rules explicit:
  - either `CUSTOM_REF` may cross to audio only as a small copyable reference-token struct
  - or `CUSTOM_REF` gets dedicated runtime support distinct from `CUSTOM_VALUE`

Right now the system behaves like the former, but the naming/documentation suggests something closer to the latter.

### Finding 3: authoring and UI tooling did not keep pace with the ABI change

The ABI and runtime now support:

- custom type ids
- custom transports
- payload metadata

But the operator authoring surfaces still mostly act like the world only has built-in port kinds.

Relevant code:

- [create_request.h](/Users/jeff/Developer/vivid/src/operator_api/create_request.h#L7)
- [node_graph_util.h](/Users/jeff/Developer/vivid/src/ui/node_graph_util.h#L158)
- [operator_creator.cpp](/Users/jeff/Developer/vivid/src/runtime/operator_creator.cpp#L48)
- [operator_creator.cpp](/Users/jeff/Developer/vivid/src/runtime/operator_creator.cpp#L112)

Concrete issue:

- `VividPortSpec` already includes `transport`, `payload_size`, `type_name`
- the UI create-operator helpers only expose built-in port kinds
- the scaffolder only generates built-in `VIVID_PORT_*` declarations
- custom types are therefore supported by the runtime more than by the product's authoring workflow

Why this matters:

Vivid's core value is operator authoring and composition. If the contract improves but the authoring experience does not, the change is only half-finished from a product perspective.

Assessment:

- runtime work: good
- product integration: incomplete

Recommended direction:

- update create/scaffold flows to support declaring custom ports explicitly
- make custom transport/type metadata first-class in authoring requests
- keep common helpers so package authors do not need to hand-roll verbose descriptor code

### Finding 4: registry conflict behavior is too fatal for a package ecosystem

Relevant code:

- [port_type_registry.cpp](/Users/jeff/Developer/vivid/src/runtime/port_type_registry.cpp#L18)
- [port_type_registry.cpp](/Users/jeff/Developer/vivid/src/runtime/port_type_registry.cpp#L37)

Current behavior:

- malformed registration -> `abort()`
- ABI mismatch -> `abort()`
- conflicting re-registration -> `abort()`

This is understandable during an ABI reset and local bring-up. It makes problems impossible to miss.

But for a real package ecosystem, this is too aggressive:

- a bad linked package should fail to load
- the runtime should report the package/type conflict clearly
- the app should not terminate the whole process if recovery is possible

Assessment:

- acceptable for short-term enforcement
- not a good final runtime behavior

Recommended direction:

- convert registration failures into package/operator load failures
- keep hard-fail behavior for internal debug builds if desired
- surface conflicts in logs, package browser, and CLI/tooling

## Was It Done Well?

### Short answer

Mostly yes, with important caveats.

### Why the implementation deserves credit

The change was not superficial. It touched:

- operator ABI
- loader behavior
- scheduler validation
- audio cross-domain transport
- operator descriptors
- control-server introspection
- operators in core and sibling repos

That indicates a serious architectural pass rather than a cosmetic rename.

The `../vivid-3d` update is good evidence that the new model improved author-facing type safety for GPU-side complex data.

Relevant example:

- [gpu_3d.h](/Users/jeff/Developer/vivid-3d/include/operator_api/gpu_3d.h#L711)

That helper pattern is better than the old HANDLE bucket because it gives package code a concrete typed declaration path.

### Why the implementation is not "finished well"

The implementation still shows signs of being optimized around the immediate movie/session use case:

- `CUSTOM_REF` is not operationally distinct enough on audio crossing
- authoring flows still lag
- registry failure semantics are still bring-up-grade, not ecosystem-grade
- type-id stability is not strong enough for long-term portability

So the right judgment is:

- the architecture moved forward
- the implementation is coherent enough to use
- but some core contracts are still provisional

## Comparison To The Old Model

### Old HANDLE-era model

Strengths:

- simple
- easy to hack forward for new opaque data cases
- good enough for a small controlled set of built-in patterns

Weaknesses:

- identity split across `type == HANDLE` plus `handle_type_id`
- awkward for tooling
- invites special cases
- poor fit for a growing package ecosystem

### New custom-type + transport model

Strengths:

- explicit type identity
- explicit transport classification
- richer introspection
- cleaner scheduler rules
- more future-proof conceptually

Weaknesses:

- custom id generation is not yet durable enough
- transport semantics are not fully realized in all runtime paths
- authoring/tooling support is incomplete

## Overall Recommendation

Keep this direction.

Do not revert to the old HANDLE-era design.

The new model is the better foundation for long-term usefulness. The right move is not to undo it, but to harden it.

## Follow-Up Priorities

If this work is continued, the highest-value follow-ups are:

1. Replace compiler-string-derived custom type ids with explicit stable ids.
2. Make `CUSTOM_REF` semantics real and unambiguous, especially across audio crossing.
3. Bring scaffolding/UI authoring flows up to the new contract.
4. Downgrade registry conflicts from process-abort to package/operator load failure.

## Final Assessment

Was this change a good idea?

Yes.

Was it done well?

Mostly, but not completely.

The implementation is strong enough to justify the direction and keep building on it. It is not yet strong enough to declare the problem fully solved for the long-term package ecosystem Vivid is aiming toward.
