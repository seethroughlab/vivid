# Architecture Guardrails

Rules for promoting new concepts in the vivid codebase.

## The three stable mechanisms

The architecture supports three ways for operators to interact:

1. **Ports** — graph-visible transport between nodes (signal, audio, texture, spread)
2. **Owned embedded composition** — host-local reusable modulation via `ChildOp<T>` / `BoundControlInstance`, serialized as host-local state
3. **Explicit outputs** — when host-local behavior must become graph-visible (triggers, gates, energy summaries)

Any new mechanism must justify itself beyond what these three already provide.

## Rules for new concepts

- **Start local.** New ideas begin inside a single operator or a single layer. Do not introduce a concept across graph schema, runtime, UI, control server, MCP, and presets simultaneously.

- **Prove before promoting.** A mechanism must demonstrate real value in at least one shipped operator before it becomes product architecture. Prototype in one place; generalize only when the pattern repeats.

- **Prefer reuse.** Before inventing new infrastructure, check whether existing operator code, `ChildOp<T>`, ordinary ports, or inspector-first editing already solve the problem.

- **Don't duplicate transport.** If data needs to travel between nodes, use ports. Do not encode transport semantics in naming conventions, hidden slots, or special-purpose graph concepts.

- **Inspector-first editing.** Host-local behavior should be editable through the inspector as regular params or embedded op panels. Avoid designs that require graph-level manipulation for host-internal concerns.

## What went wrong with role bindings

Role bindings were promoted from concept to full product architecture in one step — touching graph schema, operator API, scheduler, audio engine, GPU context, registry, control server, MCP, UI snapshot, inspector, presets, tests, and docs. This made them expensive to evaluate, expensive to revert, and difficult to reason about incrementally.

The correction: owned embedded composition achieves the same host-local modulation with simpler code, no graph-visible binding model, and standard param serialization. The lesson is that a simpler mechanism that reuses existing infrastructure is usually better than a novel abstraction that requires changes across every layer.
