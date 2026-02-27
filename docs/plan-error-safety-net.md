# Plan: Error Safety Net

## Context

PRD Section 2.7 states: "The system never requires a restart. Non-negotiable." Currently:
- An operator that throws a C++ exception crashes the entire runtime
- A WGSL shader that fails validation leaves the pipeline partially broken
- Audio underruns produce glitchy partial audio instead of clean silence
- There are no visual indicators when something goes wrong — nodes just silently break

This plan adds a basic safety net that catches ~90% of real-world failures. It does **not** attempt to survive segfaults (which requires process isolation — a much larger architectural change). The scope is:

1. **try/catch around operator process()** — catch exceptions, mark node as errored
2. **WGSL shader error tracking** — flag for UI display (fallback already works)
3. **Audio underrun detection** — detect, output silence, count for display
4. **Visual error indicators** — red accent bar, error message in inspector, XRUN counter

## 1. try/catch Around Operator process()

### Files
- `src/runtime/scheduler.h` — add error fields to `NodeState`
- `src/runtime/scheduler.cpp` — wrap process() call

### Changes to `scheduler.h` (NodeState struct, after line 51)

Add three fields:
```cpp
// Error state (set by scheduler on exception, cleared on reload)
bool errored = false;
std::string error_message;
```

### Changes to `scheduler.cpp` (Scheduler::tick)

The critical call site is `ns.loader->process(ns.instance, &ctx)`. Wrap it:

**Before the process call** (early in the per-node loop, after the `is_audio` skip):
```cpp
if (ns.errored) {
    // Zero outputs so downstream nodes get clean signals
    std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
    for (auto& sp : ns.output_spreads) { sp.clear(); }
    continue;
}
```

**Around the process call:**
```cpp
try {
    ns.loader->process(ns.instance, &ctx);
} catch (const std::exception& e) {
    ns.errored = true;
    ns.error_message = e.what();
    std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
    for (auto& sp : ns.output_spreads) { sp.clear(); }
    std::fprintf(stderr, "[vivid] operator '%s' threw: %s\n", ns.node_id.c_str(), e.what());
} catch (...) {
    ns.errored = true;
    ns.error_message = "Unknown exception";
    std::fill(ns.output_values.begin(), ns.output_values.end(), 0.0f);
    for (auto& sp : ns.output_spreads) { sp.clear(); }
    std::fprintf(stderr, "[vivid] operator '%s' threw unknown exception\n", ns.node_id.c_str());
}
```

### Clear on reload

In `Scheduler::reload_operator()`, after `init_node_state()` for the reloaded node:
```cpp
ns.errored = false;
ns.error_message.clear();
```

This ensures hot-reloading a fixed operator clears its error state and gives it a fresh start.

---

## 2. WGSL Shader Error Tracking

### File
- `src/operator_api/wgsl_filter.h`

### Current behavior
The existing `check_hot_reload()` already handles shader failure gracefully — on compile failure, it logs to stderr, updates `last_mtime_`, and keeps the old pipeline running. This is correct. No behavior change needed.

### Addition
Add a flag so the UI can show shader errors:

```cpp
bool shader_error_ = false;
std::string shader_error_msg_;
```

Set `shader_error_ = true` on compile failure (with a description). Clear on successful recompile.

Add accessors:
```cpp
bool has_shader_error() const { return shader_error_; }
const std::string& shader_error_msg() const { return shader_error_msg_; }
```

**Note:** Surfacing this to the UI requires either a virtual method on `OperatorBase` or checking the flag through the scheduler. The simplest approach: after process() for GPU nodes, check if the operator has a shader error and copy it to a `NodeState::warning_message` field. This can be deferred — the stderr logging is sufficient for now.

---

## 3. Audio Underrun Detection

### Files
- `src/runtime/audio_engine.h` — add atomic counters
- `src/runtime/audio_engine.cpp` — add timing in callback

### Changes to `audio_engine.h`

Add private members:
```cpp
std::atomic<uint32_t> underrun_count_{0};
std::atomic<bool> last_buffer_underrun_{false};
```

Add public accessors:
```cpp
uint32_t underrun_count() const { return underrun_count_.load(std::memory_order_relaxed); }
bool last_buffer_underrun() const { return last_buffer_underrun_.load(std::memory_order_relaxed); }
```

### Changes to `audio_engine.cpp` (audio_callback)

Wrap the processing block in timing:
```cpp
auto start = std::chrono::high_resolution_clock::now();
// ... existing processing ...
auto elapsed = std::chrono::high_resolution_clock::now() - start;
double elapsed_sec = std::chrono::duration<double>(elapsed).count();
double budget_sec = static_cast<double>(frame_count) / 48000.0;

if (elapsed_sec > budget_sec) {
    underrun_count_.fetch_add(1, std::memory_order_relaxed);
    last_buffer_underrun_.store(true, std::memory_order_relaxed);
    std::memset(output, 0, frame_count * 2 * sizeof(float));  // silence
} else {
    last_buffer_underrun_.store(false, std::memory_order_relaxed);
}
```

**Real-time safety:** `std::chrono::high_resolution_clock::now()` uses `mach_absolute_time()` on macOS (lock-free). `std::atomic` ops are lock-free for simple types. No heap allocations added to the audio thread.

---

## 4. Visual Error Indicators

### Files
- `src/ui/graph_snapshot.h` — add error + underrun fields
- `src/ui/node_graph_constants.h` — add error color
- `src/ui/node_graph_draw.cpp` — draw error indicators
- `src/runtime/main.cpp` — populate snapshot with error data

### Changes to `graph_snapshot.h`

Add to `NodeSnapshot`:
```cpp
bool errored = false;
std::string error_message;
```

Add to `GraphSnapshot`:
```cpp
uint32_t audio_underrun_count = 0;
bool audio_underrun_active = false;
```

### Changes to snapshot builder in `main.cpp`

When building `NodeSnapshot`, copy from `NodeState`:
```cpp
sn.errored = ns.errored;
sn.error_message = ns.error_message;
```

After building nodes:
```cpp
snap.audio_underrun_count = audio_engine.underrun_count();
snap.audio_underrun_active = audio_engine.last_buffer_underrun();
```

### Changes to `node_graph_constants.h`

```cpp
static constexpr std::array<float, 3> kErrorAccent = { 0.90f, 0.25f, 0.25f };
```

### Changes to `node_graph_draw.cpp`

**In `draw_graph()`:** When determining `dcol` for a node, check error state first:
```cpp
const float* dcol;
if (node_snap.errored) {
    dcol = kErrorAccent.data();
} else {
    dcol = domain_color(r.domain);
}
```
This turns the accent bar, port dots, and sparkline red for errored nodes.

**Red border on errored nodes:** After drawing the accent bar, if errored, draw a 2px red border around the full node rect.

**In inspector:** When the selected node is errored, show "ERROR: {message}" in red text at the top of the inspector, before the params.

**In perf bar:** Add an "XRUN {count}" indicator when `snap_.audio_underrun_count > 0`, in a warm red color. Flash/pulse when `audio_underrun_active` is true.

---

## Verification

### Exception handling
1. Create a test operator that throws `std::runtime_error` in process()
2. Add it to a graph → node shows red error state, rest of graph keeps running
3. Fix and hot-reload → error clears, node resumes normal operation

### Shader fallback
1. Edit a .wgsl file to introduce a syntax error
2. Verify: stderr shows error, node continues rendering with old shader
3. Fix the shader → node picks up the fix on next hot-reload check

### Audio underrun
1. Create an audio operator with an intentional busy-wait in process()
2. Verify: XRUN counter appears in perf bar, audio outputs silence instead of glitchy audio
3. Remove busy-wait → XRUN counter stops incrementing

### Visual indicators
1. Red accent bar + border visible on errored nodes
2. Error message visible in inspector
3. XRUN counter visible in perf bar

## Implementation Order

1. try/catch in scheduler (foundation)
2. Audio underrun detection (independent)
3. Snapshot fields + error indicators in draw code (depends on 1 + 2)
4. WGSL shader error flag (optional, lowest priority)
