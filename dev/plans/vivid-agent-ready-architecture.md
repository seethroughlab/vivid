# Vivid: Agent-Ready Architecture Plan

> **Status (February 2026):** This plan is **largely implemented**. Phases 1–3 are complete or nearly complete. The CLI tools (`build`, `inspect`, `check`, `export`, `params`, `graph`, `docs`), the MCP server, the introspection system, event injection, A/V export, and the performer UI are all shipping. Phase 4 (live streaming) remains deferred. See the [Implementation Roadmap](#part-6-implementation-roadmap) for per-item status, [Deviations](#deviations-from-original-plan) for where implementation diverged from the plan, and [Additional Items Built](#additional-items-built-not-in-original-plan) for work that went beyond the plan's scope.

## Vision

A developer messages their agent from their phone: "Add a bloom pass after the feedback loop, keep it subtle." The agent edits `chain.cpp`, rebuilds, runs the chain headless for a few seconds, reads structured introspection data to verify the bloom isn't blowing out the contrast, checks that all visual assertions still pass, iterates twice to dial in the intensity, renders a 10-second demo video with audio, and sends it to the developer on Telegram. The developer watches it over coffee, replies "perfect, now make the kick hit harder," and the cycle continues.

Vivid doesn't need to be the agent. It doesn't need to manage messaging, conversations, or AI models. It needs to be an exceptionally good tool that an agent can wield — one that provides structured feedback about its own output, produces reviewable media on demand, and exposes a clean CLI interface that any agent framework can call.

This document covers everything needed to get there: what Vivid must build internally, what it should stop building, what it should expose to external tools, and how the pieces compose into a complete agent-driven development workflow.

---

## Part 1: The Agent Workflow

### What the Agent Does (Not Vivid's Responsibility)

An agent framework like OpenClaw handles the outer shell of this workflow. This includes conversation management over messaging platforms (Telegram, Slack, WhatsApp), deciding when to act on user feedback, managing the LLM context and memory, sending files and media to the user, orchestrating multi-step tool use, and maintaining session state across interactions.

Vivid is not involved in any of this. Vivid is a CLI tool the agent calls, the same way it might call `git`, `ffmpeg`, or `cmake`.

### What Vivid Must Provide

Vivid's responsibility is to be an excellent CLI citizen that exposes four capabilities:

1. **Build** — Compile a chain project and report success/failure with structured errors.
2. **Inspect** — Run a chain headless, capture structured introspection data and frame thumbnails so the LLM can evaluate visual and audio output without human involvement.
3. **Check** — Validate introspection data against author-defined assertions and report pass/fail.
4. **Export** — Render a chain to video/audio files with simulated interactivity, producing media the agent can send to the human for review.

Everything else — the conversation, the decision-making, the file delivery — happens in the agent layer.

### The Two Loops

The agent operates in two nested loops. Vivid serves both, but in different ways.

**Inner Loop: LLM Self-Evaluation (seconds, no human)**

The agent edits code, builds, and evaluates the result using structured data. This loop runs many times silently. The LLM never needs to "see" the output as pixels — it reasons about JSON metrics, histograms, spatial brightness maps, and audio levels. This is fast and cheap.

```
Agent edits chain.cpp
    ↓
vivid build path/to/project
    ↓ (success?)
vivid inspect path/to/project --duration 2 --samples 5 --out /tmp/report
    ↓
Agent reads /tmp/report/inspection.json
Agent looks at /tmp/report/frame_002.png (optional, for vision models)
    ↓
vivid check path/to/project --duration 2
    ↓ (exit code 0?)
All assertions pass → ready for human review
    ↓ (exit code 1?)
Read failure details → iterate
```

**Outer Loop: Human Review (minutes, requires human)**

When the LLM is satisfied with the inner loop, it produces an actual video/audio export and sends it to the human. The human watches, listens, and provides subjective feedback. This loop runs infrequently — maybe once for every 5-20 inner loop iterations.

```
Agent generates a playback script (YAML) or uses a default
    ↓
vivid export path/to/project --duration 15 --output /tmp/preview.mp4
    ↓
Agent sends /tmp/preview.mp4 to user via Telegram/Slack
    ↓
User: "the feedback trails are too long, and the snare needs reverb"
    ↓
Agent interprets feedback → back to inner loop
```

The key insight: the inner loop is where Vivid does heavy lifting with introspection and assertions. The outer loop is where Vivid does heavy lifting with headless rendering and A/V export. The agent framework handles everything in between.

---

## Part 2: What Vivid Builds

### 2.1 Operator Introspection API

Every operator gets an `inspect()` method that reports its meaningful internal state as structured data. This is the foundation of the LLM's ability to evaluate visual and audio output without seeing pixels.

```cpp
struct InspectData {
    std::unordered_map<std::string, float> metrics;
    std::unordered_map<std::string, std::string> metadata;

    void set(const std::string& key, float value);
    void set(const std::string& key, const std::string& value);
    std::string toJSON() const;
};

class Operator {
public:
    virtual void init(Context& ctx) = 0;
    virtual void process(Context& ctx) = 0;

    virtual InspectData inspect() const {
        InspectData data;
        data.set("enabled", enabled ? 1.0f : 0.0f);
        return data;
    }
};
```

Each operator overrides `inspect()` with domain-specific metrics. These are not arbitrary — they should represent the information an experienced creative coder would want to know about the operator's behavior without looking at the screen.

**Visual operator examples:**

```cpp
InspectData Noise::inspect() const {
    auto data = Operator::inspect();
    data.set("scale", scale);
    data.set("speed", speed);
    data.set("octaves", static_cast<float>(octaves));
    data.set("type", noiseTypeName(type));  // "perlin", "simplex", etc.
    return data;
}

InspectData Feedback::inspect() const {
    auto data = Operator::inspect();
    data.set("decay", decay);
    data.set("energy", computeEnergy());        // avg pixel luminance of output
    data.set("pixel_change_pct", frameDelta());  // % pixels changed frame-to-frame
    data.set("frames_alive", framesAlive_);
    return data;
}

InspectData Bloom::inspect() const {
    auto data = Operator::inspect();
    data.set("threshold", threshold);
    data.set("intensity", intensity);
    data.set("radius", radius);
    data.set("bright_pixel_pct", brightPixelPercentage()); // % pixels above threshold
    return data;
}
```

**Audio operator examples:**

```cpp
InspectData BandSplit::inspect() const {
    auto data = Operator::inspect();
    data.set("bass", bass());
    data.set("mid", mid());
    data.set("high", high());
    return data;
}

InspectData Levels::inspect() const {
    auto data = Operator::inspect();
    data.set("rms", rms());
    data.set("peak", peak());
    data.set("clipping", isClipping() ? 1.0f : 0.0f);
    return data;
}

InspectData Kick::inspect() const {
    auto data = Operator::inspect();
    data.set("frequency", frequency);
    data.set("decay", decay);
    data.set("drive", drive);
    data.set("is_playing", isPlaying() ? 1.0f : 0.0f);
    return data;
}
```

The guiding principle: if an LLM asked "what is this operator doing right now?", the `inspect()` output should answer that question completely, in numbers and labels, without needing a screenshot or audio clip.

### 2.2 Output Frame Analysis

Beyond per-operator introspection, Vivid analyzes the final composited output texture and provides a structured summary. This gives the LLM spatial and color information that individual operator metrics can't capture — like "the entire output is too dark" or "there's a bright hotspot in the upper right."

```cpp
struct FrameAnalysis {
    float meanBrightness;
    float contrast;                    // std dev of luminance
    glm::vec3 dominantColor;
    float dominantHue;
    float saturationAvg;
    std::array<int, 8> histogram;      // 8-bucket luminance histogram

    // 3x3 grid of average brightness
    // Lets the LLM reason spatially: "top-left is dark, center is bright"
    std::array<float, 9> regionBrightness;

    std::string toJSON() const;
};
```

This is computed via GPU readback of the output texture, then CPU-side analysis. The cost is trivial for a single frame.

### 2.3 Chain-Level Aggregation

The Chain object provides a single call that collects introspection from all operators plus the output analysis into one JSON-serializable structure.

```cpp
struct ChainInspection {
    int frame;
    float time;
    std::vector<std::pair<std::string, InspectData>> operators;
    FrameAnalysis outputAnalysis;
    std::string toJSON() const;
};

ChainInspection Chain::inspectAll() const;
```

### 2.4 Assertion System

A `vivid-assertions.yaml` file in the project directory defines machine-checkable conditions. The LLM (or CI, or a human running tests) can verify that the chain meets its creative intent without subjective judgment.

```yaml
assertions:
  - name: feedback-alive
    operator: feedback
    metric: energy
    check: "> 0.3"
    description: "Feedback trails should remain visible, not fade to black"

  - name: good-contrast
    output: true
    metric: contrast
    check: "> 0.15"
    description: "Output should have clear visual contrast"

  - name: color-in-range
    output: true
    metric: dominantHue
    check: "0.5..0.8"
    description: "Color palette should stay in the blue-purple range"

  - name: audio-present
    operator: levels
    metric: rms
    check: "> 0.05"
    after_frame: 30
    description: "Audio should be audible after the first half-second"

  - name: no-clipping
    operator: levels
    metric: clipping
    check: "== 0"
    description: "Audio output should not clip"

  - name: kick-punch
    operator: bands
    metric: bass
    check: "> 0.4"
    when_operator: kick
    when_metric: is_playing
    when_check: "== 1"
    description: "Bass energy should be strong when the kick is active"
```

Assertions are meant to capture the author's *intent* in a way the LLM can verify. They answer: "Is this chain still doing what it's supposed to do after I changed something?" The LLM can also propose new assertions based on user feedback — if the user says "the trails should never disappear," the LLM adds a `feedback-alive` assertion and uses it going forward.

### 2.5 Event Injection for Puppeteered Playback

Interactive chains respond to keyboard, mouse, and MIDI input. To export a meaningful demo of an interactive chain, Vivid needs to simulate that input from a script. An `EventInjector` reads a timeline and feeds synthetic events into the Context during headless rendering.

```cpp
class EventInjector {
public:
    void loadScript(const std::string& path);
    void update(float time, Context& ctx);
};
```

The script format is YAML, intentionally simple enough for an LLM to generate:

```yaml
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

  - time: 10.0
    action: param_ramp
    operator: feedback
    param: decay
    from: 0.95
    to: 0.8
    duration: 5.0

  - time: 15.0
    action: trigger
    operator: flash

  - time: 20.0
    action: snapshot_recall
    name: "dark-mode"
    interpolate: 2.0
```

Supported event types:

| Action | Description |
|--------|-------------|
| `key_press` / `key_release` | Simulate keyboard input |
| `mouse_move` / `mouse_click` | Simulate mouse position and clicks |
| `param_set` | Set an operator parameter instantly |
| `param_ramp` | Interpolate a parameter over a duration |
| `midi_note` | Send a MIDI note-on event |
| `midi_cc` | Send a MIDI CC message |
| `trigger` | Call `.trigger()` on an operator |
| `snapshot_recall` | Recall a saved parameter snapshot, optionally with interpolation |

The LLM generates these scripts based on what it wants to demonstrate. If the user says "show me the transition between the intro and the drop," the LLM writes a script that simulates the relevant triggers and parameter changes, exports the video, and sends it.

### 2.6 Headless A/V Export Pipeline

The headless renderer processes the chain frame-by-frame with deterministic timing, captures video frames and audio buffers, and encodes them to files. This is the engine behind the `vivid export` command.

```cpp
// Pseudocode
HeadlessRenderer renderer(width, height);
EventInjector injector(scriptPath);
VideoEncoder encoder(outputPath, width, height, fps);
AudioEncoder audioEncoder(audioPath, sampleRate);

float dt = 1.0f / fps;
for (float t = 0; t < duration; t += dt) {
    injector.update(t, ctx);
    chain.process(ctx);

    encoder.writeFrame(renderer.readPixels());
    audioEncoder.writeSamples(chain.audioBuffer());
}

encoder.finalize();
audioEncoder.finalize();
// Mux via ffmpeg into final container
```

Video encoding should pipe raw frames to an ffmpeg subprocess — this avoids linking against codec libraries directly and leverages ffmpeg's format support. The system requires ffmpeg to be available on the machine (trivial in Docker/cloud environments).

### 2.7 Project Manifest

A `vivid-project.json` file gives the LLM a machine-readable map of the project: what it does, what parameters are available, what ranges are safe, and where to find the assertion definitions.

```json
{
  "name": "cult-visuals-act-1",
  "description": "Dark ritual opening sequence with building drum pattern and feedback visuals",
  "chain": "chain.cpp",
  "preview": {
    "resolution": [1920, 1080],
    "duration": 10.0,
    "fps": 60,
    "samples": 5
  },
  "parameters": {
    "noise.scale": {
      "type": "float",
      "range": [1.0, 20.0],
      "default": 4.0,
      "description": "Texture detail level — higher values = finer grain"
    },
    "feedback.decay": {
      "type": "float",
      "range": [0.8, 0.99],
      "default": 0.95,
      "description": "Trail persistence — higher = longer trails"
    },
    "clock.bpm": {
      "type": "float",
      "range": [60.0, 180.0],
      "default": 120.0,
      "description": "Drum machine tempo"
    },
    "bloom.intensity": {
      "type": "float",
      "range": [0.0, 2.0],
      "default": 0.5,
      "description": "Glow strength on bright areas"
    }
  },
  "snapshots": {
    "intro": "snapshots/intro.json",
    "drop": "snapshots/drop.json",
    "dark-mode": "snapshots/dark-mode.json"
  },
  "assertions": "vivid-assertions.yaml",
  "demo_script": "demo.yaml"
}
```

The LLM reads this before touching anything. It knows what knobs exist, what values are safe, and what "correct" looks like before writing a single line of code.

---

## Part 3: The CLI Interface

These are the commands an agent calls. Every command is designed for non-interactive use: structured output on stdout, meaningful exit codes, no prompts.

### `vivid build`

Compile the chain project. Report success or structured error output.

```bash
vivid build path/to/project
# Exit 0: success
# Exit 1: build failure
# Stdout (on failure): JSON with error messages, file, line numbers
```

```json
{
  "success": false,
  "errors": [
    {
      "file": "chain.cpp",
      "line": 42,
      "message": "use of undeclared identifier 'Bloon'; did you mean 'Bloom'?"
    }
  ]
}
```

The LLM reads this, fixes the typo, and rebuilds. No human needed.

### `vivid inspect`

Run the chain headless, capture introspection data and frame thumbnails across a time range.

```bash
vivid inspect path/to/project \
    --duration 2.0 \
    --samples 5 \
    --resolution 960x540 \
    --out /tmp/report/
```

Produces:

```
/tmp/report/
├── inspection.json
├── frame_000.png
├── frame_001.png
├── frame_002.png
├── frame_003.png
├── frame_004.png
└── waveform.png        (if audio chain)
```

The `inspection.json` contains per-sample introspection data for every operator plus the output analysis. This is the primary data the LLM uses for self-evaluation. The thumbnails are secondary — useful for vision-capable models but not required.

```json
{
  "project": "cult-visuals-act-1",
  "duration": 2.0,
  "samples": [
    {
      "frame": 0,
      "time": 0.0,
      "thumbnail": "frame_000.png",
      "operators": {
        "noise": { "scale": 4.0, "speed": 0.5, "octaves": 4, "type": "perlin" },
        "feedback": { "decay": 0.95, "energy": 0.72, "pixel_change_pct": 18.3 },
        "bloom": { "threshold": 0.6, "intensity": 0.5, "bright_pixel_pct": 12.1 },
        "levels": { "rms": 0.31, "peak": 0.67, "clipping": 0 },
        "bands": { "bass": 0.8, "mid": 0.3, "high": 0.1 }
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

### `vivid check`

Run assertions against a live chain and report pass/fail.

```bash
vivid check path/to/project --duration 2.0
# Exit 0: all pass
# Exit 1: one or more failures
```

```json
{
  "pass": false,
  "assertions": [
    { "name": "feedback-alive", "pass": true, "value": 0.72, "check": "> 0.3" },
    { "name": "good-contrast", "pass": true, "value": 0.22, "check": "> 0.15" },
    { "name": "color-in-range", "pass": false, "value": 0.42, "check": "0.5..0.8",
      "message": "dominantHue 0.42 is outside expected range 0.5..0.8" }
  ]
}
```

The LLM reads this, understands the hue drifted out of range, adjusts the HSV operator, and re-checks.

### `vivid export`

Render a complete video/audio file, optionally with a puppeteer script for simulated interactivity.

```bash
# Full A/V export with puppeteered events
vivid export path/to/project \
    --script demo.yaml \
    --duration 30 \
    --resolution 1920x1080 \
    --fps 60 \
    --output /tmp/preview.mp4

# Quick preview (agent uses this for faster iteration)
vivid export path/to/project \
    --duration 5 \
    --resolution 960x540 \
    --fps 30 \
    --output /tmp/quick.mp4

# Audio-only
vivid export path/to/project \
    --duration 10 \
    --audio-only \
    --output /tmp/preview.wav
```

If no `--script` is provided, the chain runs with no input events (suitable for non-interactive generative chains). If the project manifest specifies a `demo_script`, the agent can reference it by default.

The output file is what the agent sends to the user via their messaging platform. This is the human review artifact.

### `vivid params`

List all tweakable parameters in the current chain, their types, current values, and ranges (if declared in the manifest). This lets the agent discover what it can change without parsing C++.

```bash
vivid params path/to/project
```

```json
{
  "parameters": [
    { "operator": "noise", "param": "scale", "type": "float", "value": 4.0,
      "range": [1.0, 20.0], "description": "Texture detail level" },
    { "operator": "noise", "param": "speed", "type": "float", "value": 0.5 },
    { "operator": "feedback", "param": "decay", "type": "float", "value": 0.95,
      "range": [0.8, 0.99], "description": "Trail persistence" },
    { "operator": "bloom", "param": "intensity", "type": "float", "value": 0.5,
      "range": [0.0, 2.0] },
    { "operator": "clock", "param": "bpm", "type": "float", "value": 120.0,
      "range": [60.0, 180.0] }
  ]
}
```

### `vivid graph`

Dump the operator chain topology as JSON. The agent uses this to understand the signal flow before making changes.

```bash
vivid graph path/to/project
```

```json
{
  "operators": [
    { "name": "noise", "type": "Noise", "inputs": [] },
    { "name": "feedback", "type": "Feedback", "inputs": ["composite"] },
    { "name": "bloom", "type": "Bloom", "inputs": ["feedback"] },
    { "name": "hsv", "type": "HSV", "inputs": ["bloom"] },
    { "name": "composite", "type": "Composite", "inputs": ["noise", "hsv"] },
    { "name": "clock", "type": "Clock", "inputs": [] },
    { "name": "kickSeq", "type": "Sequencer", "inputs": [] },
    { "name": "kick", "type": "Kick", "inputs": [] },
    { "name": "levels", "type": "Levels", "inputs": ["kick"] },
    { "name": "bands", "type": "BandSplit", "inputs": ["kick"] },
    { "name": "audioOut", "type": "AudioOutput", "inputs": ["kick"] }
  ],
  "visualOutput": "composite",
  "audioOutput": "audioOut"
}
```

### `vivid docs`

Search documentation, recipes, and code examples from the CLI. No running instance needed.

```bash
# Search all docs for a topic
vivid docs search "bloom"

# List all available recipes
vivid docs recipe

# Get a specific recipe by name
vivid docs recipe feedback-loop

# Find code examples using a specific operator
vivid docs example Noise
```

All output is JSON, suitable for agent consumption.

---

## Part 3.5: CLI vs MCP — Two Interface Modes

Vivid exposes two complementary interface modes for agent integration.

### CLI (Batch/Headless)

Universal, stateless commands that any agent framework or CI pipeline can call. Fire-and-forget with structured JSON output on stdout.

| Command | Purpose |
|---------|---------|
| `vivid build <project>` | Compile chain, report structured errors |
| `vivid inspect <project>` | Run chain, dump introspection JSON |
| `vivid check <project>` | Run assertions, exit 0/1 |
| `vivid export <project> -o out.mp4` | Headless A/V export |
| `vivid params <project>` | List all tweakable parameters |
| `vivid graph <project>` | Dump chain topology |
| `vivid docs search <query>` | Search documentation |
| `vivid docs recipe [name]` | List or show recipes |
| `vivid docs example <operator>` | Find code examples |

The CLI is the **primary agent interface**. Any agent framework (OpenClaw, LangChain, custom scripts) can call these commands. No persistent connection required.

### MCP (Live/Interactive)

Persistent bidirectional connection to a running Vivid instance via Claude Code's MCP protocol. Enables real-time parameter control, frame capture, hot-reload monitoring, and slider sync.

| Tool | Purpose |
|------|---------|
| `set_param` | Adjust parameter values in real-time |
| `capture_frame` | Capture current frame to PNG |
| `get_pending_changes` | See slider changes from user |
| `get_runtime_status` | Check compile status after hot-reload |
| `orbit_camera` | Reposition camera for 3D scenes |
| `advance_frames` | Progress animation forward |

MCP adds **real-time interactivity on top of the CLI**. It requires `vivid mcp` running as a Claude Code MCP server. The live-instance tools (`set_param`, `capture_frame`, `get_pending_changes`, etc.) have no CLI equivalent because they require a persistent connection to a running Vivid instance.

### When to Use Which

- **Agent running headless (CI, remote, any framework):** CLI only. The complete inner loop (`build` → `inspect` → `check`) and outer loop (`export`) work without MCP.
- **Developer with Claude Code:** CLI + MCP. Claude Code uses MCP tools for real-time interaction and CLI commands for batch operations.
- **Documentation and discovery:** Both. `vivid docs` CLI commands and MCP's `search_docs`/`get_recipe`/`get_example` tools share the same underlying search engine.

---

## Part 4: Example Agent Session

Here is a concrete walkthrough of what an OpenClaw-style agent session looks like, showing exactly which Vivid commands are called and when. The agent framework handles the messaging and decision-making; Vivid handles the build, inspection, and export.

### The User Starts a Conversation

> **User (via Telegram):** "Hey, I want to add a bloom effect to the cult visuals project. Keep it subtle — just a hint of glow on the bright feedback trails."

### Agent: Understand the Project

The agent reads the project files to understand the current state.

```bash
cat path/to/project/vivid-project.json     # Read manifest
vivid graph path/to/project                 # Understand chain topology
vivid params path/to/project                # See current parameter values
cat path/to/project/chain.cpp               # Read the source
```

The agent now knows the chain is: noise → feedback → hsv → composite, with audio via clock → sequencer → kick. There's no bloom operator yet.

### Agent: Make the Change (Inner Loop, Iteration 1)

The agent edits `chain.cpp` to add a Bloom operator between feedback and hsv, sets intensity to 0.3 (subtle), and connects it.

```bash
# Agent edits chain.cpp via filesystem
vivid build path/to/project
```

Build succeeds (exit 0). If it had failed, the agent reads the error JSON and fixes the code.

### Agent: Evaluate (Inner Loop, Iteration 1)

```bash
vivid inspect path/to/project --duration 2 --samples 5 --out /tmp/report
```

Agent reads `/tmp/report/inspection.json`. It sees:

```json
"bloom": { "threshold": 0.6, "intensity": 0.3, "bright_pixel_pct": 8.2 }
```

8.2% of pixels are above the bloom threshold. The output contrast is 0.19 (was 0.22 before bloom). The agent judges: bloom is active but not overwhelming. Contrast dropped slightly, which is expected.

```bash
vivid check path/to/project --duration 2
```

All assertions pass. The `good-contrast` assertion still holds (0.19 > 0.15).

The agent could also look at the thumbnail images if it's a vision-capable model, but the structured data alone is enough to confirm the bloom is working and subtle.

### Agent: Check Temporal Behavior (Inner Loop, Iteration 2)

The agent notices the inspection only shows one moment in time. It wants to verify the bloom doesn't accumulate over time via the feedback loop.

```bash
vivid inspect path/to/project --duration 5 --samples 10 --out /tmp/report2
```

It reads the 10 samples and checks that `bloom.bright_pixel_pct` stays in the 6-12% range across all frames rather than growing unboundedly. It does. Good.

### Agent: Produce Human Review (Outer Loop)

The agent generates a playback script to show the bloom in context with the drum pattern:

```yaml
duration: 15.0
resolution: 1920x1080
fps: 60
events:
  - time: 0.0
    action: key_press
    key: SPACE
  - time: 8.0
    action: param_ramp
    operator: bloom
    param: intensity
    from: 0.3
    to: 0.8
    duration: 3.0
  - time: 13.0
    action: param_ramp
    operator: bloom
    param: intensity
    from: 0.8
    to: 0.3
    duration: 2.0
```

This script starts playback, ramps bloom up to show the range, then brings it back down.

```bash
vivid export path/to/project \
    --script /tmp/demo-bloom.yaml \
    --duration 15 \
    --resolution 1920x1080 \
    --fps 60 \
    --output /tmp/bloom-preview.mp4
```

The agent sends `/tmp/bloom-preview.mp4` to the user on Telegram with the message: "Added a subtle bloom on the feedback trails. I ramped it up briefly at 0:08 so you can see the range. Here's a 15-second preview."

### The User Responds

> **User:** "Nice! But the kick could hit harder. Can you beef up the low end?"

The agent goes back to the inner loop: edits the Kick operator's frequency and drive parameters, runs `vivid inspect`, checks that bass energy increased via BandSplit metrics, verifies no clipping via Levels, runs `vivid check`, and when satisfied, exports a new preview video and sends it.

---

## Part 5: Dev Tools Philosophy

### The Principle

Vivid's built-in UI serves the performer and the live debugger. Development happens outside Vivid, in whatever editor or agent the developer uses.

The agent doesn't need an IDE inside Vivid. It needs `chain.cpp` on the filesystem and CLI commands it can call. A human developer doesn't need one either — they have VS Code, Cursor, or Claude Code. What neither of them has is a real-time chain visualizer with live GPU thumbnails, an interactive parameter surface for on-stage tweaking, or live audio metering across an operator graph. That's what Vivid's UI should focus on.

### Remove

| Feature | Reason |
|---------|--------|
| Integrated code editor | External editors and agents are better at this. Hot-reload watches the filesystem regardless of what edits the file. |
| Integrated terminal | General-purpose tool that doesn't belong inside Vivid. The user already has a terminal. |
| File management UI | File browsers and project trees belong to the external dev environment. |

### Keep

| Feature | Reason |
|---------|--------|
| Chain visualizer (ImGui/ImNodes) | Real-time node graph with live GPU thumbnails. No external tool can replicate this. Essential for performance and debugging. |
| Performance overlay | FPS, frame time, resolution, GPU memory. Essential for live monitoring. |
| Keyboard shortcuts | `Tab`, `F`, `V`, `Esc` — the performer's control interface. |

### Enhance

**Parameter control surface.** Evolve from display-only values on nodes to an interactive panel with sliders, knobs, and XY pads. MIDI-mappable. Only shows parameters the chain author has marked as performable. Grouped by purpose (Color, Motion, Audio), not by operator. Large, high-contrast elements for dark rooms.

**Chain health monitoring.** Live visual indicators on each node powered by the same `inspect()` data that feeds the CLI. Thumbnail border colors (green/yellow/red for signal health), inline VU meters on audio nodes, frame-delta sparklines showing whether output is changing or stalled. This is `inspect()` rendered for humans instead of serialized for machines.

**Preset/snapshot system.** Save and recall parameter states by name. Instant recall or interpolated crossfade. Keyboard and MIDI triggerable. Organizable into setlists. This also feeds the `snapshot_recall` event type in puppeteer scripts — same system, two interfaces.

**Output recording.** A live "record" toggle that captures video and audio to disk during a performance, without interrupting it. Shares encoding infrastructure with the headless `vivid export` pipeline. Piping frames to ffmpeg in a background thread.

**Console/log overlay.** A read-only HUD (not an interactive terminal) showing hot-reload status, warnings, assertion results, and operator errors. Toggled with a hotkey.

### Shared Infrastructure

The key architectural insight is that the performer UI and the agent CLI share the same underlying systems:

| System | Performer UI | Agent CLI |
|--------|-------------|-----------|
| `inspect()` data | Chain health indicators, VU meters, sparklines | `vivid inspect` JSON output |
| Parameter system | On-screen sliders, MIDI mapping | `vivid params`, `param_set`/`param_ramp` in scripts |
| Snapshot system | Live preset recall via keyboard/MIDI | `snapshot_recall` events in playback scripts |
| Recording/encoding | Live output recording toggle | `vivid export` headless pipeline |
| Assertion checking | Log overlay warnings | `vivid check` pass/fail reporting |

Building for one audience directly builds for the other. There is no wasted work.

---

## Part 6: Implementation Roadmap

### Phase 1: Introspection & Self-Evaluation

This phase enables the inner loop — the LLM can build, inspect, and validate autonomously.

| Step | Work | Status | Notes |
|------|------|--------|-------|
| 1.1 | `InspectData` struct and base `inspect()` on Operator | **DONE** | `inspect_data.h`; base auto-populates from `params()` |
| 1.2 | Override `inspect()` on core operators (Noise, Feedback, HSV, Bloom, Composite) | **DONE** | Feedback has custom override; others use base auto-param population which is sufficient (no meaningful computed runtime metrics beyond params) |
| 1.3 | Override `inspect()` on audio operators (Kick, Snare, BandSplit, Levels, Clock, Sequencer) | **DONE** | All audio operators have custom inspect() — Kick/Snare expose envelope + velocity state, Clock exposes beat/bar/running, Sequencer exposes step/velocity/active_steps |
| 1.4 | `FrameAnalysis` — GPU readback + CPU histogram/brightness/spatial analysis | **DONE** | `frame_analysis.h/.cpp` — multi-format support, histogram, spatial grid, HSV analysis |
| 1.5 | `Chain::inspectAll()` aggregation | **DONE** | Aggregates all operators + output analysis |
| 1.6 | `vivid build` CLI with structured JSON error output | **DONE** | Structured JSON with file/line/column/severity |
| 1.7 | `vivid inspect` CLI command | **DONE** | JSON to stdout + optional `--out` directory with snapshot PNG |
| 1.8 | `vivid params` CLI command | **DONE** | Full param enumeration with type/range/default |
| 1.9 | `vivid graph` CLI command | **DONE** | Chain topology as JSON DAG |
| 1.10 | Assertion YAML parser | **DONE** | Implemented as JSON (not YAML) — `vivid-assertions.json` |
| 1.11 | `vivid check` CLI command | **DONE** | Dot-path resolution, numeric + string comparisons, exit 0/1 |
| 1.12 | Audio waveform overview image generation for inspect reports | **DONE** | Full-duration accumulation via recording tap; falls back to last block if tap unavailable |
| 1.13 | `vivid-project.json` manifest schema and loader | **DONE** | `project_manifest.h/.cpp` — name, chain, preview defaults, params, assertions |

**After Phase 1:** An agent can edit code, build, understand the chain, inspect output, and validate assertions — the complete inner loop. No video export yet; the LLM works entirely from structured data.

### Phase 2: Puppeteered Export & Human Review

This phase enables the outer loop — the LLM produces media the human can review.

| Step | Work | Status | Notes |
|------|------|--------|-------|
| 2.1 | `EventInjector` class — `param_set`, `key_press`, `trigger` events | **DONE** | `event_injector.cpp` — frame-based event processing |
| 2.2 | Script parser | **DONE** | JSON format (not YAML as originally planned) |
| 2.3 | Headless render loop with per-frame pixel capture | **DONE** | Deterministic timing via recording mode |
| 2.4 | Video encoding | **DONE** | Platform-native encoders (AVAssetWriter on macOS) — not ffmpeg pipe |
| 2.5 | Audio buffer capture per frame | **DONE** | Audio tap during recording |
| 2.6 | Audio encoding | **DONE** | Integrated with platform encoder |
| 2.7 | Audio/video muxing | **DONE** | Platform-native (no separate ffmpeg mux step needed) |
| 2.8 | `vivid export` CLI command | **DONE** | Full A/V export with script injection, codec selection, resolution/fps config |
| 2.9 | Add `param_ramp` event type (requires per-frame interpolation state) | **DONE** | Linear interpolation between frames |
| 2.10 | Add `midi_note`, `midi_cc` event types | **DONE** | Supported in EventInjector |
| 2.11 | Add `mouse_move`, `mouse_click` event types | **DONE** | Supported in EventInjector |
| 2.12 | Add `snapshot_recall` event type | **DONE** | Handler + validation in EventInjector; SnapshotStore::update() called each frame in main loop for crossfade |
| 2.13 | Script validation — warn on events targeting nonexistent operators/params | **DONE** | Pre-flight warns on missing operators/params |

**After Phase 2:** An agent can produce video/audio demos and send them to the user. The complete agent workflow is functional.

### Phase 3: Performer UI Enhancements

This phase improves the live performance experience. It's independent of Phases 1-2 but shares infrastructure.

| Step | Work | Status | Notes |
|------|------|--------|-------|
| 3.1 | Interactive parameter control surface (ImGui panel with sliders) | **DONE** | InspectorPanel with sliders, auto-show/hide on node select |
| 3.2 | MIDI mapping for parameters | **DONE** | `MidiIn::mapCC()` with min/max scaling |
| 3.3 | Chain health indicators on visualizer nodes | **DONE** | Green/yellow/red node borders in chain visualizer |
| 3.4 | Inline VU meters on audio nodes | **DONE** | Node overlay callback in `chain_visualizer.cpp` |
| 3.5 | Preset/snapshot save and recall | **DONE** | SnapshotStore with JSON persistence |
| 3.6 | Snapshot keyboard/MIDI triggering | **DONE** | Number keys 1–9 for recall via ShortcutManager |
| 3.7 | Live output recording | **DONE** | Background recording with record button in status bar |
| 3.8 | Console/log overlay (read-only HUD) | **DONE** | ConsolePanel — color-coded, 256-msg ring buffer |
| 3.9 | Remove code editor, terminal, file management UI | **N/A** | Never built — external editor workflow from the start |

### Phase 4 (Future): Live Streaming & Real-Time Collaboration

**Status: NOT STARTED** — deferred as planned.

Stream the headless output via WebRTC or RTMP so the human can watch in real-time and provide feedback during rendering rather than waiting for an exported file. This enables a tighter outer loop where the agent adjusts parameters live based on voice/text input.

Not a priority until Phases 1-3 are complete and battle-tested.

---

## Deviations from Original Plan

Several implementation decisions diverged from the plan:

- **JSON instead of YAML**: Both assertions (`vivid-assertions.json`) and event scripts use JSON, not YAML. This simplifies parsing (nlohmann::json already in the project) at a small readability cost.
- **Platform-native encoding instead of ffmpeg pipe**: Video export uses AVAssetWriter (macOS) and platform APIs instead of piping to ffmpeg. Removes the external dependency entirely.
- **Base `inspect()` auto-populates from `params()`**: Rather than requiring every operator to override `inspect()`, the base class automatically exposes all declared `Param<T>` values. Custom overrides are only needed for computed/derived metrics (e.g., Feedback's `energy` and `pixel_change_pct`).

---

## Additional Items Built (Not in Original Plan)

The following were implemented beyond the scope of this plan:

| Feature | Description |
|---------|-------------|
| **MCP Server** (`vivid mcp`) | Full bidirectional integration with Claude Code — real-time param control, frame capture, hot-reload monitoring, slider sync |
| **`vivid docs` CLI** | Search docs, list/show recipes, find code examples — all JSON output |
| **DevTools Framework** | PanelManager, ShortcutManager, OverlayCanvas — extensible panel system with keyboard toggles |
| **Status Bar Panel** | FPS, frame time, resolution, memory, record/snapshot buttons |
| **Performance Panel** | Real-time performance graphs (Cmd+1 toggle) |
| **GUI Widget Library** | Immediate-mode widgets: sliders, XY pads, ADSR envelopes, graphs, etc. |
| **CLI snapshot mode** | `--snapshot` flag with multi-frame capture for CI/testing and GIF creation |
| **`vivid-cef` module** | Chromium Embedded Framework integration (in progress) |

---

## Remaining Work

| Item | Description |
|------|-------------|
| **1.12** | Audio waveform overview image for inspect reports |
| **Phase 4** | Live streaming (future) |
| **Performer control surface** | The plan described large, high-contrast, purpose-grouped parameter controls for dark rooms. Current InspectorPanel is functional but not performance-optimized for stage use |

---

## Part 7: Cloud Environment

For the agent workflow to run in a cloud environment (the developer's machine is optional), the following infrastructure is needed. This is outside Vivid's codebase but should be documented alongside it.

**Dockerfile:** A reproducible build environment with CMake, a C++17 compiler, wgpu-native, and ffmpeg. This image should build Vivid from source and include all runtime dependencies.

**GPU access:** Cloud GPU instances (AWS g4dn, GCP T4, etc.) for real rendering. For faster/cheaper iteration during the inner loop, Mesa/llvmpipe software rendering may be acceptable — lower quality but no GPU required.

**Headless wgpu:** wgpu-native must be configured for offscreen rendering without a display server. On Linux this means running without X11/Wayland, using a GPU-only context. This likely already works or is close to working given Vivid's existing headless support.

**Agent setup:** An OpenClaw instance (or equivalent) running on the same machine or a nearby server, configured with a Vivid "skill" that knows the CLI commands and project structure. The skill tells the agent how to use `vivid build`, `vivid inspect`, `vivid check`, `vivid export`, and how to interpret the JSON outputs.

**File delivery:** The agent framework handles sending exported video files to the user's messaging platform. Vivid just writes the file to disk.
