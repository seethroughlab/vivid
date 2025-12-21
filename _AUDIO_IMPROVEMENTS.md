# Audio System Improvements

Future enhancements for the Vivid audio system to improve robustness and debugging.

## Current Status

The audio system is already solid for normal usage:
- Lock-free SPSCQueue for main→audio thread communication
- Pre-allocated buffers with capacity tracking (no allocations on audio thread)
- Atomic parameter reads with correct memory ordering
- Thread-safe trigger routing via `AudioOperator::trigger()` → `onTrigger()`

These improvements address edge cases and developer experience.

---

## 1. Event Queue Overflow Detection

### Problem

The SPSCQueue (Single-Producer Single-Consumer) has fixed capacity of 1024 events. When full, `push()` silently returns `false` and the event is dropped. This can happen during:
- Rapid MIDI input (many notes in quick succession)
- High-frequency parameter automation
- Stress testing with many sequencers

Dropped events cause subtle bugs: missing notes, parameters not updating, triggers not firing. These are hard to debug because there's no indication anything went wrong.

### Current Code

```cpp
// core/include/vivid/audio_event.h, lines 61-73
bool push(const T& value) {
    size_t head = m_head.load(std::memory_order_relaxed);
    size_t next = (head + 1) % Capacity;

    if (next == m_tail.load(std::memory_order_acquire)) {
        return false;  // Queue full - silently drops event!
    }

    m_buffer[head] = value;
    m_head.store(next, std::memory_order_release);
    return true;
}
```

### Proposed Solution

Add an atomic counter for dropped events that can be queried from the main thread:

```cpp
template<typename T, size_t Capacity = 1024>
class SPSCQueue {
public:
    // ... existing code ...

    bool push(const T& value) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next = (head + 1) % Capacity;

        if (next == m_tail.load(std::memory_order_acquire)) {
            m_droppedCount.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        m_buffer[head] = value;
        m_head.store(next, std::memory_order_release);
        return true;
    }

    /// Get number of dropped events since last reset
    uint64_t droppedCount() const {
        return m_droppedCount.load(std::memory_order_relaxed);
    }

    /// Reset dropped count (call after logging/displaying)
    void resetDroppedCount() {
        m_droppedCount.store(0, std::memory_order_relaxed);
    }

    /// Get current fill level (approximate, for monitoring)
    size_t size() const {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t tail = m_tail.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : (Capacity - tail + head);
    }

    /// Get maximum capacity
    constexpr size_t capacity() const { return Capacity; }

private:
    std::atomic<uint64_t> m_droppedCount{0};
    // ... existing members ...
};
```

### AudioGraph Integration

Add monitoring methods to `AudioGraph`:

```cpp
// core/include/vivid/audio_graph.h
class AudioGraph {
public:
    // ... existing code ...

    /// Get number of dropped events (queue overflow)
    uint64_t droppedEventCount() const {
        return m_eventQueue.droppedCount();
    }

    /// Reset dropped event counter
    void resetDroppedEventCount() {
        m_eventQueue.resetDroppedCount();
    }

    /// Get current event queue fill percentage (0.0 - 1.0)
    float eventQueueFillLevel() const {
        return static_cast<float>(m_eventQueue.size()) / m_eventQueue.capacity();
    }
};
```

### Usage in Chain

```cpp
void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Check for dropped events periodically
    static float lastCheck = 0;
    if (ctx.time() - lastCheck > 5.0f) {  // Every 5 seconds
        uint64_t dropped = chain.audioGraph()->droppedEventCount();
        if (dropped > 0) {
            std::cerr << "[Audio] Warning: " << dropped
                      << " events dropped (queue overflow)\n";
            chain.audioGraph()->resetDroppedEventCount();
        }
        lastCheck = ctx.time();
    }

    chain.process(ctx);
}
```

### Chain Visualizer Integration

Display in the ImGui chain visualizer (optional):

```cpp
// core/imgui/chain_visualizer.cpp
if (audioGraph) {
    float fill = audioGraph->eventQueueFillLevel();
    uint64_t dropped = audioGraph->droppedEventCount();

    ImGui::Text("Event Queue: %.0f%%", fill * 100.0f);
    if (dropped > 0) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                          "Dropped: %llu", dropped);
    }
}
```

### Files to Modify

| File | Change |
|------|--------|
| `core/include/vivid/audio_event.h` | Add `m_droppedCount`, `droppedCount()`, `resetDroppedCount()`, `size()` |
| `core/include/vivid/audio_graph.h` | Add `droppedEventCount()`, `resetDroppedEventCount()`, `eventQueueFillLevel()` |
| `core/imgui/chain_visualizer.cpp` | Display queue stats (optional) |

### Effort

1-2 hours

---

## 2. DSP Load Monitoring

### Problem

Users have no visibility into how much CPU time the audio thread is using. If audio processing takes too long:
- Buffer underruns occur (clicks, pops, dropouts)
- The system may become unstable
- No warning until audible problems appear

Knowing the DSP load helps users:
- Optimize heavy chains before problems occur
- Identify which operators are expensive
- Make informed decisions about adding more effects

### Proposed Solution

Measure the time spent in `processBlock()` relative to the buffer duration:

```cpp
// core/include/vivid/audio_graph.h
class AudioGraph {
public:
    // ... existing code ...

    /// DSP load as percentage (0.0 - 1.0+)
    /// Values > 1.0 indicate overload (processing slower than real-time)
    float dspLoad() const {
        return m_dspLoad.load(std::memory_order_relaxed);
    }

    /// Peak DSP load since last reset
    float peakDspLoad() const {
        return m_peakDspLoad.load(std::memory_order_relaxed);
    }

    /// Reset peak DSP load
    void resetPeakDspLoad() {
        m_peakDspLoad.store(0.0f, std::memory_order_relaxed);
    }

private:
    std::atomic<float> m_dspLoad{0.0f};
    std::atomic<float> m_peakDspLoad{0.0f};
};
```

### Implementation

Measure processing time in `processBlock()`:

```cpp
// core/src/audio_graph.cpp
void AudioGraph::processBlock(float* output, uint32_t frameCount) {
    using Clock = std::chrono::high_resolution_clock;

    auto start = Clock::now();

    // Process events
    processEvents();

    // Generate audio from all operators
    for (auto* op : m_executionOrder) {
        op->generateBlock(frameCount);
    }

    // Copy output
    if (m_outputOperator) {
        const AudioBuffer* buf = m_outputOperator->outputBuffer();
        if (buf && buf->isValid()) {
            std::memcpy(output, buf->samples, buf->byteSize());
        }
    }

    auto end = Clock::now();

    // Calculate DSP load
    double processingTime = std::chrono::duration<double>(end - start).count();
    double bufferDuration = static_cast<double>(frameCount) / AUDIO_SAMPLE_RATE;
    float load = static_cast<float>(processingTime / bufferDuration);

    // Smoothed load (exponential moving average)
    float currentLoad = m_dspLoad.load(std::memory_order_relaxed);
    float smoothedLoad = currentLoad * 0.9f + load * 0.1f;
    m_dspLoad.store(smoothedLoad, std::memory_order_relaxed);

    // Track peak
    float peak = m_peakDspLoad.load(std::memory_order_relaxed);
    if (load > peak) {
        m_peakDspLoad.store(load, std::memory_order_relaxed);
    }
}
```

### Per-Operator Profiling (Optional)

For more detailed profiling, track time per operator:

```cpp
// core/include/vivid/audio_operator.h
class AudioOperator : public Operator {
public:
    // ... existing code ...

    /// Last processing time in microseconds
    uint32_t lastProcessingTimeMicros() const {
        return m_lastProcessingTime.load(std::memory_order_relaxed);
    }

protected:
    std::atomic<uint32_t> m_lastProcessingTime{0};
};

// In AudioGraph::processBlock()
for (auto* op : m_executionOrder) {
    auto opStart = Clock::now();
    op->generateBlock(frameCount);
    auto opEnd = Clock::now();

    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(opEnd - opStart).count();
    op->m_lastProcessingTime.store(static_cast<uint32_t>(micros), std::memory_order_relaxed);
}
```

### Chain Visualizer Integration

Display DSP load in the visualizer:

```cpp
// core/imgui/chain_visualizer.cpp
if (audioGraph) {
    float load = audioGraph->dspLoad();
    float peak = audioGraph->peakDspLoad();

    // Color code: green < 50%, yellow < 80%, red >= 80%
    ImVec4 color = (load < 0.5f) ? ImVec4(0.3f, 1, 0.3f, 1) :
                   (load < 0.8f) ? ImVec4(1, 1, 0.3f, 1) :
                                   ImVec4(1, 0.3f, 0.3f, 1);

    ImGui::TextColored(color, "DSP Load: %.1f%%", load * 100.0f);
    ImGui::Text("Peak: %.1f%%", peak * 100.0f);

    if (load > 0.9f) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "WARNING: Near overload!");
    }
}
```

### Console Warning

Optionally log warnings when load is high:

```cpp
// In AudioGraph::processBlock()
static float lastWarningTime = 0;
if (load > 0.9f && (currentTime - lastWarningTime > 5.0f)) {
    std::cerr << "[Audio] Warning: DSP load at " << (load * 100)
              << "% - risk of dropouts\n";
    lastWarningTime = currentTime;
}
```

### Files to Modify

| File | Change |
|------|--------|
| `core/include/vivid/audio_graph.h` | Add `m_dspLoad`, `m_peakDspLoad`, accessors |
| `core/src/audio_graph.cpp` | Measure time in `processBlock()` |
| `core/include/vivid/audio_operator.h` | Add `m_lastProcessingTime` (optional) |
| `core/imgui/chain_visualizer.cpp` | Display DSP load |

### Effort

2-4 hours

---

## 3. Lock-Free Parameter Snapshots

### Problem

Currently, parameters are read individually using atomic loads:

```cpp
// In generateBlock() on audio thread
float cutoff = static_cast<float>(m_cutoff);  // Atomic read
float resonance = static_cast<float>(m_resonance);  // Atomic read
```

This works correctly but has a subtle issue: if the main thread updates multiple related parameters between atomic reads, the audio thread might see an inconsistent state:

```cpp
// Main thread (in update())
filter.cutoff = 2000.0f;    // Audio thread reads here...
filter.resonance = 8.0f;     // ...before this is written
// Audio thread sees cutoff=2000, resonance=old_value (torn read)
```

For most parameters this is benign (slight glitch for one block), but for tightly coupled parameters (e.g., filter coefficients, envelope times) it can cause audible artifacts.

### Current Approach

Individual atomic reads work correctly for single parameters. The `Param<T>` wrapper uses atomic storage and proper memory ordering. This is sufficient for most use cases.

### When This Matters

- Filter cutoff + resonance (affect coefficient calculation)
- ADSR envelope times (affect envelope shape)
- Synth frequency + detune (affect pitch calculation)
- Effect wet/dry + feedback (affect signal flow)

### Proposed Solution: Double-Buffered Parameter Snapshots

Use a double-buffer pattern where the main thread writes to one buffer while the audio thread reads from another:

```cpp
template<typename T>
class ParameterSnapshot {
public:
    /// Called from main thread to update parameters
    void beginUpdate() {
        m_writeIndex = 1 - m_readIndex.load(std::memory_order_acquire);
    }

    /// Get writable parameter reference (main thread only)
    T& writable() {
        return m_buffers[m_writeIndex];
    }

    /// Commit the update (main thread, after all params set)
    void endUpdate() {
        m_readIndex.store(m_writeIndex, std::memory_order_release);
    }

    /// Get readable parameter reference (audio thread only)
    const T& readable() const {
        return m_buffers[m_readIndex.load(std::memory_order_acquire)];
    }

private:
    T m_buffers[2];
    int m_writeIndex = 0;
    std::atomic<int> m_readIndex{0};
};
```

### Usage in Operators

```cpp
// In filter operator header
struct FilterParams {
    float cutoff = 1000.0f;
    float resonance = 1.0f;
    FilterType type = FilterType::Lowpass;
};

class AudioFilter : public AudioEffect {
    ParameterSnapshot<FilterParams> m_params;

public:
    // Main thread: update all params atomically
    void setCutoffAndResonance(float cutoff, float resonance) {
        m_params.beginUpdate();
        m_params.writable().cutoff = cutoff;
        m_params.writable().resonance = resonance;
        m_params.endUpdate();
    }

protected:
    void processEffect(...) override {
        // Audio thread: read consistent snapshot
        const auto& p = m_params.readable();
        updateCoefficients(p.cutoff, p.resonance, p.type);
        // ...
    }
};
```

### Simpler Alternative: Batch Updates

For simpler cases, just document that updates should be batched:

```cpp
// In update() - recommended pattern
auto& filter = chain.get<AudioFilter>("filter");
filter.beginParameterUpdate();  // Optional: marks start of batch
filter.cutoff = newCutoff;
filter.resonance = newResonance;
filter.endParameterUpdate();    // Commits all changes atomically
```

### When to Use Which

| Approach | Use Case |
|----------|----------|
| Individual atomics (current) | Single parameter updates, uncorrelated params |
| Batch updates | Multiple related params, simple operators |
| Double-buffered snapshots | Complex operators with many coupled params |

### Implementation Considerations

1. **Memory overhead**: Each snapshot doubles parameter memory (usually negligible)
2. **Latency**: One block delay between update and effect (usually acceptable)
3. **Complexity**: More code, harder to understand
4. **Compatibility**: Existing `Param<T>` pattern still works

### Recommendation

**Start with documentation** - for most operators, the current atomic approach is fine. Add a note to CLAUDE.md about batching related parameter updates:

```markdown
## Parameter Updates

When updating multiple related parameters (e.g., filter cutoff + resonance),
update them in sequence within the same frame:

```cpp
// Good: both updated in same frame
filter.cutoff = 2000.0f;
filter.resonance = 8.0f;

// Avoid: spreading updates across frames
if (frame % 2 == 0) filter.cutoff = 2000.0f;
else filter.resonance = 8.0f;  // May cause glitches
```
```

Only implement double-buffering for operators where torn reads cause audible problems (identified through testing).

### Files to Modify (if implementing)

| File | Change |
|------|--------|
| `core/include/vivid/param.h` | Add `ParameterSnapshot<T>` template |
| Operators with coupled params | Use snapshot pattern |
| `CLAUDE.md` | Document parameter batching |

### Effort

- Documentation only: 30 minutes
- Simple batch API: 2-4 hours
- Full double-buffering: 1-2 days (per operator)

---

## Priority Order

1. **Event Queue Overflow** (High priority, Low effort)
   - Immediate debugging benefit
   - Simple implementation
   - No API changes

2. **DSP Load Monitoring** (Medium priority, Medium effort)
   - Helps users optimize before problems
   - Good for chain visualizer
   - Non-invasive

3. **Parameter Snapshots** (Low priority, High effort)
   - Current approach works for most cases
   - Only needed for specific operators
   - Start with documentation

---

## Related Files

| Component | Files |
|-----------|-------|
| Event Queue | `core/include/vivid/audio_event.h` |
| Audio Graph | `core/include/vivid/audio_graph.h`, `core/src/audio_graph.cpp` |
| Parameters | `core/include/vivid/param.h` |
| Visualizer | `core/imgui/chain_visualizer.cpp` |
| Audio Operators | `addons/vivid-audio/include/vivid/audio/*.h` |
