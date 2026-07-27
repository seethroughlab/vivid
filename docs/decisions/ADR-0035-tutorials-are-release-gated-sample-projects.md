# ADR-0035: Tutorials Are Release-Gated Sample Projects

Status: accepted

Date: 2026-07-26

## Context

The revived website should put beginner tutorials front and center, but users cannot complete a
Vivid tutorial without downloading Vivid. The tutorial is therefore part of the release funnel, not
an independent documentation page.

The tutorial goal is also broader than education. We want the tutorial series to uncover gaps and
bugs while creating sample graphs/projects that can become showcase material, regression fixtures,
and examples for agents.

## Decision

Every beginner tutorial must produce a saved, loadable sample project and a short friction log. A
tutorial is not "done" when the prose is written; it is done when the project can be regenerated,
loaded in the signed app, inspected over MCP, and verified visually or sonically.

The initial tutorial order is:

1. Download and First Launch: install the signed GitHub Release build, open Vivid, and confirm the
   control server/MCP bridge can connect.
2. First 10 Minutes: open an example and identify Session View, Visual Graph, Output, and the
   mapping bridge.
3. First Visual Graph: create generator -> effect -> Output, tweak parameters, save the project.
4. First Sound: install Surge XT, create a simple instrument/effect path, and verify audio.
5. Clips and Scenes: build Intro and Drop scene states.
6. Audio Drives Visuals: map a musical/control signal to size, color, bloom, or motion.
7. Edit Live: change the project during playback, exercise undo/redo, save/load, and verify state
   survives.
8. Use Media: import image/video/text/model assets and document missing-file behavior.
9. Fork a Shader or Visual Operator: prove the creative coding loop directly.
10. Complete Mini Piece: combine scenes, audio, visual graph, mappings, and saved output.

Each tutorial records:

- What worked through the UI.
- What required MCP/control-server help.
- What required unexplained dependencies.
- What crashed, silently failed, or produced unclear health/errors.
- What could not be verified visually or sonically.
- Which reusable sample project was produced.

## Alternatives Considered

- **Write prose tutorials first and create sample projects later.** Rejected. That lets docs drift
  away from the product and misses the bug-finding value.
- **Use demos only, without step-by-step beginner material.** Rejected. Demos prove output quality,
  but not first-run comprehension.
- **Make tutorials source-build only.** Rejected for public onboarding. Source builds remain a
  developer path, but beginners start from signed releases.

## Consequences

- Tutorial work becomes implementation work: missing UI affordances, MCP gaps, health messages, and
  fragile project save/load behavior are in scope.
- The website can ship a placeholder before tutorials are complete, but the real public launch is
  gated by tutorial projects.
- The examples directory should eventually distinguish polished tutorial projects from exploratory
  demos.

## Progress

The first checked-in tutorial artifact is `examples/tutorials/mcp-native-first-project/`. It creates
a saved folder project through the control-server/MCP surface, uses Surge XT as the assumed beginner
instrument, authors a project-local shader operator, saves/reloads the project, and writes proof
files plus `FRICTION-LOG.md`.

The first follow-up artifact is `examples/tutorials/live-shader-edit/`. It pressure-tests the
creative-code edit loop by mutating the first tutorial's project-local shader, calling
`reload_project_files`, and verifying that the metadata-named shader operator updates without
changing graph identity.

This confirms the ADR shape: the tutorial work is already finding product gaps, and those gaps are
being fixed in Vivid before the website claims the workflow is ready.

`live-shader-edit` has since been promoted from that pressure-test into a first-class beginner
walkthrough (tutorial tier 2, ADR-0035 step 9 "Fork a Shader or Visual Operator"). It is now
self-contained — it scaffolds its own shader-only project, so it needs no Surge XT / synth
prerequisite — and it teaches the full creative-coding loop over MCP: discover the shader operator and
its backing file, edit the `.wgsl` and reload it live (the node keeps its identity), deliberately
break the shader and recover using `validate_project` / `inspect_signal_flow` diagnostics, then verify
and save. Building it drove two product fixes (self-describing shader ops in the operator catalog, and
`reload_project_files` rebuilding live shader nodes so edits/errors reach running nodes over MCP — see
ADR-0040). Its `build.py` is a runnable acceptance test with hard assertions; it produces
`project/live-edit-proof.json` and a friction log.
