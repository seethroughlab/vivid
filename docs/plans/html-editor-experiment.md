# HTML editor experiment

## Context

The `operator-editors` branch shipped eight Tier-3 editor adopters using
the native `VividEditorContext` + `VividDrawAPI` pipeline: DrumSequencer,
MSEG, Sequencer, Tracker, Arpeggiator, Euclidean, PatternSeq, ParametricEQ
in core, plus WavetableOsc (in `vivid-wavetable`) and Particles3D (in
`vivid-3d`). Each is a single-file-per-operator authoring surface with
hand-written draw-rect/line/text + hit-test code.

That pipeline works — demonstrably — but a parallel question surfaced
late in the branch: **should editors be HTML/JS instead of custom C++
paint code?** The answer is not obvious, and we don't want to reopen the
operator-editors ship to resolve it. This doc captures the experiment so
we can revisit it from a clean master.

**Sequencing:**
1. Land `operator-editors` → `master`.
2. Cut a new branch from master (`editors-html-experiment` or similar).
3. Run the experiment described below.
4. Decide: generalize, shelve, or pick a coexistence model.

## What we already know

- **MCP-data-first architecture is intact.** `VIVID_DISPLAY_HIDDEN` is a
  UI render hint only. `inspect_node` / `operator_docs` / `set_param` /
  `get_param` surface every param regardless of whether the inspector
  draws a knob for it. Evidence: `operator_docs ParametricEQ` returns
  all 17 params including the 16 marked hidden. The editor is pure
  presentation; LLM-friendliness of the graph data is unaffected by
  the render technology chosen for editors.
- **The real question is authoring velocity, not LLM-visibility.** HTML/JS
  is far easier for both humans and LLMs to write than
  `draw_rect` / `draw_line` / hit-test code. CEF also opens a future
  `get_editor_snapshot` path (DOM is structured text) that no
  canvas-based editor can offer.

## Pre-existing substrate: `vivid-cef`

We do **not** start from zero. `vivid-cef` already embeds Chromium and
ships:

- `src/cef_manager.{h,cpp}` — CEF process lifecycle.
- `src/cef_client.h` — client implementation (paint, load, input).
- `src/browser_gpu_helper.{h,cpp}` — browser-texture → GPU texture bridge.
- `src/browser_input_dispatch.{h,cpp}` — mouse / keyboard forwarding
  into the CEF render process.
- `src/browser_audio_bridge.{h,cpp}` + `browser_audio_sync_policy.h` —
  browser audio capture routed into the graph.
- `src/browser_cef_gate.{h,cpp}` — initialization gate so CEF only
  spins up when a Browser node is instantiated.
- `src/browser_op.h` + `browser_audio_in.cpp` — the two operators that
  expose the above to the graph.
- `subprocess/` — the CEF helper executable.
- Active smoke graphs: `graphs/browser_hello.json`,
  `graphs/browser_audio.json`.

~2150 LOC across 18 files. The hard parts (notarization of the CEF
subprocess bundle, GPU-texture interop, audio sync) are already solved.

What `vivid-cef` **doesn't** do today:
- Host an HTML editor panel for an operator. Today it exposes browsers
  *as graph nodes* (a `Browser` operator renders a URL into a GPU
  texture). The experiment needs to repurpose the substrate to host a
  per-operator editor window, not just a texture-producing node.
- Expose a JavaScript API that mirrors the MCP operator surface.

## Experiment scope (v1)

**Build exactly one HTML-backed editor and measure.** No generalization,
no rewrite of existing editors.

### Target

**ParametricEQ.** Reasons:
- Small param surface (17 params).
- Curve-on-plane is textbook web UI — D3 / HTML5 Canvas / SVG do
  frequency-response curves trivially.
- We already have a native version committed on master (after
  `operator-editors` lands). We can diff authoring LOC and measure
  frame-time side-by-side on the same operator.
- Draggable band nodes with scroll-for-Q map cleanly to DOM event
  handlers.

### Architecture

Add a parallel editor-registration macro:

```
VIVID_EDITOR_HTML(OperatorName, "path/to/editor.html")
```

The runtime detects the HTML variant and, when the user hits **Open
Editor**, mounts a CEF-backed window pointing at the bundled HTML file.
Native `VIVID_EDITOR(OperatorName)` keeps working unchanged — the two
paths coexist per operator.

### Bring vivid-cef into core

The CEF substrate moves from the sibling repo into `src/runtime/cef/`
(or similar). Rationale: editor panels become a first-class platform
feature, not a package-level capability. The `Browser` and
`BrowserAudioIn` operators stay where they are — they're domain
operators that happen to use the substrate, and they don't need to
become core seed operators.

Practically:
- Move `vivid-cef/src/cef_*`, `browser_gpu_helper.*`,
  `browser_input_dispatch.*`, `browser_audio_bridge.*`,
  `browser_audio_sync_policy.h`, `browser_cef_gate.*` into
  `src/runtime/cef/`.
- Keep `browser_op.h` / `browser_audio_in.cpp` in `vivid-cef`; link
  them against the now-in-core CEF runtime.
- The subprocess binary and CEF binaries stay in-tree the same way
  they do today in the package.
- Editor-mount logic lives in `src/runtime/core/editor_window_manager.*`
  (extends the existing Tier-3 window manager).

### JavaScript API

A bridge object (`window.vivid`) injected into each editor page exposes:

```
vivid.getParam(name): number | string
vivid.setParam(name, value): void
vivid.onParamChange(name, cb): unsubscribe
vivid.getOutput(name): number | lane-array
vivid.operatorInfo(): { params: [...], ports: [...], brief, tips }
vivid.host: {
  getClipboard(): string
  setClipboard(text): void
  requestFocus(): void
  setCursor(kind): void
  // mirrors VividEditorHostAPI
}
```

This is **the exact same command surface** as `VividEditorContext` +
`VividInspectorCommandAPI`, just shaped for JS. An LLM that knows how
to write native `draw_editor` already knows how to write the JS
equivalent — and, critically, knows the command names are the same
strings as `mcp__vivid__set_param` uses. Cross-learning is free.

### What the ParametricEQ HTML editor looks like

- Single `parametric_eq_editor.html` shipped alongside the operator
  source under `operators/audio/parametric_eq/editor/`.
- Rendered in one file with inline CSS + JS (no bundler dependency
  for the experiment — keep the LOC baseline honest).
- Canvas-rendered response curve, draggable `<circle>` SVG nodes over
  the canvas, scroll handlers for Q, double-click for type cycle.

## What we measure

| Metric | How | Target |
|---|---|---|
| Authoring LOC | `wc -l` on the HTML editor vs the native `parametric_eq_editor.cpp` + `_editor_shared.cpp` | HTML ≤ 60% of native |
| Frame time | Capture average ms/frame during a band drag on a reference machine (M1) | HTML within 2× of native; hard ceiling ~8 ms |
| Param-update latency | Time from mouse-move to `set_param` landing, HTML path | ≤ 4 ms (editor is not audio-rate) |
| Install footprint | Delta in bundle size from landing CEF in core | Budgeted; no target other than "know the number" |
| Startup cost | Time from "Open Editor" click to first-paint, first time vs warm | Warm ≤ 150 ms |
| LLM authoring trial | Have an LLM write a new Particles3D HTML editor from scratch. Measure LOC, attempts, and whether it lands without human fixes | Ship-able in ≤ 2 attempts |
| DOM-readability | Implement `get_editor_snapshot(node_id)` returning structured text of the rendered editor | Works on ParametricEQ HTML |

## Decision criteria

- **Generalize** if HTML wins on authoring LOC + LLM authoring trial
  *and* frame-time stays under ceiling *and* install-size delta is
  acceptable.
- **Coexist** if HTML wins on authoring but performance-critical
  editors (Tracker, high-density grids, live scope visualizations)
  prefer native — ship both paths, let operator authors choose.
- **Shelve** if CEF integration costs (startup, IPC, install size,
  notarization churn) outweigh authoring gains at this scale.

## Out of scope for the experiment

- Rewriting any of the eight editors on `master`. They keep working.
- Building a bundler / TypeScript pipeline. Plain HTML/JS for the
  experiment; tooling is a second-phase conversation.
- Making `Browser` or `BrowserAudioIn` use the core CEF substrate
  directly — they can continue linking against it through the package
  boundary, no semantics change.
- Security sandboxing for third-party package HTML editors. Assume
  trust for the experiment; formalize later if we generalize.
- Mobile / Windows specifics. macOS-first for the proof of concept.

## Risks to name upfront

- **CEF weight** — Chromium is ~100 MB+ and adds notarization cadence.
  Already paid in `vivid-cef`; brought-into-core means every install,
  not just those that install the CEF package.
- **IPC cost on drag gestures** — every param update marshals through
  JS↔C++. Fine for editors, potentially not fine for audio-rate
  modulation visualizations.
- **Two rendering idioms** — the inspector is still native painted.
  Mixing HTML editor + native inspector on the same operator risks
  visual inconsistency. Theme tokens would need a JS export.
- **State ownership** — drag state, selection, undo history could end
  up scattered between JS and C++. Design the `vivid` bridge to make
  one side authoritative from day one.

## Open questions for the experiment

- Should the CEF renderer run out-of-process (Chromium default, robust)
  or in-process (saves IPC, loses crash isolation)? Starting point:
  out-of-process, match the `Browser` operator's current pattern.
- How does the HTML editor get its initial theme / font / density?
  Proposal: inject a `<style>` block or a CSS variables object on the
  `window.vivid.theme` bridge object at mount time, derived from the
  current Vivid theme.
- Hot reload of editor HTML during development — do we piggy-back on
  CEF's devtools, or the existing operator hot-reload infrastructure,
  or both? `vivid-cef`'s `Browser` operator already supports URL
  reload; the editor path would want the same.
- `get_editor_snapshot` schema — structured text representation of
  the DOM for LLMs / tests. What level of detail: full DOM dump,
  labeled-widget list, something else?

## Verification plan (for the experiment landing)

1. `operator-editors` merged into `master`. No regressions.
2. New branch; CEF substrate in `src/runtime/cef/`; `vivid-cef`
   refactored to consume from core.
3. `VIVID_EDITOR_HTML` macro landed; `parametric_eq_editor.html` +
   inline JS shipped; param drag + scroll for Q + type cycle work.
4. Both native and HTML ParametricEQ editors load side-by-side on a
   test graph (two instances of the operator, one with each editor
   registered).
5. Measurement table above filled in; results posted into this doc.
6. Decision recorded at the top of the doc (generalize / coexist /
   shelve), and whichever follow-up branch lands from that.
