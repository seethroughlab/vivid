# Vivid 4 MCP Surface (modeled on vivid-classic)

Status: notes / design input

## Purpose

Record the shape of the MCP surface an external agent uses to author a Vivid 4 project,
grounded in what vivid-classic proved. The Vivid UI is a **pure human authoring surface**;
agents never touch it. They act entirely through MCP tools over the project text
(see ADR-0006). This doc is design input for Phase 2 (Agent Workflow Proof), not an
implementation spec.

## What vivid-classic proved

Classic exposed the runtime to external LLM clients (Claude Code, Cursor, …) and the setup
worked well:

- **External-only, no in-app agent.** Three MCP servers — a runtime bridge
  (`mcp/vivid_mcp.py`), a docs/source server (`vivid_opdev_mcp.py`), and a perception/analysis
  server (`vivid_analysis_mcp.py`) — connected over **MCP stdio → HTTP control server on
  :9876** embedded in the running app (`.mcp.json`, `control_server_dispatch.cpp`). An in-app
  chat was explicitly deferred as unnecessary — the agent client already provides it.
- **~180 tools**, organized by domain, not a flat dump.
- **Semantic compression** so output stayed manageable: `include_payload=false` by default
  (return `{ok, error, summary}`; opt into full data), `detail_level` modes on analysis,
  filtered lists, summary-first responses.
- **Perception-locked iteration.** The agent cannot see the app; it calls `capture_image()`,
  `analyze_output(mode=frame|audio|av)`, `compare_*` to perceive results. Perception is core
  architecture, not a debug bolt-on.
- **Onboarding** via `get_authoring_guide()` / `get_session_view_guide()` /
  `get_composition_patterns(intent)` — narrative recipes with gotchas and perf notes.

## Tool categories, recast for Vivid 4 session concepts

The Vivid 4 surface should keep classic's organization but speak the Vivid 4 vocabulary
(track / clip / scene / binding / layer / operator-network):

- **Session inspect** — `inspect_session_overview()` and friends: transport, tracks, scenes,
  clips, active/queued state, bindings, in one agent-readable, compressed object.
- **Session mutate** — create/edit tracks, clips, scenes; launch/queue scenes on the master
  transport.
- **Bindings** — create/inspect/edit audio-visual bindings (source → destination, curve,
  timing, scope) as first-class objects.
- **Visual** — inspect/edit Stage layers and dive into a layer's **scoped operator network**
  (add/connect operators); reference project-local operator code by file path.
- **Graph (any scope)** — the same scoped-network tools apply to **audio tracks** (their
  instrument→fx→mixer chain) as to visual layers; plus a **whole-project graph** inspection
  tool for the full audio+GPU+control view (see ADR-0007). The graph is a contextual deep view
  at every scope, never the primary surface.
- **Plugins** — host/list/set VST3/CLAP/AU; presets; capture/recall plugin state.
- **Perception** — capture frames/audio, analyze texture/spectral/loudness, compare to
  reference or to a stated intent.
- **Composition / help** — authoring guide, session-view guide, composition patterns,
  explain/diagnose.
- **Checks** — define and run persistent quality gates (task proofs).

## The text-canonical refinement (where Vivid 4 differs from classic)

Classic's project was a single JSON file, but that file was a **save/load snapshot of an
in-memory `Graph`** (`graph.cpp`) — the running runtime was canonical, and MCP edits mutated
memory directly, only writing text on `save_graph()`.

Vivid 4 sharpens this: **the project text is the source of truth for everything**
(ADR-0006, PRD #7). Implications for the MCP surface and runtime:

- MCP mutations and GUI edits are both **edits to the project text** (or a model that
  round-trips losslessly to it), not direct pokes at hidden in-memory state.
- The text covers session structure, tracks, clips, scenes, bindings, plugin refs, visual
  layers, operator graphs, and project-local code — readable and diffable.
- The runtime is a **projection of the text**, not a second authority. Reload/hot-reload is
  the normal path, so the snapshot/round-trip gotchas classic hit (operator state reset on
  reload) must be designed for, not discovered.

## How this grounds Phase 2

Phase 2 (Agent Workflow Proof) should pressure-test this surface with **mocked MCP
responses**: can an external agent complete the one-song-loop tasks (inspect a scene, vary a
bass clip, create a kick→particle binding, explain the Drop) entirely in session vocabulary,
over text, with no in-app agent affordance? The tool sketch in
[`session-view-pressure-test.md`](session-view-pressure-test.md) is the starting list; this
doc is the architectural frame around it.
