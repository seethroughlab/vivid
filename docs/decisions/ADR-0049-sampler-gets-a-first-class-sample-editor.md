# ADR-0049: Sampler Gets a First-Class Sample Editor

Status: accepted (implemented — #250–#255)

Date: 2026-08-02

## Context

Vivid has two related but uneven sample workflows:

1. **Audio clips** are edited in the docked waveform `ClipEditor`. They have waveform bins, trim/loop,
   warp mode, pitch, transients, warp points, slices, and slice-to-MIDI.
2. **Sampler instrument nodes** can load PCM into a native `Sampler` op and play either one pitched region
   across the keyboard or one sliced region per key. The node exposes parameters (`base_note`, `gain`,
   `gate`, ADSR, voices, transpose, tune) and a thumbnail waveform/playhead, but there is no first-class
   instrument editor for root note, key zones, slices, playback mode, envelope, audition, or sample
   replacement.

This creates a usability split. Audio clip editing has a rough but tangible waveform editor. Sampler has a
small node preview and parameter rows, so users cannot easily tell what sample is loaded, what part of it
will play, which key maps to which slice, whether the sample is one-shot/gated, or what is movable.

Ableton separates the clip Sample Editor from instruments, but its lesson still applies: the waveform is a
direct-manipulation editing surface with visible markers, regions, and controls. Vivid should apply that
same clarity to Sampler because slicing audio into MIDI tracks and loading samples into Sampler are central
creative workflows.

## Decision

Make Sampler a first-class edited instrument with its own dock/editor view, sharing the waveform interaction
language established by ADR-0048.

Opening a Sampler node should show a **Sampler Editor** in the bottom detail area. The editor must expose:

- Loaded sample identity and replacement/drop target.
- Waveform overview with playhead and audition.
- Root note / base note as a visible piano-key marker.
- Key range and region mapping.
- Slice boundaries when the sampler is in sliced/drum-rack mode.
- Per-slice audition and selected-slice state.
- One-shot vs gated playback.
- ADSR envelope, gain, transpose, tune, and voice count in a compact inspector.
- Clear actions: load/replace sample, detect slices, clear slices, slice to MIDI, normalize/gain, reverse
  or crop later if/when destructive processing is supported.

The Sampler Editor is not a replacement for the audio clip waveform editor. It is the instrument-facing
view of the same material. Audio Clip Editor answers "how does this clip play in time?" Sampler Editor
answers "how does this sample respond to notes?"

## Design Principles

- **One waveform language.** Trim handles, slice markers, warp/transient markers, playhead, and selected
  regions should look and behave consistently between audio clips and sampler editing.
- **Visible mapping.** A user should be able to see, without reading docs, whether the sample spans the
  keyboard or has one slice per key.
- **Audition everywhere.** Clicking a piano key, slice marker, region, or playhead-relevant control should
  make it obvious what sound will be triggered.
- **No magic conversion.** `slice to MIDI` is an explicit action with a button/menu item and a preview of
  the resulting key/slice mapping.
- **Non-destructive by default.** Crop, reverse, normalize, and destructive save-to-file behaviors require
  explicit future decisions. The first editor manipulates playback/mapping state.

## Implementation Plan

1. Extend the sampler data surface beyond `SamplerPreviewable::copy_peaks()` and `playhead()` so the UI can
   read loaded-sample identity, region starts/ends, root note, key ranges, and slice count.
2. Add a `SamplerEditor` or a sampler mode of the shared clip/sample editor substrate. Prefer a shared
   waveform component so audio clips and Sampler do not drift.
3. Route selected Sampler nodes to the editor dock from the audio node graph.
4. Replace the current "Sampler = node thumbnail + generic param rows" experience with waveform, mapping,
   and envelope controls.
5. Keep generic param rows available as a fallback/detail panel, but make the purpose-built editor the main
   interaction.
6. Add manual QA scenes for direct-loaded melodic sample, sliced drum-rack sample, and audio-clip
   slice-to-MIDI round trip.

## Relationship to MIDI Clip Editing

Sampler and MIDI Clip editing should become a paired workflow:

- Audio clip waveform: detect or place slices.
- Sampler editor: verify slice-to-key mapping and playback envelope.
- MIDI clip editor: edit the generated notes that trigger the slices.

This is the Vivid version of an Ableton-style audio-to-MIDI/sampler workflow: audio material, instrument
mapping, and MIDI triggering are adjacent and legible, but each editor has a distinct job.

## Alternatives Considered

- **Keep Sampler as generic params plus thumbnail.** Rejected. It hides the most important information:
  sample contents, region mapping, and what notes trigger.
- **Fold all Sampler controls into the audio clip editor.** Rejected. Audio clips are timeline playback;
  Sampler is note-triggered playback. They share waveform components but need different inspectors.
- **Build a full Ableton Simpler/Sampler clone immediately.** Rejected. The first goal is clear mapping and
  direct manipulation for Vivid's existing Sampler, not a full workstation sampler.

## Consequences

- Sampler needs a richer UI/read API, not just audio-thread playback params.
- The waveform editor should become a reusable component, reducing divergence between audio clips and
  Sampler.
- Slice-to-MIDI becomes understandable because the generated MIDI clip can be traced back to visible slice
  regions and key mappings.
- This work should happen after or alongside ADR-0048's control cleanup, because the same affordance
  problems affect both surfaces.

## References

- Ableton Live 12 Manual: Clip View - https://www.ableton.com/en/manual/clip-view/
- Ableton Live Manual: Audio Clips, Tempo, and Warping - https://www.ableton.com/en/live-manual/11/audio-clips-tempo-and-warping/
- Code: `app/src/audio/builtin_audio_ops.cpp`, `app/src/audio/sampler_op.h`,
  `app/src/audio/audio_op_runtime.cpp`, `app/src/ui/audio_node_graph.cpp`,
  `app/src/ui/clip_editor.cpp`, `app/src/audio/vst3_host.cpp`
