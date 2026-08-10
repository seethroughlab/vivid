# Clip-mutation helpers: native vs. bridge (Phase 7 disposition)

Date: 2026-08-09

> Records the disposition of the API-cleanup handoff's **Phase 7** ("promote Python read-modify-write
> clip helpers to native control handlers"). Outcome: **keep them in the Python bridge.** This is the
> inventory the phase asked for, plus the evidence behind the decision, so it isn't re-litigated.

## The proposal

The handoff observed that `mcp/vivid_mcp.py` mutates clips by reading (`get_clip`) and writing
(`set_clip`) from Python, and proposed promoting the core edits to native control handlers for
"validation in one place, undo/edit classification in one place, less bridge-side state."

## Inventory — Python clip-mutation tools

All compose a note list in Python and write it with one `set_clip` (REPLACE) or read-modify-write
(RMW = `get_clip` → transform → `set_clip`). All but `clear_clip` depend on `mcp/theory.py`.

| Tool | Mode | Needs `theory.py` |
|---|---|---|
| `add_notes` | RMW | ✓ (`norm_notes`) |
| `clear_clip` | RMW | — |
| `add_chord` | RMW | ✓ (`chord`) |
| `set_progression` | REPLACE | ✓ (`chord`/`roman`) |
| `arpeggiate` | RMW/REPLACE | ✓ |
| `set_drum_pattern` | REPLACE | ✓ (`drum_note`/`drum_steps`) |
| `euclidean_fill` | RMW | ✓ (`euclidean`) |
| `transpose` | RMW | ✓ |
| `quantize_to_scale` | RMW | ✓ (scale tables) |
| `harmonize` | RMW | ✓ |
| `invert_clip` / `retrograde_clip` | RMW | ✓ |
| `humanize` | RMW | ✓ |
| `quantize_rhythm` | RMW | ✓ |

`mcp/theory.py` is **434 lines / 30 functions** (chords, scales, roman numerals, voicings, euclidean,
humanize, Krumhansl–Schmuckler key detection).

## Why they stay in the bridge

1. **The stated robustness goals already hold.** Every tool funnels through `set_clip`, which is
   already a classified **atomic** edit method — `{ "set_clip", { "Edit Clip", true } }` in
   [`app/src/cli/edit_methods.cpp`](../app/src/cli/edit_methods.cpp), captured once at the dispatch
   chokepoint (`control_server.cpp` `process_pending`, ADR-0017). So each transform is already **one
   undoable, labeled edit**, and `set_clip` validates the notes in one native place. Promoting the
   transforms adds no undo/validation robustness.
2. **The cost is a large duplication for ~nil gain.** Native promotion means porting the 434-line
   `theory.py` to C++ — duplicating tested logic into the compiled core.
3. **It cuts against the product posture** (ADR-0046/0054 north star): music-theory *authoring* logic
   belongs in the accessible, LLM-empowered bridge layer, not baked into the core.

## The two real items (out of scope, noted for later)

- **`_key_ctx` is ephemeral bridge state.** The session key/scale (`set_key`/`get_key`) lives only in
  the Python process and is not saved with the project. This — not the transforms — is the genuine
  "bridge-side state" smell. Small, optional native/persistence item if it ever matters.
- **A narrow `get_clip`→`set_clip` TOCTOU.** The two dispatches aren't adjacent, so two agents editing
  the *same* clip concurrently could clobber one edit. Low impact (MCP is typically single-agent). If
  it ever matters, a clip-edit transaction/lock is a lighter fix than porting theory.
