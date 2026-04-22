# Per-voice unison editing

## What it is

v1 unison is **symmetric**: `unison_spread` and `unison_stereo` are global; every voice's detune and pan are derived from its index via the static helpers (`unison_detune_offset`, `unison_pan_position`). You can't, today, say "voice 3 is +12 cents sharp of center and hard-right, while voice 4 is -3 cents and center."

Per-voice editing would expose each voice's detune and pan as individually writable params, letting the user hand-place each voice in the scatter. Cthulhu-style.

## Why deferred from v1

v1 decides the unison scatter shows voices *at their computed positions* — you can drag to adjust the symmetric `spread`/`stereo`, but individual voices are locked. Breaking that symmetry means:

1. Adding `unison_detune_N` (N=0..15, cents) and `unison_pan_N` (N=0..15, -1..+1) params — 32 new params.
2. A toggle `unison_mode` (Symmetric | Per-voice) so existing graphs keep working.
3. Engine dispatch in `voice_mixer` / `wavetable_osc_process` to use per-voice values when the mode says so.

That's a real operator-surface change, and it needs a backward-compat plan. Not a small editor feature.

## Engine cost

**~2 hours**:
- 32 new params (`unison_detune_0..15`, `unison_pan_0..15`), hidden in the default inspector, defaulting to sentinel values (`FLT_MAX`, say) meaning "use symmetric formula."
- Add `unison_mode` param with values `SYMMETRIC | PER_VOICE`. Default symmetric.
- In the voice dispatch: if `unison_mode == PER_VOICE`, use `unison_detune_N` (fall back to the symmetric formula if sentinel); same for pan.
- Test: symmetric mode matches today's output exactly; per-voice mode applies the overrides.

Param-index stability: the 32 new params append past the existing 27; `unison_mode` appends too. No existing index moves.

## Editor cost

**~4 hours**:
- Scatter becomes directly writable: drag voice N → writes `unison_detune_N` + `unison_pan_N`. Automatically switches `unison_mode` to `PER_VOICE`.
- Voices draw at their per-voice positions when in per-voice mode (falling back to symmetric positions for voices still at sentinel).
- Mode indicator (small label near scatter): "Symmetric" or "Per-voice · 3 overrides."
- "Reset voice" gesture (double-click, or cmd-click): reset voice N to sentinel (rejoins symmetric layout).
- "Reset all voices" button: flip mode back to SYMMETRIC, clear all sentinels.

## Interactions

- The scatter in v1 is the perfect launch pad — the gesture is already there; per-voice editing just makes drags persistent per-voice rather than global.
- **[frame-stack-visualization](frame-stack-visualization.md)** is independent.
- **Polish: scatter voice tooltips** (in top-level README) — hover a voice to see its exact detune and pan, including whether it's using symmetric or override. Ship those together.

## Scope cuts

- **Per-voice phase offset**: Cthulhu-ish feature. Adds 16 more params. Defer unless someone asks.
- **Per-voice gain/volume**: 16 more params. Defer; useful mostly for sound-design experiments.
- **Per-voice wavetable pointer** (different voices, different tables): huge operator change. Out of scope.

## Test plan

- Pure-logic: voice dispatch in both modes. Given `unison_mode = SYMMETRIC`, assert layout matches `unison_detune_offset()` etc. Given `unison_mode = PER_VOICE` with specific overrides, assert the overridden positions are used.
- End-to-end: drag a voice in per-voice mode → captured `set_param("unison_detune_3", …)` + `set_param("unison_mode", PER_VOICE)`. Double-click voice → `unison_detune_3` goes back to sentinel.
- Backward compat: load a graph saved pre-expansion; assert voice layout is unchanged (all overrides at sentinel, mode symmetric).
