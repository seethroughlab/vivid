# Audit 04: Control Server, RuntimeAPI & MCP

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the command and tool surfaces that mutate or inspect Vivid at runtime, with emphasis on HTTP/MCP contract parity, graph mutation safety, error reporting, and LLM workflow reliability.

## Scope

- `src/runtime/control/`
- `mcp/`
- `docs/runtime/control_server.md`
- `docs/LLM-INTEGRATION.md`
- `docs/MCP-COMPOSITION-COOKBOOK.md`
- Control server, RuntimeAPI, MCP bridge, CLI, and integration tests

## Primary Questions

- [ ] Do HTTP endpoints, `RuntimeAPI` commands, and MCP tools agree on behavior and error shape?
- [ ] Are graph mutations atomic enough for UI, audio, and MCP clients?
- [ ] Are invalid tool calls rejected with diagnostics that are actionable for an LLM?
- [ ] Are MCP-only tools and raw HTTP endpoints clearly documented?
- [ ] Does bridge startup/restart behavior avoid stale tool surfaces?
- [ ] Are runtime capture, package, preset, modulation, and session commands scoped consistently?
- [ ] Are command routing and persistence paths tested independently of UI behavior?

## Subsystem Checklist

- [ ] Trace representative add/connect/set/save/load commands from MCP or HTTP to `RuntimeAPI`.
- [ ] Compare MCP tool definitions with control-server endpoint docs.
- [ ] Review dispatch/query/check layers for duplicated validation or inconsistent errors.
- [ ] Inspect graph file I/O boundaries, including any runtime-to-UI type dependencies.
- [ ] Check crash/reporting endpoints and health responses for useful operational detail.
- [ ] Verify tests cover malformed JSON, missing nodes/ports, package command failures, and concurrent-looking command sequences.
- [ ] Identify tool contract drift that could break composition workflows.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings Template

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|

## Completion Criteria

- [ ] Findings table is filled in or explicitly marked with no findings.
- [ ] HTTP/MCP parity issues are listed separately from RuntimeAPI implementation issues.
- [ ] Error-reporting findings include example failing inputs where possible.
- [ ] Docs that must change with tool behavior are identified.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
