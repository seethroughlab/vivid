# Reactive-visuals cookbook

The authoring methodology for music-reactive visuals that read as **obviously caused by the sound** —
the legibility principle, the band→role convention, mechanical templates, anti-patterns, the
measure-every-change loop (`analyze_output(mode="av")`), and a dead-graph decision tree.

The full, agent-facing version is the **`reactive-visuals` skill**:
[`.claude/skills/reactive-visuals/SKILL.md`](../.claude/skills/reactive-visuals/SKILL.md). It is the
source of truth — Claude loads it automatically when authoring or tuning a reactive graph. This file
is a pointer for human browsing.

## The one-line version

Reactivity reads as *caused by the sound* only when it is **punctual** (a note-on triggers a visible
spawn/burst) or **monotonic-and-large** (bass drives an obvious scale/inflation). Subtle continuous
modulation of many params reads as generic wiggle. Band→role: **bass→scale/impact, mids→motion/color,
highs→detail/sparkle, note-ons→spawn/seed, quiet→empty** — and hit *on the beat* with the transport
pulses (`transport.beat_pulse`/`downbeat`/`bar_phase`) most demos ignore.

## Measure every change

After each reactivity edit, run `analyze_output(mode="av")` and read all three lenses — per-axis
correlation, onset response, and per-band correlation. They are complementary: a continuous-drive
visual shows high `energy_motion_correlation` and low `onset_response_rate`; a punctual/percussive one
shows the reverse. Neither pattern is "dead". See the skill for the trustworthy thresholds and the
dead-graph decision tree.

Origin: adapted from the `vivid-classic` branch's `docs/COMPOSITION-GUIDE.md`, updated for main's 3D
scene-graph vocabulary and the reliable single-call `analyze_output` perception loop.
