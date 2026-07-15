#pragma once

#include <nlohmann/json.hpp>

// ADR-0017/G1 — pure JSON helpers for undo. Kept OUT of persist.cpp (which pulls Session/NodeGraph/
// GPU) so they link into the headless test. They operate only on the session JSON that
// session_to_json produces.
namespace vivid {

// The canonical DOCUMENT projection of a session snapshot: strip the performance + view state that
// must not participate in undo, so two snapshots compare equal iff the DOCUMENT is the same. Stripped:
//   - "window"                     (window size / splitter / dock — view state)
//   - "graph"."view"               (canvas pan/zoom)
//   - each track's "active"        (the launched clip — a performance field)
//   - each track's plugin "state"  (+ clap_effects[].state): the plugin's OPAQUE binary patch. It is
//     plugin-owned, edited in the plugin's own GUI (never through the gateway, so un-undoable), and
//     many plugins' getState() returns non-deterministic bytes every call. Undo manages Vivid's
//     document, not a plugin's internal patch. (Save/load still persists it — this is undo-only.)
//   - each chain node's "base"     (a legacy positional duplicate of "params"; params is authoritative)
//   - the Output node's "preview"/"launch"/"display" params (where the output is shown — view state),
//     while KEEPING its aspect/height/fit (the output format — document).
// Idempotent: projecting a projection is a no-op.
nlohmann::json canonical_document_projection(const nlohmann::json& session);

// Do two projections carry the same audio "tracks" block? Equal => an undo/redo between them need
// not touch audio at all (no track rebuild, no plugin re-instantiation) — the "Skip" restore tier.
bool audio_block_equal(const nlohmann::json& a, const nlohmann::json& b);

}  // namespace vivid
