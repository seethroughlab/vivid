# Role Bindings

Role bindings allow an operator to declare named roles (e.g. "Amplitude Envelope",
"Modulator") that can be satisfied by binding another operator instance in the graph.
The bound operator runs as a standalone node; the host reads its output at runtime.

## v1 Behavior

- Bindings target a specific `node_id + output_name` pair.
- The UI auto-picks an output only when there is exactly one compatible output
  on the target node, or when a declared `preferred_output_name` matches.
  Otherwise the user must choose from a flat list of `node.output` candidates.
- Control roles primarily accept control-domain operators.

## Domain Exceptions

Some operators are audio-domain because they use `process_audio` for
sample-accurate processing, but are semantically control sources:

- **Envelope** — sample-accurate ADSR envelope
- **LFO** — sample-accurate low-frequency oscillator

These operators are accepted for control-domain roles because they export
`VIVID_BINDABLE` and their outputs are logically control signals.

This is a **narrow, intentional exception**, not a general collapse of domain
boundaries.  Unrelated audio operators (oscillators, effects, etc.) that do not
export `VIVID_BINDABLE` remain rejected for control roles.

## Output Selection

Each `VividRoleBindingDescriptor` may specify:

| Field | Purpose |
|-------|---------|
| `preferred_output_name` | First choice when auto-selecting (e.g. `"value"`) |
| `preferred_output_semantic_tags` | Filter candidates to outputs with matching `semantic_tag` |

When a target node has multiple compatible outputs:
1. If `preferred_output_name` matches one of them, it is selected automatically.
2. Otherwise, each compatible output appears as a separate chooser item
   (`NodeName.outputA`, `NodeName.outputB`).

## Key Types

- `VividRoleBindingDescriptor` — declared by operators in `collect_role_bindings()`
- `RoleCandidate` — UI struct pairing a candidate type name with its compatible outputs
- `RoleBindingInfo` — owned UI metadata for a role (includes `candidates`)
- `RoleBindingSnapshot` — per-node snapshot of the current binding state
- `compatible_outputs()` — shared helper returning output names that match a role
