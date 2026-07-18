# Vivid — Architecture

This describes how `app/` is put together: the two-surface product model, the
`App`/`Window` split, the module layering, the thread model, and the MCP control
flow. For the *why* and the productization plan, see
[ADR-0009](../docs/decisions/ADR-0009-two-surface-bridge-and-native-reboot.md) /
[ADR-0011](../docs/decisions/ADR-0011-reboot-product-architecture.md).

## 1. The product model: two surfaces + a bridge

```
   ┌──────────────── DAW (Session view) ────────────────┐      ┌──── Visuals node-graph ────┐
   │  tracks × scenes of clips; each track = instrument  │      │  generator → FX → … → Output│
   │  (VST3) + FX chain; a mixer.                        │      │  rewireable op nodes        │
   └───────────────────────┬─────────────────────────────┘      └──────────────┬─────────────┘
                           │   audio characteristics (level/transient/3-band)   │
                           │   ──────────────  MappingRegistry  ─────────────▶  │  drives node params
                           │   ◀─────────────  (the bridge)     ─────────────   │  viz.* return path
                           └─────────────────────────────────────────────────────┘
```

The **bridge** (`ui/mapping_overview` + the `MappingRegistry` inside `ui/node_graph`)
is bidirectional: audio characteristics (`master.transient`, `track_2.low`, …) drive
visual node params, and visuals state (`viz.warp`, …) drives audio device params
(`param:T:D:I`). Each wire shapes its value `clamp → invert → curve → range → gain`.

## 2. App (shared) vs Window (per-view)

The central seam (introduced in P0.1 / [ADR-0011](../docs/decisions/ADR-0011-reboot-product-architecture.md)):

- **`App`** (`app/app.h`) — the **shared engine + document**, one per process: the
  audio `Session`, master `Transport`, the visuals `VisualGraph` + `NodeGraph` (the
  model), the `ControlServer`, the wgpu `GpuContext`, the shared source texture, the
  video player, the **`EditGateway`** (the app-wide undo/redo command sink — every
  document edit routes through it; see §6), and the **audio-thread DSP state**. The
  `ma_device` user pointer is an `App*` — so the audio thread sees only `App`, never a
  `Window`.
- **`Window`** (`app/window.h`) — **per-view state**: the surface metrics
  (`win_w/h`, `fb_w/h`, `dpi`), the splitter/dock layout, selection + drag +
  menu + plugin-editor-handle state, frame-side smoothing, its `Renderer2D` and
  `ClipEditor`, and a back-pointer to its `App`. The GLFW window user pointer is a
  `Window*`, so input dispatch is naturally per-window.

**Adding an editor window later** = construct another `Window` (its own `Renderer2D`
+ GLFW window + wgpu surface), point `.app` at the same `App`, `install_input_callbacks`
on it, and render it in the loop. No engine changes — that's the point of the split.

## 3. Module layering

```
            main.cpp            (init + wiring + teardown only, ~150 LOC)
               │ constructs
        App ───┴─── Window
         │            │
  audio/ gpu/ cli/   ui/   app/{input,frame}
         │            │       │
         └── ui/layout (pure geometry/constants) ──┘
```

- `app/frame.cpp` — `run_frame_loop(App&, Window&)`: drains MCP commands, publishes
  audio characteristics into the graph, applies drags + mappings, renders the window,
  and commits the end-of-frame undo snapshot (§6).
- `app/input.cpp` — the GLFW callbacks (`install_input_callbacks`), keyed off `Window*`.
- `app/edit_gateway`, `app/undo_manager`, `persist_undo` — the undo/redo subsystem: one
  command sink every UI + MCP document edit routes through, with a canonical-document
  projection + tiered restore (§6).
- `ui/session_view`, `ui/mapping_overview` — draw the DAW surface + the overview;
  they read a `const Window&` and reach shared state via `win.app->`.
- `ui/shader_library_view` — the content browser (ADR-0021); `gpu/file_drop_registry` +
  `app/examples` + `app/node_presets` complete the content-browsing surfaces.
- `ui/layout.h` — pure, header-only geometry + constants shared by draw + input
  (so both agree on hit-rects); window-relative helpers are `Window` methods.
- `audio/vst3_host.h` — the session C API (opaque `Session`); `audio/audio_callback`
  is the RT callback.
- `cli/control_server` — the loopback HTTP MCP backend (see §5); `cli/control_handlers_edit`
  registers `undo`/`redo`.

## 4. Thread model

Three threads; **see [docs/thread-safety.md](docs/thread-safety.md) for the rules.**

- **Audio thread** (miniaudio `audio_callback`): renders the session, advances the
  `Transport`, publishes level/3-band/transient as atomics. Must be allocation- and
  lock-free; it reads UI edits via a generation-counter + non-blocking `try_lock`.
- **UI / main thread**: the `CFRunLoopTimer`-driven frame loop — all rendering, all
  graph/session *edits*, and all applied MCP commands happen here.
- **Control-server thread** (cpp-httplib): accepts HTTP, enqueues requests, and
  **blocks on a promise**; the main thread drains the queue each frame and fulfills
  it — so MCP mutations execute on the UI thread, never concurrently.

Cross-thread channels: `Transport` atomics (audio→frame), a lock-free SPSC queue for
plugin params (UI→audio), and the generation-counter/`try_lock` copy for clip & FX
edits (UI→audio).

## 5. MCP control flow

```
Claude ──stdio MCP──▶ mcp/vivid_mcp.py ──HTTP POST /<method> {json}──▶ cli/control_server
                                                                          │ enqueue {method, body, promise}
   main thread frame tick → control.process_pending(ctx) → dispatch handler → App/Window/session/graph
                                                                          │ fulfill promise → HTTP reply
```

Replies are `{ok:true, …}` or `{ok:false, code, error}` with **stable codes**
(`cli/control_errors.h`): `bad_json`, `unknown_method`, `no_session/graph/vgraph/
transport`, `bad_arg`, `out_of_range`, `not_found`, `io_error`, `internal`, `timeout`.
Handlers validate every index (truthful `out_of_range`, never a false `ok`). Mutating methods are
also the input to undo: at the `process_pending` chokepoint, methods listed in `cli/edit_methods.*`
notify the `EditGateway` (§6); `undo`/`redo` are themselves ordinary methods
(`cli/control_handlers_edit.cpp`) that run on this same UI-thread path.

## 6. Undo / redo (ADR-0017)

One **command sink**, `App::edit_gateway` (`app/edit_gateway.*`), owns undo for the whole document.
Every edit — UI gesture *or* MCP method — calls `note_edit(label, coalesce_key)` rather than mutating
in the dark. The gateway captures a **canonical-document projection** of the session
(`persist_undo.*` — the full `session_to_json` minus performance/view state like window layout, the
launched clip, and opaque plugin state) into a depth-capped history (`app/undo_manager.*`). Capture is
**deferred to end-of-frame** (`commit_frame()` in the tick) so edits that settle during draw (e.g. a
new node's auto-layout position) are captured settled, and so several MCP calls in one frame each land
as their own entry.

Restore is **tiered** (`RestoreAudio {Skip, ParamsOnly, Full}`): a visual/mapping undo touches no
audio; a param-only audio undo applies values onto the existing structure (no plugin reload); only an
audio-topology change rebuilds. Surfaces: Cmd+Z / Cmd+Shift+Z / Cmd+Y (`app/input.cpp`), the native
Edit menu (`platform/menu_bar.*`), and the `undo`/`redo` MCP methods. A `VIVID_UNDO_AUDIT` build
asserts each frame that no edit changed the document without routing through the gateway — a missed
site is a failing assert, not a silent wrong-undo.

## 7. Retina / HiDPI

The surface is configured at the **framebuffer (physical)** size; UI is laid out in
**logical points**; `Window::dpi` bridges them (2.0 on retina). `Renderer2D`'s atlas
is rasterized at `dpi`, and the visuals viewer viewport is scaled by `dpi`.
