# ADR-0008: Agent Capability Surface Is Embedded UI Over a Vendor-Agnostic, Swappable Provider

Status: accepted

Date: 2026-06-21

## Context

[ADR-0006](ADR-0006-agent-external-mcp.md) keeps the agent external and makes project text the
source of truth, to avoid coupling Vivid's stability to fast-moving agent tech. That decision is
right and stands. But it left one consequence too blunt: "prototypes and UI must not present the
agent as an embedded feature."

Pressure-testing the Session View prototypes surfaced a real advantage that a purely external,
terminal-driven agent throws away: **contextual intent**. A button on a selected cell already
knows which cell, which scene, and what is connected; it carries the working context for free and
renders the result inline. Alt-tabbing to an external chat and re-describing the selection is
strictly worse for the creative loop.

The tension dissolves once two surfaces are separated:

1. **The model** — the provider, API keys, agent loop. This is the vendor-churn surface
   (Anthropic / OpenAI / Google / local APIs turn over fast).
2. **The intent surface** — where the user expresses intent against the current selection and sees
   proposals. This is UX, not vendor coupling.

"In the interface" was being conflated with "in the process." They are independent.

## Decision

The **intent surface may be embedded** in Vivid; the **model never is.**

- Vivid exposes a small set of **agent capabilities expressed in Vivid's own vocabulary** —
  `propose_variations(selection, count, intent)`, `explain(selection)`,
  `suggest_binding(source, dest)` — taking intent in and returning **text/diff proposals over the
  canonical project text** (ADR-0006). These are the same MCP-tool + text contract, just with
  in-app affordances that dispatch them.
- The model runs in a **separate process behind a swappable adapter**. Vivid core links **zero
  vendor SDKs** (`anthropic`, `openai`, etc. never appear in the binary). Keys live in the adapter
  or the OS keychain; core only knows whether *a* provider is attached.
- **The agent is to Vivid what a plugin is to a host.** Vivid hosts; it defines the host↔agent
  contract; it does not *be* the model. LLM API churn is absorbed by the adapter exactly as
  VST3/CLAP/AU format churn is absorbed by the plugin, not the DAW (ADR-0004, PRD principles 4,
  10, 11).
- Adapters are **swappable and out of the release surface** — Claude.app, Codex, a local model, or
  a user's own script. The pure-external mode from ADR-0006 is the special case of "no adapter
  attached; drive Vivid from an external MCP client."

The line Vivid must never cross: **no code path outside an adapter assumes any one provider's wire
format.** That is the whole of staying agnostic.

A useful corollary observed in the prototype: **deterministic edits stay in core; only generative
work crosses the seam.** Auditioning a take, keeping it, branching from the live take, launching a
scene — these are local edits and require no provider. Generating or explaining requires one. The
boundary is natural and worth preserving.

## Relationship to ADR-0006

This **refines, and does not reverse,** ADR-0006. The model stays external; text stays the source
of truth; the stable contract is still the MCP tool surface + the text format. ADR-0006's
consequence "UI must not present the agent as an embedded feature" is narrowed to its real intent:
the **model/provider** must not be embedded. Embedded **intent affordances that dispatch the
contract to an external, visibly-attached, swappable provider** are allowed and encouraged.

## Alternatives Considered

- **Purely external (no in-app affordances).** Rejected as the default: loses contextual intent
  and inline proposals; weakens the creative loop. Retained as a valid mode (zero adapters
  attached).
- **Embed a model/agent loop in core** (link a vendor SDK, manage keys, own the agent loop).
  Rejected: the exact churn coupling ADR-0006 exists to prevent.
- **Bet on one "compatible" wire format** (e.g. assume an OpenAI-compatible endpoint everywhere).
  Rejected as the core contract: tool-use/function-calling formats still diverge. A thin adapter
  per provider family is more robust than one assumed wire format.

## Consequences

- Vivid defines and versions an **agent-capability protocol in product vocabulary** (sessions,
  clips, takes, bindings), layered over the text format alongside the MCP surface.
- An **adapter interface** is a product deliverable: the seam where intents map to a provider's
  tool-calls and back to text/diff proposals. Adapters are external and may be community- or
  user-supplied.
- The UI shows the **provider seam explicitly** — which provider is attached (or none) and that it
  is swappable — so the architecture is legible and never implies a built-in model.
- Generative affordances **degrade cleanly** when no provider is attached; deterministic edits keep
  working and an external MCP client can still drive Vivid.
- Phase 2 agent-workflow proofs exercise the **capability protocol + adapter seam + text contract**,
  not a built-in model.
