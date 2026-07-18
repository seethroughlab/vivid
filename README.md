# Vivid

Vivid is an **agent-first audiovisual environment**: a real macOS C++ application that joins two
best-in-class surfaces — a **DAW** (tracks × scenes of clips, each track an instrument + FX chain,
with a mixer) and a **rewireable visuals node-graph** (generator → FX → … → Output) — through a
bidirectional **mapping bridge**, and drives all of it **MCP-natively** so a coding agent can
author sessions, patch the graph, and wire audio↔visual mappings over a control API.

The application lives under [`app/`](app/) (GLFW + wgpu-native/Metal + miniaudio + VST3). The agent
bridge lives under [`mcp/`](mcp/). This is the product trunk; the Session View DAW, the visuals
graph, and the bridge are all **implemented and running today** — not a future target. Recent
product-layer work has landed too: **whole-document undo/redo** (Cmd+Z across the graph, mappings,
and audio), **content browsing** (a shader library, examples picker, file drop, node presets), and a
curated VST3/CLAP param inspector.

## Build & run (macOS)

```sh
cmake -S app -B app/build && cmake --build app/build -j
app/build/vivid.app/Contents/MacOS/vivid          # control server logs on 127.0.0.1:9876
uv run --directory mcp vivid_mcp.py               # the MCP bridge (the app must be running)
```

The control server keeps draining while the app is backgrounded, so an agent can drive it over MCP
without the window frontmost (see [`app/README.md`](app/README.md)).

## Where to start

- **[`CLAUDE.md`](CLAUDE.md)** — repo navigation + current status (the fastest orientation).
- **[`app/ARCHITECTURE.md`](app/ARCHITECTURE.md)** — the two-surfaces + bridge design, the
  thread/ownership seams (App/Window, audio vs UI thread), and the control-server flow.
- **[`app/README.md`](app/README.md)** — build, run, tests, sanitizers, CI.
- **[`mcp/README.md`](mcp/README.md)** — the agent tool surface.
- **[`docs/decisions/`](docs/decisions/)** — the ADRs. Recent load-bearing ones: ADR-0009
  (two surfaces + bridge), ADR-0011 (reboot product architecture), ADR-0012 (per-track audio graph),
  ADR-0013 (focus-first UI), ADR-0016 (a shader file is an operator), ADR-0017 (every edit is
  reversible — whole-document undo/redo), ADR-0021 (content is browsable).

## History & references

- The mature predecessor is preserved on the **`vivid-classic`** branch (tagged
  `vivid-classic-final`) — the engineering benchmark and parts bin, not the architecture to
  renovate in place.
- Early Vivid-4 planning artifacts (the pre-runtime PRD, the Session-View pressure-test
  prototypes, and the original phased plan) live under [`docs/`](docs/) as **historical evidence**
  of how the product was reasoned into being — they predate the current trunk and are not the
  current spec. Prefer `CLAUDE.md` + `app/ARCHITECTURE.md` + the ADRs for current truth.
