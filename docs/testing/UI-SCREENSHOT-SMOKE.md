# UI Screenshot Smoke

`GUI_SMOKE` is the narrow always-on windowed regression lane for editor flows that headless tests
miss. It still captures whole-window screenshots, but screenshots are now secondary evidence. A
case passes only if it satisfies explicit semantic assertions about the final UI/runtime state.

This remains a regression/debug harness rather than the primary architecture for interactive
LLM control of a live Vivid session.

## What It Covers

The per-push `GUI_SMOKE` lane currently exercises:

- inspector screenshots for stable built-in graphs such as `instanced_shapes` and `scale_lfo`
- visible-output validation for the `env` inspector case
- synthetic graph drop reload in a real windowed session
- synthetic single-match file drop with explicit node/file-param assertions
- synthetic multi-match file drop with chooser-state assertions
- scripted windowed editor flows for:
  - node drag
  - copy/paste followed by undo/redo
  - wire reconnect

The package- and environment-sensitive cases are split out into `GUI_ENV`, which is intended for
scheduled or release validation rather than per-push gating.

`UI_SMOKE` remains the deterministic non-windowed companion lane. It covers retained-mode editor
contracts and widget behavior without a real OS window. It should hold the broad interaction logic;
`GUI_SMOKE` should stay narrow and reserved for real window/surface/editor integration risks.

## What A Passing Run Means

Each per-push GUI case must prove at least one user-meaningful success condition, such as:

- required nodes exist
- no required node is marked as a missing operator
- required connections exist and forbidden connections do not
- selected node ids match expectations
- dropped-file params were actually assigned
- chooser overlays are open or closed as expected
- native file dialogs did not appear unexpectedly
- scripted node movement changed layout by a minimum distance
- copy/paste results do not overlap originals beyond the configured threshold
- screenshots contain nontrivial output when the case is meant to validate visible rendering

Whole-window screenshots and coarse baselines are still useful, but they are only checked after the
semantic assertions pass.

Harness failures are classified separately from app regressions. A failing case now reports whether
it was caused by:

- `harness`: missing dump fields, missing artifacts, or lane/setup breakage
- `process`: the spawned windowed app did not exit cleanly
- `semantic`: the user-visible success condition failed
- `baseline`: semantic checks passed but the screenshot drifted from the blessed baseline

The lane also rejects known crash or rendering-failure signatures such as:

- `Error in wgpuQueueSubmit`
- `Scissor Rect`
- `set_scissor_rect`

## Runtime Test Seams

The windowed harness uses internal test-only CLI seams in `vivid`:

- `--test-drop-path`
- `--test-drop-screen-pos`
- `--test-drop-frame`
- `--test-ui-script`
- `--test-dump-ui-state`

`--test-dump-ui-state` writes a machine-readable snapshot of the final UI/runtime state, including:

- node ids, types, layouts, and missing-operator status
- file-param values
- graph connections
- selected node ids
- chooser/modal state needed by the smoke cases
- native file dialog invocation counts
- optional checkpoint states captured during scripted flows

These seams are intentionally test-only and do not change the user-facing graph/operator contract.

## How To Run

Local runs are opt-in because they launch the GUI:

```bash
VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1 \
ctest --test-dir build --output-on-failure -L '^GUI_SMOKE$'
```

To include the package/environment lane locally:

```bash
HOME=$PWD/build/.test_ui_screenshot_smoke/gui_env/home \
./build/vivid link ../vivid-wavetable

HOME=$PWD/build/.test_ui_screenshot_smoke/gui_env/home \
./build/vivid rebuild vivid-wavetable

VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1 \
VIVID_ENABLE_GUI_ENV_SMOKE=1 \
ctest --test-dir build --output-on-failure -L '^GUI_ENV$'
```

Artifacts are written under:

```text
build/.test_ui_screenshot_smoke/gui_smoke/
build/.test_ui_screenshot_smoke/gui_env/
```

## CI Split

- `HEADLESS_SMOKE`: fast headless smoke coverage
- `UI_SMOKE`: deterministic retained-mode interaction tests with no real window
- `GUI_SMOKE`: always-on windowed semantic smoke for core editor workflows
- `GUI_ENV`: scheduled/release windowed coverage for package-dependent or environment-sensitive cases

The default GitHub smoke workflow runs `GUI_SMOKE` on every push. `GUI_ENV` is intended for a
separate scheduled or release-oriented workflow because those cases require additional setup and
should not gate every commit. The scheduled `GUI_ENV` workflow explicitly checks out the
package fixture repo, links it into an isolated lane-specific `HOME`, rebuilds it, verifies that
the package is resolvable, and then runs the package-aware smoke cases. Both workflows upload lane
artifacts on failure for triage.

Per-push `GUI_SMOKE` must not contain package-dependent graphs. If a graph depends on linked
packages, external devices, multiple displays, or other machine setup, it belongs in `GUI_ENV`.

## Two Screenshot Workflows

**Isolated debug repro**

- launch `vivid --screenshot ...` directly
- use this for focused repros of a crash or a specific GUI regression

**LLM / MCP workflow**

- operate on the already-running Vivid instance through the runtime/control-server/MCP path
- use `capture_image(mode=\"interface\")` for live-session screenshots when needed
- do not treat `GUI_SMOKE` as the primary architecture for interactive control

The current Phase 4 inspector signoff cases captured through that live workflow are:

- `graphs/gpu/instanced_shapes_demo.json` → `shapes`
- `graphs/gpu/instanced_shapes_demo.json` → `scale_lfo`
- `graphs/gpu/particle_envelope_demo.json` → `env`

If another Vivid runtime is already active for MCP on `127.0.0.1:9876`, launching additional
windowed test runs may collide on the control-server port. That is a harness concern, not the
intended live-session workflow. The smoke harness now isolates lane artifacts and uses a lane-local
`HOME`/`TMPDIR`, but it still should not be treated as the primary live-session path.
