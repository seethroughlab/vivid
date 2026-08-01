// Headless test for the structural blank-vs-empty predicate (gpu/output_feed.h, P2-03).
// output_is_fed() is the single rule behind "the active Output has a feed" — pure topology,
// no GPU — so the render path, the health snapshot, and the MCP quality check all agree.
#include "gpu/output_feed.h"
#include "test_helpers.h"

int main() {
    using vivid::output_is_fed;

    // No Output node at all (output_index() == -1) → not fed, whatever the other args.
    CHECK(!output_is_fed(-1, -1, 0));
    CHECK(!output_is_fed(-1, 2, 5));

    // Output exists but its in(0) is unconnected (-1) → empty by design → not fed.
    CHECK(!output_is_fed(0, -1, 1));
    CHECK(!output_is_fed(3, -1, 4));

    // Output exists and a real, in-range producer feeds it → fed.
    CHECK(output_is_fed(1, 0, 2));
    CHECK(output_is_fed(4, 2, 5));
    CHECK(output_is_fed(0, 0, 1));   // a node may (degenerately) be its own feed index

    // A feed index out of range (a stale edge after node removal) is NOT a valid feed.
    CHECK(!output_is_fed(1, 5, 2));
    CHECK(!output_is_fed(1, 2, 2));   // == node_count is out of range (indices are 0..count-1)

    return vivid::test::summary("test_output_feed");
}
