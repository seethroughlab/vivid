#pragma once
// ADR-0046 (Operators Are Composable Primitives First): ranking policy for the add-node chooser.
// Composable primitives are the preferred catalog surface; bundled RECIPE operators are still
// offered, but they must not crowd out the building blocks — so they sink to the bottom of the list.
//
// Factored out of NodeGraph::chooser_show so the ordering rule is unit-testable without a live
// renderer (see app/tests/test_chooser_rank.cpp).

#include <algorithm>
#include <vector>

#include "operator_api/types.h"   // VividOperatorRole

namespace vivid::ui {

// The one policy predicate: which roles get demoted below primitives. Today only RECIPE; kept as a
// named function so the rule has a single home if the policy grows (e.g. also demote deprecated ops).
inline bool chooser_role_is_demoted(VividOperatorRole role) {
    return role == VIVID_OP_ROLE_RECIPE;
}

// Stable-partition chooser rows so demoted-role (recipe) ops sink below everything else, preserving
// the relative order within each group. `role_of(entry)` yields the entry's role (DEFAULT for rows
// that carry no operator descriptor, e.g. bridge/data-source rows — those stay in the top group).
template <typename Entry, typename RoleOf>
void demote_recipes(std::vector<Entry>& entries, RoleOf role_of) {
    std::stable_partition(entries.begin(), entries.end(),
                          [&](const Entry& e) { return !chooser_role_is_demoted(role_of(e)); });
}

// The uppercase chip label a chooser row shows for its role, or nullptr for DEFAULT (no chip — plugins,
// shaders, bridge/data rows). This is the "labeled" half of the ADR decision (recipes are labeled AND
// ranked); kept here, next to the ranking policy, so both stay unit-testable without a live renderer.
inline const char* role_chip_label(VividOperatorRole role) {
    switch (role) {
        case VIVID_OP_ROLE_SOURCE:    return "SOURCE";
        case VIVID_OP_ROLE_TRANSFORM: return "TRANSFORM";
        case VIVID_OP_ROLE_ADAPTER:   return "ADAPTER";
        case VIVID_OP_ROLE_RENDERER:  return "RENDERER";
        case VIVID_OP_ROLE_SINK:      return "SINK";
        case VIVID_OP_ROLE_RECIPE:    return "RECIPE";
        default:                      return nullptr;   // DEFAULT / unclassified — no chip
    }
}

}  // namespace vivid::ui
