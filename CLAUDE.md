# Vivid — repo navigation

Vivid is an agent-first audio-visual environment. The trunk (`vivid-4`) holds a real macOS C++
application under **`app/`**: two best-in-class surfaces — a DAW (tracks × scenes of clips, each
track an instrument + FX chain) and a rewireable visuals node-graph — joined by a bidirectional
**mapping bridge** and driven MCP-natively.

## Where things are
- **`app/`** — the C++ app (GLFW + wgpu-native + miniaudio + VST3). Entry: `app/src/main.cpp`.
  - `app/src/audio/` — VST3 host + multi-track session (`vst3_host.h` is the session C API).
  - `app/src/gpu/` — wgpu context, the visuals `VisualGraph`, shader/effect ops, video.
  - `app/src/ui/` — `Renderer2D`, the node-graph editor, clip editor, `ui_style.h` (the palette/widgets).
  - `app/src/cli/control_server.{h,cpp}` — loopback HTTP control server (the MCP backend).
  - `app/src/mapping.h` — the `MappingRegistry` (the bridge); `app/src/persist.*` — session JSON.
- **`mcp/`** — `vivid_mcp.py`, a FastMCP (stdio) bridge proxying tools to the control server. See `mcp/README.md`.
- **`docs/decisions/`** — ADRs. Strategy: **ADR-0010** (PoC promoted to the product seed) →
  **ADR-0011** (productization target + the trunk decision). Product surfaces: **ADR-0014** (the
  visual graph is home), **ADR-0016** (a shader file is an operator). The **platform-gap set**
  (proposed, 2026-07-14): **ADR-0017** every edit is reversible · **ADR-0018** a bad operator must not
  cost you your work · **ADR-0019** nothing fails silently · **ADR-0020** the inner loop is visible ·
  **ADR-0021** content is browsable.
- **`docs/roadmap/poc-to-product.md`** — the PoC→product assessment + phased roadmap (P0–P4, largely
  landed).
- **`docs/roadmap/classic-platform-gap.md`** — the remaining classic→trunk gap: the **product** lift
  (undo, resilience, error surfaces, the authoring loop, content browsing). The finding: *the trunk
  has excellent plumbing and almost no presentation.* Read before picking up any of ADR-0017..0021.
- **`vivid-classic`** (git branch) — the mature predecessor; the engineering benchmark (operator/ABI
  model, codegen, packages, tests/CI, production gate, docs culture). Read via `git show vivid-classic:<path>`.

## Build & run (macOS)
```sh
cmake -S app -B app/build && cmake --build app/build -j
app/build/vivid.app/Contents/MacOS/vivid        # logs: control server on 127.0.0.1:9876
uv run --directory mcp vivid_mcp.py                      # the MCP bridge (app must be running)
```

## Status (2026-07-01)
`app/` is the **product trunk** (the branch formerly `poc-cpp-prototype`, now folded into `vivid-4`;
what began as a proof of concept, promoted per ADR-0010). Target is an extensible, cross-platform-capable
platform (≈ vivid-classic's architecture where it helps). Strategy (ADR-0011, **accepted**): **keep `app/`
as the trunk and adopt classic's platform by selective lift**. Build identity is now `vivid` / `com.vivid.app`
/ "Vivid"; the audio-session C API lives in `namespace vivid::session`.

Current trunk has P0-P4 style productization work in place: App/Window decomposition, headless tests,
CI/gate scaffolding, named control-server errors, runtime health/version surfaces, operator ABI +
loader/package/hot-reload pieces, semantic metadata, MCP eval harness, and release scaffolding.
Before choosing next work, read [ADR-0009](docs/decisions/ADR-0009-two-surface-bridge-and-cpp-poc.md),
[ADR-0010](docs/decisions/ADR-0010-poc-proven-production-seed.md),
[ADR-0011](docs/decisions/ADR-0011-poc-to-product-architecture.md), and
[the roadmap](docs/roadmap/poc-to-product.md).
