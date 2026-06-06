// Lane-value clean-break, Phase 1: the operator-API side of the value model.
//
// Proves (compile-time) that get_multiplicity_behavior<T>() picks an explicit
// kMultiplicityBehavior override when present and otherwise derives from the
// operator's lane_behavior, and (runtime) that value_view.h is includable and
// its typed helpers behave. The lane API is untouched; this is purely additive.

#include "operator_api/operator.h"
#include "operator_api/value_view.h"
#include "test_helpers.h"

#include <string>

namespace {

struct PlainOp {};  // declares neither kLaneBehavior nor kMultiplicityBehavior
struct ReduceLane     { static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_REDUCTION; };
struct StructuralLane { static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL; };
struct KernelLane     { static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_KERNEL; };
struct Overridden {
    static constexpr VividLaneBehavior         kLaneBehavior         = VIVID_LANE_POINTWISE;   // would derive Map
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_COLLECT;  // explicit wins
};

using vivid::detail::get_multiplicity_behavior;

// Compile-time contract proofs (default derivation + per-lane mapping + override).
static_assert(get_multiplicity_behavior<PlainOp>()        == VIVID_MULTIPLICITY_MAP,      "default derives Map");
static_assert(get_multiplicity_behavior<ReduceLane>()     == VIVID_MULTIPLICITY_REDUCE,   "REDUCTION -> Reduce");
static_assert(get_multiplicity_behavior<StructuralLane>() == VIVID_MULTIPLICITY_GENERATE, "STRUCTURAL -> Generate");
static_assert(get_multiplicity_behavior<KernelLane>()     == VIVID_MULTIPLICITY_KERNEL,   "KERNEL -> Kernel");
static_assert(get_multiplicity_behavior<Overridden>()     == VIVID_MULTIPLICITY_COLLECT,  "explicit kMultiplicityBehavior wins");

}  // namespace

int main() {
    std::fprintf(stderr, "--- test_value_model ---\n");

    // value_view.h is includable from operator-style code and its typed helpers work.
    float buf[3] = {1.0f, 2.0f, 3.0f};
    VividValueView v{};
    v.data         = buf;
    v.value_count  = 3;
    v.value_type   = VIVID_VALUE_FLOAT;
    v.multiplicity = VIVID_MULTIPLICITY_MANY;
    check(vivid_value_floats(&v) == buf, "float accessor returns data for a Float view");
    check(vivid_value_strings(&v) == nullptr, "string accessor rejects a Float view");
    check(vivid_value_count(&v) == 3u, "value_count helper");

    // Runtime mirror of the lane->behavior derivation (so the test exercises it too).
    check(vivid::detail::multiplicity_behavior_from_lane(VIVID_LANE_POINTWISE) == VIVID_MULTIPLICITY_MAP,
          "POINTWISE -> Map");
    check(vivid::detail::multiplicity_behavior_from_lane(VIVID_LANE_KERNEL) == VIVID_MULTIPLICITY_KERNEL,
          "KERNEL -> Kernel");

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
