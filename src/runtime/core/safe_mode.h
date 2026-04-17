#pragma once

#include "runtime/core/crash_recovery.h"

#include <string>
#include <unordered_set>

namespace vivid {

// ---------------------------------------------------------------------------
// SafeModeConfig — in-memory safe-mode state carried by RuntimeCore.
//
// Activated by the --safe-mode CLI flag.  Populated at startup from the most
// recent CrashRecord (if any).  Consumed by the graph compiler to disable
// suspect nodes and by main.cpp to skip audio_engine.start() and hot-reload
// init.  Never persisted to the graph JSON.
// ---------------------------------------------------------------------------
struct SafeModeConfig {
    bool active = false;

    // Node IDs and operator types the compiler must treat as disabled
    // (missing_operator = true, reason = "disabled").
    std::unordered_set<std::string> disabled_node_ids;
    std::unordered_set<std::string> disabled_types;

    // Quarantined types from the crash-history scan (Phase 4).  Receives
    // reason "quarantined" from the compiler.  When a type is in both
    // disabled_types and quarantined_types, main.cpp removes it from
    // disabled_types so the UI shows a single consistent label.
    std::unordered_set<std::string> quarantined_types;

    // Purely informational, copied from the originating CrashRecord so UI
    // and logs can explain why safe mode was engaged.
    std::string crash_operator;
    std::string crash_node_id;
    std::string crash_reason;     // "SIGSEGV", "SIGBUS", etc.
};

// Build a SafeModeConfig from a prior-run CrashRecord.
//
// active is always true — the caller (main.cpp) decides whether to apply
// this config based on the --safe-mode flag.  When rec is nullptr, the
// disabled sets are empty; safe mode still skips audio start + hot-reload
// but no nodes are suppressed.
//
// Disabling strategy:
//   - Add rec->node_type (or operator_name as fallback) to disabled_types so
//     every instance of the suspect operator is suppressed on this and any
//     subsequent graph load.
//   - Also add rec->node_id to disabled_node_ids for the specific node the
//     crash was attributed to.  Explicit IDs are useful for Phase 5 MCP
//     callers that want per-node surgery.
inline SafeModeConfig compute_safe_mode_config(const CrashRecord* rec) {
    SafeModeConfig c;
    c.active = true;
    if (!rec) return c;

    c.crash_operator = rec->operator_name;
    c.crash_node_id  = rec->node_id;
    c.crash_reason   = rec->signal_name;

    if (!rec->node_type.empty()) {
        c.disabled_types.insert(rec->node_type);
    } else if (!rec->operator_name.empty()) {
        c.disabled_types.insert(rec->operator_name);
    }
    if (!rec->node_id.empty()) {
        c.disabled_node_ids.insert(rec->node_id);
    }
    return c;
}

} // namespace vivid
