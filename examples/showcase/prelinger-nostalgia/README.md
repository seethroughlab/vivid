# Prelinger Nostalgia

A Boards of Canada inspired audio-visual performance piece with full MIDI controller integration.

## Overview

This example demonstrates:
- **Multi-file chain organization** - Separating music, MIDI mapping, and performance logic
- **Live performance with MIDI** - Real-time control via Akai MIDImix
- **Section-based song structure** - Automatic progression through intro/verse/chorus/outro
- **Audio-visual synthesis** - Vintage film aesthetics synced with warm analog-style audio

## Requirements

- Vivid framework built with audio and video addons
- Akai MIDImix controller (or adapt `midi_mapping.h` for your controller)
- Video file: `AboutBan1935.mp4` (from Prelinger Archive)

### Downloading Videos

```bash
cd examples/showcase/prelinger-nostalgia
./download-videos.sh --quick   # Downloads one ~35MB video
./download-videos.sh           # Downloads all curated videos (~500MB)
```

## Running

```bash
./build/bin/vivid examples/showcase/prelinger-nostalgia
```

## Song Structure

72 bars at 72 BPM = 60 seconds total

| Section | Bars | Mood | Notes |
|---------|------|------|-------|
| intro | 0-8 | Am9 | Dreamy, sparse opening |
| verse1 | 8-24 | Fmaj7 | Warm, building |
| chorus | 24-32 | Dm7 | Introspective, bells auto-trigger |
| verse2 | 32-48 | Em7 | Mysterious |
| chorus2 | 48-56 | Dm7 | Introspective, bells auto-trigger |
| outro | 56-72 | Am9 | Dreamy, fading |

The song progresses automatically through sections. Use MIDI buttons to jump between sections or restart.

## Controller Mapping: Akai MIDImix

### Faders (Mix Levels)

| Channel | Parameter | Notes |
|---------|-----------|-------|
| 1 | Pad Volume | PolySynth through LadderFilter |
| 2 | Lead Volume | WavetableSynth |
| 3 | Bells Volume | FMSynth |
| 4 | Clouds Volume | Granular atmosphere |
| 5 | Delay Mix | Echo wet/dry |
| 6 | Reverb Mix | Space/wash |
| 7 | Tape Saturation | Lo-fi character |
| 8 | Master Volume | Final output |

### Knob Row 1: Filters & Time Effects

| Channel | Parameter | Range |
|---------|-----------|-------|
| 1 | Pad Filter Cutoff | 200-4000 Hz |
| 2 | Pad Filter Resonance | 0-100% |
| 3 | Lead Filter Cutoff | 500-5000 Hz |
| 4 | Lead Filter Resonance | 0-100% |
| 5 | Delay Time | 100-1000 ms |
| 6 | Delay Feedback | 0-90% |
| 7 | Reverb Size | 0-100% |
| 8 | Reverb Damping | 0-100% |

### Knob Row 2: Texture Effects

| Channel | Parameter | Range |
|---------|-----------|-------|
| 1 | Tape Wow | 0-100% |
| 2 | Tape Flutter | 0-100% |
| 3 | Tape Hiss | 0-50% |
| 4 | Tape Age | 0-100% |
| 5 | Granular Position | 0-100% |
| 6 | Granular Density | 1-50 grains/s |
| 7 | Granular Pitch | 0.25-2.0x |
| 8 | Granular Spray | 0-100% |

### Knob Row 3: Visual Effects

| Channel | Parameter | Range |
|---------|-----------|-------|
| 1 | Bloom Intensity | 0-300% |
| 2 | Bloom Threshold | 0-100% |
| 3 | Feedback Decay | 80-99% |
| 4 | Feedback Zoom | 0.99-1.02 |
| 5 | Film Grain Intensity | 0-50% |
| 6 | HSV Saturation | 0-100% |
| 7 | Hue Shift | -0.5 to +0.5 |
| 8 | Video Speed | 0.5-1.5x |

### Solo Buttons: Section Navigation

| Channel | Function |
|---------|----------|
| 1 | Previous Section |
| 2 | Next Section |
| 3 | Restart Song |
| 4 | Skip to Chorus |
| 5 | Trigger Bells |

### Mute Buttons: Toggles

| Channel | Function |
|---------|----------|
| 1 | Toggle Film Grain |
| 2 | Toggle CRT Effect |
| 3 | Toggle Feedback |
| 4 | Trigger Flash |
| 5 | Pause/Resume |
| 6 | Freeze Granular |

## Performance Tips

1. **Set initial fader positions** before starting:
   - Master (Ch8) at 70%
   - Pad (Ch1) at 70%
   - Lead (Ch2) at 40%
   - Bells, Clouds lower

2. **Filter sweeps are your main expression**:
   - Pad cutoff (Row 1, Ch1) for dramatic builds
   - Lead cutoff (Row 1, Ch3) for brightness

3. **Use section navigation for transitions**:
   - Let the song auto-advance for hands-free performance
   - Use Solo buttons to jump around or restart

4. **Visual accents on beat drops**:
   - Flash button (Mute Ch4) for strobes
   - Feedback zoom (Row 3, Ch4) for psychedelic trails

5. **Tape wow/flutter for lo-fi character**:
   - Subtle settings (0.1-0.3) for warmth
   - Higher settings for broken tape effect

## File Structure

```
prelinger-nostalgia/
  chain.cpp        # Main entry (setup + update)
  music.h          # Chord voicings & musical data
  midi_mapping.h   # MIDImix CC assignments & scaling
  performance.h    # State, sections, and helpers
  CLAUDE.md        # Architecture documentation
  README.md        # This file
  assets/
    AboutBan1935.mp4
```

## Adapting for Other Controllers

1. Copy `midi_mapping.h` and rename (e.g., `launchpad_mapping.h`)
2. Update CC constants to match your controller
3. Adjust scaling functions if needed
4. Change `midi.openPortByName()` in `chain.cpp`

## Audio Chain

```
PolySynth -> LadderFilter -+
WavetableSynth -> LadderFilter -+-> AudioMixer -> TapeEffect -> Delay -> Reverb -> Limiter -> Out
FMSynth ----------------------+
Granular ---------------------+
```

## Visual Chain

```
VideoPlayer -> HSV -> Bloom -> Feedback -> FilmGrain -> Vignette -> CRTEffect -> Flash -> Out
```
