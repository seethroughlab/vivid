# Lesson 05: Audio-Reactive

Make visuals that respond to sound - the foundation of VJ and music visualization work.

## What You'll Learn

- Using `AudioIn` for microphone input
- Analyzing audio with `Levels` and `BandSplit`
- Driving visual parameters from audio
- The concept of audio-reactive art

## Prerequisites

- Completed Lesson 04: Images and Assets
- A microphone or audio playing through your system

## Run It

```bash
./build/bin/vivid projects/getting-started/05-audio-reactive
```

Play some music or make sounds - watch the visuals react!

## Walkthrough

### The Audio-Reactive Concept

Audio-reactive visuals use sound analysis to drive visual parameters:

```
[Microphone] → [Analysis] → [Values] → [Visual Parameters]
     ↓             ↓           ↓              ↓
   Sound      Levels/FFT    0.0-1.0     Scale, Color, etc.
```

### Capturing Audio

```cpp
auto& audio = chain.add<AudioIn>("audio");
```

This captures audio from your default input device (usually microphone).

### Analyzing Audio

**Levels** gives you overall volume:
```cpp
auto& levels = chain.add<Levels>("levels");
levels.input("audio");

float volume = levels.rms();  // 0.0 to 1.0
```

**BandSplit** separates frequencies:
```cpp
auto& bands = chain.add<BandSplit>("bands");
bands.input("audio");

float bass = bands.bass();    // Low frequencies (kick drum)
float mid = bands.mid();      // Mid frequencies (vocals, guitar)
float high = bands.high();    // High frequencies (hi-hats, cymbals)
```

### Driving Visuals

In `update()`, use the analysis values:

```cpp
void update(Context& ctx) {
    auto& bands = ctx.chain().get<BandSplit>("bands");
    auto& noise = ctx.chain().get<Noise>("noise");

    // Bass makes noise scale larger
    noise.scale = 4.0f + bands.bass() * 20.0f;
}
```

## Try It

1. **Play music**: The visuals should pulse with the beat
2. **Try different music**: Notice how different genres affect the visuals
3. **Adjust sensitivity**: Change the multipliers to find good responsiveness
4. **Add beat detection**: See AGENTS.md for `BeatDetect` examples

## Common Audio-Reactive Patterns

| Audio Property | Visual Effect |
|----------------|---------------|
| Bass | Size, pulse, camera shake |
| Mids | Color shift, complexity |
| Highs | Sparkle, detail, speed |
| Beat | Flash, trigger events |
| Overall volume | Brightness, intensity |

## Smoothing

Audio values can be jittery. Use the `smoothing` parameter:

```cpp
levels.smoothing = 0.9f;  // 0 = instant, 1 = very smooth
```

Higher smoothing = smoother motion but slower response.

## Next Steps

- **Lesson 06**: Video and webcam input
- **Deep dive**: `modules/vivid-audio/examples/` for synthesis, sequencing, and more
