# Plan: Production Gate — Phase 8c (sustained silence / black detection)

## Context

Phase 8c is the final sub-phase of the umbrella Phase 8 plan and the largest. It adds the two stateful runtime probes the parent plan promised but Phases 3–5 deferred:

- **Sustained silence**: a graph that should produce audio but renders nothing for N seconds.
- **Sustained black**: a graph that should render visuals but stays at near-zero brightness for N seconds.

`OutputAnalyzer` is stateless and `runtime_health` is currently a per-call snapshot, so neither can detect "sustained over time." Phase 8c adds the missing layer: a small fixed-capacity sampler on `RuntimeCore` that captures audio peak + GPU sink brightness once per frame, then exposes window-aggregated booleans (`silence_active`, `black_active`) to `runtime_health::collect()`.

This is Phase 8c of `docs/plans/production-gate-phase8.md`, shipping as its own commit on the `worktree-production-gate-and-health` branch.

## Decisions locked in (with rationale)

- **Sliding-window state lives on `RuntimeCore`** as a new `RuntimeHealthSamplers` member (header `src/runtime/core/runtime_health_samplers.h`). Per-frame sampling driven by a new public method `RuntimeCore::sample_runtime_health(double time)`. main.cpp calls it once per frame after `runtime.tick()` (so the sample reflects the just-completed frame's analysis output); `test_demo_graphs` calls it in the same per-tick loop.
- **Window default = 5 seconds**, sample cadence = once per call. With 60 Hz frame rate, ring capacity = 360 slots (one extra slot of headroom) × `(double, float)` = ~5.7 KB per ring. Two rings (audio + visual) = ~11.5 KB allocated once at `adopt_prepared_build()`. Negligible.
- **`time` argument is caller-domain**: live runtime passes wall-clock-ish time; tests pass `frame * dt` sim time. Sampler doesn't care — it just compares `now - sample.time` to `window_seconds`. This means tests can deterministically drive the window math.
- **Min-samples gate before firing = 30** (≈ 0.5s at 60 fps). Avoids false positives during the first half-second of a graph (e.g. an envelope's attack ramping up from zero).
- **Audio sample source: max of `AnalysisSnapshot::peak` across all nodes/channels**, taken from the same lock-free `core.audio_frame_bridge().active_analysis()` that 8b's clipping aggregator uses. No new probe.
- **Visual sample source: brightness from `find_effective_gpu_sink()`'s `GpuNodeState::frame_analysis->brightness()`**. If the sink is -1, or the node has no `gpu`, or `frame_analysis` is null, the sample is `nullopt` — sampler treats this graph as `not_applicable` for black detection (no samples → `black_active = false`).
- **Detection thresholds**: silence if `peak < 0.001` (matches `test_demo_graphs.cpp:472`'s existing audio-output check); black if `brightness < 4` (out of 255, ≈ 1.5%). Both numbers are conservative — visible content is well above them.
- **Analysis enable is already on by default** (`frame_executor.h:68: analysis_enabled_ = true`, `audio_executor.h:108: analysis_enabled_{true}`). No new toggles needed in `test_demo_graphs`.
- **Domain filtering happens in budget evaluation, NOT in `runtime_health`.** The snapshot reports `silence_active=true` regardless of graph type; the budget filters by `applies_to=audio,av` so a control-only graph never trips it. Mirrors the Phase 5 pattern.
- **`apply_severity_rules` reads `audio.silence_active` / `gpu.black_active` only** — the rollup function stays pure. Window math is in the sampler.

## Scope

In scope:
- New header `src/runtime/core/runtime_health_samplers.h` + `.cpp`:
  - `RuntimeHealthSamplers` class (two ring buffers + reducers).
  - Public methods: `clear()`, `sample(double time, float audio_peak, std::optional<float> gpu_brightness)`, `audio_silence_active(double window_s, double now)`, `visual_black_active(double window_s, double now)`, `audio_window_seconds(double now)`, `visual_window_seconds(double now)`.
- New `RuntimeHealthSamplers samplers_` member on `RuntimeCore`. Cleared/resized in `adopt_prepared_build()`. Public `health_samplers()` accessor (const + mutable).
- New `RuntimeCore::sample_runtime_health(double time)` — reads audio peak from bridge + GPU brightness from sink, calls `samplers_.sample(...)`.
- New fields on `RuntimeHealthSnapshot`:
  ```cpp
  // AudioHealth:
  double silence_window_seconds = 0.0;
  bool   silence_active = false;
  // GpuHealth:
  double black_window_seconds = 0.0;
  bool   black_active = false;
  ```
- `populate_minimal()` reads from `core.health_samplers()` to fill the new fields.
- New severity rules (in `apply_severity_rules`):
  - `sustained_silence` (Warning) — when `audio.silence_active`. Subject = "audio". Message includes window seconds.
  - `sustained_black` (Warning) — when `gpu.black_active`. Subject = "video".
- `main.cpp` calls `runtime.sample_runtime_health(time)` once per frame, post-`runtime.tick()`.
- `tests/integration/test_demo_graphs.cpp` calls `runtime.sample_runtime_health(time)` inside the per-tick loop (using the same `time = frame * 0.016` it already computes).
- Uncomment `no_sustained_silence` and `no_sustained_black` budget entries in `tools/production_gate_budgets.toml`.
- Extend `_check_budget` switch in `production_gate_report.py` for both codes.
- Update `to_json` to emit the four new fields.
- Tests:
  - `tests/control/test_runtime_health_samplers.cpp` (new) — unit tests for the sampler class: feed N samples, check window detection, verify min-samples gate, verify ring wraparound.
  - `test_runtime_health_snapshot.cpp` — severity-rollup cases for both new rules.
  - `test_production_gate_report.py` — budget cases for both codes + domain filter (silence on a control-only graph stays silent).
- New fixtures: `tests/cli/fixtures/health/sustained_silence.json`, `sustained_black.json`.

Out of scope (deferred):
- Per-node silence attribution (graph-level only).
- Reset semantics on variation switch / preset recall.
- Hysteresis (once silence fires, when does it un-fire?). Default behavior: if any sample in the window goes above threshold, `silence_active = false`. Simple and reactive.

## Files

New:
- `src/runtime/core/runtime_health_samplers.h`
- `src/runtime/core/runtime_health_samplers.cpp`
- `tests/control/test_runtime_health_samplers.cpp`
- `tests/cli/fixtures/health/sustained_silence.json`
- `tests/cli/fixtures/health/sustained_black.json`

Modified:
- `src/runtime/core/runtime_core.{h,cpp}` — `RuntimeHealthSamplers` member, `sample_runtime_health()`, clear in `adopt_prepared_build()`.
- `src/runtime/core/runtime_health.{h,cpp}` — new fields, populate from samplers, severity rules, JSON.
- `src/runtime/core/main.cpp` — call `sample_runtime_health(time)` post-tick.
- `tests/integration/test_demo_graphs.cpp` — same call inside per-tick loop.
- `tools/production_gate_budgets.toml` — uncomment the two future-probe entries.
- `tools/production_gate_report.py` — extend `_check_budget`.
- `cmake/app.cmake` + `cmake/tests.cmake` — add new `.cpp` to source lists (mirror runtime_health.cpp wiring).
- `cmake/tests/10-runtime-control-graph.cmake` — register the new sampler test (HEADLESS_SMOKE label).
- `tests/control/test_runtime_health_snapshot.cpp` — 2 new severity cases.
- `tests/cli/test_production_gate_report.py` — 2 new budget cases + 1 domain-filter case.
- `tests/cli/fixtures/budgets/default.toml` — add the two new budgets.
- `docs/testing/production-gate.md` — note the new fields.

## Code sketch

### `runtime_health_samplers.h`

```cpp
#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace vivid {

// Fixed-capacity ring buffer + window-aggregating reducers used by
// runtime_health to detect sustained silence / black output. Owned by
// RuntimeCore; sampled once per frame via RuntimeCore::sample_runtime_health.
//
// The `time` axis is caller-domain (sim time in tests, wall-clock in live).
// All window queries use the same domain.
class RuntimeHealthSamplers {
public:
    static constexpr size_t kCapacity = 360;          // ~6 seconds at 60 Hz
    static constexpr float  kSilenceThreshold = 0.001f;
    static constexpr float  kBlackThreshold = 4.0f;   // brightness 0–255
    static constexpr int    kMinSamples = 30;         // ~0.5s at 60 Hz
    static constexpr double kDefaultWindowSeconds = 5.0;

    void clear();   // called from RuntimeCore::adopt_prepared_build

    // Sample one frame. `gpu_brightness` is nullopt when no GPU sink is
    // available (control-only graph) or when frame_analysis isn't populated.
    void sample(double time, float audio_peak,
                std::optional<float> gpu_brightness);

    // Window query. Returns true iff:
    //   - at least kMinSamples samples exist within [now - window, now], AND
    //   - every such sample is below threshold.
    // Both predicates are necessary to avoid false positives at startup.
    bool audio_silence_active(double now,
                              double window = kDefaultWindowSeconds) const;
    bool visual_black_active (double now,
                              double window = kDefaultWindowSeconds) const;

    // Window-fullness in seconds (oldest in-window sample → now). Surfaced
    // in the snapshot so consumers can see "we have N seconds of evidence".
    double audio_window_seconds(double now,
                                double window = kDefaultWindowSeconds) const;
    double visual_window_seconds(double now,
                                 double window = kDefaultWindowSeconds) const;

private:
    struct Slot { double time = 0.0; float value = 0.0f; bool valid = false; };
    Slot audio_ring_[kCapacity]{};
    Slot visual_ring_[kCapacity]{};
    size_t audio_head_ = 0;   // next write index
    size_t visual_head_ = 0;
    bool   visual_ever_sampled_ = false;  // distinguishes "no GPU sink" from "all-black"
};

}  // namespace vivid
```

### `RuntimeCore::sample_runtime_health(double time)`

```cpp
void RuntimeCore::sample_runtime_health(double time) {
    // Audio: max of |peak| across nodes/channels in active analysis.
    float audio_peak = 0.0f;
    const auto& a = audio_frame_bridge_.active_analysis();
    for (const auto& nodes : a.peak)
        for (float v : nodes) {
            float av = std::fabs(v);
            if (av > audio_peak) audio_peak = av;
        }

    // Visual: brightness from the effective GPU sink, if any.
    std::optional<float> brightness;
    int sink = find_effective_gpu_sink();
    if (sink >= 0 && compiled_graph_) {
        const auto& n = compiled_graph_->nodes[sink];
        if (n.gpu && n.gpu->frame_analysis) {
            brightness = n.gpu->frame_analysis->brightness();
        }
    }

    samplers_.sample(time, audio_peak, brightness);
}
```

### Severity rules

```cpp
if (snap.audio.silence_active) {
    snap.findings.push_back({
        "sustained_silence", Severity::Warning, "audio",
        "Audio output has been silent for "
            + std::to_string(snap.audio.silence_window_seconds) + "s.",
    });
    bump(Severity::Warning);
}
if (snap.gpu.black_active) {
    snap.findings.push_back({
        "sustained_black", Severity::Warning, "video",
        "Video output has been black for "
            + std::to_string(snap.gpu.black_window_seconds) + "s.",
    });
    bump(Severity::Warning);
}
```

### Budget evaluator extension

```python
elif code == "no_sustained_silence":
    a = health.get("audio", {}) or {}
    if bool(a.get("silence_active", False)):
        ws = a.get("silence_window_seconds", 0)
        return f"audio silent for {ws}s"
elif code == "no_sustained_black":
    g = health.get("gpu", {}) or {}
    if bool(g.get("black_active", False)):
        ws = g.get("black_window_seconds", 0)
        return f"video black for {ws}s"
```

## Test design

`tests/control/test_runtime_health_samplers.cpp` — pure unit test for the sampler:
- Empty sampler → both queries false; window_seconds = 0.
- Feed 10 silent audio samples (peak=0) → fewer than `kMinSamples` → `silence_active=false`.
- Feed 30 silent audio samples spanning 0.5s → `silence_active=true`; `silence_window_seconds≈0.5`.
- Feed 30 silent then 1 loud → `silence_active=false` (one above-threshold sample breaks the window).
- Feed 30 silent at t=[0..0.5], then ask at now=10 → samples are out of window → `silence_active=false`.
- Same shape for visual brightness.
- Visual: never sampled (control-only graph) → `black_active=false` regardless of window.
- Ring wraparound: feed `kCapacity + 50` samples, confirm only the most recent are queried.

`test_runtime_health_snapshot.cpp` — 2 new severity cases:
- Construct snapshot with `audio.silence_active=true`, run rules → `sustained_silence` finding fires.
- Construct snapshot with `gpu.black_active=true`, run rules → `sustained_black` finding fires.

`test_production_gate_report.py` — 3 new cases:
- Sustained silence on audio-domain graph → `degraded` with `no_sustained_silence` breach.
- Sustained black on gpu-domain graph → `degraded` with `no_sustained_black` breach.
- Sustained silence on `["control"]` graph → no breach (domain filter excludes it).

Fixtures: per-graph health JSONs with `audio.silence_active: true` (silence fixture) and `gpu.black_active: true` (black fixture).

## Verification

Local (in worktree):
```bash
# Sampler unit tests
cmake --build build --target test_runtime_health_samplers -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R test_runtime_health_samplers --output-on-failure -V

# Snapshot rollup + control_server back-compat
cmake --build build --target test_runtime_health_snapshot test_control_server -j$(sysctl -n hw.logicalcpu)
ctest --test-dir build -R 'test_runtime_health_snapshot|test_control_server' --output-on-failure

# Pytest including the new budget cases
uv run --with pytest pytest tests/cli/test_production_gate_report.py -v

# Full gate — confirm no false positives on real demo graphs
cmake --build build --target production_gate_core -j$(sysctl -n hw.logicalcpu)
python3 -c "
import json, glob
files = glob.glob('build/reports/health/*.json')
silent = [f for f in files if json.load(open(f))['health']['audio'].get('silence_active')]
black  = [f for f in files if json.load(open(f))['health']['gpu'].get('black_active')]
print(f'silent: {len(silent)}/{len(files)}'); print(' ', silent[:5])
print(f'black:  {len(black)}/{len(files)}');  print(' ', black[:5])
"
```

Expected: very few graphs (ideally zero) trip the new probes. If many do, the gate has surfaced more real signal — same outcome as 8b's clipping discovery — and we can decide per-graph whether to fix the demo or relax the budget.

Negative test:
- Live `curl -X POST http://localhost:9876/get_runtime_health` after running a deliberately-silent graph (e.g. zero-gain Mixer feeding `audio_out`) — confirm `audio.silence_active: true` after 30+ frames.

## Risks

1. **False positives on slow-starting graphs.** Mitigated by `kMinSamples=30` (≈0.5s before any decision). If a demo's audio attack takes >0.5s + the per-graph child runs only 60 ticks (1s), the early portion may still trip silence. Mitigation: `test_demo_graphs` already runs 240 ticks (4s) for audio graphs, so ≥3.5s post-attack samples will dominate the window. Real signal, not false positive.
2. **GPU readback cost.** `frame_analysis` does a 4×4 pixel readback per GPU node per frame. Already in production for live UI; cost is documented as trivial. No change.
3. **`time` domain consistency.** main.cpp passes wall-clock-ish; tests pass sim. As long as the sampler's stored `time` and `now` queries come from the same caller, the math is correct. Document in the header.
4. **Visual sink can be -1** (control-only graph). Handled by `optional<float>` and `visual_ever_sampled_` so we don't claim "all black" when we never sampled.
5. **Phase 8b discovery may repeat.** 8b surfaced 11 clipping breaches the first time. 8c may surface real silence/black on graphs that don't actually produce those modes for their full tick window (e.g. a sequencer with a long rest). Document budget tuning workflow in `production-gate.md`.

## What I will do on approval

1. Move this plan to `docs/plans/production-gate-phase8c.md`.
2. Add `runtime_health_samplers.{h,cpp}`. Wire into both CMake source lists.
3. Add `samplers_` member + `sample_runtime_health()` to `RuntimeCore`. Clear in `adopt_prepared_build()`.
4. Add new fields + severity rules + JSON to `runtime_health.{h,cpp}`.
5. Insert `runtime.sample_runtime_health(time)` post-tick in main.cpp + per-tick in test_demo_graphs.
6. Uncomment + extend Python switch for the two new budgets.
7. Write `test_runtime_health_samplers.cpp` (sampler unit tests).
8. Add 2 snapshot test cases + 3 pytest budget cases + 2 fixtures.
9. Run the test suite + production_gate_core. Inspect health JSONs for any new silence/black surfacings.
10. Live-test by running a known-silent fixture graph.
11. Report back.
