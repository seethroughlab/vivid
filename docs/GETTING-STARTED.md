# Getting Started

Welcome to Vivid — a real-time engine where audio and visuals live in the same graph. Everything runs live: drag a slider and hear the change instantly.

## 1. Launch and Watch

Open Vivid and go to **File → Open Example...**

Start with **"Welcome to Vivid"** (`showcase_demo`). Just sit back — three drum instruments drive three different visual shapes. The kick triggers an orange circle, the snare a white diamond, the hi-hat a blue hexagon. Nothing is pre-animated; the music IS the animation.

## 2. Start Tweaking

While the showcase is running, try clicking nodes in the graph and changing their parameters:

- **Clock** → drag **BPM** to speed up or slow down the beat
- **DrumSequencer** → toggle grid cells to change the pattern — watch how the visuals respond instantly
- **Feedback** → try **decay** (trail length) and **rotate** (spiral speed)
- **Bloom** → crank **intensity** for more glow

Every change takes effect immediately. There's no "play" button — Vivid is always live.

## 3. Explore More Examples

Use **File → Open Example...** to browse. Here's a suggested progression:

| Order | Example | What it shows |
|-------|---------|---------------|
| 1 | **Welcome to Vivid** | Audio-reactive visuals — drums trigger shapes |
| 2 | **Feedback** | Dramatic visual effects from one parameter chain |
| 3 | **Four on the Floor** | A full drum machine |
| 4 | **Audio-Visual Sync** | One LFO drives both audio pitch and visual zoom |
| 5 | **Hello Audio** | Simplest audio patch: oscillator → gain → output |
| 6 | **Getting Started** | Simplest visual patch: clock → LFO → noise → output |
| 7 | **Audio-Reactive Visuals** | Audio RMS level modulates visual brightness |
| 8 | **Lanes: Repeat** | Multiple visual elements from one signal |

The first three are about *experiencing*. The next five teach you *how it works*, one concept at a time. Each graph has sticky notes that explain what's happening.

## 4. Save Variations

Press **V** to open the variation surface at the bottom of the graph.

1. Tweak parameters until you like the current state
2. Click **+ Save New** to store it as a variation
3. Keep tweaking — the card shows a dot when you've changed something
4. Click **Branch** to explore a new direction without losing the original
5. Audition between variations by clicking cards
6. Use quantize mode (Beat / Bar / 4Bar) for tempo-synced switches

Variations persist when you save the graph.

## 5. Build Your First Graph

Once you're comfortable with the examples:

1. **File → New Graph** gives you an empty canvas with `audio_out` and `video_out` sinks
2. **Double-click** the canvas (or right-click → Add Node) to open the operator browser
3. Add an **Oscillator**, connect its output to `audio_out` — you'll hear a tone
4. Add a **NoiseTexture**, connect its texture to `video_out` — you'll see animated noise
5. Add an **LFO** and wire its value to both the oscillator's frequency and the noise's scale
6. You've just built your first audio-visual patch

## 6. What's in the Graph

Every Vivid graph has three domains:

- **Audio** (blue wires) — sample-rate processing: oscillators, filters, drums, effects
- **GPU** (green wires) — frame-rate visuals: noise, shapes, feedback, bloom, particles
- **Control** (orange wires) — frame-rate signals: LFOs, clocks, sequencers, math

Audio and GPU nodes can't connect directly — they run at different speeds. **Control** nodes bridge between them. When you see a dotted wire, that's a cross-domain bridge carrying a value between cadences.

---

## Next Steps

- **Explore the full example library** — use the tag filters in the example browser to find graphs by domain, difficulty, or style
- **Install a package** — **File → Package Browser** to add operator libraries like `vivid-glitch`
- **Create your own operator** — use **+ New Operator** in the node browser or `vivid scaffold-operator` from the CLI

## Build From Source (Developers)

```bash
git clone --recursive https://github.com/seethroughlab/vivid.git
cd vivid
cmake -B build
cmake --build build
./build/vivid graphs/intro/showcase_demo.json
```

Requires CMake 3.20+ and a C++17 toolchain (Clang recommended on macOS).
