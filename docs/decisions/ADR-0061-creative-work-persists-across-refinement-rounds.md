# ADR-0061: Creative Work Persists Across Bounded Refinement Rounds

Status: proposed — product direction confirmed; workflow proof pending

Date: 2026-09-17

Refines: [ADR-0008](ADR-0008-agent-capability-surface.md) and the
[PRD](../product/PRD.md). Design context and sequencing:
[continuous creative workflow](../product/continuous-creative-workflow.md).

## Context

A creator with a full-time job can give occasional direction and feedback, but cannot supervise
every edit. Vivid must support songs and visuals developing across days and agent conversations.
An interactive edit/capture loop alone does not preserve the purpose of the work, earlier choices,
or what should happen when the creator is unavailable.

The user has confirmed native Mac review and free exploration in candidates, with the creator
choosing what becomes current. Useful progress per minute of creator attention is the objective.

## Decision

1. **Make refinement persistent and bounded.** The product loop is direction → exploration →
   review → refinement. A work item records an objective, scope, dependencies, budget, and stopping
   conditions. Each execution attempt is a run. Continuous availability does not imply continuous
   computation; a round can end with no worthwhile improvement.
2. **Keep creative memory in the project.** Versioned briefs, protections, candidate provenance,
   feedback, and review decisions are durable, agent-readable records. A fresh agent must recover
   the work without the previous conversation. UI and MCP manipulate the same records. Provider
   credentials and machine-specific execution details remain separate.
3. **Preserve human creative authority.** Agents explore within the brief and protections. Only an
   explicit creator choice promotes a candidate. Silence does not accept or reject work. Feedback
   retains the user's wording, its exact version/passage target, and any structured interpretation.
   Project feedback does not automatically become a global taste preference.
4. **Distinguish protections from preferences.** A hard protection identifies what cannot change;
   a preference guides exploration. Scope includes relevant dependencies: preserving notes alone
   does not preserve their sound. Revised protections invalidate affected queued work and prevent
   stale results from being promoted under obsolete rules.
5. **Limit demand for review.** Publish a small set of meaningful alternatives and a focused
   question. A full review queue pauses dependent exploration; independent authorized work may
   continue. Start by testing two alternatives and one unresolved decision per work item, not by
   making these universal fixed limits. Notify on meaningful review readiness, failure, or required
   action rather than every iteration.
6. **Separate evidence from preference.** Technical checks, content/intent evaluation, and human
   preference are distinct records. Follow [ADR-0026](ADR-0026-evaluation-asserts-quality-against-intent.md)
   when an evaluator is unavailable. Model scores cannot silently establish creative approval.

## Alternatives Considered

- **Conversation-only memory.** Loses continuity on restart or provider changes and makes review
  depend on reading a transcript.
- **Unbounded autonomous refinement.** Encourages drift, resource consumption, and review backlog.
- **Require approval for every edit.** Consumes the attention this workflow is intended to conserve.
- **Automatically promote the highest-scoring candidate.** Conflicts with the confirmed authority
  model and confuses an evaluator's judgment with the creator's preference.

## Consequences

Vivid gains durable coordination records in addition to creative content. Their schema, migrations,
and retention become product responsibilities. Agents can be replaced without replacing project
memory. Finishing/exporting remains a milestone within the continuing project.

The first proof starts from an existing scene; blank-project composition and full-song development
remain later scope. Execution availability is addressed separately by
[ADR-0063](ADR-0063-background-work-uses-an-external-runner.md).

## Acceptance Evidence Required

Run two refinement rounds with real media. Restart the agent between them and demonstrate preserved
direction, feedback, and protections. Leave a review unanswered and show bounded work rather than
queue growth. Record a no-improvement outcome without inventing a candidate. Evidence is pending.
