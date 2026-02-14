# Vivid: LLM Iteration & Cloud Development Plan

## Overview

This plan prepares Vivid for agent-driven development workflows where an LLM iterates autonomously on creative code, validates its own work through structured introspection, and produces human-reviewable A/V exports — all running headless in a cloud environment. The goal: an LLM does 10–20 iterations silently for every 1 message the human sends.

Two phases address two fundamentally different needs:

- **Phase 1 — LLM Self-Evaluation:** Fast, cheap, structured. The agent edits `chain.cpp`, runs introspection, checks assertions, and iterates without human involvement.
- **Phase 2 — Human Review via Puppeteered Export:** The agent produces actual video/audio exports with simulated interactivity so a human can experience the result and provide subjective feedback.

---

## Phase 1: Operator Introspection & Automated Validation

### 1.1 Base Introspection API

Add an `inspect()` method to the base `Operator` class. Every operator overrides it to report domain-specific metrics.

```cpp
struct InspectData {
    std::unordered_map<std::string, float> metrics;
    std::unordered_map<std::string, std::string> metadata;

    void set(const std::string& key, float value) { metrics[key] = value; }
    void set(const std::string& key, const std::string& value) { metadata[key] = value; }

    std::string toJSON() const;
};

class Operator {
public:
    virtual void init(Context& ctx) = 0;
    virtual void process(Context& ctx) = 0;

    // New: introspection for LLM evaluation
    virtual InspectData inspect() const {
        InspectData data;
        data.set("enabled", enabled ? 1.0f : 0.0f);
        return data;
    }
};
```

#### Example Operator Overrides

```cpp
// Visual: Noise
InspectData Noise::inspect() const {
    auto data = Operator::inspect();
    data.set("scale", scale);
    data.set("speed", speed);
    data.set("octaves", static_cast<float>(octaves));
    data.set("type", noiseTypeName(type));
    return data;
}

// Visual: Feedback
InspectData Feedback::inspect() const {
    auto data = Operator::inspect();
    data.set("decay", decay);
    data.set("energy", computeEnergy());         // avg pixel brightness of output
    data.set("pixel_change_pct", frameDelta());   // % pixels changed from previous frame
    data.set("frames_alive", framesAlive_);
    return data;
}

// Audio: BandSplit
InspectData BandSplit::inspect() const {
    auto data = Operator::inspect();
    data.set("bass", bass());
    data.set("mid", mid());
    data.set("high", high());
    return data;
}

// Audio: Levels
InspectData Levels::inspect() const {
    auto data = Operator::inspect();
    data.set("rms", rms());
    data.set("peak", peak());
    data.set("clipping", isClipping() ? 1.0f : 0.0f);
    return data;
}
```

### 1.2 Output Texture Analysis

A `FrameAnalysis` struct provides LLM-readable spatial and color information from the final output texture without requiring the LLM to interpret raw pixels.

```cpp
struct FrameAnalysis {
    float meanBrightness;
    float contrast;                    // std dev of luminance
    glm::vec3 dominantColor;
    float dominantHue;
    float saturationAvg;
    std::array<int, 8> histogram;      // 8-bucket luminance histogram

    // 3x3 grid of average brightness for spatial understanding
    // e.g., "top-left is dark, center is bright"
    std::array<float, 9> regionBrightness;

    std::string toJSON() const;
};
```

This requires a GPU readback of the output texture, then CPU-side analysis. Cost is minimal for a single frame.

### 1.3 Chain-Level Inspection

The Chain itself aggregates inspection data from all operators plus the output analysis.

```cpp
struct ChainInspection {
    int frame;
    float time;
    std::vector<std::pair<std::string, InspectData>> operators;
    FrameAnalysis outputAnalysis;

    std::string toJSON() const;
};

ChainInspection Chain::inspectAll() const {
    ChainInspection result;
    result.frame = currentFrame_;
    result.time = currentTime_;
    for (auto& [name, op] : operators_) {
        result.operators.push_back({name, op->inspect()});
    }
    if (outputTexture_) {
        result.outputAnalysis = analyzeTexture(outputTexture_);
    }
    return result;
}
```

### 1.4 CLI: `vivid inspect`

Captures multiple sample frames across a time range and outputs structured data plus thumbnail images.

```bash
vivid inspect path/to/project --duration 2.0 --samples 5 --out report/
```

Produces:

```
report/
├── inspection.json       # All introspection data for all sampled frames
├── frame_000.png         # Thumbnail at t=0.0
├── frame_001.png         # Thumbnail at t=0.5
├── frame_002.png         # Thumbnail at t=1.0
├── frame_003.png         # Thumbnail at t=1.5
├── frame_004.png         # Thumbnail at t=2.0
└── waveform.png          # Audio waveform overview (if audio chain present)
```

Example `inspection.json` structure:

```json
{
  "project": "my-project",
  "samples": [
    {
      "frame": 0,
      "time": 0.0,
      "thumbnail": "frame_000.png",
      "operators": {
        "noise": { "scale": 4.0, "speed": 0.5, "octaves": 4, "type": "perlin" },
        "feedback": { "decay": 0.95, "energy": 0.0, "pixel_change_pct": 100.0 },
        "audioOut": { "rms": 0.0, "peak": 0.0 }
      },
      "output": {
        "meanBrightness": 0.48,
        "contrast": 0.22,
        "dominantHue": 0.62,
        "saturationAvg": 0.74,
        "histogram": [12, 45, 89, 120, 95, 40, 8, 3],
        "regionBrightness": [0.3, 0.4, 0.3, 0.5, 0.7, 0.5, 0.3, 0.4, 0.3]
      }
    }
  ]
}
```

### 1.5 Assertion System

A `vivid-assertions.yaml` file in the project directory defines pass/fail conditions the LLM (or CI) can evaluate automatically.

```yaml
assertions:
  - name: feedback-alive
    operator: feedback
    metric: energy
    check: "> 0.3"
    description: "Feedback should not fade to black"

  - name: good-contrast
    output: true
    metric: contrast
    check: "> 0.15"
    description: "Output should not be flat or washed out"

  - name: audio-present
    operator: audioOut
    metric: rms
    check: "> 0.05"
    after_frame: 30
    description: "Audio should be audible after first half-second"

  - name: color-in-range
    output: true
    metric: dominantHue
    check: "0.5..0.8"
    description: "Should stay in blue-purple range"

  - name: no-clipping
    operator: audioOut
    metric: clipping
    check: "== 0"
    description: "Audio should not clip"
```

CLI:

```bash
vivid check path/to/project --duration 2.0
# Exit code 0 = all assertions pass
# Exit code 1 = one or more failures
# Stdout: JSON report of pass/fail with details
```

### 1.6 Implementation Priority

1. `InspectData` struct and base `inspect()` method on Operator
2. Override `inspect()` on all existing operators (can be incremental — start with the most common ones)
3. `FrameAnalysis` with GPU readback + CPU analysis
4. `Chain::inspectAll()` aggregation
5. `vivid inspect` CLI command (depends on existing headless rendering)
6. Assertion parser and `vivid check` CLI command
7. Audio waveform rendering for the report

---

## Phase 2: Puppeteered Playback & A/V Export

### 2.1 Event Injection System

A headless runtime needs to simulate everything that normally comes from a live user: keyboard/mouse input, MIDI events, parameter changes, and time progression. An `EventInjector` reads a timeline script and feeds synthetic events into the Context.

```cpp
class EventInjector {
public:
    void loadScript(const std::string& path);

    // Called each frame by the headless runtime
    void update(float time, Context& ctx) {
        while (nextEvent_ && nextEvent_->time <= time) {
            applyEvent(*nextEvent_, ctx);
            nextEvent_ = advance();
        }
    }

private:
    void applyEvent(const ScriptEvent& event, Context& ctx);
};
```

### 2.2 Playback Script Format

A YAML-based automation format that covers the common interaction types.

```yaml
# playback-script.yaml
duration: 30.0
resolution: [1920, 1080]
fps: 60

events:
  - time: 0.0
    action: key_press
    key: SPACE

  - time: 2.0
    action: param_set
    operator: noise
    param: scale
    value: 8.0

  - time: 5.0
    action: midi_note
    note: 60
    velocity: 100
    channel: 0

  - time: 5.0
    action: key_press
    key: "1"

  - time: 10.0
    action: param_ramp
    operator: feedback
    param: decay
    from: 0.95
    to: 0.8
    duration: 5.0

  - time: 15.0
    action: mouse_move
    x: 960
    y: 540

  - time: 20.0
    action: midi_cc
    controller: 1
    value: 127
    channel: 0
```

Supported event types:

| Action | Description |
|--------|-------------|
| `key_press` / `key_release` | Simulate keyboard input |
| `mouse_move` / `mouse_click` | Simulate mouse position and clicks |
| `param_set` | Set an operator parameter to a value instantly |
| `param_ramp` | Smoothly interpolate a parameter over a duration |
| `midi_note` | Send a MIDI note on event |
| `midi_cc` | Send a MIDI CC message |
| `trigger` | Call `.trigger()` on an operator (e.g., Flash, Kick) |

### 2.3 Headless A/V Render Pipeline

The headless runtime processes the chain frame-by-frame, captures video frames and audio buffers, and encodes them to files.

```cpp
// Pseudocode
HeadlessRenderer renderer(width, height);  // wgpu offscreen
EventInjector injector("playback-script.yaml");
VideoEncoder encoder("output.mp4", width, height, fps);  // ffmpeg-based
AudioEncoder audioEncoder("output.wav", sampleRate);

float dt = 1.0f / fps;
for (float t = 0; t < duration; t += dt) {
    injector.update(t, ctx);
    chain.process(ctx);

    auto pixels = renderer.readPixels();
    encoder.writeFrame(pixels);

    auto samples = chain.audioBuffer();
    audioEncoder.writeSamples(samples);
}

encoder.finalize();
audioEncoder.finalize();
// Optionally mux video + audio into final container via ffmpeg
```

### 2.4 CLI: `vivid export`

```bash
# Full A/V export with puppeteered input
vivid export path/to/project \
    --script playback-script.yaml \
    --duration 30 \
    --resolution 1920x1080 \
    --fps 60 \
    --output preview.mp4

# Quick visual-only preview (lower res, shorter)
vivid export path/to/project \
    --duration 5 \
    --resolution 960x540 \
    --fps 30 \
    --output quick-preview.mp4

# Audio-only export
vivid export path/to/project \
    --duration 10 \
    --audio-only \
    --output preview.wav
```

### 2.5 LLM Script Generation

The playback script format is intentionally simple enough that an LLM can generate it. Given a project manifest with known parameters and a user request like "show me 10 seconds where the noise scale ramps up while the kick pattern plays," the LLM generates the YAML, runs the export, and sends the result.

### 2.6 Implementation Priority

1. `EventInjector` class with support for `param_set`, `key_press`, and `trigger` events
2. YAML script parser
3. Headless render loop with frame capture (extend existing headless support)
4. Video encoding via ffmpeg (pipe raw frames to ffmpeg subprocess)
5. Audio buffer capture and WAV export
6. Audio/video muxing
7. `vivid export` CLI command
8. Add `param_ramp`, `midi_note`, `midi_cc`, `mouse_move` event types
9. Script validation (warn on events targeting nonexistent operators/params)

---

## Phase 2.5 (Future): Live Streaming

Instead of export-then-review, stream the headless output in real-time via WebRTC or RTMP so the human can watch live and provide feedback while the chain runs. This is a significantly larger lift but would enable a true collaborative loop where the LLM adjusts parameters in real-time based on voice/text feedback.

Not a priority until Phases 1 and 2 are solid.

---

## How the Phases Compose in an Agent Loop

```
┌─────────────────────────────────────────────────────┐
│  INNER LOOP (Phase 1) — seconds, no human needed    │
│                                                     │
│  LLM edits chain.cpp                                │
│       ↓                                             │
│  vivid inspect → structured JSON + thumbnails       │
│       ↓                                             │
│  vivid check → assertions pass?                     │
│       ↓ no: iterate        ↓ yes: proceed           │
│  ← back to edit       move to outer loop            │
└─────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────┐
│  OUTER LOOP (Phase 2) — minutes, human reviews      │
│                                                     │
│  LLM generates playback script                      │
│       ↓                                             │
│  vivid export → video/audio file                    │
│       ↓                                             │
│  Agent sends to human (Telegram, Slack, etc.)       │
│       ↓                                             │
│  Human: "transition at 10s is too abrupt"           │
│       ↓                                             │
│  Back to inner loop with new constraints            │
└─────────────────────────────────────────────────────┘
```

---

## Project Manifest: `vivid-project.json`

Supports both phases by giving the LLM a machine-readable description of what the chain does and what's tweakable.

```json
{
  "name": "cult-visuals-act-1",
  "description": "Dark ritual opening sequence with building drum pattern",
  "chain": "chain.cpp",
  "preview": {
    "resolution": [1920, 1080],
    "duration": 10.0,
    "samples": 5
  },
  "parameters": {
    "noise.scale": {
      "range": [1.0, 20.0],
      "default": 4.0,
      "description": "Texture detail level"
    },
    "feedback.decay": {
      "range": [0.8, 0.99],
      "default": 0.95,
      "description": "Trail persistence — higher = longer trails"
    },
    "clock.bpm": {
      "range": [60, 180],
      "default": 120,
      "description": "Tempo"
    }
  },
  "assertions": "vivid-assertions.yaml"
}
```

---

## Cloud Environment Requirements

For either phase to work in a cloud/agent context:

- **Docker image** with Vivid build toolchain (CMake, C++17 compiler, wgpu-native)
- **GPU access** for real rendering (cloud GPU instance) or **Mesa/llvmpipe** for software fallback during fast iteration
- **ffmpeg** installed for video encoding (Phase 2)
- **Headless wgpu** configured for offscreen rendering (no display server required)
- Reproducible build documented in a `Dockerfile`
