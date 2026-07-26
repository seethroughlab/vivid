# ADR-0028: One Source-Id Language for the Audio→Visual Bridge

Status: accepted (2026-07-25)

Date: 2026-07-25

Extends [ADR-0025](ADR-0025-cpp17-organization-and-patterns.md) and builds on the bridge source-id grammar
header (`app/src/app/bridge_source.h`, introduced in #141).

## Context

The audio→visual bridge lets any audio-side signal drive a visual param. A "source" is named by a string id
the visuals graph resolves (`DataNode.source` / `MappingRegistry`). That naming is **mid-migration**, and
the seam is the debt:

- **Two encodings coexist.** The original encoding is a packed integer, `char_id_for(t, kind) = 100 + t*8 +
  kind` (`app/src/ui/layout.h:107`), with `kind` 0..7 (level/transient/low/mid/high + note/velocity/gate).
  The newer sources (per-track/master FFT bands, per-node RMS/FFT, modulator control-out) are **string ids**
  (`track_<id>.fft.k`, `node_<t>_<n>.ctl`, …). The frame publisher still emits the *scalar* track/master
  characteristics through the legacy `char_id` path (`graph.set_value(char_id_for(tid, k), …)`) while
  emitting everything else as strings (`graph.set_source_by_id(...)`). `NodeGraph` carries a `set_value(int)`
  compat wrapper and decodes legacy `char_id`s on load to keep old documents working.

- **The grammar was duplicated.** #141 pulled the string grammar into one header (`bridge_source.h`) shared
  by the frame publisher and the Tab/right-click catalog builder, because the two must emit byte-identical
  ids or a wired node silently reads nothing. That header is the seed — but the *scalar* path still bypasses
  it via `char_id`, so the migration is only half-finished.

- **Per-frame string churn.** `publish_bridge_sources` (`app/src/app/frame.cpp`) rebuilds source-id strings
  every frame for every track and every audio-graph node — `"track_" + std::to_string(tid) + ".fft." +
  std::to_string(k)` and friends — allocating and hashing constant strings 60×/second on the UI thread. The
  ids are stable for the life of a node; only their *values* change per frame.

The result is a naming layer that speaks two languages, resolves through two code paths, and re-allocates
its vocabulary every frame.

## Decision

Make the string source-id the **single** language of the bridge, and stop re-deriving it per frame.

1. **Finish the `char_id → string` migration.** Route the scalar track/master characteristics through
   `bridge_source.h` (`track_source(tid, kind)` / `master.<kind>`) like every other source, so the publisher
   has exactly one emission path (`set_source_by_id`). Retire `set_value(int)` and the live `char_id`
   emission. Keep `char_id` decoding **only** as a persistence shim for loading old documents (a one-way
   legacy→string map at load time), clearly marked as compat, not as a live runtime encoding.

2. **`bridge_source.h` is the sole grammar.** Every id — scalar, fft, node rms/fft, ctl, note — is built by a
   helper there. No call site concatenates `"track_"` / `"node_"` / `"master."` by hand. The kind tables
   (`kTrackKindLabels` / `kTrackKindSuffixes`) are the single source of truth for the kind list.

3. **Intern ids so publication doesn't allocate.** A source's id is stable for a node's lifetime, so build it
   once and publish by a cheap handle. Concretely: resolve `(track_id, kind)` / `(track_id, node_id, kind)`
   to a small integer **slot** the first time it is seen (an interner keyed by the id string), and have
   `publish_bridge_sources` write values by slot each frame. The string is created once per source, not once
   per source per frame. This keeps the string as the *identity* while removing it from the hot path — the
   value write becomes an array store, not a map insert over a freshly-built string.

## Alternatives Considered

- **Keep both encodings indefinitely.** Rejected. The dual path is a standing DRY hazard (a fourth place
  that emits ids, `list_mapping_sources` in the control layer, already has to agree with two others) and the
  scalar/string split makes "how is this source named?" have two answers depending on kind.
- **Go the other way — make `char_id` the one encoding.** Rejected. The integer packing (`100 + t*8 + kind`,
  8 kinds max per track) cannot express fft bands, per-node sources, or control-out without inventing more
  packed ranges; strings already express all of them and are self-describing in the UI, MCP, and persisted
  documents.
- **Cache the built strings but keep publishing by string key.** Rejected as insufficient: caching avoids the
  allocation but still hashes a string per source per frame. Interning to an integer slot removes both.
- **Do nothing about churn (it's "only" UI-thread).** Rejected. It is cheap per call but scales with
  tracks × nodes × frames and sits on the frame path; interning is a small, contained change with a real
  ceiling benefit as sessions grow.

## Consequences

- **Positive:** One naming language, one emission path, one grammar header — "how is a source named?" has a
  single answer, and the publisher/catalog/introspection surfaces can't drift.
- **Positive:** No per-frame string allocation in `publish_bridge_sources`; value publication is an array
  store keyed by an interned slot.
- **Positive:** `char_id` shrinks to a clearly-scoped load-time persistence shim, so new code never touches
  the legacy encoding.
- **Tradeoff:** The interner is new machinery (a string→slot map built lazily on the UI thread, invalidated
  when nodes/tracks are added or removed) — it must invalidate correctly on topology change or a stale slot
  publishes to the wrong source. A headless test over "add/remove track ⇒ slots re-resolve" guards this.
- **Tradeoff:** Old-document compatibility must be verified — a persisted graph using `char_id` mappings has
  to load into the string world unchanged.
- **Follow-up:** After migration, `list_mapping_sources` (control layer) should also build its ids through
  `bridge_source.h`, closing the last hand-rolled emitter.
