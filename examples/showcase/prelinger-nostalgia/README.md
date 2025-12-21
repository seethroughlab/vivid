# Prelinger Nostalgia

Boards of Canada-inspired audio-visual piece combining:
- **PolySynth** with rich 6-note chord voicings (Am9, Fmaj7, Dm7, Em7)
- **TapeEffect** for analog warmth (wow, flutter, saturation, hiss)
- **FilmGrain** and **CRT** effects for vintage film aesthetic

## Quick Start

```bash
# Run with procedural visuals (no download needed)
./build/bin/vivid examples/showcase/prelinger-nostalgia

# Or download public domain footage first
cd examples/showcase/prelinger-nostalgia
./download-videos.sh --quick   # Downloads one ~35MB video
./download-videos.sh           # Downloads all curated videos (~500MB)
```

## Controls

| Key | Action |
|-----|--------|
| 1-4 | Switch mood/chord |
| SPACE | Pause/Resume |
| G | Toggle film grain |
| C | Toggle CRT effect |
| TAB | Show parameters |

## Video Sources

The `download-videos.sh` script fetches curated public domain films from the [Prelinger Archive](https://archive.org/details/prelinger) - educational and industrial films from the 1930s-1950s that work well with this nostalgic aesthetic.

To use video instead of the procedural noise, edit `chain.cpp` and replace the `Noise` operator with `VideoPlayer` (see comments in file).

## Audio-Visual Connections

- Chord changes every 8 seconds with smooth envelope transitions
- TapeEffect parameters drift slowly for organic feel
- Visual hue shifts subtly with each mood
- Film grain and brightness flicker for vintage authenticity
