# Simplification And Consolidation Pass After The Role-Binding Experiment

## Summary

This pass should optimize for a clean architectural reset, not just "finish deleting role bindings." The code is already moving in the right direction; the remaining work is to make the whole system coherent again across runtime, UI, docs, tests, demos, and team habits.

Chosen defaults:

- broader sweep
- keep historical docs, but append explicit correction notes instead of rewriting history
- no compatibility layer for `role_bindings`
- no new graph-visible child-node model
- embedded composition remains owned, curated, inspector-first

The goal at the end of this pass is:

- role bindings are gone everywhere that matters
- embedded/local composition is the only supported model for host-local reusable modulation
- ordinary ports are clearly the only graph transport mechanism
- docs, audits, control-server surfaces, snapshots, demos, and tests all tell the same story
- the repo has a small set of explicit architectural rules to prevent another "promote too early" cycle

## Key Changes

### 1. Finish the role-binding removal completely

Treat this as a full-system removal, not a partial code refactor.

Implementation:

- remove all remaining role-binding concepts from:
  - graph schema and load/save
  - operator contract and runtime config injection
  - scheduler, audio engine, GPU context, registry, control server, MCP bridge
  - UI snapshot/read model and inspector behaviors
  - tests, fixtures, demo graphs, and runtime docs
- reject graphs containing `role_bindings` with a clear load error
- remove any command, RPC, or MCP surfaces that still mention or expose role-binding operations
- remove the `bindable` concept from control operators entirely

Definition of done:

- there is no active runtime or product path that treats role bindings as a current feature
- remaining references exist only in archive/history docs

### 2. Consolidate the replacement model around three stable mechanisms

Make the post-switch model explicit and consistent everywhere.

Lock the architecture to:

- **Ports** for graph-visible transport only
- **Owned embedded composition** for host-local reusable logic
- **Explicit outputs** when host-local behavior must become graph-visible

Implementation direction:

- current GPU operators that relied on role bindings should continue using owned reusable operator code internally
- host-local modulation should be driven by curated embedded slot/operator choices, not arbitrary cross-node binding
- where generic external control is still useful, expose an ordinary signal input rather than reintroducing a host-slot graph concept
- where other domains need to react to host-local behavior, expose outputs like triggers, gates, spreads, or energy summaries rather than exposing the private mechanism

Important constraint:

- do not use this pass to invent a second complex embedded-node architecture
- default to inspector-first local editing and flat host ownership

### 3. Simplify adjacent concepts that were inflated by the experiment

Use this broader sweep to remove nearby complexity that only existed to support or explain role bindings.

Focus areas:

- inspector and snapshot model:
  - remove `Referenced By` and role-centric readback concepts if they no longer represent a stable product feature
  - keep the snapshot contract small and stable
- presets and recall:
  - keep them param- and host-state-based
  - do not preserve special binding restoration logic
- control-server and MCP semantics:
  - keep only stable runtime manipulation and perception tools
  - remove role-binding-specific mutation language from docs and payload descriptions
- demo graphs and factory presets:
  - convert them fully to the owned/local model so examples stop teaching the old architecture implicitly

This is the consolidation part of the pass: remove the support structures that were only necessary because role bindings existed.

### 4. Make the docs internally consistent without erasing history

Active docs should describe the new architecture plainly. Historical docs should stay historical, but they must stop misleading readers about current truth.

Apply this split:

- **active docs**:
  - `EMBEDDED-OPERATOR-SLOTS.md` becomes the current design source of truth
  - runtime, control-server, and LLM docs describe the current model only
  - remove current-tense role-binding language from active engineering docs
- **historical and audit docs**:
  - keep the original narrative
  - append short correction notes where needed:
    - role bindings were an intermediate design, not the final direction
    - the codebase has since been simplified back toward owned embedded composition
  - update release and audit summaries that currently overstate role-binding permanence or currentness

This preserves useful history without forcing future readers to reverse-engineer which document is still authoritative.

### 5. Add a short architectural guardrail document for future experimentation

The repo needs one explicit "how we promote ideas" rule set so this pattern does not repeat.

Add a concise design rule section or doc that says:

- new concepts start local, not first-class across every layer
- a mechanism must justify itself beyond:
  - ordinary ports
  - owned/local composition
  - explicit outputs
- do not promote an experimental concept into graph schema, runtime, UI, control server, MCP, and preset system all at once
- require one proving phase before a new abstraction becomes product architecture
- prefer reuse of existing operator code and inspector-first editing over new structural graph concepts

This should be short, opinionated, and written for future contributors and agents.

## Public Interfaces / Types

Intentional public-surface outcome of this pass:

- `role_bindings` is no longer a valid graph concept
- no role-binding commands, endpoints, or tools remain in the control server or MCP bridge
- operator contracts no longer declare role-binding descriptors or bindable state
- active host-local reusable modulation is represented through owned embedded composition and host state
- graph-visible sharing remains ordinary ports and explicit outputs only

User-facing behavior after the pass:

- graphs that still contain `role_bindings` fail fast with a clear error
- current demo graphs and inspectors teach the owned/local model by example
- no current doc tells users to use role bindings

## Test Plan

### Code and contract cleanup

- full repo search shows no current role-binding references outside archive/history docs
- builds pass after deleting role-binding code paths
- negative tests confirm:
  - `role_bindings` graphs are rejected clearly
  - removed RPC and MCP operations are gone

### Replacement-model verification

- affected GPU operators still produce live output under the owned/local model
- preset save/recall works without binding-specific machinery
- ordinary signal-port cases still function where generic transport is intended
- host-local-to-graph cases use explicit outputs rather than leaked private state

### Demo and UX validation

- updated demo graphs load and behave correctly
- inspector surfaces remain readable after removing role-centric panels
- active runtime, control-server, and MCP docs match the actual behavior of the running product

### Documentation consistency

- active docs consistently describe:
  - ports
  - owned embedded composition
  - explicit outputs
- audit and history docs include explicit correction notes where they would otherwise imply role bindings are still current

## Assumptions

- We are close enough to the switch that this round should finish and consolidate, not reopen the architecture.
- Pre-release status means simplification is more valuable than backward compatibility.
- The new stable model is:
  - owned, reusable, curated embedded composition for host-local behavior
  - ordinary ports for transport
  - explicit outputs for cross-domain reuse of results
- This pass should include a small amount of adjacent cleanup beyond the raw switch, but it should not turn into a second major experimental architecture effort.
