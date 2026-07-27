# Showcase QA / screenshot harness (ADR-0037 gate)

The one hard gate before reviving the website: every hero image must come from a **refreshed,
regenerable, loadable, screenshottable** saved project (ADR-0037). This harness is that QA path.

For each showcase in the curated registry it runs, in order:

**regenerate** (run the builder) → **load** the saved portable artifact → **verify**
(`validate_project` + `get_health` + `run_quality_check`) → **warm-capture** a hero PNG (+
`analyze_frame`) → **gate** PASS / WARN / FAIL. It writes `reports/<id>.json` + `reports/index.json`
and prints a summary; it exits non-zero iff any showcase FAILs.

The registry covers ADR-0037's five showcase types (`registry.py`):

| id | type | source |
|----|------|--------|
| `first-project` | 1 first-run beginner | tutorial `mcp-native-first-project` |
| `pulse-song` | 2 Session View + visual graph | demo `pulse` |
| `mirror-bridge` | 3 audio-reactive mapping bridge | demo `mirror` |
| `shader-edit` | 4 creative-coding (project-local shader) | tutorial `live-shader-edit` |
| `neon-song` | 5 curated free-plugin music | demo `neon` |

## Run it

The app must already be running (this harness does **not** launch it). Attach to a dev build for
iteration, or the signed `/Applications/vivid.app` to produce the checked-in hero media.

```sh
# dev build:  VIVID_DISCARD_RECOVERY=1 app/build/vivid.app/Contents/MacOS/vivid
uv run examples/demos/showcase/runner.py --list                       # show the registry
uv run examples/demos/showcase/runner.py --select shader-edit --app-build dev
uv run examples/demos/showcase/runner.py --audio --app-build dev      # full run + audio checks
uv run examples/demos/showcase/runner.py --no-regen --app-build signed  # smoke saved artifacts
```

Key flags: `--select ID...`, `--type N`, `--port`, `--no-regen` (load+verify+capture only),
`--audio` (play transport + `no_audio_clipping`), `--force` (regen even when a prereq is missing),
`--warm-tries`/`--warm-delay`, `--json`.

## The gate (`gates.py`, pure)

A showcase **PASSES** iff: non-blank hero (`captured and not is_blank`, cross-checked with
`analyze_frame`) **and** `validate_project.valid` **and** **not** `validate_project.degraded` (a
loaded-but-broken plugin/shader is `valid:true, degraded:true`) **and** `run_quality_check` overall
`!= fail`. Anything short of PASS but not a hard failure is **WARN** (health severity, quality warn,
a declared prereq missing on this machine, or a slow hero warm-up). Missing prereqs cap the verdict
at WARN — the visual + structural gate still decides. The verdict logic is pure and unit-tested
(`tests/test_showcase.py`), so it runs headless in CI even though capture needs a real GPU + app.

## Hero images

Checked-in hero PNGs live in `heroes/<id>.png`, written directly by a green run. Re-running
overwrites in place, so `git status` shows which heroes changed and the report's `analysis.hash`
says whether a change is meaningful. `reports/` is git-ignored (run outputs are regenerated).

## Prerequisites

Missing prerequisites degrade to WARN, never hard-fail:
[Surge XT](https://surge-synthesizer.github.io/) (`SURGE`/`SURGE_FX` in `vivid_demo.py`),
BPB Cassette Drums (`~/Library/Audio/Plug-Ins/VST3/Cassette Drums.vst3`), and `clang++` (Xcode CLT,
for compiled project operators). When a showcase's prereq is absent, the harness skips regeneration
and loads the existing saved artifact if present.

## Tests

```sh
uv run examples/demos/showcase/tests/test_showcase.py
```
