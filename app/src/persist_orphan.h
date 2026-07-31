#pragma once

#include <nlohmann/json.hpp>

// ADR-0018 / Phase-4 audit P1-02: preserve a MISSING operator's authored values across save/load.
//
// A visual-graph chain node whose `op_type` isn't registered (uninstalled/quarantined package, or a
// project opened on a machine without that operator) has no live instance, so it reports zero params.
// The `params` / `file_params` / `pinned` the loader read from JSON therefore have nowhere to land,
// and on the next save the serializer — which iterates the (empty) live param list — would write them
// back as empty, permanently dropping the user's tuned values. The node's identity/topology/asset
// survive, but the parameter data does not.
//
// These two pure helpers make a round-trip through a degraded project lossless: at LOAD we stash the
// raw fragment on the node (capture_orphan_payload); at SAVE we splice it back verbatim
// (apply_orphan_payload). If the package is later installed and the project reloaded, the normal
// by-name param restore supersedes the orphan. Pure JSON transforms so they are unit-testable without
// the GPU/NodeGraph stack.
namespace vivid {

// Keys carried on a chain node that key off the (missing) op's params and must be preserved verbatim.
inline constexpr const char* kOrphanKeys[] = { "params", "file_params", "pinned" };

// Collect the orphan-preserved keys present on a serialized chain node into a fresh object. Returns an
// empty object when the node carries none (nothing to preserve).
inline nlohmann::json capture_orphan_payload(const nlohmann::json& chain_entry) {
    nlohmann::json out = nlohmann::json::object();
    if (chain_entry.is_object())
        for (const char* k : kOrphanKeys)
            if (chain_entry.contains(k)) out[k] = chain_entry[k];
    return out;
}

// Splice a previously captured orphan payload back onto a node being serialized, overwriting the
// empty placeholders the live (zero-param) serializer produced for a missing op.
inline void apply_orphan_payload(nlohmann::json& node_out, const nlohmann::json& orphan) {
    if (!orphan.is_object()) return;
    for (const char* k : kOrphanKeys)
        if (orphan.contains(k)) node_out[k] = orphan[k];
}

}  // namespace vivid
