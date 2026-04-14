# Architecture Guardrails

Rules for promoting new concepts in the vivid codebase.

## The four stable mechanisms

The architecture supports four deliberately separate mechanisms:

1. **Ports** — graph-visible transport between nodes (signal, audio, texture, lane array)
2. **Owned child operators** — host-local executable behavior with private state via `ChildOp<T>`
3. **Primitive params with compound widgets** — persisted float/int/bool/file/text values, optionally edited through built-in or package-defined inspector widgets
4. **Explicit outputs** — when host-local behavior must become graph-visible (triggers, gates, energy summaries)

Custom port types are for graph-visible wire payloads. They are not custom param types. Any new mechanism must justify itself beyond these existing surfaces.

## Rules for new concepts

- **Start local.** New ideas begin inside a single operator or a single layer. Do not introduce a concept across graph schema, runtime, UI, control server, MCP, and presets simultaneously.

- **Prove before promoting.** A mechanism must demonstrate real value in at least one shipped operator before it becomes product architecture. Prototype in one place; generalize only when the pattern repeats.

- **Prefer reuse.** Before inventing new infrastructure, check whether existing operator code, `ChildOp<T>`, primitive params with a compound widget, ordinary ports, or inspector-first editing already solve the problem.

- **Don't duplicate transport.** If data needs to travel between nodes, use ports. Do not encode transport semantics in naming conventions, hidden slots, or special-purpose graph concepts.

- **Inspector-first editing.** Host-local behavior should be editable through the inspector as primitive params, compound param widgets, or owned child-op panels. Avoid designs that require graph-level manipulation for host-internal concerns.

## Keep Host-Local Behavior Small

Owned child-op composition covers host-local modulation with no graph-visible binding model and standard param serialization. If internal behavior needs user editing, expose ordinary parent params or compound param widgets. If internal behavior must become graph-visible, expose an explicit output port. New host-local mechanisms need a design doc and a concrete shipped use case that cannot be solved with `ChildOp<T>`, primitive params, widgets, and ports.
