// Lane-value clean-break: the operator-API side of the value model.
//
// Proves (compile-time) that get_multiplicity_behavior<T>() returns an operator's
// declared kMultiplicityBehavior, and defaults to Map when none is declared
// (lane_behavior derivation was removed in 7e.6a), and (runtime) that value_view.h
// is includable and its typed helpers behave.

#include "operator_api/operator.h"
#include "operator_api/value_view.h"
#include "test_helpers.h"

#include <string>

namespace {

struct PlainOp {};  // declares no kMultiplicityBehavior → defaults to Map
struct ReduceOp     { static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_REDUCE; };
struct GenerateOp   { static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE; };
struct KernelOp     { static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_KERNEL; };
struct CollectOp    { static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_COLLECT; };

using vivid::detail::get_multiplicity_behavior;

// Compile-time contract proofs (declared value + default).
static_assert(get_multiplicity_behavior<PlainOp>()    == VIVID_MULTIPLICITY_MAP,      "default is Map");
static_assert(get_multiplicity_behavior<ReduceOp>()   == VIVID_MULTIPLICITY_REDUCE,   "declared Reduce");
static_assert(get_multiplicity_behavior<GenerateOp>() == VIVID_MULTIPLICITY_GENERATE, "declared Generate");
static_assert(get_multiplicity_behavior<KernelOp>()   == VIVID_MULTIPLICITY_KERNEL,   "declared Kernel");
static_assert(get_multiplicity_behavior<CollectOp>()  == VIVID_MULTIPLICITY_COLLECT,  "declared Collect");

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

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
