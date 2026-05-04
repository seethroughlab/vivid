## What are lanes?

A lane is one parallel element in a multi-valued signal. A scalar is a one-lane value. A chord is a four-lane value. A drum sequencer output is a six-lane trigger stream — one lane per drum voice.

Operators that understand lanes process each lane independently in a single pass. A single FmSynth node with a six-lane note input spawns six voices simultaneously. A single Shape2D with a six-lane position input renders six shapes.

This is Vivid's vectorized execution model: fewer nodes, more parallel instances.

## What you'll build

A six-voice drum machine using DrumSequencer driving individual drum voices, with each voice feeding its own effects chain through lane-aware routing.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| ClockFr | Control | Tempo clock |
| DrumSequencer | Control | Six-track step sequencer outputting trigger lanes |
| DrumKit | Control | Routes notes to per-slot outputs (demuxer) |
| DrumKick | Audio | Synthesized kick |
| DrumSnare | Audio | Synthesized snare |
| DrumHiHat | Audio | Synthesized hi-hat |
| Mixer | Audio | Combines multiple audio streams |
| audio_out | — | Audio output |

## Step 1: Clock and DrumSequencer

1. Add **ClockFr**. Set `bpm` to **120**.
2. Add **DrumSequencer**. Connect `clock1/beat_phase` → `drumseq1/beat_phase`.

DrumSequencer has six track outputs. Each track is an independent step pattern. Open the inspector and program a pattern — click steps to enable/disable hits on each track.

Default mapping (tracks 1–6): kick, snare, hi-hat, clap, tom, cymbal.

## Step 2: Wire individual drum voices

Each DrumSequencer track output is a single-lane trigger. Connect each to its corresponding drum operator:

```
drumseq1/track_1 → drumkick1/trigger
drumseq1/track_2 → drumsnare1/trigger
drumseq1/track_3 → drumhihat1/trigger
```

Connect all drum outputs to a **Mixer**:
```
drumkick1/output  → mixer1/input_1
drumsnare1/output → mixer1/input_2
drumhihat1/output → mixer1/input_3
```

Connect `mixer1/output` → `audio_out/input`.

You should hear the drum pattern.

## Step 3: Add an Euclidean pattern

Euclidean rhythms distribute N hits as evenly as possible across M steps. They produce musical patterns found in traditional music worldwide.

1. Add **Euclidean**. Set `hits` to **3** and `steps` to **8** (the classic tresillo pattern).
2. Connect `clock1/beat_phase` → `euclidean1/beat_phase`.
3. Connect `euclidean1/trigger` → `drumtom1/trigger`.
4. Add **DrumTom** → `mixer1/input_4`.

Now the tom plays a 3-against-8 euclidean pattern over the sequencer.

## Step 4: Lanes in the GPU domain

The same lane concept applies to visuals. With a lane-bearing Control signal you can drive multiple instances of a GPU operator simultaneously.

1. Add **Shape2D** and **Render2D** → `video_out`.
2. Add **SpreadNoise** (Control). Set `count` to **6**.
3. Connect `spreadnoise1/value` → `shape1/pos_x`.

Shape2D detects the six-lane input and renders six shapes, each at a different horizontal position from the SpreadNoise output.

## What's happening

```
ClockFr → DrumSequencer → track_1 (1-lane trigger) → DrumKick
                        → track_2 (1-lane trigger) → DrumSnare
                        → track_3 (1-lane trigger) → DrumHiHat

SpreadNoise (6-lane scalar) → Shape2D/pos_x → renders 6 shapes
```

The graph compiler inspects the lane count at each connection. When a lane-bearing signal enters an operator, the compiler creates N logical execution instances of that operator. The `shapes` rendered, the `voices` spawned, and the `drum hits` triggered all correspond to the lane count of their input.

## Tip: Probability

DrumSequencer supports per-step probability. Set a step's probability below 1.0 to make it sometimes skip — good for adding subtle variation to a pattern without changing the base grid.

## Next steps

- [Audio-Reactive Visuals](../audio-reactive-visuals/) — use drum trigger energy to drive visuals
- [Building a Complete AV Patch](../complete-av-patch/) — combine this with reactive GPU visuals
