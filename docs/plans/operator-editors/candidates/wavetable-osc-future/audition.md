# Audition button

## What it is

A "play a note" button in the editor window that triggers the oscillator at a reference pitch (e.g. C4, 440Hz-ish) for a configurable duration (default ~500ms). Lets the user hear their edits immediately without needing the operator wired into a running graph.

Cthulhu has it. Serum has it. Vital has it. Every modern synth editor has a version. The gesture varies (dedicated button, space bar, MIDI input passthrough) but the effect is the same: **make the editor self-sufficient for iterative authoring**.

## Why deferred from v1

Audition isn't just an editor feature — it's a platform extension. The editor needs to push an audio trigger into the operator's input lanes (frequencies + gates) without going through a user-built graph, and the operator's output has to route to the speakers. Today's `VividEditorContext` has no host-side audio pathway; adding one is a design decision that affects every future editor, not just WavetableOsc's.

So we shipped v1 without audition and left it as a platform item.

## What needs to happen in the editor context

New host callback, something like:

```cpp
typedef struct VividEditorHostAPI {
    // … existing clipboard/cursor/focus/etc …

    // Trigger a one-shot audition note through the node's inputs.
    // The host fabricates the input lane state and routes the operator's
    // output to the audio bus for `duration_ms`, then releases.
    // `pitch_hz` drives the frequencies lane; `velocity_01` drives velocities.
    // Guards on null — pass 0 as duration to cancel any playing audition.
    void (*audition_note)(void* opaque,
                          float pitch_hz,
                          float velocity_01,
                          uint32_t duration_ms);
    int  (*is_auditioning)(void* opaque);
} VividEditorHostAPI;
```

Host-side work:
- Sub-graph injection: create a minimal audio path (operator → audio_out) when the editor requests audition. Tear down when done.
- Virtual input lanes: fabricate `frequencies = {pitch}`, `gates = {1}`, `velocities = {velocity}` for the operator's input ports.
- Timing: release gate after `duration_ms`, allow tail-out.

This is a real chunk of host work — probably 4–6 hours inside the runtime's editor-window manager, independent of any editor.

## Why do it at the platform level

Once `host.audition_note` exists, every synth-class operator benefits:
- WavetableOsc, AnalogOsc, SubOsc, SampleSlice, Granular
- Future ChildOp<Synth> composites
- Any future `vivid-drums` voice modules

Building it once, well, pays back across the catalog.

## Editor-side cost (post-platform)

Trivial — a button in the top strip that calls `ctx->host.audition_note(...)`. ~20 lines including hover state.

## Interactions

- **[frame-stack-visualization](frame-stack-visualization.md)** — auditioning while watching the position highlight is the real authoring flow.
- **[live-monitoring](live-monitoring.md)** — once we have the output scope, audition drives it directly. Output scope without audition only works when the graph is running; audition without output scope works but leaves the user hearing-without-seeing.

## Scope cuts

- **MIDI keyboard passthrough**: Serum lets the user type keys to pitch audition notes. Useful but separate; defer.
- **Sustain mode**: hold the button to sustain. Can be implied by a long `duration_ms` plus a manual cancel; nicer UX later.
- **Per-operator default pitch**: probably doesn't need to be configurable; C4 / 440Hz-ish / velocity 0.8 is fine for all synths.

## Test plan

Hard to test audition end-to-end without a real audio device. What we *can* test:
- Button click → `host.audition_note(...)` callback fires with the expected args.
- Null-host safety — editor doesn't crash when `host.audition_note` is null (tests always pass a null, so this is free coverage).
- Pure-logic: the pitch/velocity/duration defaults used by the button.

Actual audio-path correctness is covered at the host level once we build it.
