#pragma once

#include <string>

struct VividOperatorDescriptor;

namespace vivid {

// Produce a stable fingerprint of an operator's public interface.
//
// Serializes the descriptor's name + flags + params + ports into a canonical
// text form, then SHA-256s the result. Changes on rename, type change,
// default-value change, added/removed param or port, flag change, or any
// semantic-metadata change. Does NOT depend on dylib path, ABI version, or
// implementation internals.
//
// Returns "sha256:<64-hex>". Returns empty string if the descriptor is null.
std::string operator_descriptor_hash(const VividOperatorDescriptor* desc);

}  // namespace vivid
