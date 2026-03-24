# Manual Validation Results

## Session Info

- Build: `81893d52` (RelWithDebInfo)
- Machine: Apple M4 Max, macOS 26.3.1
- Date: 2026-03-24

## E1. GPU Rendering and Texture Flow

| Case | Result | Notes |
|------|--------|-------|
| Chained GPU operators render | Pass | Previews update live, no frozen frames or stale textures |
| Shader compile error surfacing | Pass | WGSLFilter showed error in UI, app stayed running |
| Cmd-F fullscreen | **Fail** | No response — fullscreen not functional |
| Save behavior | **Fail** | Cmd-S silently overwrites `default_graph.json` with no prompt |

## E2. Audio Output and Routing

| Case | Result | Notes |
|------|--------|-------|
| Basic audio output | Pass | Oscillator -> gain -> audio_out produces sound immediately |
| LFO amplitude adjustment | **Fail (blocker)** | Adjusting LFO amplitude kills audio permanently — does not recover when reduced back |
| LFO thumbnail overflow | **Fail (UX)** | Thumbnail waveform renders at full screen height when amplitude is large |

## E3. Graph Editing

| Case | Result | Notes |
|------|--------|-------|
| Add/Delete nodes | Pass | Clean instantiation and removal |
| Connect/Disconnect wires | Pass | Data propagation starts/stops correctly |
| Copy/Paste | Pass | Unique IDs, internal wiring preserved |
| Drag/Group select | Pass | Relative spacing maintained |
| Cellular Automata operator | **Fail (blocker)** | App crashes (SIGABRT) on add — wgpu-native panics on texture usage validation error in `wgpuQueueSubmit` → `GpuContext::end_frame` |

## E4. File I/O

| Case | Result | Notes |
|------|--------|-------|
| Save | **Fail** | No "Save As" — always overwrites `default_graph.json` |
| Default graph protection | **Fail** | Users can accidentally overwrite shipped `default_graph.json` |
| Title bar | **Fail (UX)** | Shows "default_graph.json" for new sessions — should show "Unsaved Document" |
| Load/Recent files | Not tested | Blocked by save issues |
| Finder association | Not tested | Blocked by save issues |

## E5. Parameter UI

| Case | Result | Notes |
|------|--------|-------|
| Sliders | Pass | Continuous update while dragging |
| Color Picker | Pass | GPU output updates on color change (tested with Shape) |
| Typed Input | Pass | Values accepted on Enter |
| Dropdowns/Toggles | Pass | Immediate application |

## E6. MIDI

| Case | Result | Notes |
|------|--------|-------|
| All cases | Deferred | No MIDI source available for this session |

## E7. Capture

| Case | Result | Notes |
|------|--------|-------|
| Screenshot | Pass | Readable image written |
| Video recording | Pass | Playable output file produced |
| LFO modulation in feedback_demo | **Fail** | LFOs not modulating Shape operator — confirms systemic LFO issue from E2 |

## E8. Packages

| Case | Result | Notes |
|------|--------|-------|
| Install vivid-3d | **Fail** | Spinner never completes (3+ min wait), then app refuses to quit — force quit required |
| Install vivid-plexus | **Fail** | CMake error shown but text is not wrapped — runs off right edge of screen |
| Build progress feedback | **Fail (UX)** | No live build output or progress indication during package install |
| Long error display | **Fail (UX)** | Error messages need text wrapping or scrollable view |
| Uninstall | Not tested | No package successfully installed |

## E9. Themes

| Case | Result | Notes |
|------|--------|-------|
| Built-in theme switching | Pass | Immediate color update |
| Custom theme load | Pass | Applies without restart |

## E10. Fullscreen / External Display

| Case | Result | Notes |
|------|--------|-------|
| Fullscreen | **Fail** | No working method (Cmd-F, menu, window button) |
| External display | Deferred | No external monitor available |

## Summary

| Area | Verdict |
|------|---------|
| GPU Rendering | Pass |
| Audio Output | Pass (basic), Fail (LFO modulation) |
| Graph Editing | Pass (basic), Fail (Cellular Automata crash) |
| File I/O | **Fail** — save workflow fundamentally broken |
| Parameter UI | Pass |
| MIDI | Deferred |
| Capture | Pass |
| Packages | **Fail** — install hangs, error display broken |
| Themes | Pass |
| Fullscreen | **Fail** — not functional |

## Release Blockers Found

1. **Cellular Automata crash** — SIGABRT on add (wgpu-native texture validation panic in `wgpuQueueSubmit`)
2. **LFO amplitude kills audio permanently** — audio engine enters unrecoverable state when LFO amplitude drives frequency out of range
3. **No Save As / save overwrites default graph** — users lose work and can corrupt shipped assets

## Required Fixes Before Release

1. Fix Cellular Automata texture usage conflict or make `gpu_submit` survive wgpu validation errors gracefully
2. Fix LFO amplitude → audio recovery (clamp frequency, or recover audio engine from bad state)
3. Implement Save As dialog, protect default_graph.json from overwrite, show "Unsaved Document" in title bar
4. Implement fullscreen toggle (Cmd-F or menu item)

## UX Issues (Non-Blocking But Should Fix)

1. LFO thumbnail overflow — clip waveform rendering to node bounds
2. Package install progress — show live build output instead of indefinite spinner
3. Package error display — wrap long error messages, make scrollable
4. Package install must not block app quit
