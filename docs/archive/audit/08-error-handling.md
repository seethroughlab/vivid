# Phase 8: Error Handling & Robustness

**Date:** 2026-04-03
**Status:** Complete

## Summary Table

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| E-01 | High | Null Pointer | Frame executor dereferences `cn.instance` without null check — segfault if instance creation fails silently | `src/runtime/graph/frame_executor.cpp` |
| E-02 | Medium | Thread Safety | Lane lift instances copied without synchronization during rebuild | `src/runtime/graph/audio_executor.cpp` |
| E-03 | Info | Error Reporting | Consistent `CommandResult` pattern throughout runtime_api | `src/runtime/control/runtime_api.cpp` |
| E-04 | Info | Resource Cleanup | GPU, audio, and file resources properly released in all paths | Various |
| E-05 | Info | Graceful Degradation | GPU failure, audio unavailable, corrupt graphs, missing operators all handled gracefully | Various |
| E-06 | Info | Thread Safety | Audio-frame bridge uses lock-free double-buffering with proper atomics | `src/runtime/audio/audio_frame_bridge.h` |
| E-07 | Info | Boundary Checks | Parameter and port access consistently bounds-checked | Various |

## Severity Definitions

Same scale as Phase 1.

---

## Findings

### E-01: Frame executor null instance dereference [High]

**What:** `frame_executor.cpp` calls `cn.loader->process_gpu(cn.instance, ...)` and `cn.loader->process_frame(cn.instance, ...)` without checking that `cn.instance` is non-null. If `graph_compiler.cpp` fails to create an instance (e.g., `create_instance()` returns null) without setting `cn.missing_operator = true`, the frame executor will segfault.

**The chain:**
1. `graph_compiler.cpp`: `cn.instance = loader->create_instance()` — no null check on result
2. `frame_executor.cpp`: checks `cn.missing_operator || !cn.loader` but NOT `!cn.instance`
3. Dereference via `cn.loader->process_gpu(cn.instance, ...)` crashes

**Note:** The audio executor has the correct guard at line 365: `if (!cn.loader || !cn.instance || cn.errored) continue;`

**Recommendation:** Add `!cn.instance` to the existing guard in frame_executor.cpp. Also mirror the audio executor's pattern.

**Effort:** Trivial (1 line change)

### E-02: Lane lift instance copy without synchronization [Medium]

**What:** `audio_executor.cpp` copies `cn.instance` to lane group instances during graph rebuild without synchronization with the audio callback thread. If the audio thread is executing while the frame thread rebuilds, a race condition is possible.

**Why it matters:** Could cause use-after-free if the old instance is deleted while the audio thread still references it. In practice, the rebuild sequence likely prevents this (audio engine shuts down before rebuild), but the code doesn't enforce this ordering.

**Recommendation:** Document the invariant (audio must be stopped during rebuild) or add an assertion. No immediate code change needed if the shutdown ordering is guaranteed by callers.

### E-03: CommandResult pattern is consistent [Info]

All `RuntimeAPI` methods return `CommandResult{bool ok, std::string message}`. This is used consistently throughout the control server dispatch, MCP bridge, and main loop. Error messages are descriptive and include context.

### E-04: Resource cleanup is thorough [Info]

- **GPU:** `gpu_context.cpp` releases surface, queue, device, adapter, instance in correct order in `shutdown()`
- **Audio:** `ma_device` properly deleted in all paths (init failure, shutdown, destructor)
- **Files:** All `fopen()`/`popen()` calls have matching closes. `std::ifstream`/`std::ofstream` use RAII.
- **Memory:** Smart pointers (`unique_ptr`, `shared_ptr`) used throughout. One raw `new ma_device` in audio_executor has proper manual cleanup.

### E-05: Graceful degradation is well-implemented [Info]

| Failure | Behavior |
|---------|----------|
| GPU init fails | App continues, nodes output black |
| Audio device unavailable | Silent processing, null device fallback |
| Corrupt graph JSON | Parse error caught, partial load with warnings |
| Missing operator | Node flagged as missing, outputs zero, UI shows "MISSING" badge |
| Schema version too new | Hard reject with clear error message |
| Operator crash (exception) | Caught, node flagged `errored=true`, other nodes continue |

### E-06: Audio-frame bridge threading is correct [Info]

Lock-free double-buffering between audio and frame threads using `std::atomic<int>` with acquire/release memory ordering. No mutex contention on the hot path.

### E-07: Boundary checks are consistent [Info]

- Parameter indices validated via map lookup before array access
- Port indices bounds-checked before use
- Resolution clamped to 8192x8192 maximum
- Choice index bounds-checked before accessing labels
- Division-by-zero guards in remap calculations

---

## Prioritized Action Plan

### Immediate (applied with this audit)
1. **E-01** — Add null instance guard in frame_executor.cpp (High, Trivial)

### Deferred
2. **E-02** — Document rebuild ordering invariant in audio_executor.cpp (Medium, Trivial)
