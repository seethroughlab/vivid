## What you'll build

A shape that pulses in sync with a drum beat — the foundational Vivid AV pattern. Audio drives visuals through the Control domain bridge.

## The core idea

Audio and GPU operators run at different speeds and can't wire directly. The path is:

```
Audio output → AudioAnalysis → (scalar) → Smooth → GPU parameter
```

**AudioAnalysis** is an audio passthrough that also emits scalar `rms` and `peak` outputs. These cross from the audio domain into the control domain automatically. **Smooth** shapes the fast-changing audio signal into something visually useful.

## Operators used

| Operator | Domain | Role |
|----------|--------|------|
| ClockFr | Control | Tempo clock |
| DrumKick | Audio | Synthesized kick drum |
| Gain | Audio | Amplitude control + rms output |
| AudioAnalysis | Audio | Passthrough that emits rms/peak scalars |
| Smooth | Control | Fast attack, slow release shaper |
| Shape2D | GPU | SDF polygon shape (outputs a drawable) |
| Render2D | GPU | Rasterizes a drawable to a texture |
| video_out | — | Display |
| audio_out | — | Audio output |

## Step 1: Build the audio side

1. Add **ClockFr**. Set `bpm` to **120**.
2. Add **DrumKick**. Connect `clock1/beat_trigger` → `drumkick1/trigger`.
3. Add **Gain**. Connect `drumkick1/output` → `gain1/input`. Set `gain` to **0.8**.
4. Connect `gain1/output` → `audio_out/input`.

You should hear a kick on every beat at 120 BPM.

## Step 2: Build the visual side

Shape2D and Render2D are a pair: Shape2D computes the SDF geometry (outputs a `drawable`), Render2D rasterizes it to a `texture`.

1. Add **Shape2D**. Set `sides` to **6** (hexagon). Set `r`, `g`, `b` to your preferred color.
2. Add **Render2D**. Connect `shape1/drawable` → `render1/drawable`.
3. Connect `render1/texture` → `video_out/input`.

A static hexagon appears in the preview.

## Step 3: Bridge audio to visuals

Now connect the kick's energy to the shape's size.

1. Add **AudioAnalysis**. Insert it inline: disconnect `gain1/output` → `audio_out/input`, connect `gain1/output` → `analysis1/input`, connect `analysis1/output` → `audio_out/input`. The audio still plays — AudioAnalysis is a passthrough.

2. Add **Smooth**. Set `rise_time` to **0.005** and `fall_time` to **0.3**.

3. Connect `analysis1/rms` → `smooth1/input`.

4. Connect `smooth1/value` → `shape1/scale_x` and `smooth1/value` → `shape1/scale_y`.

The hexagon now pulses with each kick hit.

## Step 4: Keep the shape visible between hits

With the current setup the shape shrinks almost to nothing between beats. Add a baseline so it's always visible.

Select the wire from `smooth1/value` to `shape1/scale_x`. In the inspector, set **to_min** to **0.05** and **to_max** to **0.5**. Do the same for the `scale_y` wire.

Now the shape has a small resting size and grows to 0.5 on hard hits.

## Why both axes?

Connecting only `scale_x` squashes the hexagon into a horizontal strip. Both `scale_x` and `scale_y` must track together for a radial pulse effect.

## What's happening

```
ClockFr → DrumKick → Gain → AudioAnalysis → audio_out
                              ↓
                           rms (scalar, bridged to control)
                              ↓
                           Smooth (fast attack, slow release)
                              ↓
                        Shape2D/scale_x + scale_y
```

The key bridge: `AudioAnalysis/rms` is a `VIVID_PORT_SCALAR` — a control-compatible signal emitted from an audio-domain operator. The graph compiler routes it through the `AudioFrameBridge`, which samples the scalar once per frame and delivers it at frame rate. Smooth then shapes the fast transient into a visually smooth envelope.

## Tip: Try different analysis sources

Instead of `rms`, try `analysis1/peak` for a sharper response, or swap AudioAnalysis for a **Gain** node and use `gain1/rms` directly — Gain also exposes analysis outputs.

## Next steps

- [Control and Modulation](../control-and-modulation/) — drive the shape with LFOs instead of audio
- [Multi-Voice Patterns with Lanes](../multi-voice-lanes/) — run multiple shapes from multiple drum voices
- [Building a Complete AV Patch](../complete-av-patch/) — combine this with a full drum machine and effects chain
