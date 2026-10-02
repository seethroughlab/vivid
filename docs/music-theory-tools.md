# Music-theory MCP tools

The Vivid MCP bridge (`mcp/vivid_mcp.py`) exposes a music-theory layer so an agent can author
by musical intent — chords, scales, grooves — instead of raw MIDI. It's a zero-dependency
pure-Python engine (`mcp/theory.py`) that composes down to the app's `set_clip`/`get_clip`
control endpoints. `mcp/test_theory.py` is the dependency-free correctness suite
(`uv run --directory mcp test_theory.py`).

**Pitch convention:** C4 = middle C = MIDI 60. Anywhere a pitch is taken, `p` accepts a MIDI
int *or* a name: `C4`, `F#3`, `Bb5`, `Db2` (sharps `#`/`s`, flats `b`).

## Foundation
| tool | what |
|------|------|
| `get_clip(track, scene)` | read a clip back → `{notes:[{p,s,d,v}], length}` |
| `add_notes(track, scene, notes)` | **append** notes (set_clip is full-replace) |
| `clear_clip(track, scene)` | empty a clip |

## Harmony
- `add_chord(track, scene, symbol, beat, dur, octave, inversion, voicing)` — **append** a chord.
- `set_progression(track, scene, chords, beats_per_chord, octave, voicing, key, scale, voice_lead, bass)` —
  **replace** with a progression.

**Chord symbols:** root (`C`, `F#`, `Bb`) + quality + extensions + optional `/bass`.
Qualities/extensions: `maj` (or none), `m`/`min`/`-`, `dim`/`°`, `aug`/`+`, `sus2`, `sus4`,
`6`, `m6`, `7`, `maj7`/`M7`/`Δ`, `m7`, `m7b5`/`ø`, `dim7`/`°7`, `9`, `maj9`, `m9`, `add9`,
`11`, `m11`, `13`, `maj13`, `m13`, `7sus4`. Slash bass: `C/G`, `Dm7/A`.
Voicings: `close` (default), `open`, `drop2`; `inversion` = 0,1,2,…

**Roman numerals** (when `set_progression(key=…)` is given): `I ii iii IV V vi vii`.
- A **bare** numeral, optionally with a trailing `7`, is diatonic: the scale sets the quality by
  stacking thirds (`V7` = dominant, `IV7` = maj7, `vii` = dim in major).
- **Any other suffix** names the quality with the chord-symbol vocabulary above: `IVmaj7`, `vi9`,
  `ii11`, `Iadd9`, `Vsus4`, `viiø7`. The numeral's **case sets the third**: lower case means minor
  (`vi9` = Am9 in C) unless the suffix already sets it (`dim`, `°`, `ø`, `m7b5`, `sus`, `aug`).
- An **accidental** prefix marks a borrowed chord on the chromatic degree, with quality from case +
  suffix: `bVII` = Bb, `bVII7` = Bb7 (dominant), `bVIImaj7` = Bbmaj7, `bvi` = Abm.

**Voice leading** (`voice_lead=True`, the default): `voicing` shapes the **first** chord only; each
later chord is re-voiced to move least from the one before. Common tones are held, the lowest note
stays in C3–C5, and adjacent semitone clusters are avoided. This is what makes a pad sound smooth.
`voice_lead=False` gives block chords, each voiced independently. `bass=True` adds the root (or the
slash bass) as its own voice in C2–B2. A slash bass always stays underneath and is never voice-led.

```
set_progression(track=0, scene=0, key="D", octave=3, voicing="open", beats_per_chord=4,
                chords=["IVmaj7","V","iii","vi","bVIImaj7","IV","ii9","Iadd9"])
```

## Scales & key
- `set_key(root, scale)` / `get_key()` — a session context the tools below default to (**ephemeral** v1).
- `get_scale(root, scale)` — the scale's MIDI notes + names.
- `quantize_to_scale(track, scene, root, scale)` — snap off-key notes into the scale.

**Scales:** `major`, `minor`/`natural_minor`, `harmonic_minor`, `melodic_minor`, the modes
(`ionian dorian phrygian lydian mixolydian aeolian locrian`), `pentatonic_major`,
`pentatonic_minor`, `blues`, `whole_tone`, `chromatic`.

## Transforms (read-modify-write on a clip)
- `transpose(track, scene, semitones)`
- `arpeggiate(track, scene, chord, pattern, rate, octaves)` — pattern `up|down|updown|downup`.
- `harmonize(track, scene, degree)` — add a diatonic voice `degree` scale-steps away (2 = a third).
- `invert_clip(track, scene, axis)` · `retrograde_clip(track, scene)`

## Rhythm
- `set_drum_pattern(track, scene, patterns, bar_beats, bars)` — **replace** with a kit pattern.
- `euclidean_fill(track, scene, drum, pulses, steps, rotation)` — **append** a Euclidean rhythm.
- `humanize(track, scene, timing, velocity)` · `quantize_rhythm(track, scene, grid)`

**Drum names** (→ GM notes): `kick`(36) `snare`(38) `rim`(37) `clap`(39) `hat`(42)
`openhat`(46) `tom_lo`(45) `tom_mid`(47) `tom_hi`(50) `crash`(49) `ride`(51) `cowbell`(56)
`tambourine`(54) — or a raw MIDI int.
**Step-strings:** one char per step, spread across `bar_beats`: `x` = hit, `1`–`9` = velocity,
`.`/`-`/` ` = rest. e.g. `"x..x..x."` (tresillo), `"9..5..7."` (accents).
**Euclidean:** `E(pulses, steps)` spreads hits maximally evenly — `E(3,8)` = `x..x..x.`.

## Analysis (heuristic)
- `analyze_clip(track, scene)` → `{key:{root,scale,confidence}, chords:[…per bar], range, note_count}`
  (Krumhansl–Schmuckler key detection + best-fit chord per bar). Short/ambiguous clips are unreliable.

## Notes / limits
- **Replace vs append:** `set_clip`/`set_progression`/`arpeggiate`/`set_drum_pattern` replace the
  whole clip; `add_notes`/`add_chord`/`euclidean_fill` append.
- Note tools apply to **MIDI tracks** only (not the audio/sampler track).
- The engine is fixed at **4/4** (`bar_beats` defaults to 4); the `set_key` context is bridge-side
  and does not persist with the session. Both are candidate follow-ups.
