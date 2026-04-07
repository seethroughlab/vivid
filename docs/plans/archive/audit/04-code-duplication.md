# Phase 4: Code Duplication

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| D-01 | Medium | Test Boilerplate | ~15 test files redefine identical `check()`/`check_float()` functions | `tests/` |
| D-02 | Medium | Operator Variant | `pattern_seq` FR/AU variants are 99% identical, missing `_core.h` | `operators/control/pattern_seq/` |
| D-03 | Info | Operator Variant | 11 of 20 control operators properly use `_core.h` for shared logic | `operators/control/` |
| D-04 | Info | Parameter Setup | ~40 lines of setup boilerplate per operator — inherent to plugin architecture | `operators/` |
| D-05 | Info | UI Drawing | Minimal duplication across draw files — widget-specific variation is appropriate | `src/ui/graph/` |
| D-06 | Info | Utilities | String utils centralized in `common/string_util.h`, JSON usage is idiomatic | `src/common/`, `src/runtime/control/` |

## Severity Definitions

Same scale as Phase 1.

---

## Findings

### D-01: Test helper functions duplicated across ~15 files [Medium]

**What:** Most test files independently define identical `check()` and `check_float()` helper functions (~15 lines each). A shared `tests/test_helpers.h` exists but is not included by most tests.

**Example (repeated in each file):**
```cpp
static int failures = 0;
static void check(bool cond, const char* msg) {
    if (!cond) { std::fprintf(stderr, "  FAIL: %s\n", msg); failures++; }
    else       { std::fprintf(stderr, "  PASS: %s\n", msg); }
}
static void check_float(float actual, float expected, const char* msg) {
    if (std::fabs(actual - expected) > 1e-4f) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else { std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual); }
}
```

Additionally, ~8 test files repeat a graph/registry setup pattern (~12 lines each) for creating an operator registry, loading a graph, and building a runtime.

**Total duplicated lines:** ~325 (225 check functions + 100 setup boilerplate)

**Recommendation:** Consolidate into `tests/test_helpers.h` with templated `check()` supporting both 2-arg and tolerance-based `check_float()`. Add a `setup_test_runtime()` helper for graph-based tests. Update all test files to include the shared header.

**Effort:** Small

---

### D-02: `pattern_seq` FR/AU variants are 99% identical [Medium]

**What:** `operators/control/pattern_seq/pattern_seq_fr.cpp` (185 lines) and `pattern_seq_au.cpp` (192 lines) share ~170 identical lines — parameter definitions, port setup, and the core `compute()` function. Only the frame/audio tick wrapper differs (~15 lines).

**Why it matters:** Other control operators (clock, envelope, lfo, step_seq, etc.) properly extract shared logic into a `*_core.h` header, keeping the variants as thin wrappers of 15-30 lines. `pattern_seq` is the exception.

**Recommendation:** Create `pattern_seq_core.h` containing the shared class definition, parameters, ports, and `compute()` logic. Reduce each variant to a ~20 line wrapper.

**Effort:** Small

---

### D-03: FR/AU variant pattern is well-structured [Info]

20 control operators have both frame-rate and audio-rate variants. The project uses a consistent pattern:

| Pattern | Operators | Duplication |
|---------|-----------|-------------|
| `_core.h` shared header | 11 operators (clock, envelope, lfo, smooth, step_seq, arpeggiator, drum_sequencer, tracker, euclidean, note_pattern, sequencer) | Minimal — variants are 15-30 line wrappers |
| Inline shared logic | 9 simpler operators (gate, sample_hold, phase_to_midi, quantizer, etc.) | Low — these are small enough that duplication is acceptable |

The `_core.h` pattern works well: shared state, parameters, and computation logic live in the header; each variant just implements the cadence-specific tick function.

---

### D-04: Parameter setup boilerplate is inherent to plugin architecture [Info]

Every operator repeats ~40 lines of parameter/port definition and collection:
- `Param<float>` member declarations
- `collect_params()` override pushing each param
- `collect_ports()` override defining I/O ports
- Semantic tag/description annotations

This boilerplate is inherent to the `VIVID_REGISTER` macro architecture, which uses static introspection at plugin load time. The pattern is consistent and clear — reducing it would require a DSL or code generation layer that adds complexity without proportional benefit.

---

### D-05: UI drawing code has minimal duplication [Info]

After the Wave 1 split of `node_graph_draw.cpp` into 5 files, checked for repeated patterns:
- Tooltip drawing: 2-3 instances with similar shadow+rect+text patterns, but each tooltip has different styling and content
- Hit-test patterns: Shared via template `hit_test_rect()` in `node_graph.cpp`
- Rect+text combos: Inherent to immediate-mode rendering — not extractable without losing clarity

Widget-specific variation makes further factoring counterproductive.

---

### D-06: String and JSON utilities are properly centralized [Info]

- **String utilities:** `format_float`, `format_int`, `format_uint` centralized in `common/string_util.h`
- **JSON construction:** Consistent idiomatic usage of nlohmann/json throughout `control_server_dispatch.cpp` and `control_server_query.cpp`
- **Path manipulation:** Uses `std::filesystem` throughout — no custom utilities duplicated

---

## Action Items

1. **D-01:** Unify test helpers into shared `test_helpers.h` (Medium, Small effort)
2. **D-02:** Extract `pattern_seq_core.h` (Medium, Small effort)
