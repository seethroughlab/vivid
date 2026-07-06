# Vivid (`app/`)

A real macOS C++ proof-of-concept for Vivid: **two best-in-class surfaces** — a DAW
(tracks × scenes of clips; each track an instrument + FX chain) and a rewireable
visuals node-graph — joined by a bidirectional **mapping bridge** and driven
**MCP-natively**. See [ADR-0009](../docs/decisions/ADR-0009-two-surface-bridge-and-cpp-poc.md)
for the product thesis and [ADR-0011](../docs/decisions/ADR-0011-poc-to-product-architecture.md)
for where it's going (keep this trunk; adopt vivid-classic's platform by selective lift).

- **Architecture & thread model:** [ARCHITECTURE.md](ARCHITECTURE.md)
- **Audio-thread safety rules (read before touching the engine):** [docs/thread-safety.md](docs/thread-safety.md)
- **MCP control bridge:** [`../mcp/README.md`](../mcp/README.md)

## Build & run (macOS)

```sh
cmake -S app -B app/build && cmake --build app/build -j
app/build/vivid.app/Contents/MacOS/vivid        # control server logs on 127.0.0.1:9876
```

The agent bridge (the app must be running):

```sh
uv run --directory mcp vivid_mcp.py
```

> The frame loop (and the control-server drain it runs each tick) **keeps pumping while the
> app is backgrounded**, so an agent can drive it over MCP without the window frontmost — the
> outer loop ticks directly rather than relying on a run-loop timer (macOS does not fire
> timers for a background app), and App Nap is disabled at startup. When fully occluded the
> renderer may briefly log `surface texture unavailable` and skip drawing a frame; that is
> benign and does not stop the control server.

## Tests, sanitizers, CI

The headless test suite needs none of the GUI/GPU/audio/VST deps, so it configures and
runs anywhere (Linux CI included):

```sh
cmake -S app -B build -DVIVID_BUILD_APP=OFF -DVIVID_BUILD_TESTS=ON
cmake --build build -j && ctest --test-dir build --output-on-failure
```

- `-DVIVID_SANITIZE=ON` → AddressSanitizer + UndefinedBehaviorSanitizer
- `-DVIVID_SANITIZE_THREAD=ON` → ThreadSanitizer (mutually exclusive with the above)
- CI ([`.github/workflows/headless-tests.yml`](../.github/workflows/headless-tests.yml))
  builds + runs the suite on Linux in a normal and an ASan/UBSan matrix.

## Source map (`app/src/`)

| Dir | What lives there |
|---|---|
| `app/`   | `App` (shared engine/document), `Window` (per-view state), `input`, `frame` loop, `shell`-free entry (`main.cpp`) |
| `audio/` | VST3 host + multi-track session (`vst3_host.h` = the session C API), sampler, the RT `audio_callback` |
| `gpu/`   | wgpu context, the visuals `VisualGraph`, shader/effect ops, render targets, video, texture source |
| `ui/`    | `Renderer2D`, `layout` (geometry/constants), `node_graph` editor, `clip_editor`, `session_view`, `mapping_overview`, `ui_style` |
| `cli/`   | the loopback HTTP `control_server` (MCP backend) + `control_errors`/`control_parse` |
| `midi/`  | `MidiClip` types |
| `platform/` | macOS frame timer (CFRunLoop) |

Each dir has a `CLAUDE.md` with its specifics and invariants.
