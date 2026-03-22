# Role Bindings Finalization Plan

## Summary

The tightening pass fixed the major structural issues. One core follow-up item remains to make Role Bindings fully match the intended contract, plus one documentation/testing clarification:

- finish the **UI/output-selection model**
- document and lock the intentional **audio-for-control bindable exception**

The implementation is now sound enough that this is a true finalization pass, not another redesign.

## Key Changes

### 1. Finish output-aware binding in the UI
The runtime now checks `target_output_name`, but the inspector chooser still works as “pick a node, then infer one output”.

Update the chooser model so it can bind to a concrete target output:
- replace the current node-only chooser items with output-aware candidates
  - either `node_id + output_name` pairs
  - or a two-step flow: choose node, then choose output when needed
- if a target node has exactly one compatible output, bind it directly
- if multiple outputs are compatible:
  - prefer the role’s `preferred_output_name` when present and valid
  - otherwise choose by semantic-tag match if you want that in v1
  - otherwise show a second output picker instead of guessing

Update UI metadata/snapshots as needed:
- preserve enough role metadata to drive output selection deterministically
- optionally show the bound output in the inspector chip/body, not just the target node

Update tests:
- add one multi-output bindable test fixture
- add UI/runtime command coverage for selecting a non-default output
- keep the existing invalid-output rejection tests

### 2. Make candidate discovery output-aware
Right now candidate filtering is still mostly type/domain based.

Extend the role-binding candidate path so it can answer:
- which graph nodes are compatible for a role
- which outputs on those nodes are compatible

Implementation direction:
- add a helper that inspects target node descriptors and returns compatible outputs
- use that helper from both:
  - inspector chooser building
  - `set_role_binding(...)` validation path, where appropriate

This keeps the chooser and validation logic aligned instead of duplicating output rules.

### 3. Document and lock the audio-for-control exception
Keep the current `audio_for_control` exception as an intentional design rule.

Product rule:
- some operators are audio-domain for runtime reasons but still valid control-role bindables
- `Envelope` and `LFO` are the canonical examples because they use `process_audio` for sample accuracy
- this is a narrow semantic exception, not a general rule that any audio operator may satisfy a control role

Implementation/documentation work:
- document this explicitly in the role-binding docs and any relevant runtime docs
- make the wording clear that this is a bindable/operator-specific exception, not a broad domain collapse
- preserve the existing validation logic that allows these audio-domain control sources

Tests:
- keep positive coverage for `Envelope` and `LFO` satisfying control roles
- add or keep negative coverage showing that unrelated audio operators are still rejected for control roles
- if needed, add a small bindable audio test fixture that proves the exception is intentional and bounded

### 4. Clarify and lock the v1 product behavior
Once the above is implemented, document the exact v1 rule:

- Role Bindings target `node_id + output_name`
- the UI may auto-pick an output only when there is exactly one valid choice or a declared preferred output
- otherwise the user must choose the output
- control roles are primarily control-domain, with a narrow audio-for-control exception for specific bindable control sources such as `Envelope` and `LFO`

Update the role-binding doc and any runtime/UI docs that describe the feature so the behavior matches the shipped implementation.

## Public Interfaces / Types

Likely additions or refinements:
- output-aware role-binding candidate structure for UI/runtime use
- possible UI chooser state update from `node_id` items to `node_id + output_name` items

Behavioral contract changes:
- role binding selection becomes explicitly output-aware in the inspector
- bound output should be visible in inspector state, not just stored internally
- the audio-for-control exception is documented as intentional rather than treated as an implementation accident

## Test Plan

Extend the current passing suite:
- `test_role_binding_commands`
- `test_role_binding_registry`
- `test_graph`
- `test_graph_snapshot_contract`

Add:
- multi-output bindable fixture
- successful bind to non-default output
- chooser/runtime path for preferred output
- positive coverage for intentional audio-domain control bindables
- negative coverage for unrelated audio operators in control roles
- inspector snapshot/assertion that bound output is preserved and shown correctly

## Assumptions And Defaults

- The intended v1 contract remains:
  - bindings target a specific output
  - some audio-domain operators may intentionally serve control roles when they are semantically control sources
- The current architecture is good and should remain in place.
- The remaining work is to close the gap between the runtime contract and the inspector UX, and to document the intentional domain exception clearly.
