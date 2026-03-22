# Role Bindings Spec To Replace Embedded Operators

## Summary

Replace the current **embedded operator slot** model with **Role Bindings**.

New conceptual model:
- **Params**: local configuration on a node
- **Ports**: runtime dataflow between nodes
- **Role Bindings**: named structural references where a host node binds one of its declared roles to a specific output on another node

This is a full replacement, not a compatibility layer. The current unstaged/under-development embedded-op concept should be removed rather than preserved under legacy names.

Chosen defaults:
- **User-facing term:** `Role Binding`
- **v1 domain scope:** control nodes only
- **binding target:** specific output on a node, not just the node as a whole
- **runtime realization:** a bound node can still be realized as `shared` or `per_voice` by the host
- **normal node behavior:** bound nodes remain ordinary graph nodes with normal params, ports, thumbnails, and inspector behavior
- **custom inspector rule:** core UI owns binding UI; custom inspectors do not need to implement binding support themselves

## Key Changes

### 1. Replace embedded slot descriptors with role descriptors
Remove:
- `VividEmbeddedSlotDescriptor`
- `VividSlotScope`
- `collect_embedded_slots(...)`
- `embedded_slot_count` / `embedded_slots` on `VividOperatorDescriptor`
- embedded-slot runtime config in process/audio contexts

Add a new descriptor concept for host operators:
- `VividRoleBindingDescriptor`
  - `role_id`
  - `label`
  - `accepted_domain`
  - `runtime_scope`
    - `shared`
    - `per_voice`
  - `allowed_operator_types`
  - `preferred_output_semantic_tags`
  - `preferred_output_name`
  - `default_operator_type`

Add a new authoring hook on operators:
- `collect_role_bindings(std::vector<VividRoleBindingDescriptor>&)`

This keeps role declarations on the host operator, but changes the meaning from “owned embedded instance slot” to “named bindable role”.

### 2. Replace graph `embedded_ops` state with `role_bindings`
Remove from `Graph::NodeDef`:
- `embedded_ops`

Add:
- `role_bindings`

Binding state should store:
- `target_node_id`
- `target_output_name`
- optional UI/runtime flags only if needed in v1
  - no copied child param state
  - no copied child bypass state
  - no copied child type name except as derivable from the target node

Recommended shape:
- `role_id -> RoleBindingState`
- `RoleBindingState`:
  - `target_node_id`
  - `target_output_name`

This keeps the source of truth on the referenced node, not the host.

### 3. Treat bindings as a new graph relationship, not a wire
Bindings are not normal dataflow wires and should not participate in scheduler topology the same way ports do.

Add a binding relationship model:
- host node declares roles
- graph stores `host.role_id -> target_node.output`
- UI draws a distinct reference line style
- runtime resolves those references during build

Behavior:
- normal graph wires still carry data between ports
- role bindings express structural usage, not direct port transport
- a node can be both:
  - used normally via ports/wires
  - and bound into one or more host roles

### 4. Host runtime reads bound node definitions, not embedded child state
Replace the current embedded runtime config model with binding-resolution config.

Host operators with declared role bindings receive, through runtime context, a per-role resolved binding config:
- role id
- bound node id
- bound node type
- bound output name
- resolved loader/factory access for runtime realization
- referenced node param snapshot as needed for build-time realization

Important design:
- the referenced node remains the authoring-time source of truth
- the host may realize that referenced node differently at runtime:
  - `shared`: one realization for the host
  - `per_voice`: one realization per active voice

This cleanly separates:
- graph identity
- structural binding
- runtime instancing

### 5. Bound nodes remain normal graph nodes
This is the core product rule.

A node used in a role binding must still:
- appear in the graph like any other node
- keep its normal params and ports
- be editable via the normal inspector
- render its normal thumbnail
- be reusable in multiple bindings across different hosts

A role binding does not convert the node into a hidden/private child object.

### 6. Inspector behavior
The core inspector gains a **Role Bindings** section for host nodes.

For each declared role:
- show label
- show scope badge (`Shared` / `Per-Voice`)
- show bound node and output, or empty state
- actions:
  - `Bind`
  - `Rebind`
  - `Clear`
  - `Jump To Node`
  - `Select Node`

Binding chooser behavior:
- choose from compatible existing nodes in the graph
- filter by:
  - accepted domain
  - allowed operator types
  - compatible outputs
- if no compatible node exists, offer:
  - create a new compatible node and bind it immediately

Display:
- use node name/type chip instead of embedded type chip
- show output name when relevant
- optionally show the bound node’s thumbnail in the inspector section

Custom inspector behavior:
- keep the current principle
- the core inspector renders role bindings after standard/custom inspector content
- custom inspector APIs do not need to know how to render or mutate bindings in v1

### 7. Graph canvas behavior
Bound nodes stay visible as normal nodes.

Add a new visual relationship:
- **binding line** from host node to bound node
- visually distinct from normal wires
  - thinner
  - dashed or dotted
  - neutral or role-colored
- optional badge on the host node showing bound roles
- optional badge on the bound node showing reference count / “used by”

Selection behavior:
- selecting a host node shows its role bindings in the inspector
- selecting a bound node shows normal inspector plus “Referenced By” list
- clicking a binding line can show a small binding inspector or just highlight host + target in v1

### 8. Runtime/API command replacement
Remove:
- `set_embedded_op`
- `clear_embedded_op`
- `set_embedded_op_param`
- `set_embedded_op_bypass`

Add:
- `set_role_binding(node_id, role_id, target_node_id, target_output_name)`
- `clear_role_binding(node_id, role_id)`
- `auto_bind_role(node_id, role_id, target_node_id)`
  - optional helper that chooses the preferred output automatically
- `list_role_binding_candidates(node_id, role_id)`
  - for UI filtering / chooser support

Do not add role-local param commands, because bound nodes are edited through the normal node param path.

Dirty/rebuild behavior:
- binding changes are structural and require rebuild
- normal param changes on bound nodes continue to use existing node param mutation paths

### 9. Referenced-by introspection
Add a graph/runtime query for reverse lookup:
- given `node_id`, list all host roles referencing it

Use this in:
- inspector “Referenced By”
- optional node badges
- future graph tooling

### 10. Migration / removal strategy
Because backward compatibility is not a concern:
- remove embedded-operator terminology and machinery instead of aliasing it
- update docs, tests, UI, runtime, and operator API consistently in the same refactor
- do not keep `embedded_*` names around in public surfaces

Subsystems to replace cleanly:
- `Graph::NodeDef::embedded_ops`
- `VividEmbeddedSlotDescriptor`
- `VividEmbeddedSlotRuntimeConfig`
- `EmbeddedControlInstance` as the public conceptual center
- embedded RuntimeAPI / command sink commands
- embedded inspector UI and chooser
- embedded scheduler build config

If a small internal helper survives temporarily during refactor, rename it to binding-oriented terminology before the feature is considered done.

## Public Interfaces / Types

Additions:
- `VividRoleBindingDescriptor`
- host hook: `collect_role_bindings(...)`
- graph state: `role_bindings`
- runtime commands:
  - `set_role_binding(...)`
  - `clear_role_binding(...)`
  - `list_role_binding_candidates(...)`
- UI snapshot fields for:
  - declared role bindings
  - resolved current binding
  - reverse references

Removals:
- `VividEmbeddedSlotDescriptor`
- `VividSlotScope`
- `VividEmbeddedSlotRuntimeConfig`
- `collect_embedded_slots(...)`
- `embedded_ops`
- embedded-op RuntimeAPI commands
- embedded-op UI snapshot and inspector terminology

## Test Plan

### Graph / schema tests
- save/load role bindings round-trip
- invalid binding targets are preserved in graph data but rejected/ignored at scheduler build with diagnostics
- binding stores `target_node_id + target_output_name`

### Registry / compatibility tests
- candidate filtering by:
  - domain
  - allowed operator types
  - compatible outputs
- auto-binding prefers the declared preferred output when available

### Runtime / scheduler tests
- host with a `shared` role binding resolves the referenced node correctly
- host with a `per_voice` role binding realizes the referenced node per active voice
- changing a bound node’s params affects all hosts referencing it
- clearing or rebinding a role requires rebuild and updates runtime correctly

### UI / interaction tests
- inspector can bind, rebind, clear, and jump to a bound node
- bound nodes remain normal selectable graph nodes
- binding lines render distinctly from port wires
- “Referenced By” appears for bound nodes
- custom-inspector nodes still show the core-managed Role Bindings section

### Replacement acceptance tests
- `WavetableSynth` pilot uses role bindings instead of embedded ops
- one `Envelope` node can be bound into multiple host roles
- the same bound node can still participate in ordinary graph behavior
- no public `embedded_*` terminology remains in the user-facing system

## Assumptions And Defaults

- The current embedded-op system is development-stage and may be removed cleanly.
- The preferred long-term model is **normal nodes + role bindings**, not host-owned hidden embedded instances.
- v1 role bindings target **control-domain nodes only**.
- Bindings point to a **specific output** on a node, even if the UI usually simplifies this to “pick a node”.
- The core inspector and graph UI own binding visualization and mutation; custom inspectors are not required to implement binding support.
- Runtime realization scope (`shared` vs `per_voice`) is declared by the host role, not by the referenced node itself.
