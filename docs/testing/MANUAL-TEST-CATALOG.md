# Manual Test Catalog

This catalog defines manual validation for Vivid 1.0 functional areas that are not fully covered by automated tests.

## Test Session Setup

- Platform: macOS 14+ (Apple Silicon and Intel where available)
- Build: Debug and RelWithDebInfo
- Audio: at least one output device enabled
- MIDI: at least one hardware or virtual MIDI source available when running MIDI sections
- Start from a clean launch, then repeat selected cases after loading a non-trivial graph from `graphs/`

## Reporting Format

For each case, record:

- `Result`: Pass / Fail
- `Build`: commit hash + build type
- `Machine`: model + macOS version
- `Notes`: observed behavior, logs, screenshot/video if failure

## Graph Editing

### Add/Delete Nodes

- Expected behavior: nodes instantiate from palette immediately; delete removes node and connected wires.
- Pass criteria:
  - Adding a node creates it exactly once at pointer location or default placement.
  - Deleting a selected node removes it and all incident wires without UI corruption.
- Fail criteria:
  - Duplicate node creation on one action.
  - Dangling wire visuals or stale selection after delete.

### Connect/Disconnect Wires

- Expected behavior: valid ports connect, invalid type/domain combinations are rejected cleanly.
- Pass criteria:
  - Valid connection draws immediately and data propagates.
  - Disconnect action removes wire and updates downstream value.
- Fail criteria:
  - Crash/hang during drag-connect.
  - Invalid wire accepted or valid wire blocked with no reason.

### Copy/Paste

- Expected behavior: copied selection pastes with parameters and internal wiring preserved.
- Pass criteria:
  - Pasted nodes receive unique IDs.
  - Internal connections among pasted nodes are retained.
- Fail criteria:
  - ID collisions or missing parameters after paste.

### Drag/Reorder and Group Selection

- Expected behavior: single/multi-node drag updates layout consistently.
- Pass criteria:
  - Marquee selection includes only visually enclosed nodes.
  - Group drag preserves relative spacing.
- Fail criteria:
  - Nodes jump unexpectedly or selection box is offset from cursor.

## Parameter UI

### Sliders

- Expected behavior: continuous control from min to max with clamping.
- Pass criteria:
  - Value updates continuously while dragging.
  - Releasing preserves final value in graph JSON save/load round-trip.
- Fail criteria:
  - Stutter, delayed updates, or out-of-range persisted values.

### XY Pads

- Expected behavior: both axes update together and map to correct parameters.
- Pass criteria:
  - Horizontal and vertical movement affect the intended controls.
  - Values clamp at bounds and remain stable on release.
- Fail criteria:
  - Axis swap, jitter, or one axis fails to update.

### Color Picker

- Expected behavior: color changes propagate to dependent GPU operators.
- Pass criteria:
  - Visual output changes on every confirmed color change.
  - Saved graph restores exact color.
- Fail criteria:
  - Output updates only after extra interaction or color resets on reload.

### Typed Input, Dropdowns, Toggles

- Expected behavior: text input validates, dropdown/toggle state changes apply immediately.
- Pass criteria:
  - Invalid text is rejected or clamped without breaking UI.
  - Dropdown/toggle persists through save/load.
- Fail criteria:
  - UI enters invalid state or control displays value different from runtime value.

## Audio

### Audio Output and Routing

- Expected behavior: audio graph produces audible output with correct routing to AudioOut.
- Pass criteria:
  - Known-good graph produces sound within 1 second of run.
  - Muting/removing source nodes silences output immediately.
- Fail criteria:
  - Silence with valid graph, unintended feedback, or stuck output after stop.

### Sample Rate and Buffer Handling

- Expected behavior: switching device/rate/buffer does not crash and audio remains stable.
- Pass criteria:
  - Device changes apply without restarting app.
  - No persistent crackle/dropout under moderate CPU load.
- Fail criteria:
  - Crash, severe artifacts, or device lockup.

## GPU

### Rendering and Texture Flow

- Expected behavior: GPU nodes render and pass textures downstream each tick.
- Pass criteria:
  - Preview updates at runtime with no frozen intermediate.
  - Chained operators (e.g., noise -> bloom -> composite) produce expected output.
- Fail criteria:
  - Black frames, stale textures, or domain-crossing corruption.

### Shader Compile Errors

- Expected behavior: WGSL errors are surfaced and last good output remains stable.
- Pass criteria:
  - Error is visible to user (log/UI) with actionable location.
  - Runtime does not crash; graph remains interactive.
- Fail criteria:
  - Silent shader failure or app instability after compile error.

## Packages

### Install/Uninstall from UI

- Expected behavior: package install adds operators; uninstall removes them cleanly.
- Pass criteria:
  - Installed package operators appear in palette without restart.
  - Uninstall removes operator entries and artifacts.
- Fail criteria:
  - Partial install state, stale operator entries, or crash on uninstall.

## File I/O

### Save/Load, Recent Files, File Association

- Expected behavior: graphs serialize fully and reopen identically.
- Pass criteria:
  - Save then load restores nodes, wires, params, layout.
  - Recent files list opens correct document.
  - Opening via Finder association launches Vivid with selected graph.
- Fail criteria:
  - Data loss, malformed graph load, or wrong recent-file target.

## MIDI

### Input Mapping, Learn Mode, Hot-Plug

- Expected behavior: MIDI events map correctly and survive device reconnects.
- Pass criteria:
  - Note on/off and CC messages drive mapped parameters.
  - Learn mode captures expected source control.
  - Disconnect/reconnect device recovers without restart.
- Fail criteria:
  - Stuck notes, wrong channel mapping, or lost device after reconnect.

## Variations and Presets

### Save/Recall and Interpolation

- Expected behavior: variation states are stored and recalled deterministically.
- Pass criteria:
  - Recall reproduces parameter state exactly.
  - Preset file round-trip preserves values and metadata.
  - Interpolation (where supported) transitions smoothly between states.
- Fail criteria:
  - Drift between save and recall or preset incompatibility errors.

## Capture

### Screenshot and Video Capture

- Expected behavior: capture starts/stops on command and outputs valid files.
- Pass criteria:
  - Screenshot writes readable image with current frame.
  - Video capture writes playable file with expected duration and frame count.
- Fail criteria:
  - Empty/corrupt output, frame drops far beyond expected performance limits, or capture lock after stop.

## Themes

### Theme Switching and Custom Theme Load

- Expected behavior: runtime theme switch updates UI consistently.
- Pass criteria:
  - Switching built-in themes updates colors and text contrast immediately.
  - Loading valid custom theme applies without restart.
- Fail criteria:
  - Unstyled widgets, unreadable text, or partial theme application.

## Fullscreen and External Display

### Fullscreen Enter/Exit

- Expected behavior: transition is stable and input mapping remains correct.
- Pass criteria:
  - Enter/exit fullscreen works repeatedly without rendering issues.
  - Cursor/input interactions still align with visible UI and operator input space.
- Fail criteria:
  - Frozen frame, broken focus, or incorrect coordinate mapping.

### External Display Output

- Expected behavior: rendering can target projector/external monitor reliably.
- Pass criteria:
  - Output appears on selected display with correct aspect handling.
  - Disconnecting external display returns gracefully to primary display.
- Fail criteria:
  - Crash/blank output on display changes.

## Movie Playback

See [MOVIE-PLAYBACK-VALIDATION.md](MOVIE-PLAYBACK-VALIDATION.md) for the full movie playback validation checklist covering video-only and AV-synced playback, loop/once/hold-last modes, HAP and H.264/HEVC paths, seek/scrub behavior, source changes, window state, and telemetry verification.

## macOS-Specific Notes

- First-run permissions:
  - Microphone access may be required for audio input operators.
  - Camera access may be required for `WebcamIn`.
- App bundle/plugin loading:
  - Ensure `vivid.app/Contents/PlugIns/` exists in build outputs during local testing where plugin discovery depends on bundle layout.
- Gatekeeper/quarantine:
  - Downloaded package binaries may need quarantine removal in developer environments; prefer local builds for repeatable testing.
- MIDI:
  - Use Audio MIDI Setup (IAC Driver) for deterministic loopback tests when hardware is unavailable.
- Fullscreen/display spaces:
  - Validate behavior with “Displays have separate Spaces” both enabled and disabled.
