# Vivid Dev Tools: Philosophy & Direction

## The Shift

Vivid's built-in tooling was originally conceived as a self-contained development environment — a code editor, terminal, chain visualizer, and parameter controls all living inside the application. The assumption was that the developer would work *inside* Vivid.

That assumption is becoming obsolete. The developer increasingly works inside an LLM-assisted environment — Claude Code, Cursor, OpenClaw, or whatever comes next. These tools already have world-class editors, terminals, file management, and version control. Building a second, worse version of all that inside Vivid is effort spent competing with teams of hundreds on a problem that's already solved and getting better fast.

What those external tools *cannot* provide is real-time visual and audio feedback during a live session. They can't show you the chain graph with live thumbnails while you're performing. They can't give you a parameter surface to tweak on stage. They can't tell you at a glance that your feedback loop is decaying too fast or your audio is clipping.

**The new principle: Vivid's built-in tools serve the performer and the live debugger, not the developer.**

Development happens outside Vivid. Performance, monitoring, and real-time interaction happen inside it.

## What to Remove

### Code Editor

The integrated code editor should be removed entirely. It will never match the capabilities of VS Code, Cursor, or a terminal-based editor, and in an agent-driven workflow nobody is hand-editing code inside Vivid anyway. The hot-reload system doesn't need an internal editor — it watches the filesystem and rebuilds when `chain.cpp` changes, regardless of what changed it.

### Integrated Terminal

Same reasoning. The terminal is a general-purpose tool that external environments do better. Vivid doesn't need to be a terminal emulator. If a user needs to run a command, they have a terminal already open — it's where they launched Vivid from.

### Any File Management UI

File browsers, project trees, file creation dialogs — all of this belongs to the external development environment. Vivid should know where its project lives and load from there. It doesn't need to help you navigate your filesystem.

## What to Keep

### Chain Visualizer (ImGui/ImNodes)

This is Vivid's most distinctive UI feature and it serves the performer directly. Seeing the operator graph as connected nodes with live thumbnails is something no external tool can replicate — it's real-time GPU output rendered inline. This stays and should be treated as a first-class feature.

### Performance Overlay

FPS, frame time, resolution, GPU memory — this is essential for live performance monitoring and debugging. A performer needs to know immediately if they're dropping frames. This stays.

### Keyboard Shortcuts for Live Control

`Tab` for visualizer, `F` for fullscreen, `V` for vsync, `Esc` to quit. These are the performer's interface. They stay and could be expanded.

## What to Enhance

### Parameter Control Surface

The current parameter display shows values on nodes. This should evolve into an interactive control surface — a dedicated panel (toggled with a hotkey) where the performer can tweak exposed parameters in real-time via sliders, knobs, or XY pads. Think of it as Vivid's equivalent of a MIDI control surface, but on screen.

Design considerations:

- Only parameters explicitly marked as "performable" in the chain should appear. Not every `float` on every operator — the author curates what's exposed.
- Group by purpose, not by operator. "Color," "Motion," "Audio" sections make more sense on stage than "noise operator," "feedback operator."
- Support MIDI mapping so physical controllers can bind to these same parameters. The on-screen surface and a MIDI controller should be interchangeable views of the same parameter state.
- Large, high-contrast UI elements. This will be used in dark rooms, possibly at a distance.

### Chain Health Monitoring

Extend the chain visualizer with live health indicators on each node — a quick visual language for "this operator is behaving normally" vs. "something is wrong." This directly supports the performer/debugger role.

Possible indicators:

- **Thumbnail border color** — green when output has signal, yellow when output is nearly black/silent, red when producing NaN/inf or completely static for too long.
- **Audio level meters** on audio operator nodes — tiny inline VU meters so you can see signal flow through the audio chain at a glance.
- **Frame delta indicator** — a small sparkline or dot that pulses when the operator's output is changing. A static dot means the output hasn't changed in several frames, which might indicate a stalled feedback loop or a generator that stopped.

This is essentially a live version of the `inspect()` data from the LLM introspection plan, but rendered visually for human consumption instead of structured as JSON for machine consumption. Same underlying data, different presentation.

### Preset / Snapshot System

For live performance, the ability to save and recall parameter states instantly is critical. A performer should be able to:

- Save the current state of all performable parameters as a named snapshot.
- Recall a snapshot instantly (hard cut) or interpolate to it over a duration (crossfade).
- Organize snapshots into a setlist or cue sequence.
- Trigger snapshots via keyboard shortcuts or MIDI.

This is explicitly a *performer* feature, not a development feature. It doesn't need undo/redo or version history — that's what git is for.

### Output Recording

A simple "record" toggle that captures the live output to disk — both video and audio — without stopping the performance. This isn't the same as the headless `vivid export` command (which is for offline/agent use). This is a live capture for archiving performances or grabbing a segment to review later.

Implementation could be as simple as piping frames to ffmpeg in a background thread, which the dev tools export path may already support.

### Console / Log Overlay

While the full terminal should be removed, a read-only log overlay is valuable for the live debugger. Hot-reload status ("Recompiled in 340ms"), warnings ("Feedback energy dropping below threshold"), assertion failures if `vivid-assertions.yaml` is present, and operator errors should be visible as a scrolling log that can be toggled on/off.

This is not an interactive terminal. You can't type into it. It's a heads-up display for the runtime.

## Summary

| Feature | Action | Rationale |
|---------|--------|-----------|
| Code editor | **Remove** | External tools do this better; agents don't need it |
| Integrated terminal | **Remove** | General-purpose tool that doesn't belong inside Vivid |
| File management UI | **Remove** | Belongs to the external dev environment |
| Chain visualizer | **Keep** | Core performer/debugger tool, unique to Vivid |
| Performance overlay | **Keep** | Essential for live monitoring |
| Live keyboard shortcuts | **Keep** | Performer's primary interface |
| Parameter control surface | **Enhance** | Evolve from display-only to interactive, MIDI-mappable |
| Chain health monitoring | **Add** | Visual inspect() for humans — live node health indicators |
| Preset/snapshot system | **Add** | Critical for live performance cue management |
| Output recording | **Add** | Live capture for archiving and review |
| Console/log overlay | **Add** | Read-only HUD replacing the interactive terminal |

## The Two Audiences

After this refocus, Vivid's tooling cleanly serves two audiences through two interfaces:

**The Performer** uses Vivid's built-in UI — the chain visualizer, parameter surface, presets, keyboard/MIDI controls, and output recording. This is the runtime experience during a show or a creative session.

**The Developer** (human or LLM agent) uses external tools for code editing and Vivid's CLI commands (`vivid inspect`, `vivid check`, `vivid export`) for validation and preview. The development experience is entirely outside the Vivid window.

These two paths share the same underlying architecture — the `inspect()` data feeds both the chain health indicators and the JSON reports, the parameter system serves both the on-screen sliders and the `param_set`/`param_ramp` events in puppeteer scripts, and the recording system shares encoding infrastructure with the headless export pipeline. Building for one audience directly benefits the other.
