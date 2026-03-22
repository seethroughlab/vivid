# Role Bindings Tightening Plan

## Summary

The Role Bindings refactor landed well overall, but three follow-up fixes are needed to make the implementation match the intended product model:

- finish the **specific-output binding** contract end-to-end
- restore the intended **control-only v1 scope**
- make the graph format change **explicitly incompatible** if that is the intended policy

The biggest issue is that bindings currently behave like “bind a node, store an output string” rather than a real validated `node_id + output_name` reference.

## Key Changes

### 1. Complete output-level binding support
Treat bindings as targeting a **specific output**, not just a node.

Update runtime validation:
- extend `set_role_binding(...)` to validate that:
  - the target node actually has the named output
  - the output is compatible with the role
- reject invalid `target_output_name` instead of storing it blindly

Update metadata flow:
- carry `preferred_output_name` and `preferred_output_semantic_tags` through:
  - descriptor copy
  - UI/operator info cache
  - graph snapshot role metadata

Update chooser behavior:
- stop hardcoding `"value"`
- when a node has one compatible output, bind it directly
- when multiple outputs are compatible, choose the preferred one if declared
- otherwise open a second lightweight output chooser or use a deterministic rule that is explicitly encoded

Update candidate filtering:
- filter candidate nodes by **compatible outputs**, not just by type/domain
- optionally expose `node_id + output_name` candidate pairs to the UI rather than only node IDs

Update runtime config/tests:
- ensure `bound_output_name` is actually meaningful in scheduler/runtime behavior
- add a regression test with a multi-output bindable node

### 2. Reassert control-only v1 scope
If v1 is meant to be control-only, remove the current audio exception.

Update validation/candidate enumeration:
- remove the `audio_for_control` exception in `bindable_candidates(...)`
- remove the same exception in `validate_role_binding(...)`

Audit bindable exports:
- keep `VIVID_BINDABLE(...)` on control operators only for v1 unless there is an explicit reason otherwise

Tests:
- add a focused negative test that an audio-domain bindable cannot satisfy a control-domain role
- keep existing control-role positive coverage

### 3. Make the graph schema change explicit
The graph model has changed from `embedded_ops` to `role_bindings`, and that should be reflected in schema handling.

Update graph schema version:
- bump `GRAPH_SCHEMA_VERSION`

Decide the load policy and implement it consistently:
- if old `embedded_ops` graphs should now be unsupported, reject them clearly
- if temporary migration support is desired during development, make it explicit and one-way, not silent/accidental

Tests:
- add a schema-version test covering the chosen policy
- add a load/save test that confirms old embedded-op graphs do not silently round-trip as if nothing changed

### 4. Tighten UI metadata for role bindings
The UI currently has enough to show bindings, but not enough to honor output-level binding correctly.

Extend `RoleBindingInfo` / snapshot metadata to include:
- preferred output name
- preferred output semantic tags, or a distilled compatible-output view
- possibly a list of compatible outputs per candidate node if that proves simplest

Use that metadata in:
- inspector role-binding chooser
- future “create and bind” flows
- reference display if you want to show `node.output` in the inspector

### 5. Add focused regression tests for the remaining gaps
Add or extend tests for:

- `set_role_binding(...)` rejects nonexistent output names
- binding a multi-output node chooses the correct output
- UI/metadata path preserves preferred output info
- control-only validation rejects audio bindables
- graph schema behavior matches the chosen incompatibility policy

Keep the current passing tests as the core smoke/regression set.

## Public Interfaces / Types

Likely additions or refinements:
- `RoleBindingInfo` should gain output-selection metadata
- role-binding candidate query may need to return output-aware candidates rather than only type names/node IDs

Behavioral contract changes:
- `RuntimeAPI::set_role_binding(...)` must validate `target_output_name`
- control-domain roles no longer accept audio-domain bindables in v1
- graph schema version increments to reflect the incompatible model change

## Test Plan

Run and extend the current role-binding suite:
- `test_role_binding_commands`
- `test_role_binding_registry`
- `test_bound_control_instance`
- `test_graph`
- `test_graph_snapshot_contract`

Add scenarios for:
- invalid target output name
- valid multi-output target
- preferred-output selection
- audio bindable rejected for control role
- graph schema/version incompatibility handling

## Assumptions And Defaults

- The intended v1 contract is still:
  - control-only role bindings
  - binding to a specific output
- Backward compatibility with the old embedded-op graph model is not required.
- It is better to reject or explicitly migrate old graphs than to preserve silent ambiguous behavior.
- The current role-binding architecture is worth keeping; this is a tightening pass, not a redesign.
