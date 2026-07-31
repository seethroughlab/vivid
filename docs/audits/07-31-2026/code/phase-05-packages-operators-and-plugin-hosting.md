# Phase 5: Packages, Operators, And Plugin Hosting

Status: proposed

## Purpose

Verify that built-in operators, external packages, hot reload, and hosted audio plugins behave like a
release-supported extension system rather than a development-only mechanism.

## User Task

Browse, load, edit, fail, reload, and remove operators or packages while the host remains stable and
the project remains understandable.

## Hypothesis

If extension boundaries are healthy, Vivid can ship with credible creative breadth without letting
bad content compromise the host.

## Pressure Test

Audit operator descriptors, package manifests, hot reload, file watching, validation, quarantine,
plugin scan/cache behavior, hosted plugin params, presets, and package CLI/control flows.

## Scope

- Built-in operators, packaged operators, package manifests, descriptor validation, package manager,
  compiler, file watcher, hot reload, quarantine, operator catalog, plugin scan/cache, VST3/CLAP
  hosting, plugin windows, params, presets, and operator-authoring docs.
- Package and plugin behavior visible through UI, CLI, and MCP/control APIs.

Out of scope: exhaustive third-party plugin certification unless a format is named as
release-supported.

## Audit Procedure

1. Inventory release-supported built-in operators, packaged operators, example packages, and hosted
   plugin formats.
2. Run descriptor and manifest validation against good, malformed, missing-field, and incompatible
   examples.
3. Exercise load, unload, hot reload, package rebuild/link/uninstall, and file watching while a
   project references affected operators.
4. Review plugin scan/cache flows for determinism, failure isolation, and user-visible diagnostics.
5. Trace parameter metadata from operator/plugin definition through UI, persistence, agent/control
   APIs, and reload.

## Evidence To Collect

- Operator/package inventory with release status.
- Validation output for representative good and bad content.
- Hot reload transcript with project state before and after.
- Plugin scan/cache notes, including failed plugin behavior.
- Param metadata trace for at least one built-in operator and one hosted plugin if supported.

## Deliverables

- Extension-system readiness report.
- Bad-content containment findings.
- Operator/plugin metadata consistency matrix.

## Acceptance Criteria

- Invalid operators and packages fail validation with actionable diagnostics.
- Hot reload cannot leave dangling runtime or UI references.
- Built-in and packaged operators follow the same descriptor and parameter rules.
- Plugin scan/cache behavior is deterministic enough for release support.
- External plugin windows and parameters remain isolated from host stability.

## Failure Modes

- A bad operator crashes the host or corrupts a project.
- Hot reload succeeds partially and leaves invisible stale state.
- Package metadata diverges from operator metadata.
- Plugin-hosted parameters cannot be saved, restored, or inspected consistently.

## Evidence Log

- Pending.

## Open Questions

- Which packages are bundled, supported, or development-only at first release?
- Which plugin formats and capabilities are public release surface?
- Should hot reload be documented for users, operator authors, or only maintainers?

## Follow-Up Plans

- Link operator-audit reports, validation fixes, package docs, and plugin-hosting bugs here.
