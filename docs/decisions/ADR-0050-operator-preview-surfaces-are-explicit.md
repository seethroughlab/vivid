# ADR-0050: Operator Preview Surfaces Are Explicit

Status: accepted

Date: 2026-08-03

## Context

Vivid operators can appear in more than one compact visual context.

Visual operators have a straightforward node-card thumbnail model: the visual graph usually blits the
operator's live output texture into the card. Shader and GPU operators that produce pixels get this for
free; non-texture visual operators can render a small preview into their own output texture.

Audio operators are more nuanced. A note-generator operator can appear as:

1. A **Session View generator cell**, where the cell represents a piece of musical material in the song
   grid.
2. An **audio graph node card**, where the node represents a processing/generation device in a track's
   graph.

Today both surfaces may use the same optional operator hook:
`draw_thumbnail(const VividThumbnailContext*)`. Other audio node previews are host-generated: live output
scopes for instruments/effects, control scopes and compound-widget previews for modulators, active-note
strips for note utilities, and sample waveforms for Sampler.

This is a reasonable implementation shape, but the ABI language is blurrier than the product model. The
ABI exposes one `VividThumbnailContext` with dimensions, draw API, param snapshot, accent, and time. It
does not say whether the operator is drawing for a session cell, an audio graph node, a chooser row, a
future docs/reference card, or some other compact surface. As custom C++ audio operators become a public
creative-coding path, authors need to know what they are drawing and what assumptions are safe.

## Decision

Make **operator preview surfaces explicit** in the operator ABI and documentation.

Keep a single compact-preview drawing hook, but extend `VividThumbnailContext` with an additive purpose
field that tells the operator why the host is asking for a drawing.

The purpose enum exists for what the *existing* fields cannot express. Layout is already answered by
`surface_width`/`surface_height` (a generator can already draw a wide contour for a node vs a square ring
for a cell by reading the aspect ratio) and the param snapshot is already in `param_values`. So purpose
is **not** a layout hint — it carries **semantic state assumptions** the geometry can't: chiefly, whether
a live instance exists. The motivating case is `CATALOG`, where an operator is drawn with no instantiated
node behind it, so it must render from defaults and must not assume a loaded sample, a running voice, or a
meaningful `time`.

Purposes ship in two waves. **Populated now** (each has a real call site):

- `VIVID_PREVIEW_DEFAULT` — the compatibility value; also what a host passes before a call site is updated.
- `VIVID_PREVIEW_SESSION_CELL` — compact musical material in the Session View grid.
- `VIVID_PREVIEW_AUDIO_NODE` — compact device/node preview in the audio graph.

**Reserved (defined, not yet populated)** — no consumer exists today, so an operator must not wait on them;
they are added when a call site does:

- `VIVID_PREVIEW_VISUAL_NODE` — a visual node preview *when the host asks an operator to draw rather than
  blitting its output texture*. The visual graph blits the output texture today and calls no
  `draw_thumbnail`, so this has no consumer yet.
- `VIVID_PREVIEW_CATALOG` — static/lightly-animated picker or reference-page preview with no live instance.

`VIVID_PREVIEW_DEFAULT` is `0`, so a zero-initialized `VividThumbnailContext{}` (which both current call
sites already use) yields `DEFAULT` for free. Existing operators that ignore the field continue to work.

The hook should be documented as **operator-drawn compact preview**, not only "thumbnail." A preview is a
small authored representation of the operator or musical material. A thumbnail is one common rendering of
that preview.

## Design Principles

- **One hook, explicit context.** Do not add separate ABI exports such as `draw_session_cell_thumbnail` and
  `draw_audio_node_thumbnail` unless the data contracts truly diverge. A purpose enum keeps the ABI small
  while removing ambiguity.
- **Host-generated previews stay host-generated.** Scopes, sampler waveforms, active-note strips, and
  compound-widget previews are runtime/UI observations. Operators should not be forced to redraw what the
  host can derive more accurately and safely.
- **UI-thread and snapshot-only.** `draw_thumbnail`/compact preview remains UI-thread, read-only, and based
  on `VividThumbnailContext`. It must not touch audio-thread state, allocate heavy resources, or depend on
  mutable live `Param<>` members.
- **Same drawing can serve multiple surfaces.** A generator may draw the same ring or melodic contour for
  both `SESSION_CELL` and `AUDIO_NODE`, but that is an author choice, not an ABI assumption.
- **Preview purpose is not operator role.** `VividAudioRole`/`VividOperatorRole` say what the operator is
  (ADR-0046/0047). Preview purpose says where/how the host is displaying it. Different axes; never conflate.
- **Purpose is not layout.** Aspect ratio and size live in `surface_width`/`surface_height`; branch on those
  for layout. Reach for `purpose` only when the semantic contract differs (live instance vs none).
- **The ABI symbol does not change.** The hook stays `vivid_draw_thumbnail` / `OperatorBase::draw_thumbnail`;
  only the documentation adopts the word "preview." Renaming the export would orphan every installed
  operator dylib, so this is a docs-only reframing.
- **Additive, host→op.** `VividThumbnailContext` is host-filled and passed to the operator by pointer
  (the opposite direction from `VividOperatorDescriptor`). The append rule: add `purpose` at the END, never
  reorder or resize existing fields. An older operator built against the shorter struct simply never reads
  the field; a newer operator reads it and the host always populates it (the zero-init `tc{}` already
  guarantees `DEFAULT`). Bumps `VIVID_OPERATOR_ABI_VERSION`; the loadable floor is unchanged.

## Per-Purpose Data Contract

What an operator may rely on when drawing, by purpose. Everything is UI-thread and read-only regardless.

| Purpose | Live instance? | `param_values` | `time` | Notes |
|---|---|---|---|---|
| `DEFAULT` | maybe | may be null | may be 0 | Assume the least; treat like `CATALOG` if you branch at all. |
| `SESSION_CELL` | yes | current node params | transport beats | Musical-material preview; animation to the beat is welcome. |
| `AUDIO_NODE` | yes | current node params | clock beats | Device/node preview; same data as the cell, usually the same drawing. |
| `CATALOG` (reserved) | **no** | defaults or null | 0 | No instantiated node — render from defaults; assume no sample/voice/animation. |
| `VISUAL_NODE` (reserved) | yes | current node params | frame time | Only when the host asks an op to draw instead of blitting its texture. |

## Implementation Plan

1. Add a `VividPreviewPurpose` enum to `operator_api/types.h`: `DEFAULT=0`, `SESSION_CELL`, `AUDIO_NODE`,
   `VISUAL_NODE`, `CATALOG` (the last two defined but reserved — see Decision). Append
   `VividPreviewPurpose purpose` as the LAST member of `VividThumbnailContext`, bump
   `VIVID_OPERATOR_ABI_VERSION` (floor unchanged), and record the additive entry in the ABI changelog.
2. Populate `purpose = VIVID_PREVIEW_SESSION_CELL` at the Session View generator-cell call site
   (`ui/session_view.cpp`) and `VIVID_PREVIEW_AUDIO_NODE` at the audio-graph generator-node call site
   (`ui/audio_node_graph.cpp`). Both already zero-init `tc{}`, so no other host field changes.
3. Do NOT rename the hook: `vivid_draw_thumbnail` / `draw_thumbnail` stay; docs adopt "compact preview."
4. Update operator-authoring docs with the Per-Purpose Data Contract table and the difference between
   operator-drawn previews, host-generated scopes/waveforms/note-strips, and visual-graph texture blits.
5. Leave the example generators drawing identically for `SESSION_CELL` and `AUDIO_NODE` (no forced branch);
   add a purpose branch only where it demonstrably improves a preview.
6. Add an ABI fixture test: the test-fixture dylib records the `purpose` it received; the loader test drives
   `draw_thumbnail` across the dlopen boundary and asserts the value arrives, and that a dylib built without
   the field still loads at the new ABI. Reserved values (`VISUAL_NODE`/`CATALOG`) get no call site yet.

## Alternatives Considered

- **Leave the ABI as one generic thumbnail hook and document current behavior only.** Rejected. It keeps the
  ABI minimal, but it makes custom audio operators rely on host/UI convention that is not visible from the
  ABI.
- **Create separate exports for every surface.** Rejected for now. It multiplies ABI surface area before
  there is evidence that session cells and audio nodes need different data contracts.
- **Make all audio graph previews operator-drawn.** Rejected. Live scopes, sampler peaks, and active-note
  strips are better host responsibilities because they are derived from runtime state the host already owns.
- **Remove operator-drawn audio thumbnails entirely.** Rejected. Note generators benefit from a compact,
  authored pattern preview that is not visible in an audio scope.

## Consequences

- Custom C++ audio authors get a clearer contract for the two preview contexts they care about.
- The ABI remains additive and compatible with existing packages.
- Docs and generated reference pages can distinguish "operator supplies compact preview" from "host supplies
  live node preview."
- Tests can verify preview intent instead of relying on comments in `session_view.cpp` and
  `audio_node_graph.cpp`.
- The UI remains free to use host-generated previews where those are more truthful than an operator drawing.

## References

- ADR-0023: Shared Graph UI Substrate
- ADR-0042: Operator Audit and Definition of Done
- ADR-0047: Note, Control, and Value Streams Need First-Class Ports
- Code: `app/src/operator_api/types.h`, `app/src/operator_api/operator.h`,
  `app/src/ui/session_view.cpp`, `app/src/ui/audio_node_graph.cpp`,
  `app/src/audio/builtin_audio_ops.cpp`
