# Telegram Test Project

Audio-reactive visuals with synth-generated audio. Designed for remote iteration via Telegram bot.

## Commands

```bash
vivid .                    # Run with audio output
vivid . --show-ui          # Run with devtools
vivid build .              # Compile check
vivid inspect . --frame 10 # Structured metrics JSON
vivid check .              # Run assertions (exit 0 = pass)
vivid params .             # List tweakable parameters
vivid graph .              # Chain topology
```

## Chain Architecture

```
Audio:   Oscillator(55Hz saw) → Delay → AudioOutput
Analysis: Levels + BandSplit (on oscillator)
Visual:  Noise → Feedback → Bloom → output
```

Reactivity mappings (in `update()`):
- Bass → noise scale
- Mids → animation speed
- Volume → bloom intensity
- Highs → bloom threshold
- Energy → feedback decay
- Bass → feedback rotation

## Modules

- **Audio**: Oscillator, Delay, AudioOutput, Levels, BandSplit
- **Effects**: Noise, Feedback, Bloom

## Key Parameters

| Operator | Parameter | Default | Range | Audio Driver |
|----------|-----------|---------|-------|--------------|
| osc | frequency | 55 | 20–20000 | — |
| osc | volume | 0.4 | 0–1 | — |
| delay | delayTime | 400 | 0–2000 | — |
| delay | feedback | 0.5 | 0–0.99 | — |
| noise | scale | 4.0 | 0.1–20 | bass |
| noise | speed | 0.5 | 0–5 | mids |
| noise | octaves | 3 | 1–8 | — |
| feedback | decay | 0.92 | 0–1 | volume |
| feedback | zoom | 1.002 | 0.5–2 | — |
| feedback | rotate | 0.003 | -0.1–0.1 | bass |
| bloom | threshold | 0.5 | 0–1 | highs |
| bloom | intensity | 0.8 | 0–5 | volume |
| bloom | radius | 12.0 | 1–50 | — |

## Remote Workflow (Telegram)

### Inner Loop (autonomous, no export)
1. Edit chain.cpp
2. `vivid build .` — **MUST pass (exit 0) before continuing.** If non-zero, read the error, fix chain.cpp, and rebuild. Never proceed with a failing build.
3. `vivid inspect . --frame 10` — check metrics (brightness, contrast, audio RMS, spectrum)
4. `vivid check .` — run assertions including `audio.*` paths (exit 0 = pass)
5. Repeat until satisfied

### Outer Loop (send preview to user)
6. `vivid build .` — gate check before export (must pass)
7. `vivid export . -o /tmp/preview.mp4 --duration 5 --fps 30 --audio`
8. Send video via `send_file` MCP tool
9. Summarize changes and inspection data

### Error Handling Rules
- **Never run `vivid inspect`, `vivid check`, or `vivid export` without a passing `vivid build` first.** These commands hang indefinitely on compile errors instead of exiting.
- `vivid build .` is the only CLI command that exits with a non-zero code on compile failure. Always use it as a gate.
- For interactive sessions, use `--exit-on-error` flag: `vivid . --exit-on-error` — exits immediately on any compile error instead of waiting for hot-reload.

### Export Defaults for Telegram
- Quick preview: `--duration 5 --fps 30`
- Standard preview: `--duration 15 --fps 30`
- Note: `--resolution` flag may crash — omit it and use native resolution
- Keep under 50MB (Telegram bot limit)

## Audio Assertions

Use `audio.*` paths in `vivid-assertions.json` to validate audio properties:

```json
{"path": "audio.rmsLevel", "op": ">", "value": 0.01, "message": "Audio not silent"}
{"path": "audio.spectrum.bass", "op": ">", "value": 0.02, "message": "Has bass content"}
{"path": "audio.peakLevel", "op": "<", "value": 0.95, "message": "No clipping"}
{"path": "audio.spectrum.subBass", "op": ">", "value": 0.0, "message": "Sub-bass present"}
{"path": "audio.crestFactor", "op": ">", "value": 1.0, "message": "Has dynamics"}
```

Available `audio.*` paths: `rmsLevel`, `peakLevel`, `rmsLeft`, `rmsRight`, `crestFactor`, `duration`, `isSilent`, `spectrum.subBass`, `spectrum.bass`, `spectrum.lowMid`, `spectrum.mid`, `spectrum.highMid`, `spectrum.high`.

## Suggested Modifications
1. Change oscillator waveform (Sine, Triangle, Square, Saw, Pulse)
2. Add a second oscillator at a different frequency for beating
3. Add Colorize between noise and feedback for color mapping
4. Increase feedback zoom for spiral effect
5. Add ChromaticAberration after bloom for edge distortion
6. Adjust smoothing values (0.5 punchy, 0.9 smooth, 0.95 ambient)
