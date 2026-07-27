# MCP-Native First Project

ADR-0034 Golden Path A: build a tiny audiovisual project through Vivid's MCP/control-server surface.
This is the first beginner tutorial artifact for the website revival.

## What You Build

The generated project contains:

- a Surge XT CLAP instrument track named `tone`;
- a short MIDI clip;
- a project-local GLSL fragment at `assets/shaders/pulse_field.glsl`;
- a `CustomShader -> Feedback -> Blur -> Output` visual graph;
- four audio-to-visual mappings;
- a saved, reloadable folder project under `project/`;
- proof files: `project/proof.json`, `project/capture.png`, and `project/FRICTION-LOG.md`.

The point is not the final look. The point is to prove the loop: an MCP client can inspect Vivid,
author creative code and graph structure, save the project, reload it, and verify that the result
still renders and sounds.

## Before You Start

Install Surge XT so the CLAP bundle exists here:

```text
/Library/Audio/Plug-Ins/CLAP/Surge XT.clap
```

Official download: https://surge-synthesizer.github.io/

Homebrew:

```sh
brew install --cask surge-xt
```

The broader free-plugin starter list is one folder up:
`examples/tutorials/free-plugin-starter-list.md`.

Launch Vivid before running the builder. The app must expose the control server on
`127.0.0.1:9876`; set `VIVID_PORT` if you use a different port.

For a disposable tutorial run, launch Vivid with autosave recovery discarded:

```sh
VIVID_DISCARD_RECOVERY=1 ./build/vivid.app/Contents/MacOS/vivid
```

Use this only when you do not need to recover unsaved work from a previous Vivid session. If you
want to preserve recovery data but skip the recovery prompt for automation, use `VIVID_NO_RECOVER=1`
instead.

## Run The Builder

From the repo root:

```sh
uv run examples/tutorials/mcp-native-first-project/build.py
```

The script checks the control server, the expected Surge XT CLAP path, and Vivid's visible plugin
catalog before regenerating `project/`. If something is missing, it prints a checklist and exits
without deleting the previous generated project.

After it finishes, open this folder project in Vivid:

```text
examples/tutorials/mcp-native-first-project/project
```

## What To Inspect

Open `project/proof.json`. A good run has:

- `post_reload_capture_frame.captured: true`
- `post_reload_capture_frame.is_blank: false`
- `validate_project.valid: true`
- `all_quality_checks.overall: "pass"`
- `mappings_resolve.overall: "pass"`

Open `project/project.json` and confirm the shader path is portable:

```json
"file": "assets/shaders/pulse_field.glsl"
```

The saved project should not contain a machine-specific absolute shader path.

## Mapping Discovery

The tutorial teaches the bridge through discovery, then uses a first-class helper to connect the
mapping. The builder does this automatically, but the beginner-facing flow should read like this.

1. Ask Vivid what can drive mappings:

```json
{"method": "list_mapping_sources"}
```

Find:

- `tone gate`
- `tone note`
- `master level`
- `master transient`

2. Ask Vivid what visual params can be mapped:

```json
{"method": "list_mapping_destinations", "scope": "visual"}
```

Find the `CustomShader` params:

- `warp`
- `hue`
- `glow`
- `density`

3. Connect intent, not raw strings:

```json
{
  "method": "map_audio_to_visual_param",
  "source": "track",
  "track_name": "tone",
  "characteristic": "gate",
  "node_id": 42,
  "param": "warp",
  "amount": 0.9,
  "lo": 0.18,
  "hi": 0.8
}
```

Repeat for:

- `track tone note -> CustomShader.hue`
- `master level -> CustomShader.glow`
- `master transient -> CustomShader.density`

The helper response includes canonical `src` and `dst` strings for debugging, but the user or agent
does not need to construct `track_<id>.*` or `node:<id>.*` manually.

## If Something Fails

If the builder cannot reach Vivid, launch the app and confirm the control server port.

If Surge XT is missing, install Surge XT and rerun the builder. The tutorial intentionally fails
early with install paths and links instead of creating a silent project.

If the visual output is blank after reload, inspect `project/proof.json`, then check whether
`assets/shaders/pulse_field.glsl` is present and whether `project.json` stores that relative path.

If Vivid prints a recovery warning before the builder resets the session, relaunch with
`VIVID_DISCARD_RECOVERY=1` for a clean disposable tutorial run.

## Why This Starts With A Shader

Project-local C++ is central to Vivid's long-term creative coding story, but it requires compiler
and package setup. This first tutorial starts with a project-local shader because it proves the
MCP-native creative loop with less setup. The next ADR-0034 artifact should deepen this into live
shader editing, then project-local operators.

## Friction Log

The builder writes `project/FRICTION-LOG.md`. Treat it as part of the tutorial output. It records
what worked, what was confusing, and which product gaps the tutorial exposed.
