# Operator Creation + MCP-Assisted Development Test Plan

This document defines manual and semi-automated validation for Milestone 1 item 5.

Scope:

- End-to-end operator creation workflow
- Domain variants (Control, Audio, GPU, Composite)
- MCP `scaffold_operator` invocation through the control server
- LLM-assisted edit -> hot-reload -> runtime verification loop
- Scaffolding into package directories
- First-build warning checks

## Preconditions

- Build Vivid in Debug or RelWithDebInfo
- Control server enabled and reachable on port `9876`
- Hot-reload path functioning
- At least one graph open and editable
- For package tests: local linked package directory available

## Evidence to Capture

For each case record:

- `Result`: Pass / Fail
- `Domain`: control / audio / gpu / composite
- `Operator Name`: scaffolded type
- `Latency`: approximate scaffold + first compile + hot-reload time
- `Notes`: logs, JSON-RPC request/response payload, and any warnings/errors

## Workflow Definition

Expected workflow:

1. Scaffold operator files from runtime/MCP tooling.
2. Edit implementation to produce identifiable behavior.
3. Save and hot-reload in running graph.
4. Insert/use operator in graph and verify output.
5. Repeat across all required domains.

## Test Cases

### OC-1 End-to-End Scaffold -> Edit -> Reload -> Verify

- Steps:
  1. Scaffold a new operator in a writable operators directory.
  2. Implement a simple deterministic behavior.
  3. Save to trigger build/reload.
  4. Add operator node to graph and verify output signal/texture/value.
- Pass criteria:
  - Files are generated in expected path.
  - Operator compiles and reloads without restarting app.
  - Runtime output matches edited behavior.
- Fail criteria:
  - Missing scaffold files, compile failure without diagnostics, or unusable operator after reload.

### OC-2 Domain Variant Coverage

- Steps:
  1. Repeat OC-1 for:
     - one Control operator
     - one Audio operator
     - one GPU operator
     - one Composite operator
- Pass criteria:
  - All four variants scaffold successfully.
  - Each variant runs in its intended domain without crashes.
- Fail criteria:
  - Any variant fails scaffold contract or cannot be instantiated.

### OC-3 MCP `scaffold_operator` via Control Server

- Steps:
  1. Send JSON-RPC request to MCP bridge/control API for `scaffold_operator`.
  2. Validate response payload indicates success.
  3. Confirm expected files are created on disk.
- Pass criteria:
  - Request succeeds with clear structured response.
  - Generated files match requested name/domain/path.
- Fail criteria:
  - Request fails silently, malformed response, or files generated in wrong location.

### OC-4 LLM-Guided Edit Loop

- Steps:
  1. Scaffold operator.
  2. Use LLM tooling to apply a small behavior change.
  3. Save and hot-reload.
  4. Validate resulting behavior in graph.
- Pass criteria:
  - LLM-produced edit compiles and reloads.
  - Behavior change is observable and consistent.
- Fail criteria:
  - Tooling loop breaks (no edit, no reload, or invalid generated code) without actionable error.

### OC-5 Scaffold into Existing Package Directory

- Steps:
  1. Choose an existing package directory (linked or local package root).
  2. Scaffold a new operator inside that package.
  3. Build/reload package operator.
- Pass criteria:
  - Files are created under package layout, not forced into top-level `operators/`.
  - Package-local build path resolves includes and links correctly.
- Fail criteria:
  - Scaffolder ignores package target path or produces non-buildable package output.

### OC-6 First-Build Warning Check

- Steps:
  1. Build each freshly scaffolded operator before manual edits.
  2. Capture compiler output.
- Pass criteria:
  - First build succeeds with no warnings for baseline scaffold templates.
- Fail criteria:
  - Template emits warnings by default (indicates scaffold quality regression).

## Suggested Minimal Operator Behaviors for Verification

- Control: emit constant or time-based value with obvious numeric output.
- Audio: generate low-volume sine tone with controllable frequency.
- GPU: output solid color/gradient tied to a parameter.
- Composite: route control -> audio or control -> GPU in a clearly observable way.

## Exit Criteria for Milestone Item 5

Item 5 is complete when:

- All cases `OC-1` through `OC-6` have at least one passing run on current `master`.
- Domain variant coverage includes Control/Audio/GPU/Composite.
- MCP request/response artifacts are captured for at least one successful run.
- Any failures are linked to follow-up issues with reproduction steps.
