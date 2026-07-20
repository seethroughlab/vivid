# Surge Lead

A bundled **musical + audio-reactive** example (**File > Open Example**). A
**Surge XT** instrument plays a lead melody; its master analysis (`transient` /
`low` / `high`) drives a **ShapeGrid**'s size, rotation and colour through the
mapping bridge — sound shaping the visuals, Vivid's core thesis.

**Requires Surge XT installed** (the free synth, https://surge-synthesizer.github.io;
here the CLAP build at `/Library/Audio/Plug-Ins/CLAP/Surge XT.clap`). Press play
after opening. The instrument loads asynchronously — give it a moment to sound.

Note: because a plugin instrument on a graph track doesn't yet build clip-note
routing on its own, the track keeps a muted native instrument as the note-graph
anchor (`midi_clip → selector`), with the selector's notes wired into Surge.
