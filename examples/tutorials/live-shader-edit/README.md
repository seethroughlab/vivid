# Live Shader Edit

ADR-0040 follow-up artifact: edit the project-local shader from
`mcp-native-first-project/` and verify what Vivid updates live.

This tutorial is intentionally a pressure test. A beginner should eventually be able to say:

1. Open the first project.
2. Edit `shaders/pulse_field.wgsl`.
3. See the visual change while the project keeps playing.
4. Save/reload and keep the edited shader.

## Prerequisite

Run the first tutorial first:

```sh
uv run examples/tutorials/mcp-native-first-project/build.py
```

That creates:

```text
examples/tutorials/mcp-native-first-project/project
```

## Run

Launch Vivid, then run:

```sh
uv run examples/tutorials/live-shader-edit/build.py
```

For a disposable tutorial launch, use:

```sh
VIVID_DISCARD_RECOVERY=1 ./build/vivid.app/Contents/MacOS/vivid
```

## What The Builder Checks

The builder:

- loads the first tutorial project;
- captures a pre-edit frame;
- edits `shaders/pulse_field.wgsl`;
- captures after the plain file edit;
- asks Vivid to reload project files;
- captures again;
- writes `project/live-edit-proof.json`.

## Expected Learning

The creative-coding promise is that an MCP-authored project-local shader can change while Vivid is
running. A plain file edit is allowed to be passive, but `reload_project_files` should re-scan the
project shader tier and make the edited `PulseField` operator visible without changing the graph.

## Current Result

The pressure test now expects:

- launching with `VIVID_DISCARD_RECOVERY=1` starts cleanly;
- the edited shader remains portable under `shaders/pulse_field.wgsl`;
- `reload_project_files` refreshes project shader operators;
- `PulseField` keeps its node identity while the shader body changes;
- the post-reload quality check passes.
