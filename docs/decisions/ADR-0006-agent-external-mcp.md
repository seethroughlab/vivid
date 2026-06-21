# ADR-0006: Agent Is an External MCP Client; Text Is the Source of Truth

Status: accepted

Date: 2026-06-17

Refined by: [ADR-0008](ADR-0008-agent-capability-surface.md) — the intent surface may be embedded
in the UI as long as the model stays external behind a swappable, vendor-agnostic adapter.

## Context

Vivid 4 is agent-first, but the agent world changes fast: models, clients, protocols, and
agent UX conventions turn over on a timescale far shorter than a creative tool's lifetime.
Embedding a specific agent (or an agent chat surface) inside the environment would couple
Vivid's stability to that churn and create a second, fragile source of project state.

Earlier prototypes embedded an "Agent Actions" panel inside the environment, which implied
the agent is a built-in feature. That is the coupling we want to avoid.

## Decision

The agent is **not** integrated into the Vivid environment. Instead:

- Vivid ships a **comprehensive MCP server** — a detailed set of tools that any external
  agent can call to inspect and manipulate a project.
- The agent is an **external MCP client** (e.g. Claude Code today, something else later),
  swappable and outside Vivid's release surface.
- **Text is the source of truth for everything**: session structure, tracks, clips, scenes,
  bindings, plugin references, visual layers, operator graphs, and project-local code all
  live as readable, diffable text files.
- The GUI and the MCP server are both **views and manipulators over that text** — neither is
  a second store of state.

The stable contract Vivid commits to is therefore the **MCP tool surface + the text
format**, not an integration with any particular agent.

## Precedent

Vivid Classic already ran this way and it worked well. It exposed the runtime to external
LLM clients via three MCP servers (a runtime bridge, a docs/source server, and a
perception/analysis server) over **MCP stdio → an HTTP control server embedded in the running
app**, with ~180 domain-organized tools, semantic compression (summary-first, opt-in
payloads), and perception-locked iteration. An in-app chat was explicitly deferred as
unnecessary. The classic setup is the prior art this decision adopts. See
[`../experiments/mcp-surface.md`](../experiments/mcp-surface.md).

One refinement on classic: its on-disk JSON was a save/load **snapshot of an in-memory
graph** (the runtime was canonical). Vivid 4 makes the **project text canonical** — the
runtime is a projection of the text, and MCP edits and GUI edits are both edits to that text.

## Alternatives Considered

- **Embed an agent/chat inside Vivid.** Rejected: couples the tool to fast-moving agent tech
  and to one vendor; creates an in-app state path competing with the text.
- **Agent talks to an in-memory project API only (no canonical text).** Rejected: loses
  diffability, recoverability, and human/agent co-editing; violates PRD principle #7.

## Consequences

- Vivid must keep the project text format clean, complete, and stable enough to be the real
  interface for both humans and agents.
- The MCP tool surface is a first-class product deliverable with its own design and
  versioning, layered over the text format.
- Agent capability can advance without Vivid changes, as long as the MCP + text contract
  holds.
- Prototypes and UI must not present the agent as an embedded feature; any in-app agent
  affordance is reframed as MCP activity over text.
- Agent-workflow proofs (Phase 2) exercise the MCP + text contract, not an in-environment
  agent panel.
