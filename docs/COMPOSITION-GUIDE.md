# Composition Guide

This is a reference for the **mechanics** of Vivid graph composition — the primitives, anti-patterns, and diagnostic tools that let you author any AV graph cleanly regardless of the aesthetic you're chasing. Think of it as scales and voice-leading rules in music theory: always useful to know, never a substitute for knowing what you want to express.

**What this guide is not:** a set of "good" AV patterns to imitate. What's compelling depends on the project, the user, and the specific precedent you're translating from — none of that belongs in a generic reference. When building a graph for a real task, start from an external precedent (see "Translating references into Vivid" below), not from this guide's template list.

---

## Translating references into Vivid

For any real AV composition task, the compositional target comes from outside — a YouTube video, a project page, an artist's work, a track, an image, or a mood description. The job is to extract structural and stylistic properties from that precedent and translate them into Vivid's operator vocabulary.

### Workflow

1. **Ask for the precedent.** Before touching the graph, ask the user for a reference — URL, YouTube link, image path, audio track path, artist name, or text description of the vibe. Don't assume defaults.
2. **Ask how to use it.** Reference fidelity is a per-task choice: *imitate closely* (match palette, density, tempo, motion), *inspired-by* (extract principles, apply loosely), *style-only* (visual language without replicating specific elements), or *opposite-of* (use as a negative target). Ask which.
3. **Ingest the reference:**
   - URL (YouTube, project page, direct image) → `fetch_reference(url)` downloads a representative image and scrapes metadata
   - Local image file → use its path directly
   - Audio track → use its path directly
   - Text description or artist name → use LLM knowledge; no tool call needed
4. **Extract structure:**
   - For images: `analyze_image(path)` returns color palette, luminance/contrast, composition descriptors, style tags
   - For audio: `analyze_track(path)` returns key, BPM, spectral character, structural form, mood tags
   - For text references: reason about the style using your general knowledge of the artist/aesthetic
5. **Translate to operators.** Map the extracted descriptors onto Vivid's operator vocabulary using your judgment:
   - A "high-contrast monochrome dense grid" suggests `Shape2D` with `Repeat` + aggressive `Bloom` + minimal palette
   - A "warm organic pulsing" suggests `Metaball` + `Smooth` envelope follower + amber/red color params
   - A "rhythmic percussive" suggests drum operators + `Smooth` envelope followers to shapes
   - The Mechanical Primitives section below tells you *how* to wire each chunk of that translation; your compositional judgment decides *which* chunks to use.
6. **Build incrementally.** Add nodes and wire them in small increments. After each meaningful change, `capture_image(mode="output")` or `compare_output_to_reference(reference_path)` to see the state.
7. **Iterate toward the reference.** Use `compare_output_to_reference` to pair the current capture with the reference image. Judge visually; adjust params. If metrics suggest mechanical issues (near-black output, dead motion), use `diagnose_composition_issue` to get a concrete fix list.
8. **Stop when it matches the intent.** Not when it matches an abstract "good" — when it evokes the precedent the way the user asked for.

The MCP tools support this loop end-to-end. They do not supply taste.

---

## Mechanical Primitives

The rest of this guide is value-neutral mechanics — rules of the road for wiring AV graphs that work, regardless of what aesthetic you're chasing. These are about avoiding broken output (invisible shapes, sub-frame flashes, dead motion), not about choosing what to build.

### Core principles

### 1. Audio is animation, not background

Every audio event should drive a visual event. If your audio fires a drum on every beat but your visuals just loop an LFO in parallel, you have two unrelated performances layered on top of each other — not one composition. The difference is legibility: audience members can perceive audio-visual coupling within ~80 ms of an onset; anything else reads as coincidence.

**Signal flow should go:** `audio event → envelope → visual parameter`. Not `metronome → both sides in parallel`.

### 2. Build in layers

A compelling AV graph typically has **3+ reactivity axes** layered, not one:
- an envelope driving size or scale,
- a spectral feature driving color or hue,
- an onset driving a discrete event (hue shift, shape count, trigger).

Single-axis reactivity feels monotone even when the axis is strong. `audio_reactive_demo` scored `energy_motion_correlation = +0.90` on the displacement axis alone, but still reads as "subtle" because nothing else responds.

### 3. Drive both geometric axes

When modulating a Shape2D's size from a single source, wire the source to **both** `scale_x` **and** `scale_y`. Driving only one axis stretches the shape into an ellipse instead of pulsing it. This was the single most-repeated bug across the intro set — it appeared in `showcase_demo`, `av_metronome_demo`, and `demo.json`.

```json
// WRONG — shape distorts horizontally, stays fixed vertically
{ "from": "lfo1/value", "to": "shape1/scale_x" }

// RIGHT — shape pulses radially
{ "from": "lfo1/value", "to": "shape1/scale_x" },
{ "from": "lfo1/value", "to": "shape1/scale_y" }
```

### 4. Leave baseline presence

When remapping audio peaks to shape scale, don't let the `to_min` collapse to near-zero. A shape at 1% of canvas is invisible between hits. Bump `to_min` to at least **0.03–0.05** so the shape is always faintly present, growing on hits rather than flickering into existence:

```json
// WRONG — shape vanishes between hits
"from_min": 0.0, "from_max": 0.8, "to_min": 0.01, "to_max": 0.35

// RIGHT — visible baseline, pulses on hits
"from_min": 0.0, "from_max": 0.8, "to_min": 0.05, "to_max": 0.35
```

### 5. Measure, don't guess

After any reactivity change, run `analyze_output(mode="av", window_seconds=3)` and read both metric families. A graph can *look* compelling to you while the metrics reveal it's flickering sub-frame, or *feel* reactive while `energy_brightness_correlation` is flat because the coupling is on motion. Metrics don't replace judgment; they catch failure modes before you ship.

---

## Mechanical templates

Four recurring signal-flow shapes that solve specific *mechanical* problems cleanly. These are not aesthetic recommendations — they're answers to "how do I wire this" once you've decided *what* you want to wire. You build compelling graphs by combining and adapting these (along with other shapes not listed here), guided by your reference.

> **Also available as a tool.** `get_composition_patterns(intent)` returns these templates as structured JSON with recommended parameters, example connection wiring, and exemplar graph paths — convenient when authoring through MCP.

### Pattern A — Drum-driven pulse

**Signal flow:** `drum/peak → Smooth → shape/scale_x + scale_y`

Each drum's instantaneous peak feeds an envelope follower (`Smooth`, alias `EnvelopeFollower`) with snappy attack and long decay, which drives a shape's scale on both axes. One shape per drum gives an immediately legible AV pairing.

- **Smooth params to start with:** `rise_time=0.005, fall_time=0.4` (the "Envelope follower (snappy)" factory preset)
- **Shape remap:** `from_max` matches the drum's typical peak (0.4–0.8); `to_min` ≥ 0.05; `to_max` ≤ 0.35
- **Exemplar:** `graphs/intro/showcase_demo.json`
- **Expected metric:** `onset_response_rate > 0.85` for a working drum kit

### Pattern B — Continuous reactivity

**Signal flow:** `audio/rms → visual/param` (optionally through a shallow remap)

For sustained audio (oscillators, pads, drones), post-gain RMS drives a visual parameter directly. No envelope follower needed — the audio is already continuous.

- **Typical wiring:** `gain.rms → displace.amount` or `gain.rms → bloom.intensity`
- **Exemplar:** `graphs/intro/audio_reactive_demo.json`
- **Expected metric:** `energy_motion_correlation > 0.7` (or brightness correlation, depending on which visual parameter is driven)
- **Caveat:** correlation can still be near-zero if the coupling drives hue, displacement, or position rather than brightness. Check all three correlation axes.

### Pattern C — Parametric sync

**Signal flow:** `shared LFO/metronome → both audio param AND visual param`

One source drives both sides in parallel. Not technically audio-reactive, but produces a tight phase-locked AV feel because both domains move on the same timing. Useful when you want deterministic choreography rather than emergent coupling.

- **Typical wiring:** `lfo/value → osc/frequency` and `lfo/value → metaball/pos_x`
- **Exemplars:** `graphs/intro/av_demo.json`, `graphs/intro/av_metronome_demo.json`
- **Expected metric:** correlations and onset rate are irrelevant here — the coupling is the shared source, not a measurable response.

### Pattern D — Spectral color

**Signal flow:** `audio FFT features → hue / color params`

Treble energy → warm hues; bass energy → cold hues; spectral centroid → saturation. Currently requires FFT bin reduction by hand (no `SpectralFeatures` operator yet — on the roadmap). With `FFTAnalysis` emitting 512 lane bins, you can tap specific ranges via `Math` + `Select` operators. This pattern is listed for completeness but is harder than A/B/C.

---

## Anti-patterns

Fastest path to improvement is recognizing these in a working graph.

| Symptom | Likely cause | Fix |
|---|---|---|
| Shape flashes sub-frame, mean brightness < 0.01 | Driving shape scale from raw `peak` port without envelope | Insert a `Smooth` (rise ≈ 0.005, fall ≈ 0.4) between peak and scale |
| Shape distorts into an ellipse instead of pulsing | Single-axis scale driving (`scale_x` wired, `scale_y` static) | Add the matching `scale_y` connection |
| Shape vanishes between beats | `to_min` too low (e.g., 0.01) | Bump `to_min` to 0.03–0.05 |
| Every drum fires but motion_correlation ≈ 0 | 160 ms sample rate undersamples brief peaks | Check `onset_response_rate` instead — correlation breaks down for short events |
| Feedback/trail rich but correlations are *negative* | Feedback decay shifts visual peak later than audio peak | Expected — trust `onset_response_rate`, not Pearson. Reactivity is real but phase-shifted |
| "Audio-reactive" demo scores `energy_brightness_correlation` near zero | Coupling drives motion or displacement, not brightness | Check `energy_motion_correlation` — it captures shape/position change that doesn't affect luminance |
| Graph has `video_out` but output is black | Some upstream gate isn't receiving a signal, or opacity/alpha is zero, or `scale_x × scale_y` is tiny | `analyze_output(mode="frame")` and follow the upstream chain; solo individual nodes to isolate |

---

## Metric thresholds you can trust

From empirical calibration on the intro set. These are rough — read them as "consistent with a mechanically-working graph," not as aesthetic pass/fail gates. A graph can land inside every range below and still be boring; a graph can fall outside some of them and still be exactly the effect the user asked for (e.g., a deliberately dim ambient piece will read as "near-black" by the metric).

| Metric | What a mechanically-working graph usually looks like |
|---|---|
| `mean_brightness` | 0.05–0.4 (below 0.02 usually means shapes aren't rendering) |
| `contrast` | 0.1–0.3 (higher than 0.4 is usually high-contrast noise, not composition) |
| `motion_magnitude` | 0.05–0.3 (below 0.01 means output is ~static) |
| `energy_brightness_correlation` | Ignore for percussive content; useful for continuous RMS→brightness coupling (expect > 0.5 if that's the intent) |
| `energy_motion_correlation` | > 0.5 for continuous reactivity graphs |
| `onset_response_rate` | > 0.7 for any percussive AV graph |
| `reactivity_latency_ms` | < 300 (higher = visual peak lags audio peak; check your feedback/smoothing) |
| `detected_onsets` | Matches your rhythmic density expectation (3–4/s for full drum kit at 120 BPM) |

### Why both metric families?

Two complementary lenses:

- **Per-axis correlation** answers: "does the visual track audio energy linearly?" — good for continuous coupling, breaks down when there's smoothing, feedback, or sub-sampled peaks.
- **Onset response rate** answers: "when audio events happen, does something visible happen?" — robust to smoothing and feedback lag, blind to continuous coupling.

If one is high and the other is low, that's a feature — it tells you which mode of reactivity you have.

---

## Diagnosing a dead graph

When a graph feels wrong, `analyze_output(mode="av", window_seconds=3)` first, then follow this decision tree:

```
audio.rms == 0
├── graph has audio_out connected: wait 4 s after load_graph and retry (audio settling)
└── no audio_out: that's fine for pure-visual graphs; use mode="frame" instead

visual.mean_brightness < 0.01
├── scale_x * scale_y tiny (< 0.001)?  → scale issue (see anti-patterns)
├── opacity/alpha at 0 in composite?   → param bug
└── upstream chain broken?            → inspect_graph, trace disconnected wire

visual.motion_magnitude < 0.01 and you expected motion
├── LFO freq too low (< 0.5 Hz) for the 3 s window?  → try a shorter window
├── single-axis driving (see anti-pattern 2)?         → drive both scale axes
└── Smooth falltime too long?                         → reduce to 0.2 s

onset_response_rate < 0.3 on a drum-driven graph
├── peak → scale without Smooth?     → add Smooth(rise=5ms, fall=400ms)
├── to_min at 0.01?                   → bump to 0.05
└── feedback decay swallowing hits?  → reduce fb/decay to 0.85-0.9

all three correlations near zero but onset_response_rate is high
└── graph IS reactive, just event-driven. Trust onset_response_rate; correlations are the wrong lens.
```

---

## Further reading

- `docs/plans/compelling-demos/00-case-study-findings.md` — original Phase 0 rating of the intro set
- `docs/plans/compelling-demos/02-phase-2-envelope-follower.md` — why `Smooth` fixes `showcase_demo` and what the metric-vs-reality gap looked like
- `docs/plans/compelling-demos/03-onset-response-verification.md` — how the `onset_response_rate` metric was designed and validated
- `docs/plans/compelling-demos/05-intro-audit.md` — the sweep that turned up `av_metronome_demo` and `demo.json`
- `docs/LLM-INTEGRATION.md §9` — the perception system architecture these metrics slot into
- `graphs/intro/*.json` — working exemplars for each pattern above
