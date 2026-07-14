# ADR-0021: Content Is Browsable

Status: proposed

Date: 2026-07-14

Amends: [ADR-0016](ADR-0016-shaders-are-content.md) — which declares that a shader file is content,
and then gives the user no way to *see* the content.

Decided: shaders, media, examples, and presets become **first-class browsable content** behind one
asset library, generalized from the shader library that already exists. **Drag-and-drop onto a
graph** is the fast path in: drop a file, get the node that handles it.

## Context

`TODO.md` asks the question this ADR answers:

> *"do we have a way to browse shaders that are shipped with vivid? If not, we should."*

We do not. And the question generalizes: there is no way to browse **anything**. Not the shipped
shaders, not the example projects that are already in the repo, not media, not presets.

- **Shaders.** ADR-0016 turns `.wgsl` files into operator types and ships a library of them. A
  library you cannot browse is a directory you have to already know the contents of.
- **Examples.** `examples/demos/projects/{pulse,drift,neon,grid}` already ship. There is **no
  picker**. The only way to open one is File → Open and navigate to it, which requires knowing it
  exists and where it lives.
- **Media.** No import, no index, no library. A file param is a raw path.
- **Presets.** Only VST3 plugins have them (`app/src/audio/vst3_presets.h`, plus a generic
  `list_presets` / `load_preset` MCP flow). A *node* — a shader, a visual op, an audio op — cannot
  save or recall a preset at all.
- **File drop.** Dropping a `.mp4` or a `.png` on the graph does nothing.

### The seed already exists

We should not build an asset library from scratch, because `app/src/gpu/shader_library.{h,cpp}` is
already three-quarters of one:

- a **three-tier search path** (shipped / user / project),
- a **catalog** of rows, each with a name, params, and — importantly — an `error` string, so an entry
  that failed to parse *still appears*, carrying why (`shader_library.h:23-30`),
- an **mtime watch** that hot-reloads edits with last-good fallback,
- and `fork()`, to copy a shipped item into a user-editable one.

That is: discovery across scopes, an index with per-entry metadata and errors, and live refresh.
Swap "shader" for "asset kind" and it is the asset library. **Generalize it. Do not write a parallel
system** — a second discovery/index path would drift from the first, and shaders would end up being
browsable one way and images another.

Classic's `src/runtime/assets/asset_library*.cpp` is the target shape: merged package + workspace
scopes, content-hash dedupe, an index carrying kind-specific metadata, `list_assets` / `import_asset`
/ `inspect_asset`. Classic ships exactly one kind (wavetables) — proof that the layer is worth
building before the kinds are.

### The Tab chooser is the browser we already have

`app/src/ui/chooser.{h,cpp}` is a *good* piece of UI: shared by both graphs, type-to-filter with
ranking (`ui/text_match.h`), badges, summaries, and greyed-out rows carrying a `disabled_note` that
explains *why* they're disabled. ADR-0014 established it as **"the ONE way to add a node."**

Content browsing should extend it, not compete with it. A user looking for a plasma shader should
find it by pressing Tab and typing "plasma" — because under ADR-0016 that shader **is** an operator,
so it is already in the chooser's catalog. The browser dialog is for the richer case (thumbnails,
tags, kinds, preview), not a second front door.

## Decision

1. **One `AssetLibrary`, generalized from `ShaderLibrary`.** Kinds: shader, image, video, audio,
   and (later) wavetable. Per kind: a search path across shipped / user / project scopes, an index
   with kind-specific metadata, content-hash dedupe, and the same "a broken entry still appears,
   carrying its error" rule that `ShaderLibrary` already enforces. Import copies a file into the
   user library; discovery finds package assets read-only in place.

2. **A browser dialog** with kind and tag filters and thumbnails, reachable from (a) the Tab chooser,
   for the richer view, and (b) any file param tagged with an `asset_kind` — so a "texture" param
   opens the image browser instead of a raw file dialog.

3. **File drop.** A drop registry (classic: `src/runtime/core/file_drop_registry.{h,cpp}`) in which
   **operators declare the extensions they handle**. Dropping `clip.mp4` on the graph offers the ops
   that take video; dropping `noise.png` offers the ones that take a texture; one match auto-creates.
   Wire it to the existing `set_node_file_param` / `set_node_asset` control methods — the plumbing
   for *setting* the asset already exists, only the gesture is missing.

4. **An examples browser.** File → Open Example, over the projects already in `examples/`. Reuse the
   recents machinery in `app/src/app/file_actions.{h,cpp}`. This is the cheapest item in the entire
   ADR set and probably the biggest first-run difference: today a new user opens Vivid to an empty
   graph and no evidence that four finished demos ship with it.

5. **Node presets.** Generalize the preset *shape* the VST3 path already has to any node: save the
   current params of a node under a name; recall them; ship factory presets alongside an operator.
   For a shader-file operator, the preset is a small JSON of its declared params — near-free, given
   ADR-0016 already makes those params first-class.

### Boundary rule — what this is not

- **Not asset relinking.** Moved media stays broken (visibly, via ADR-0019's badges — the trunk
  already tracks `missing_media` in `ProjectState`). A relinking flow is a follow-on; classic never
  had one either.
- **Not a content registry or marketplace.** Local scopes only — shipped, user, project. Remote
  package catalogs are a separate, later question (and one `TODO.md` raises independently: *"How are
  we going to deal with community-contributed packages?"*).
- **Not a replacement for the Tab chooser.** The chooser stays the one way to add a node. The browser
  is a richer view onto the same catalog, not a second door.
- **Not persistence by `asset_id`.** Graphs keep storing canonical paths, exactly as classic decided
  for its v1. An id-based indirection is a schema change and it can wait for a reason to exist.

## Consequences

**Good.** Completes ADR-0016 — a shader library you can browse, fork, and drop into a graph is the
feature; a directory of `.wgsl` files is not. Makes the demos that already ship *discoverable*, which
is disproportionately valuable for a first run. Generalizing `ShaderLibrary` rather than duplicating
it means one discovery path, one index, one error convention.

**Costs.** Generalizing a working, in-use class is riskier than writing a new one beside it — the
shader path is live on this branch and must not regress. C1 should be a strict refactor (behavior
identical, tests green) *before* any new kind is added.

**Risk.** Scope. "Asset library" can absorb unbounded work — tags, favorites, ratings, audition,
batch import, duplicate detection. Classic deferred every one of those. Ship the layer with one or
two kinds and a filter, and let real usage ask for the rest.

## Implementation

### C1 — Generalize `ShaderLibrary` → `AssetLibrary`

A pure refactor: kinds, an index, scoped search paths. `ShaderLibrary` becomes the shader *kind*
over the generic layer. No new behavior.

*Verify:* `app/tests/test_shader_library.cpp` must pass **unchanged**. Run the app and confirm the
shipped shaders still register as operator types exactly as they do today. This gate is the whole
safety of the refactor — if the existing test needs editing, the refactor changed behavior and is
wrong.

### C2 — Browser dialog + chooser integration

Kind/tag filter, thumbnails. Reachable from the chooser and from `asset_kind` file params.

*Verify:* run the app — press Tab, confirm shader operators still appear and rank as before; open the
browser, filter by kind, confirm every shipped shader is listed **including ones with parse errors,
showing why** (the `ShaderLibraryEntry::error` contract).

### C3 — File drop

Drop registry; operators declare handled extensions; drop on the canvas → offer/create.

*Verify:* run the app — drag an `.mp4` onto the visual graph and confirm the video node is created
with its file param set. Drag an unhandled extension and confirm a clear "nothing handles this"
message (ADR-0019's toast), not silence.

### C4 — Examples browser

File → Open Example over `examples/`, using the existing recents/file-actions machinery.

*Verify:* run the app, open each of the four shipped demos from the picker, confirm each loads and
plays.

### C5 — Node presets

Save/recall named param sets per node; factory presets shipped with an operator.

*Verify:* save a preset on a shader node, change its params, recall the preset, confirm the params
return. Confirm the preset survives a session save/load round-trip, and that it appears over MCP
alongside the existing plugin `list_presets` flow rather than as a second, parallel one.
