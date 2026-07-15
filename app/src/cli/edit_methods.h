#pragma once

#include <string>

// ADR-0017/G2 — the one table of which control-server methods are DOCUMENT edits (and their undo
// labels), consulted once at the dispatch chokepoint (control_server.cpp process_pending) so every
// MCP mutation is captured in one place. A new handler is undoable only when listed here; read-only
// (get_*/list_*/status) and performance methods (launch/arm/record/…) are simply absent. Grows as
// G2 (visual+mapping) → G3 (audio) route more families.
namespace vivid {

struct EditMethodInfo {
    const char* label;      // human undo label ("Add Node")
    bool        coalesces;  // true => a rapid run of this method merges into one entry (param-ish)
};

// The edit info for a mutating method, or nullptr for read-only / non-document methods.
const EditMethodInfo* edit_method_info(const std::string& method);

}  // namespace vivid
