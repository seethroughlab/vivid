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

}  // namespace vivid::ui
