# Phase 5: Agent And MCP Experience

Status: proposed

## Purpose

Verify that agent-facing workflows are inspectable, useful, and aligned with the public promise of
MCP-native creative coding.

## User Task

Ask an agent to inspect a project, explain the current audiovisual state, make a constrained edit,
and report what changed.

## Hypothesis

If the agent experience is credible, users can trust automation because it can see, explain, and
modify the same creative objects the UI exposes.

## Pressure Test

Run representative MCP/control-server scenarios for introspection, edit operations, audio analysis,
visual analysis, project recovery, mappings, and package interactions.

## Scope

- MCP/control-server methods exposed as release-facing or docs-facing capabilities.
- Agent-readable project, graph, audio, visual, mapping, package, and diagnostic state.
- Agent edit commands that change creative state.
- Error reporting, undoability, and explainability of agent-driven edits.

Out of scope: general agent personality, remote hosting, or unsupported private commands unless
they leak into public docs or examples.

## Audit Procedure

1. Inventory public or semi-public control methods and group them by inspect, edit, analyze,
   recover, package, and internal.
2. Run an agent-style scenario: inspect project, explain current state, make a constrained edit,
   verify result, undo or revert if supported, and summarize changes.
3. Compare method names, response fields, and error messages with product vocabulary.
4. Test failure paths: invalid object id, unsupported edit, bad package/operator, missing project,
   and stale state.
5. Mark methods that should be hidden, renamed, documented, or explicitly considered development
   surface for first release.

## Evidence To Collect

- Control method inventory with release status.
- Transcript of at least one successful agent edit scenario.
- Transcript of at least three failure scenarios.
- Vocabulary mismatch list across UI, docs, and command responses.

## Deliverables

- MCP readiness matrix: method group, user value, risk, release status, and docs status.
- Agent workflow findings with reproduction commands or transcripts.
- Public/private control-surface recommendation.

## Acceptance Criteria

- Agent-readable state uses the same vocabulary as the UI and docs.
- Edit commands have deterministic, inspectable effects.
- Errors are structured enough for an agent to explain and recover.
- Agent changes are undoable or clearly scoped.
- The control surface does not expose release-unsafe commands as polished public affordances.

## Failure Modes

- The agent can mutate state that the UI cannot explain.
- The agent reports success after a partial or failed edit.
- Control methods leak internal terms into user-facing output.
- MCP examples depend on unreleased local-only setup.

## Evidence Log

- Pending.

## Open Questions

- Which MCP methods are part of the first-release product promise?
- Should agent edits always enter the same undo history as UI edits?
- What is the public wording for commands that are useful but still experimental?

## Follow-Up Plans

- Link MCP eval updates, command schema changes, and documentation fixes here.
