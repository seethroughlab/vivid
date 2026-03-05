# Perception API Spec (Milestone 4 Phase 0)

Date: 2026-03-05
Status: Accepted (planning contract)

## Purpose

Define the canonical data contract for LLM perception features before implementation:

- per-node introspection
- graph-level diagnostics
- checks
- MCP/control-server transport shape

This spec is intentionally narrow: it defines schemas and ownership boundaries, not implementation details.

## Envelope Contract

All perception endpoints and MCP tools should use a common response envelope:

```json
{
  "ok": true,
  "schema_version": 1,
  "result": {}
}
```

Error shape:

```json
{
  "ok": false,
  "schema_version": 1,
  "error": {
    "code": "invalid_request",
    "message": "missing field: checks"
  }
}
```

## Severity Model

All diagnostics/check outputs use:

- `critical`: likely broken output or invalid runtime state
- `warning`: degraded quality or suspicious behavior
- `info`: advisory only

## Canonical Objects

### NodeIntrospection

```json
{
  "node_id": "foo1",
  "type": "Noise",
  "domain": "gpu",
  "health": {
    "errored": false,
    "message": ""
  },
  "params": {
    "scale": 0.5
  },
  "outputs": [
    {
      "name": "out",
      "kind": "control_float",
      "scalar": 0.42
    },
    {
      "name": "spectrum",
      "kind": "control_spread",
      "spread": {
        "length": 64
      }
    }
  ],
  "domain_metrics": {
    "audio": {
      "rms": 0.12,
      "peak": 0.45
    },
    "gpu": {
      "width": 800,
      "height": 600,
      "format": "rgba16float"
    },
    "control": {
      "spread_outputs": 2
    }
  }
}
```

Rules:

- `params` contains current values only.
- `outputs` may include scalar and/or spread summary.
- `domain_metrics` keys are optional and domain-dependent.

### GraphDiagnostics

```json
{
  "summary": {
    "critical": 1,
    "warning": 2,
    "info": 0
  },
  "findings": [
    {
      "id": "missing_operator_type",
      "severity": "critical",
      "node_id": "particles1",
      "message": "Operator type not resolved; placeholder active.",
      "suggestion": "Install/link package that provides this operator."
    }
  ]
}
```

Rules:

- Findings must be deterministic in order.
- `id` should be stable for test assertions.

### CheckDefinition (v1)

```json
{
  "id": "audio_peak_under_0_95",
  "type": "state_check",
  "path": "nodes.audio_out_1.domain_metrics.audio.peak",
  "op": "<=",
  "value": 0.95,
  "tolerance": 0.0,
  "severity": "warning",
  "message": "Audio peak too high",
  "when": {
    "path": "graph.node_count",
    "op": ">",
    "value": 0
  },
  "after_frame": 30,
  "for_frames": 1
}
```

```json
{
  "id": "no_critical_diagnostics",
  "type": "diagnostic_check",
  "op": "count_by_severity_eq",
  "severity": "critical",
  "value": 0,
  "message": "Critical diagnostics must be zero",
  "check_diagnostics_ids": []
}
```

Supported `op` values:

- `exists`
- `not_exists`
- `==`
- `!=`
- `>`
- `>=`
- `<`
- `<=`
- `between`

Check types:

- `state_check`: evaluate a path from introspection/diagnostics snapshot
- `diagnostic_check`: evaluate counts/presence over diagnostic findings

Supported diagnostic operators:

- `count_by_severity_eq`
- `count_by_severity_lte`
- `count_by_severity_gte`
- `finding_present`
- `finding_absent`

### CheckResult

```json
{
  "id": "audio_peak_under_0_95",
  "passed": false,
  "skipped": false,
  "severity": "warning",
  "type": "state_check",
  "path": "nodes.audio_out_1.domain_metrics.audio.peak",
  "op": "<=",
  "expected": 0.95,
  "actual": 1.12,
  "message": "Audio peak too high"
}
```

Report wrapper:

```json
{
  "all_passed": false,
  "all_critical_passed": true,
  "summary": {
    "passed": 4,
    "failed": 1,
    "skipped": 0,
    "critical_failed": 0,
    "warning_failed": 1,
    "info_failed": 0
  },
  "results": []
}
```

Rules:

- Checks must be deterministic in evaluation order (sorted by `id`).
- Paths are restricted to a documented allow-list (no arbitrary JSONPath evaluator).
- `tolerance` applies to numeric `==`, `!=`, and `between` bounds.
- `for_frames > 1` means condition must hold for N consecutive evaluations.

## Check Storage Policy (Official)

Checks are not embedded directly in graph JSON by default.

Storage model:

- Primary: checks live in external profile files (for example `checks/dev.json`, `checks/ci.json`).
- Graph metadata may store an optional profile reference only (for example `checks_profile: "dev"`), not full check definitions.
- API may accept ad-hoc checks in request bodies for one-off MCP/interactive runs; these are ephemeral and not persisted unless explicitly saved to a profile file.

Rationale:

- keeps graph files focused on creative patch state
- reduces merge churn/noise from CI guardrail edits
- allows multiple check profiles per graph without mutating the patch

## Ownership Boundaries

Runtime core owns:

- collection of node/graph runtime state
- per-domain metrics extraction available from engine state
- diagnostics and check evaluation logic

Control server owns:

- transport envelope (`ok`, `schema_version`, error object)
- endpoint routing and validation

MCP bridge owns:

- tool-level argument shaping
- concise human-facing summaries
- forwarding full structured payloads unchanged where possible

## Caching and Compute Policy

Default policy:

- introspection snapshot: lightweight, can be cached for one frame/tick
- diagnostics: computed on-demand from latest snapshot
- checks: computed on-demand; no persistent mutation

Non-goal in first implementation:

- long-lived historical windowing in core perception endpoints

## Schema Versioning

- `schema_version` is required on all responses.
- Start at `1`.
- Backward-compatible additive fields do not require version bump.
- Breaking changes (field rename/removal/type change) require increment and compatibility notes.

## Legacy Fit Notes

Useful legacy patterns adopted conceptually:

- structured hints with severity + suggestion (`analysis_hints`)
- path-based checks with optional guards (`assertion`, adapted to checks)
- compact machine-readable outputs for MCP/CLI tooling

Not adopted as-is:

- legacy monolithic payload shapes tied to old chain architecture
- any implementation assumptions that bypass current runtime/control-server boundaries

Accepted now (Milestone 4 scope):

- deterministic, severity-ranked diagnostics with stable finding IDs
- explicit check evaluation with CI-friendly summary/result envelopes
- compact MCP-facing summaries with optional full payload passthrough

Explicitly deferred (post-Milestone 4):

- advanced visual metrics (color harmony, symmetry, spatial balance)
- advanced audio metrics (LUFS/compliance, deeper spectral descriptors)
- temporal/cross-domain scoring (reactivity latency/correlation windows)
