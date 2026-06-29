# Vivid — repo navigation

Vivid is an agent-first audio-visual environment. This branch (`poc-cpp-prototype`) holds a real macOS
C++ proof of concept under **`app/`**: two best-in-class surfaces — a DAW (tracks × scenes of clips,
each track an instrument + FX chain) and a rewireable visuals node-graph — joined by a bidirectional
**mapping bridge** and driven MCP-natively.

## Where things are
- **`app/`** — the C++ PoC (GLFW + wgpu-native + miniaudio + VST3). Entry: `app/src/main.cpp`.
  - `app/src/audio/` — VST3 host + multi-track session (`vst3_host.h` is the session C API).
  - `app/src/gpu/` — wgpu context, the visuals `VisualGraph`, shader/effect ops, video.
  - `app/src/ui/` — `Renderer2D`, the node-graph editor, clip editor, `ui_style.h` (the palette/widgets).
  - `app/src/cli/control_server.{h,cpp}` — loopback HTTP control server (the MCP backend).
  - `app/src/mapping.h` — the `MappingRegistry` (the bridge); `app/src/persist.*` — session JSON.
- **`mcp/`** — `vivid_mcp.py`, a FastMCP (stdio) bridge proxying tools to the control server. See `mcp/README.md`.
- **`docs/decisions/`** — ADRs. Current: **ADR-0010** (PoC promoted to the product seed, accepted) →
  **ADR-0011** (productization target + the trunk decision).
- **`docs/roadmap/poc-to-product.md`** — the PoC→product assessment + phased roadmap (P0–P4).
- **`vivid-classic`** (git branch) — the mature predecessor; the engineering benchmark (operator/ABI
  model, codegen, packages, tests/CI, production gate, docs culture). Read via `git show vivid-classic:<path>`.

## Build & run (macOS)
```sh
cmake -S app -B app/build && cmake --build app/build -j
app/build/vivid_poc.app/Contents/MacOS/vivid_poc        # logs: control server on 127.0.0.1:9876
uv run --directory mcp vivid_mcp.py                      # the MCP bridge (app must be running)
```

## Status (2026-06-29)
The PoC is **proven, not yet a product base**. Target is an extensible, cross-platform platform
(≈ vivid-classic's architecture). The pivotal open decision is the **trunk** (grow `app/` vs. port the
product layer onto classic's runtime) — see ADR-0011; P0 hygiene (decompose `main.cpp`, tests/CI/
sanitizers, control-server input validation, `app/` docs) is trunk-agnostic and can start first.
