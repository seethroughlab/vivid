## What you'll build

Take an existing texture and apply two GPU effects to it: a glow (Bloom) and a motion trail (Feedback). You'll learn how GPU effects chain together and how to tune their parameters.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| NoiseTexture | GPU | Generates animated Perlin/Simplex noise |
| Bloom | GPU | Adds glow to bright pixels |
| Feedback | GPU | Composites current frame over previous frame |
| video_out | — | Displays the final texture |

## Step 1: Create a source texture

A video effect processes an existing texture. Start with **NoiseTexture** as a source.

Add **NoiseTexture**. Connect `noise/texture` → `video_out/input`. You should see animated noise. The noise self-animates using its built-in `speed` parameter.

## Step 2: Add Bloom

Bloom extracts bright pixels and blurs them to create a glow. Add **Bloom**.

Chain it between the noise and the output:
1. Disconnect `noise/texture` → `video_out/input`
2. Connect `noise/texture` → `bloom/input`
3. Connect `bloom/texture` → `video_out/input`

The noise now glows on its bright regions.

**Tune Bloom:**
- `threshold` (default 0.8) — raise it to glow only the brightest spots; lower it to glow more
- `radius` — spread of the blur kernel
- `passes` — more passes = wider, softer glow (costs GPU time)

## Step 3: Add Feedback

Feedback composites the current frame over the previous frame with a slow decay. Add **Feedback**.

Chain it after Bloom:
1. Disconnect `bloom/texture` → `video_out/input`
2. Connect `bloom/texture` → `feedback/input`
3. Connect `feedback/texture` → `video_out/input`

The noise now leaves glowing trails that decay over time.

**Tune Feedback:**
- `decay` — how quickly the trail fades (lower = longer trails)
- `zoom` — subtle zoom each frame creates a pulsing tunnel effect
- `rotation` — small values create a slow spin

## What's happening

```
NoiseTexture → Bloom → Feedback → video_out
```

Each GPU operator takes a `gpu_texture` input and outputs a new `gpu_texture`. Effects chain by passing textures downstream. The chain is evaluated on the GPU on every frame.

Feedback is special: it reads from its own previous output, creating a recursive loop. This is safe in Vivid — the graph compiler detects the feedback edge and routes it through a one-frame delay buffer.

## Tip: Effect order matters

Bloom before Feedback creates glowing trails. Feedback before Bloom would create trails first, then glow everything including old trail residue — a more intense, accumulative look. Try both.

## Next steps

- [Your First Audio Generator](../first-audio-generator/) — build the audio side
- [Control and Modulation](../control-and-modulation/) — drive effect parameters with LFOs
