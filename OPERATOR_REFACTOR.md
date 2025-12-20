# Operator Base Class Expansion

Analysis of additional operator base classes inspired by TouchDesigner, with focus on audio creation and reducing boilerplate.

## Current Architecture

Vivid has two specialized base classes beyond the root `Operator`:

```
Operator (base)
├── TextureOperator     - GPU texture effects (2D image processing)
├── AudioOperator       - Audio synthesis and generation (48kHz)
│   └── AudioEffect     - Audio effects with dry/wet mixing
└── (direct subclasses) - Value operators (Math, Logic), 3D geometry, cameras, lights
```

The base `Operator` class defines output types via enum:
```cpp
enum class OutputKind {
    Texture,     // GPU textures (most common)
    Value,       // Single float
    ValueArray,  // Array of floats (control signals)
    Geometry,    // 3D meshes
    Camera,      // Camera configuration
    Light,       // Light sources
    Audio,       // Audio buffer (PCM samples)
    AudioValue   // Audio analysis (levels, FFT)
};
```

## TouchDesigner Comparison

| TD Type | Description | Vivid Equivalent | Status |
|---------|-------------|------------------|--------|
| **TOPs** | 2D textures | `TextureOperator` | Complete |
| **CHOPs** | Channels (audio, motion, control) | `AudioOperator` | Partial - missing control-rate signals |
| **SOPs** | 3D geometry | `OutputKind::Geometry` | No base class |
| **DATs** | Data tables/text | None | Use JSON/config files |
| **MATs** | Materials/shaders | Inline in `Render3D` | Low priority |
| **COMPs** | Containers | `Chain` itself | Different architecture |

---

## Recommended Base Classes

### 1. SignalOperator (High Priority)

**Purpose**: Control-rate signal processing for LFOs, envelopes, sequencers, MIDI CC.

TouchDesigner CHOPs handle both audio-rate (48kHz) and control-rate (60Hz) signals. Vivid's `AudioOperator` is audio-rate only, running on a dedicated audio thread. A `SignalOperator` would handle control signals efficiently on the main thread.

#### Use Cases
- **LFO** → modulate effect parameters (filter cutoff, brightness, scale)
- **Envelope Follower** → react to audio levels for audio-reactive visuals
- **MIDI CC Mapping** → hardware control surfaces
- **Step Sequencer** → trigger notes, control patterns
- **Smooth/Filter** → smooth noisy sensor inputs
- **Math on Signals** → combine multiple control signals

#### Proposed Interface

```cpp
// core/include/vivid/signal_operator.h
#pragma once
#include <vivid/operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>

namespace vivid {

/**
 * Base class for control-rate signal operators.
 *
 * SignalOperator produces multi-channel float outputs at frame rate (60Hz),
 * similar to TouchDesigner CHOPs but without audio-rate processing.
 *
 * Use AudioOperator for audio-rate (48kHz) processing on the audio thread.
 * Use SignalOperator for control signals on the main thread.
 */
class SignalOperator : public Operator, public ParamRegistry {
public:
    SignalOperator() = default;
    virtual ~SignalOperator() = default;

    // --- Channel Access ---

    /** Get value of channel by index */
    float channel(int index) const {
        return (index >= 0 && index < channelCount()) ? m_channels[index] : 0.0f;
    }

    /** Get value of channel by name */
    float channel(const std::string& name) const {
        for (size_t i = 0; i < m_channelNames.size(); ++i) {
            if (m_channelNames[i] == name) return m_channels[i];
        }
        return 0.0f;
    }

    /** Number of output channels */
    int channelCount() const { return static_cast<int>(m_channels.size()); }

    /** Get channel name */
    std::string channelName(int index) const {
        return (index >= 0 && index < channelCount()) ? m_channelNames[index] : "";
    }

    /** Get all channel values as array */
    const std::vector<float>& channels() const { return m_channels; }

    // --- Operator Interface ---

    OutputKind outputKind() const override { return OutputKind::ValueArray; }

    /** Override to provide parameter declarations */
    std::vector<ParamDecl> params() override { return registeredParams(); }

protected:
    /** Set number of channels (call in init) */
    void setChannelCount(int count) {
        m_channels.resize(count, 0.0f);
        m_channelNames.resize(count);
    }

    /** Set channel name (call in init) */
    void setChannelName(int index, const std::string& name) {
        if (index >= 0 && index < channelCount()) {
            m_channelNames[index] = name;
        }
    }

    /** Set channel value (call in process) */
    void setChannel(int index, float value) {
        if (index >= 0 && index < channelCount()) {
            m_channels[index] = value;
        }
    }

    /** Mutable access to channels for bulk updates */
    std::vector<float>& channelsMutable() { return m_channels; }

    std::vector<float> m_channels;
    std::vector<std::string> m_channelNames;
};

} // namespace vivid
```

#### Example: LFO Operator

```cpp
// core/include/vivid/signals/lfo.h
#pragma once
#include <vivid/signal_operator.h>
#include <cmath>

namespace vivid {

/**
 * Low Frequency Oscillator for parameter modulation.
 *
 * Outputs a periodic signal at control rate (frame rate).
 * Can output sine, triangle, saw, square, or random waveforms.
 */
class LFO : public SignalOperator {
public:
    enum class Waveform { Sine, Triangle, Saw, Square, Random };

    Param<float> rate{"rate", 1.0f, 0.001f, 100.0f};      // Hz
    Param<float> amplitude{"amplitude", 1.0f, 0.0f, 10.0f};
    Param<float> offset{"offset", 0.0f, -10.0f, 10.0f};
    Param<float> phase{"phase", 0.0f, 0.0f, 1.0f};        // 0-1 phase offset
    Param<int> waveform{"waveform", 0, 0, 4};             // Waveform enum

    LFO() {
        registerParam(rate);
        registerParam(amplitude);
        registerParam(offset);
        registerParam(phase);
        registerParam(waveform);
    }

    void init(Context& ctx) override {
        setChannelCount(1);
        setChannelName(0, "value");
        m_phase = 0.0f;
    }

    void process(Context& ctx) override {
        if (!needsCook()) return;

        // Advance phase based on frame time
        float dt = ctx.deltaTime();
        m_phase += static_cast<float>(rate) * dt;
        m_phase = fmodf(m_phase, 1.0f);

        // Calculate waveform value
        float t = fmodf(m_phase + static_cast<float>(phase), 1.0f);
        float value = 0.0f;

        switch (static_cast<Waveform>(static_cast<int>(waveform))) {
            case Waveform::Sine:
                value = sinf(t * 2.0f * M_PI);
                break;
            case Waveform::Triangle:
                value = 4.0f * fabsf(t - 0.5f) - 1.0f;
                break;
            case Waveform::Saw:
                value = 2.0f * t - 1.0f;
                break;
            case Waveform::Square:
                value = t < 0.5f ? 1.0f : -1.0f;
                break;
            case Waveform::Random:
                // Sample-and-hold random
                if (t < m_lastT) m_randomValue = (rand() / (float)RAND_MAX) * 2.0f - 1.0f;
                value = m_randomValue;
                m_lastT = t;
                break;
        }

        setChannel(0, value * static_cast<float>(amplitude) + static_cast<float>(offset));
        didCook();
    }

    std::string name() const override { return "LFO"; }

private:
    float m_phase = 0.0f;
    float m_lastT = 0.0f;
    float m_randomValue = 0.0f;
};

} // namespace vivid
```

#### Example: Envelope Follower

```cpp
// core/include/vivid/signals/envelope_follower.h
#pragma once
#include <vivid/signal_operator.h>
#include <vivid/audio_operator.h>

namespace vivid {

/**
 * Extracts amplitude envelope from audio input.
 *
 * Outputs smoothed RMS level of audio signal for audio-reactive visuals.
 */
class EnvelopeFollower : public SignalOperator {
public:
    Param<float> attack{"attack", 0.01f, 0.001f, 1.0f};    // seconds
    Param<float> release{"release", 0.1f, 0.001f, 2.0f};   // seconds
    Param<float> gain{"gain", 1.0f, 0.0f, 10.0f};

    EnvelopeFollower() {
        registerParam(attack);
        registerParam(release);
        registerParam(gain);
    }

    void init(Context& ctx) override {
        setChannelCount(3);
        setChannelName(0, "level");      // Smoothed RMS
        setChannelName(1, "peak");       // Peak hold
        setChannelName(2, "raw");        // Unsmoothed RMS
    }

    /** Connect to audio source */
    EnvelopeFollower& input(AudioOperator* audio) {
        setInput(audio);
        return *this;
    }

    void process(Context& ctx) override {
        auto* audioInput = dynamic_cast<AudioOperator*>(getInput(0));
        if (!audioInput) {
            setChannel(0, 0.0f);
            setChannel(1, 0.0f);
            setChannel(2, 0.0f);
            return;
        }

        // Get audio buffer and compute RMS
        const auto& buffer = audioInput->outputBuffer();
        float sum = 0.0f;
        for (uint32_t i = 0; i < buffer.sampleCount(); ++i) {
            sum += buffer.samples[i] * buffer.samples[i];
        }
        float rms = sqrtf(sum / buffer.sampleCount()) * static_cast<float>(gain);

        // Smooth with attack/release
        float dt = ctx.deltaTime();
        float attackCoef = 1.0f - expf(-dt / static_cast<float>(attack));
        float releaseCoef = 1.0f - expf(-dt / static_cast<float>(release));

        if (rms > m_smoothed) {
            m_smoothed += (rms - m_smoothed) * attackCoef;
        } else {
            m_smoothed += (rms - m_smoothed) * releaseCoef;
        }

        // Peak hold with decay
        if (rms > m_peak) m_peak = rms;
        m_peak *= 0.99f;  // Slow decay

        setChannel(0, m_smoothed);
        setChannel(1, m_peak);
        setChannel(2, rms);
        didCook();
    }

    std::string name() const override { return "EnvelopeFollower"; }

private:
    float m_smoothed = 0.0f;
    float m_peak = 0.0f;
};

} // namespace vivid
```

#### Parameter Modulation System

To connect signals to parameters, add modulation support to `ParamRegistry`:

```cpp
// In param.h or param_registry.h

/** Modulation source binding */
struct ModulationBinding {
    SignalOperator* source = nullptr;
    int channelIndex = 0;
    float depth = 1.0f;      // Modulation amount
    float center = 0.0f;     // Center value (param value becomes center)
};

// In ParamRegistry class:
class ParamRegistry {
public:
    /** Modulate a parameter with a signal */
    void modulate(const std::string& paramName, SignalOperator* signal,
                  int channel = 0, float depth = 1.0f) {
        m_modulations[paramName] = {signal, channel, depth, 0.0f};
    }

    /** Get modulated parameter value */
    float modulatedValue(const std::string& paramName, float baseValue) {
        auto it = m_modulations.find(paramName);
        if (it == m_modulations.end()) return baseValue;

        const auto& mod = it->second;
        if (!mod.source) return baseValue;

        return baseValue + mod.source->channel(mod.channelIndex) * mod.depth;
    }

    /** Clear modulation */
    void clearModulation(const std::string& paramName) {
        m_modulations.erase(paramName);
    }

private:
    std::unordered_map<std::string, ModulationBinding> m_modulations;
};
```

#### Usage Example

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create LFO
    auto& lfo = chain.add<LFO>("lfo");
    lfo.rate = 0.5f;        // 0.5 Hz (2 second cycle)
    lfo.amplitude = 50.0f;  // Modulation range

    // Create oscillator with modulated frequency
    auto& osc = chain.add<Oscillator>("osc");
    osc.frequency = 440.0f;
    osc.modulate("frequency", &lfo, 0, 1.0f);  // LFO modulates frequency

    // Create filter with envelope-followed cutoff
    auto& envelope = chain.add<EnvelopeFollower>("env");
    envelope.input(&osc);
    envelope.attack = 0.01f;
    envelope.release = 0.2f;

    auto& filter = chain.add<Filter>("filter");
    filter.input(&osc);
    filter.cutoff = 1000.0f;
    filter.modulate("cutoff", &envelope, 0, 2000.0f);  // Audio-reactive cutoff

    // Visual feedback
    auto& noise = chain.add<Noise>("noise");
    noise.modulate("scale", &lfo, 0, 2.0f);  // LFO modulates visual noise scale

    chain.output("noise");
}
```

---

### 2. GeometryOperator (Medium Priority)

**Purpose**: Base class for 3D geometry generators to reduce boilerplate and standardize mesh handling.

Currently `Sphere`, `Cube`, `Plane`, `Cylinder`, `Cone`, `Torus` all implement similar patterns for mesh generation and dirty tracking. A base class would consolidate this.

#### Proposed Interface

```cpp
// core/include/vivid/geometry_operator.h
#pragma once
#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/render3d/mesh_data.h>

namespace vivid {

/**
 * Base class for 3D geometry generators.
 *
 * Provides standardized mesh access, dirty tracking, and bounding box computation.
 * Subclasses implement generateMesh() to create geometry.
 */
class GeometryOperator : public Operator, public ParamRegistry {
public:
    GeometryOperator() = default;
    virtual ~GeometryOperator() = default;

    // --- Mesh Access ---

    /** Get the generated mesh data */
    const MeshData& mesh() const { return m_mesh; }

    /** Check if mesh needs GPU upload (changed since last check) */
    bool meshChanged() {
        bool changed = m_meshDirty;
        m_meshDirty = false;
        return changed;
    }

    /** Get axis-aligned bounding box */
    const BoundingBox& boundingBox() const { return m_boundingBox; }

    // --- Operator Interface ---

    OutputKind outputKind() const override { return OutputKind::Geometry; }

    void process(Context& ctx) override {
        if (!needsCook()) return;

        generateMesh();
        computeBoundingBox();
        m_meshDirty = true;

        didCook();
    }

    std::vector<ParamDecl> params() override { return registeredParams(); }

protected:
    /** Override to generate mesh data */
    virtual void generateMesh() = 0;

    /** Set mesh data (marks dirty) */
    void setMesh(MeshData mesh) {
        m_mesh = std::move(mesh);
        m_meshDirty = true;
        markDirty();
    }

    /** Compute bounding box from mesh vertices */
    void computeBoundingBox() {
        if (m_mesh.positions.empty()) {
            m_boundingBox = {{0,0,0}, {0,0,0}};
            return;
        }

        glm::vec3 min(FLT_MAX), max(-FLT_MAX);
        for (size_t i = 0; i < m_mesh.positions.size(); i += 3) {
            glm::vec3 v(m_mesh.positions[i], m_mesh.positions[i+1], m_mesh.positions[i+2]);
            min = glm::min(min, v);
            max = glm::max(max, v);
        }
        m_boundingBox = {min, max};
    }

    MeshData m_mesh;
    BoundingBox m_boundingBox;
    bool m_meshDirty = false;
};

} // namespace vivid
```

#### Simplified Sphere Example

```cpp
// Before (current implementation ~100 lines)
class Sphere : public Operator, public ParamRegistry {
    // Manual mesh storage, dirty tracking, bounding box...
};

// After (with GeometryOperator base)
class Sphere : public GeometryOperator {
public:
    Param<float> radius{"radius", 1.0f, 0.01f, 100.0f};
    Param<int> segments{"segments", 32, 4, 128};

    Sphere() {
        registerParam(radius);
        registerParam(segments);
    }

    void init(Context& ctx) override {
        generateMesh();
    }

    std::string name() const override { return "Sphere"; }

protected:
    void generateMesh() override {
        m_mesh = MeshGenerator::sphere(
            static_cast<float>(radius),
            static_cast<int>(segments)
        );
    }
};
```

---

### 3. ComputeOperator (Low Priority - Future)

**Purpose**: GPU compute shader base class for parallel data processing.

WebGPU compute shaders enable GPU-accelerated algorithms that don't fit the render pipeline model.

#### Use Cases
- **Particle Systems** - GPU-based particle physics
- **Physics Simulation** - cloth, fluid, soft body
- **Point Cloud Processing** - filtering, downsampling
- **Image Analysis** - histogram, feature detection
- **Neural Networks** - inference acceleration

#### Proposed Interface (Sketch)

```cpp
// core/include/vivid/compute_operator.h
#pragma once
#include <vivid/operator.h>
#include <webgpu/webgpu.h>

namespace vivid {

/**
 * Base class for GPU compute shader operators.
 *
 * Provides storage buffer management and compute dispatch.
 */
class ComputeOperator : public Operator {
public:
    virtual ~ComputeOperator();

protected:
    /** Create compute pipeline from WGSL source */
    void createPipeline(Context& ctx, const std::string& wgslSource,
                        const std::string& entryPoint = "main");

    /** Create storage buffer */
    void createStorageBuffer(const std::string& name, size_t size,
                            const void* initialData = nullptr);

    /** Map buffer for CPU read */
    void* mapBuffer(const std::string& name);
    void unmapBuffer(const std::string& name);

    /** Dispatch compute shader */
    void dispatch(Context& ctx, uint32_t groupsX,
                  uint32_t groupsY = 1, uint32_t groupsZ = 1);

    /** Read buffer data back to CPU */
    template<typename T>
    std::vector<T> readBuffer(const std::string& name);

private:
    WGPUComputePipeline m_pipeline = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    std::unordered_map<std::string, WGPUBuffer> m_buffers;
};

} // namespace vivid
```

**Note**: Defer implementation until a specific use case requires it. WebGPU compute has complexities around buffer synchronization and async readback.

---

## Architecture Decisions

### Audio vs Signal Separation

**Decision**: Keep `AudioOperator` and `SignalOperator` separate.

| Aspect | AudioOperator | SignalOperator |
|--------|---------------|----------------|
| **Rate** | 48,000 Hz | 60 Hz (frame rate) |
| **Thread** | Audio thread | Main thread |
| **Buffer Size** | 1024 samples | Single values |
| **Use Case** | Sound synthesis | Parameter modulation |
| **Threading** | Lock-free, real-time | Normal game loop |

The `AudioValue` output kind already bridges these domains - audio analysis operators output `AudioValue` which can be read from the main thread.

### Modulation Approaches

**Option A: Explicit modulate() call** (Recommended)
```cpp
osc.modulate("frequency", &lfo, 0);
```
- Clear intent in code
- Easy to debug
- No hidden behavior

**Option B: Automatic via Param<T>**
```cpp
osc.frequency.connect(&lfo, 0);
```
- More elegant
- But magical - value changes unexpectedly
- Harder to trace

**Recommendation**: Start with Option A. Can add Option B later as syntactic sugar.

---

## Implementation Plan

### Phase 1: SignalOperator Foundation
1. Create `core/include/vivid/signal_operator.h`
2. Create `core/src/signal_operator.cpp`
3. Add to core CMakeLists.txt
4. Test with simple manual signal reading

### Phase 2: Basic Signal Operators
1. `LFO` - sine/saw/square/triangle
2. `Smooth` - low-pass filter for signals
3. `Math` operations on signals (add, multiply, clamp)

### Phase 3: Modulation System
1. Add `ModulationBinding` to `ParamRegistry`
2. Add `modulate()` / `clearModulation()` methods
3. Update parameter reading to check modulation

### Phase 4: Audio-Signal Bridge
1. `EnvelopeFollower` - audio → signal
2. `AudioAnalyzer` - FFT bands as channels
3. `BeatDetector` - onset detection

### Phase 5: GeometryOperator
1. Create base class
2. Migrate existing geometry primitives
3. Add common utilities (bounding box, LOD hints)

---

## Files to Create

| File | Description |
|------|-------------|
| `core/include/vivid/signal_operator.h` | Base class for control-rate signals |
| `core/src/signal_operator.cpp` | Implementation |
| `core/include/vivid/signals/lfo.h` | LFO operator |
| `core/include/vivid/signals/smooth.h` | Signal smoothing |
| `core/include/vivid/signals/envelope_follower.h` | Audio → signal |
| `core/include/vivid/signals/sequencer.h` | Step sequencer |
| `core/include/vivid/geometry_operator.h` | Base class for 3D geometry |

## Files to Modify

| File | Change |
|------|--------|
| `core/include/vivid/param.h` | Add ModulationBinding |
| `core/include/vivid/param_registry.h` | Add modulate() method |
| `core/CMakeLists.txt` | Add new source files |
| `addons/vivid-render3d/include/vivid/render3d/*.h` | Migrate to GeometryOperator |
