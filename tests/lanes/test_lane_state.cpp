// Unit tests for LaneStateService — per-lane persistent state keyed by
// (node_idx, lane_id). Verifies get/retire/sweep/identity semantics.

#include "runtime/graph/lane_state.h"
#include <cstdio>
#include <cstring>
#include "test_helpers.h"

// ---------------------------------------------------------------------------
// Test 1: get returns zero-initialized storage
// ---------------------------------------------------------------------------

static void test_get_zero_initialized() {
    std::fprintf(stderr, "\n--- lane_state: get returns zero-initialized storage ---\n");

    vivid::LaneStateService svc;
    svc.pre_allocate(0, 1, 16);

    auto* ptr = static_cast<uint8_t*>(svc.get(0, 1, 16));
    check(ptr != nullptr, "get returns non-null");

    bool all_zero = true;
    for (uint32_t i = 0; i < 16; ++i) {
        if (ptr[i] != 0) all_zero = false;
    }
    check(all_zero, "storage is zero-initialized");
}

// ---------------------------------------------------------------------------
// Test 2: same (node_idx, lane_id) returns same pointer
// ---------------------------------------------------------------------------

static void test_identity_stable() {
    std::fprintf(stderr, "\n--- lane_state: same key returns same pointer ---\n");

    vivid::LaneStateService svc;
    svc.pre_allocate(0, 42, 32);

    void* p1 = svc.get(0, 42, 32);
    void* p2 = svc.get(0, 42, 32);
    check(p1 == p2, "same (node_idx, lane_id) returns same pointer");

    // Write to p1, read from p2 — should see the same data.
    static_cast<float*>(p1)[0] = 3.14f;
    check(static_cast<float*>(p2)[0] == 3.14f, "writes through p1 visible via p2");
}

// ---------------------------------------------------------------------------
// Test 3: different lane_ids get different storage
// ---------------------------------------------------------------------------

static void test_different_lanes() {
    std::fprintf(stderr, "\n--- lane_state: different lane_ids get different storage ---\n");

    vivid::LaneStateService svc;
    svc.pre_allocate(0, 1, 8);
    svc.pre_allocate(0, 2, 8);

    void* p1 = svc.get(0, 1, 8);
    void* p2 = svc.get(0, 2, 8);
    check(p1 != p2, "different lane_ids return different pointers");
}

// ---------------------------------------------------------------------------
// Test 4: different node_idxs get different storage for same lane_id
// ---------------------------------------------------------------------------

static void test_different_nodes() {
    std::fprintf(stderr, "\n--- lane_state: different nodes get different storage ---\n");

    vivid::LaneStateService svc;
    svc.pre_allocate(0, 1, 8);
    svc.pre_allocate(1, 1, 8);

    void* p1 = svc.get(0, 1, 8);
    void* p2 = svc.get(1, 1, 8);
    check(p1 != p2, "different node_idxs return different pointers");
}

// ---------------------------------------------------------------------------
// Test 5: retire + sweep frees storage across all nodes sharing lane_id
// ---------------------------------------------------------------------------

static void test_retire_and_sweep() {
    std::fprintf(stderr, "\n--- lane_state: retire + sweep frees storage across nodes ---\n");

    vivid::LaneStateService svc;
    svc.pre_allocate(0, 10, 16);
    svc.pre_allocate(1, 10, 16);

    void* p1 = svc.get(0, 10, 16);
    void* p_other_node = svc.get(1, 10, 16);
    check(p1 != nullptr, "entry exists before retire");
    check(p_other_node != nullptr, "same lane_id exists on another node before retire");

    // Write a marker value
    static_cast<uint8_t*>(p1)[0] = 0xAB;
    static_cast<uint8_t*>(p_other_node)[0] = 0xCD;

    svc.retire(0, 10);
    svc.sweep_retired();

    // After sweep, the lane_id is cleared for every node, not just the caller.
    void* p2 = svc.get(0, 10, 16);
    void* p2_other_node = svc.get(1, 10, 16);
    check(static_cast<uint8_t*>(p2)[0] == 0, "after retire+sweep, get returns zero (scratch)");
    check(static_cast<uint8_t*>(p2_other_node)[0] == 0,
          "after retire+sweep, other-node state for same lane_id is also cleared");
}

// ---------------------------------------------------------------------------
// Test 6: allocate_lane_id returns monotonic values
// ---------------------------------------------------------------------------

static void test_allocate_lane_id() {
    std::fprintf(stderr, "\n--- lane_state: allocate_lane_id is monotonic ---\n");

    vivid::LaneStateService svc;
    uint32_t id1 = svc.allocate_lane_id();
    uint32_t id2 = svc.allocate_lane_id();
    uint32_t id3 = svc.allocate_lane_id();

    check(id1 > 0, "first id > 0 (0 reserved for positional)");
    check(id2 == id1 + 1, "ids are sequential");
    check(id3 == id2 + 1, "ids are sequential");
}

// ---------------------------------------------------------------------------
// Test 7: clear resets everything
// ---------------------------------------------------------------------------

static void test_clear() {
    std::fprintf(stderr, "\n--- lane_state: clear resets everything ---\n");

    vivid::LaneStateService svc;
    svc.pre_allocate(0, 1, 8);
    void* p1 = svc.get(0, 1, 8);
    static_cast<uint8_t*>(p1)[0] = 0xFF;

    uint32_t id_before = svc.allocate_lane_id();

    svc.clear();

    // After clear, same key returns scratch (zero).
    void* p2 = svc.get(0, 1, 8);
    check(static_cast<uint8_t*>(p2)[0] == 0, "after clear, storage is zeroed");

    // Lane ID counter resets.
    uint32_t id_after = svc.allocate_lane_id();
    check(id_after == 1, "lane_id counter resets to 1 after clear");
}

// ---------------------------------------------------------------------------
// Test 8: first-access allocation is identity-stable (no pre_allocate needed)
// ---------------------------------------------------------------------------

static void test_first_access_stable() {
    std::fprintf(stderr, "\n--- lane_state: first-access allocation is identity-stable ---\n");

    vivid::LaneStateService svc;
    // No pre_allocate — get() allocates on first access.
    void* p1 = svc.get(5, 999, 32);
    check(p1 != nullptr, "first access returns non-null");

    // Zero-initialized.
    bool all_zero = true;
    for (uint32_t i = 0; i < 32; ++i) {
        if (static_cast<uint8_t*>(p1)[i] != 0) all_zero = false;
    }
    check(all_zero, "first access is zero-initialized");

    // Write a marker.
    static_cast<uint8_t*>(p1)[0] = 0xAB;

    // Second access returns same pointer with preserved data.
    void* p2 = svc.get(5, 999, 32);
    check(p1 == p2, "second access returns same pointer (identity-stable)");
    check(static_cast<uint8_t*>(p2)[0] == 0xAB, "data preserved across calls");

    // Different lane_id on same node gets different storage.
    void* p3 = svc.get(5, 1000, 32);
    check(p3 != p1, "different lane_id gets different storage");
}

// ---------------------------------------------------------------------------
// Test 9: live entry counts track per-node retained state
// ---------------------------------------------------------------------------

static void test_live_entry_counts() {
    std::fprintf(stderr, "\n--- lane_state: live entry counts track retained state ---\n");

    vivid::LaneStateService svc;
    svc.set_node_capacity(4);
    check(svc.live_entry_count(0) == 0, "initial live count is zero");

    svc.pre_allocate(0, 10, 8);
    check(svc.live_entry_count(0) == 1, "pre_allocate increments live count");

    (void)svc.get(0, 10, 8);
    check(svc.live_entry_count(0) == 1, "repeated get does not double count");

    (void)svc.get(1, 10, 8);
    check(svc.live_entry_count(1) == 1, "first access allocation increments other node count");

    svc.retire(0, 10);
    svc.sweep_retired();
    check(svc.live_entry_count(0) == 0, "retire sweep clears node 0 count");
    check(svc.live_entry_count(1) == 0, "retire sweep clears node 1 count");

    (void)svc.get(2, 77, 8);
    check(svc.live_entry_count(2) == 1, "new retained entry counted");
    svc.clear();
    check(svc.live_entry_count(2) == 0, "clear resets live counts");
}

// ---------------------------------------------------------------------------

int main() {
    std::fprintf(stderr, "=== test_lane_state ===\n");

    test_get_zero_initialized();
    test_identity_stable();
    test_different_lanes();
    test_different_nodes();
    test_retire_and_sweep();
    test_allocate_lane_id();
    test_clear();
    test_first_access_stable();
    test_live_entry_counts();

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
