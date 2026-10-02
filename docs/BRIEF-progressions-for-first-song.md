# Brief: progression tools for the first song

**Why.** The next goal for Vivid is a finished song, not more framework. Melodic IDM in the vein of
Kettel, Proswell, Ochre, Ulrich Schnauss: chord progressions that carry a narrative. Jeff wants to
experiment with progressions inside Vivid. This brief is only what that song needs. **Hard stop:
when an 8-bar progression with extended chords plays smoothly through a pad, stop building.**

## What already exists (do not rebuild)
- `mcp/theory.py` + `set_progression` in `mcp/vivid_mcp.py`: roman numerals in a key, borrowed
  chords (`bVII`), diatonic 7ths (`V7`), chord symbols with maj7/m9/add9/sus/slash bass, voicings
  `close`/`open`/`drop2`, persisted session key (`set_key`). See `docs/music-theory-tools.md`.
- `ChordOp` in `app/src/audio/builtin_audio_ops.cpp`: repeats ONE chord (5 qualities). Not a
  progression tool.

## Gaps (the work)
1. **Roman numerals don't take extensions.** `roman()` only knows a trailing `7`. Make it accept
   the same qualities `chord()` does: `IVmaj7`, `vi9`, `ii11`, `Iadd9`, `Vsus4`, `bVIImaj7`,
   `bVImaj7`. Today `bVII7` is forced to a dominant 7th and `bVIImaj7` is impossible.
2. **`voicing` is ignored when `key` is given.** `set_progression` only applies `voicing` to
   chord symbols. Apply it to roman numerals too.
3. **No voice leading.** Add `voice_lead: bool` (default true for progressions): keep the first
   chord as voiced, then choose each next chord's inversion/octave to minimise total voice movement
   from the previous one (keep common tones; keep the lowest note within a sensible range, e.g.
   C3–C5). Optional `bass: bool` that adds the chord root an octave or two below as its own voice.
   This is the single biggest factor in the "smooth pad" sound.
4. **Tests** in `mcp/test_theory.py`: `IVmaj7` in C = F A C E; `bVIImaj7` in C = Bb D F A;
   voice-led `IVmaj7 V iii vi` moves no voice more than a 4th between adjacent chords.

## Out of scope for now
- An in-app Progression operator (UI for editing chords by hand). Only build it if experimenting
  through the MCP tools turns out to be too slow during the actual song. If so, port the quality and
  voicing logic from `~/Developer/chord_box/src/ChordEngine.cpp` rather than writing it fresh.
- Chord-aware arps, rhythm patterns per chord, 3/4, progression suggestions.

## Done means
Jeff can say "8 bars in D: IVmaj7 V iii vi, then bVIImaj7 IV ii9 Iadd9, voice-led, open voicing,
1 bar each" and hear it on a pad track in Vivid.
