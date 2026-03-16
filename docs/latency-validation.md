# Latency Validation

Bounded evidence for the PRD's responsiveness claims. Four scenarios prove that core operations complete within strict time budgets, with repeatable, CI-stable pass/fail assertions.

## Scenarios

| # | Name | Threshold | What it proves |
|---|------|-----------|----------------|
| 1 | Parameter Responsiveness | 50 ms | A parameter change is reflected in operator output within one scheduler tick |
| 2 | Routing Responsiveness | 100 ms | A topology change (connect) propagates through rebuild + tick |
| 3 | Compatible Hot Reload | 200 ms (advisory) | An operator reload completes and produces correct output |
| 4 | Introspection Refresh | 100 ms | Introspection APIs reflect topology changes immediately after rebuild + tick |

### Scenario 1: Parameter Responsiveness

Sets `scale=42.0` on a TestOp node, ticks the scheduler, and verifies the output is `84.0` (scale * 2.0). The entire set-param + tick + read cycle must complete in under 50 ms.

### Scenario 2: Routing Responsiveness

Connects node A's output to node B's scale input, applies the pending topology change, ticks, and verifies B's output reflects A's value. The connect + rebuild + tick cycle must complete in under 100 ms.

### Scenario 3: Compatible Hot Reload (advisory)

Loads AudioReloadOp v1, verifies output, then hot-reloads to v2 and verifies the new output. Marked advisory because filesystem operations introduce CI variance. The reload + process cycle must complete in under 200 ms.

### Scenario 4: Post-Change Introspection Refresh

Adds a node, connects it, rebuilds, ticks, then queries `inspect()` and `list_nodes()`. Both must reflect the new topology. The full cycle must complete in under 100 ms.

## Cross-Domain Latency Model

These thresholds are informed by the runtime's tick structure:

- **Control tick**: ~16 ms (60 Hz frame rate)
- **Audio block**: ~5 ms (256 samples at 48 kHz)

The thresholds are set well above measured values to avoid flaky failures while still catching regressions that would violate the PRD's "same-frame" responsiveness claims.

## How to Run

```bash
# Via ctest
ctest --test-dir build -R test_latency_validation

# Direct execution
./build/test_latency_validation build

# With JSON output on stdout (structured output for CI)
./build/test_latency_validation build --json 2>/dev/null
```

## Advisory vs Hard Assertions

- **Hard**: Scenarios 1, 2, 4 — failures increment the failure count and cause a non-zero exit.
- **Advisory**: Scenario 3 (hot reload) — timing is reported but does not cause test failure. Filesystem-dependent operations vary too much across CI environments.

## What Is Intentionally Not Measured

- Export pipeline latency (separate workflow, not a frame-loop operation)
- Application startup time (one-time cost, not a responsiveness claim)
- Package install/compile (network + filesystem bound, not runtime)
