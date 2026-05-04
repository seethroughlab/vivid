## What you'll build

An animated visual: a glowing blob that moves in a figure-eight pattern, driven by two LFOs. You'll learn how GPU operators and Control operators connect to make things move.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| Metaball | GPU | Renders SDF blobs as a texture |
| LfoFr | Control | Oscillates a value at a set frequency |
| video_out | — | Displays the GPU texture output |

## Step 1: Create a new graph

Choose **File → New Graph**. The canvas opens with a `video_out` node already placed.

## Step 2: Add a Metaball

Double-click the canvas to open the operator browser. Type **Metaball** and add it.

Connect `metaball/texture` → `video_out/input`. You should see a static white blob in the preview.

## Step 3: Make it move

The blob is static because nothing is driving its position. Add an **LfoFr** operator.

Connect `lfo1/value` → `metaball/pos_x`.

Now the blob sweeps left and right. The LfoFr generates a -1 to +1 sine wave at 1 Hz by default.

## Step 4: Add vertical motion

Add a second **LfoFr**. Set its `frequency` to **0.7** (a different rate so the paths don't repeat immediately).

Connect `lfo2/value` → `metaball/pos_y`.

The blob now traces a Lissajous figure across the output.

## Step 5: Tune the motion

- **lfo1 → frequency**: how fast the horizontal sweep is (Hz)
- **lfo1 → amplitude**: how far left/right it moves
- **metaball → count**: add more blobs (up to 8) — they share the same position input, spreading automatically with the `spread` parameter
- **metaball → threshold**: controls blob merge distance and size

## What's happening

```
LfoFr (Control) → Metaball/pos_x (GPU)
LfoFr (Control) → Metaball/pos_y (GPU)
Metaball/texture (GPU) → video_out/input
```

The **Control** domain generates scalar values that change over time at frame rate (~60 Hz). These values can wire directly into any numeric parameter on any operator, including GPU operators. This is how Vivid animations work: Control drives everything.

The GPU operator (Metaball) reads those values on each frame and renders accordingly. The output is a `gpu_texture` — a GPU-side image — that flows into `video_out`.

## Next steps

- [Your First Video Effect](../first-video-effect/) — process this texture through effects
- [Control and Modulation](../control-and-modulation/) — deeper look at the Control domain
- [Audio-Reactive Visuals](../audio-reactive-visuals/) — drive visuals from audio instead of LFOs
