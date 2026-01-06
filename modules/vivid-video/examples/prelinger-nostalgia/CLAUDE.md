# Prelinger Nostalgia

A Boards of Canada inspired audio-visual performance piece with full MIDI controller integration.

## Vision

Evokes the nostalgic, dreamlike quality of BoC's music combined with vintage Prelinger Archive footage. The performance combines:

- **Warm polyphonic pads** through Moog-style ladder filters
- **Evolving wavetable leads** with subtle detuning
- **FM bell textures** for ethereal accents
- **Tape effect warmth** (wow, flutter, saturation)
- **Vintage film aesthetics** (grain, CRT, feedback)

## File Structure

```
prelinger-nostalgia/
├── chain.cpp        # Main entry point (~300 lines)
├── music.h          # Chord voicings & musical data
├── midi_mapping.h   # Controller CC assignments & scaling
├── performance.h    # State management & helpers
├── CLAUDE.md        # This file
└── assets/
    └── AboutBan1935.mp4
```

### Why This Structure?

Following the **wipeout-viz pattern**, this example demonstrates how to refactor a complex chain into manageable, reusable modules:

1. **music.h** - Pure data, no dependencies beyond notes.h
2. **midi_mapping.h** - Controller-specific, easily swapped for other hardware
3. **performance.h** - State & helpers, imports music.h
4. **chain.cpp** - Clean setup/update, imports everything

## Controller: Akai MIDImix

8 channel strips, each with 3 knobs + mute + solo + fader.

### Layout

| Row | Ch1 | Ch2 | Ch3 | Ch4 | Ch5 | Ch6 | Ch7 | Ch8 |
|-----|-----|-----|-----|-----|-----|-----|-----|-----|
| **Knob 1** | Pad Cut | Pad Res | Lead Cut | Lead Res | Delay T | Delay FB | Rev Size | Rev Damp |
| **Knob 2** | Wow | Flutter | Hiss | Age | Grain Pos | Density | Pitch | Spray |
| **Knob 3** | Bloom | Thresh | FB Dec | FB Zoom | Film Gr | Sat | Hue | Vid Spd |
| **Mute** | Grain | CRT | Feedback | Flash | Pause | Freeze | - | - |
| **Solo** | Am9 | Fmaj7 | Dm7 | Em7 | Bells | - | - | - |
| **Fader** | Pad | Lead | Bells | Clouds | Delay | Reverb | Tape | Master |

## Audio Chain

```
PolySynth → LadderFilter ─┐
WavetableSynth → LadderFilter ─┼→ AudioMixer → TapeEffect → Delay → Reverb → Limiter → Out
FMSynth ─────────────────────┤
Granular ────────────────────┘
```

## Visual Chain

```
VideoPlayer → HSV → Bloom → Feedback → FilmGrain → Vignette → CRTEffect → Flash → Out
```

## Performance Tips

1. **Start with faders at sensible levels** (Master 70%, Pad 70%, Lead 40%)
2. **Filter sweeps are your main expression** - Row 1, Ch1-2
3. **Solo buttons for instant mood changes** - combine with filter sweeps
4. **Flash on beat drops** - Mute button Ch4
5. **Feedback zoom for psychedelic builds** - Knob Row 3, Ch4

## Adapting for Other Controllers

To use a different controller:

1. Copy `midi_mapping.h` and rename
2. Update the CC constants to match your controller
3. Adjust scaling functions if needed
4. Change `midi.openPortByName()` in chain.cpp
