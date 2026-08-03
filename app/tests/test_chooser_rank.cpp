// ADR-0046: the add-node chooser ranks composable primitives above bundled RECIPE operators.
// This pins the ordering policy (ui/chooser_rank.h) without a live renderer: recipes sink to the
// bottom, and relative order is preserved within each group (a stable partition).
#include "ui/chooser_rank.h"
#include "test_helpers.h"

#include <string>
#include <vector>

namespace {
struct Row { std::string name; VividOperatorRole role; };
}  // namespace

int main() {
    using vivid::ui::chooser_role_is_demoted;
    using vivid::ui::demote_recipes;

    // The policy predicate: only RECIPE is demoted today; every primitive role stays.
    CHECK(chooser_role_is_demoted(VIVID_OP_ROLE_RECIPE));
    CHECK(!chooser_role_is_demoted(VIVID_OP_ROLE_DEFAULT));
    CHECK(!chooser_role_is_demoted(VIVID_OP_ROLE_SOURCE));
    CHECK(!chooser_role_is_demoted(VIVID_OP_ROLE_ADAPTER));
    CHECK(!chooser_role_is_demoted(VIVID_OP_ROLE_RENDERER));

    // A catalog with recipes interleaved among primitives.
    std::vector<Row> rows = {
        { "AudioSpectrum", VIVID_OP_ROLE_SOURCE  },
        { "Instancer",     VIVID_OP_ROLE_RECIPE  },
        { "LaneRamp",      VIVID_OP_ROLE_SOURCE  },
        { "Emitter",       VIVID_OP_ROLE_RECIPE  },
        { "Image",         VIVID_OP_ROLE_DEFAULT },   // unclassified => primitive
        { "Solids",        VIVID_OP_ROLE_RECIPE  },
    };

    demote_recipes(rows, [](const Row& r) { return r.role; });

    // Primitives first, in their original relative order...
    CHECK(rows[0].name == "AudioSpectrum");
    CHECK(rows[1].name == "LaneRamp");
    CHECK(rows[2].name == "Image");
    // ...then the recipes, also in their original relative order.
    CHECK(rows[3].name == "Instancer");
    CHECK(rows[4].name == "Emitter");
    CHECK(rows[5].name == "Solids");

    // Idempotent: partitioning an already-partitioned list changes nothing.
    demote_recipes(rows, [](const Row& r) { return r.role; });
    CHECK(rows[0].name == "AudioSpectrum");
    CHECK(rows[5].name == "Solids");

    // All-primitive and all-recipe lists are left untouched.
    std::vector<Row> prims = { { "A", VIVID_OP_ROLE_SOURCE }, { "B", VIVID_OP_ROLE_DEFAULT } };
    demote_recipes(prims, [](const Row& r) { return r.role; });
    CHECK(prims[0].name == "A" && prims[1].name == "B");

    return vivid::test::summary("test_chooser_rank");
}
