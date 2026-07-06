# Session View Interface Reset

Status: active guidance

## Purpose

This note gets the interface work back onto the Phase 1 proof track. It responds to the
`full-interface-prototype.html` exploration, which contains useful ideas but designs too much of the
future application shell before the Session View primary path has been proven.

## Diagnosis

The full-interface prototype currently tries to answer several later questions at once:

- Session View as the primary surface.
- Stage/layer compositing.
- Scoped operator networks.
- Whole-project graph inspection.
- Project-local code browsing.
- External-agent MCP architecture.

Those are real Vivid 4 topics, but bundling them into one early mock weakens the first proof. Phase 1
should answer one question: can the user and agent complete the one-song audiovisual loop workflow
from Session View without opening the graph?

## Direction

Treat `full-interface-prototype.html` as later-phase research, not the accepted Phase 1 prototype.

The next Phase 1 mock should be narrower:

- One screen centered on Session View.
- Transport visible at all times.
- Tracks, clips, scenes, and queued/active launch state as the main interaction.
- Selected object inspector that explains clips, scenes, tracks, and bindings.
- Visual bindings visible as session objects.
- Small live preview or scene-intensity preview only if it clarifies the selected state.
- Mocked agent action area for the scripted tasks: variations, binding creation, and explanation.

The next Phase 1 mock should avoid:

- top-level Stage, Graph, Network, or Code modes
- explanatory UI copy that argues for the product model
- whole-application navigation
- project-local code display
- final MCP/in-app-agent decisions
- deep graph vocabulary on the primary path

## Product Test

The mock passes only if the user can complete these tasks without needing the parked surfaces:

- launch Verse, Chorus, and Drop on bar boundaries
- select a visual clip and understand what drives it
- ask for bass variations through the mocked agent action area
- add or preview a kick-to-visual binding
- ask why the Drop looks more intense and receive a session-level explanation

If any of those tasks requires Stage, Graph, Network, or Code, redesign the Session View mock before
promoting those surfaces into the prototype.

## Parking Lot

Ideas from the full-interface prototype that may return later:

- Stage/layer compositor as a Phase 5 or later visual-authoring surface.
- Scoped operator network as a Phase 6 or later deep implementation view.
- Project-local code view as a project-code authoring proof, likely outside the main Vivid UI.
- Whole-project graph as an optional inspection escape hatch.
- External-only agent/MCP architecture as a Phase 2 decision after mocked agent workflow proof.
