# Vivid — Architecture

This describes how `app/` is put together: the two-surface product model, the
`App`/`Window` split, the module layering, the thread model, and the MCP control
flow. For the *why* and the productization plan, see
[ADR-0009](../docs/decisions/ADR-0009-two-surface-bridge-and-cpp-poc.md) /
[ADR-0011](../docs/decisions/ADR-0011-poc-to-product-architecture.md).

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

The central seam (introduced in P0.1 / [ADR-0011](../docs/decisions/ADR-0011-poc-to-product-architecture.md)):

- **`App`** (`app/app.h`) — the **shared engine + document**, one per process: the
  audio `Session`, master `Transport`, the visuals `VisualGraph` + `NodeGraph` (the
  model), the `ControlServer`, the wgpu `GpuContext`, the shared source texture, the
  video player, and the **audio-thread DSP state**. The `ma_device` user pointer is an
  `App*` — so the audio thread sees only `App`, never a `Window`.
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
  audio characteristics into the graph, applies drags + mappings, renders the window.
- `app/input.cpp` — the GLFW callbacks (`install_input_callbacks`), keyed off `Window*`.
- `ui/session_view`, `ui/mapping_overview` — draw the DAW surface + the overview;
  they read a `const Window&` and reach shared state via `win.app->`.
- `ui/layout.h` — pure, header-only geometry + constants shared by draw + input
  (so both agree on hit-rects); window-relative helpers are `Window` methods.
- `audio/vst3_host.h` — the session C API (opaque `Session`); `audio/audio_callback`
  is the RT callback.
- `cli/control_server` — the loopback HTTP MCP backend (see §5).

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
Handlers validate every index (truthful `out_of_range`, never a false `ok`).

## 6. Retina / HiDPI

The surface is configured at the **framebuffer (physical)** size; UI is laid out in
**logical points**; `Window::dpi` bridges them (2.0 on retina). `Renderer2D`'s atlas
is rasterized at `dpi`, and the visuals viewer viewport is scaled by `dpi`.
