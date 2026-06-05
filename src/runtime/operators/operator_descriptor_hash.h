#pragma once

#include <string>

struct VividOperatorDescriptor;

namespace vivid {

// Produce a stable fingerprint of an operator's *binding-relevant* interface.
//
// Serializes the descriptor's name + flags + params (including per-param
// semantic_* tags) + ports into a canonical text form, then SHA-256s the result.
// This covers the parts that determine whether a saved graph still binds and
// runs: rename, param/port type change, default-value change, added/removed
// param or port, flag change.
//
// It deliberately does NOT include the v3 presentation metadata (display_name,
// keywords, summary) — those are UI/search-only and don't affect graph binding,
// so a display-name-only change will not flip this fingerprint (and won't trip a
// project lockfile). Also independent of dylib path, ABI version, and
// implementation internals.
//
// Returns "sha256:<64-hex>". Returns empty string if the descriptor is null.
std::string operator_descriptor_hash(const VividOperatorDescriptor* desc);

}  // namespace vivid
