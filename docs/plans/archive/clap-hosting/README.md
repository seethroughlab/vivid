# CLAP Plugin Hosting

## Context

Vivid's native audio operators are a long-term investment, but building sounds that compete with 20 years of commercial synthesis is a multi-year effort. CLAP hosting unblocks the music-making story now: users bring plugins they already own (Surge XT, Diva, Valhalla, Serum, etc.), and Vivid provides the graph, modulation, and visual layer.

CLAP is preferred over VST3: MIT license (no Steinberg fees), pure C ABI (stable, simple to host), explicit thread-safety contracts, and native per-voice modulation that maps cleanly to Vivid's control architecture.

This does **not** replace native operators — it runs alongside them. A user can mix CLAP instruments with Vivid's built-in synths in the same graph.

## Prior Research

A thorough prior investigation lives in git history at commit `20b03887` (file `CLAP_HOST.md`). It covers the CLAP protocol, lifecycle, event model, class interfaces, and a phased implementation plan. **Read it before starting.** The key difference from that document: it was designed for an older `vivid-audio` chain model. The current architecture uses the standard Vivid operator model — dylibs, `AudioProcessable`, the operator API.

## Operators to Build

### `clap_instrument`
- **Domain:** Audio
- **Inputs:** MIDI/note events (via the standard note-input port already used by existing synths)
- **Outputs:** `audio_float` (stereo)
- **Params:** `plugin_path` (string), `plugin_id` (string, for bundles with multiple plugins), `plugin_state` (opaque blob, persisted as base64 in graph JSON)
- All plugin parameters are exposed as dynamic Vivid params — they appear in the inspector, are automatable, and accept mod assignments

### `clap_effect`
- **Domain:** Audio
- **Inputs:** `audio_float` (stereo)
- **Outputs:** `audio_float` (stereo)
- **Params:** Same as above
- Transparent passthrough; plugin processes the audio buffer in-place

## Architecture

```
Runtime
├── CLAPHost (singleton, lives in runtime core)
│   ├── Plugin scanner (scans /Library/Audio/Plug-Ins/CLAP, ~/Library/...)
│   ├── Scan cache (JSON, invalidated on mtime change)
│   └── clap_host_t callbacks (log, request_restart, request_callback, etc.)
│
└── Per operator instance: CLAPPluginInstance
    ├── dlopen'd .clap bundle
    ├── Lifecycle: init → activate → start_processing → [process loop] → stop → deactivate → destroy
    ├── Audio processing: clap_process_t wired to Vivid's audio buffer
    ├── Event queue: note-on/off, param changes, per-voice expression
    └── GUI: delegates to EditorWindowManager (existing infrastructure)
```

The `CLAPHost` singleton is owned by `RuntimeCore` and initialized at startup. Individual operator instances hold a `CLAPPluginInstance` — the host does not own instances, operators do.

## Key Integration Points

| Concern | Where |
|---|---|
| Operator dylib structure | Follow `operators/audio/` conventions; one dir per operator |
| AudioProcessable | `src/operator_api/audio_operator.h` — `process_audio()` called on audio thread |
| Note input | Same port type used by `MidiInput` → existing synths; reuse the wire format |
| Dynamic params | Operator declares params at `probe()` time after scanning the plugin's param list |
| Plugin state persistence | Stored as base64 blob in a `TEXT` param; serialized with graph JSON |
| Plugin GUI | `EditorWindowManager::open(node_id)` — the operator editor system already handles this |
| Thread safety | CLAP audio thread = Vivid audio thread; host callbacks must be thread-aware |
| CMake | Add to `cmake/operators.cmake`; CLAP headers via FetchContent (free-audio/clap @ 1.2.x) |

## Plugin GUI Hosting

The operator editor system (`EditorWindowManager`, shipped) already manages secondary GLFW windows. CLAP plugin GUIs are native views (Cocoa `NSView` on macOS) that need to be embedded or parented to a host window.

The integration: when a user opens the editor for a `clap_instrument` node, `EditorWindowManager` creates a new GLFW window, and the CLAP GUI is parented to that window's native handle via `CLAP_WINDOW_API_COCOA`. The plugin's `gui->set_parent()` / `gui->show()` sequence runs in response.

This is new territory — the existing editor windows host Vivid's own `Renderer2D` drawing. CLAP GUI hosting requires embedding a foreign native view instead. Expect platform-specific work here.

## Phased Delivery

### Phase 1 — Headless instrument (no GUI)
- `CLAPHost` singleton + `CLAPPluginInstance` wrapper
- Plugin scanning + scan cache
- `clap_instrument` operator: load plugin by path, receive notes, output audio
- Dynamic params: expose plugin params in inspector, persist state in graph JSON
- Test with Surge XT (open source, CLAP-native, free)

**Done when:** A user can add a `clap_instrument` node, set `plugin_path` to Surge XT, play it via MIDI input, hear audio output, and save/reload the graph with plugin state intact.

### Phase 2 — Effect operator
- `clap_effect` operator: audio in → plugin → audio out
- Latency reporting via `CLAP_EXT_LATENCY`
- Tail handling via `CLAP_EXT_TAIL`

**Done when:** A Valhalla reverb (or any CLAP effect) can be inserted in an audio chain.

### Phase 3 — Plugin GUI
- `EditorWindowManager` integration for CLAP native views
- macOS Cocoa view embedding
- Resize sync, show/hide, teardown on node delete / graph reload
- Inspector "Open Plugin GUI" button (alongside "Open Editor")

**Done when:** Double-clicking a CLAP node (or pressing Cmd+E) opens the plugin's native UI in a separate window.

### Phase 4 — Plugin browser
- UI for browsing scanned plugins by name/vendor/feature tag
- Replace `plugin_path` string param with a picker
- MCP tool: `list_clap_plugins`, `load_clap_plugin`

## Test Plugins (all free / open source)
- [Surge XT](https://surge-synthesizer.github.io/) — full synth, excellent CLAP implementation
- [Vital](https://vital.audio/) — wavetable (free tier)
- [Dexed](https://asb2m10.github.io/dexed/) — DX7 emulator
- [Chow DSP plugins](https://chowdsp.com/) — various effects

## Key Risks

| Risk | Mitigation |
|---|---|
| Plugin crashes taking down Vivid | Start without process isolation; add later if needed |
| Dynamic params at probe time | Vivid must support operators declaring params after plugin load, not just at compile time |
| GUI embedding complexity | macOS Cocoa embedding is the hard case; defer to Phase 3 |
| Thread safety bugs | Use CLAP's thread-check extension during development |
| Plugin state schema | Base64 blob is opaque — if a plugin's state format changes, old graphs may not restore cleanly |

## References
- CLAP spec: https://github.com/free-audio/clap
- clap-helpers (C++ host utilities): https://github.com/free-audio/clap-helpers
- Prior Vivid research: `git show 20b03887:CLAP_HOST.md`
- Operator editor system: `docs/plans/archive/operator-editors/README.md`
- Operator API contract: `src/operator_api/CLAUDE.md`
