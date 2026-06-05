// Test: PluginSlot<HandleT> triple-buffer plugin-handle swap (audit 05-R2-F3).
//
// Exercises the mechanism the VST3/CLAP/AU instrument + effect operators share
// (extracted to operators/shared/plugin_common/plugin_slot.h): MAIN stages,
// AUDIO swaps in and retires to a 2-deep dying queue (never deleting on the
// audio thread), MAIN reclaims, and a third retire overflows by dropping the
// oldest. Uses a mock handle that records start/stop/destroy/overflow events —
// no real plugin or audio device needed. This is the device-free safety net for
// the eventual PluginOperatorBase extraction (05-R2-F2).

#include "shared/plugin_common/plugin_slot.h"
#include <vector>
#include "test_helpers.h"

using vivid::plugin_common::PluginSlot;

namespace {

// Mock plugin handle + a shared ledger of lifecycle events keyed by handle id.
struct World {
    std::vector<int> started, stopped, destroyed, overflowed;
};
struct MockHandle {
    int id;
    bool valid = true;   // mirrors VST3 `pend->component != nullptr` validity gate
    bool processing = false;
    World* w = nullptr;
};

// SDK-op callables (RT-safe: just record into the ledger / flip a flag).
auto stop_fn  = [](MockHandle* h) { if (h->processing) { h->processing = false; h->w->stopped.push_back(h->id); } };
auto valid_fn = [](MockHandle* h) { return h->valid; };
auto start_fn = [](MockHandle* h) { h->processing = true; h->w->started.push_back(h->id); };
auto destroy_fn = [](MockHandle* h) { h->w->destroyed.push_back(h->id); delete h; };
auto overflow_fn = [](MockHandle* h) { h->w->overflowed.push_back(h->id); /* leaked, do not delete */ };

MockHandle* make(World& w, int id, bool valid = true) { return new MockHandle{id, valid, false, &w}; }

// Convenience: run one audio-thread swap.
bool swap(PluginSlot<MockHandle>& slot) {
    return slot.swap_in_pending(stop_fn, valid_fn, start_fn, destroy_fn, overflow_fn);
}

bool contains(const std::vector<int>& v, int id) {
    for (int x : v) if (x == id) return true;
    return false;
}

}  // namespace

int main() {
    std::fprintf(stderr, "\n=== test_plugin_slot ===\n\n");

    // 1) Stage + swap: the staged handle becomes active and is started; no retire.
    {
        World w;
        PluginSlot<MockHandle> slot;
        check(slot.active() == nullptr, "active starts null");
        check(swap(slot) == false, "swap with no pending is a no-op");

        slot.stage(make(w, 1));
        check(slot.has_pending(), "stage sets pending");
        check(swap(slot) == true, "swap consumes pending");
        check(slot.active() && slot.active()->id == 1, "handle 1 is active");
        check(contains(w.started, 1) && w.started.size() == 1, "handle 1 started once");
        check(w.stopped.empty() && w.destroyed.empty() && w.overflowed.empty(), "no retire/destroy/overflow");
        slot.destroy_all(destroy_fn);
    }

    // 2) Swap a second handle over an active one: old is stopped + retired, then
    //    reclaimed (destroyed) on the main thread.
    {
        World w;
        PluginSlot<MockHandle> slot;
        slot.stage(make(w, 1)); swap(slot);
        slot.stage(make(w, 2)); swap(slot);
        check(slot.active()->id == 2, "handle 2 is active after second swap");
        check(contains(w.stopped, 1), "handle 1 stopped on retire");
        check(w.destroyed.empty(), "retired handle NOT destroyed on the audio thread");

        int n = slot.reclaim(destroy_fn);
        check(n == 1 && contains(w.destroyed, 1), "main-thread reclaim destroys handle 1");
        check(slot.active()->id == 2, "active unchanged by reclaim");
        slot.destroy_all(destroy_fn);
    }

    // 3) Three retires with no intervening reclaim → overflow drops the oldest.
    //    Swaps: A,B,C,D. Retire chain: A→dying, then B retire pushes A→dying2,
    //    then C retire overflows A. Final queue holds C(dying) + B(dying2); D active.
    {
        World w;
        PluginSlot<MockHandle> slot;
        slot.stage(make(w, 10)); swap(slot);  // active=10
        slot.stage(make(w, 11)); swap(slot);  // retire 10 → dying
        slot.stage(make(w, 12)); swap(slot);  // retire 11 → dying, 10 → dying2
        slot.stage(make(w, 13)); swap(slot);  // retire 12 → dying, 11 → dying2, 10 overflows
        check(slot.active()->id == 13, "handle 13 active after four swaps");
        check(w.overflowed.size() == 1 && contains(w.overflowed, 10), "oldest (10) overflowed exactly once");
        check(!contains(w.destroyed, 10), "overflowed handle was leaked, not destroyed");

        int n = slot.reclaim(destroy_fn);
        check(n == 2, "reclaim destroys the two queued dying handles");
        check(contains(w.destroyed, 11) && contains(w.destroyed, 12), "11 and 12 reclaimed");
        slot.destroy_all(destroy_fn);  // cleans up 13; 10 stays leaked (expected)
    }

    // 4) An invalid staged handle (failed load) is destroyed immediately and
    //    leaves active null.
    {
        World w;
        PluginSlot<MockHandle> slot;
        slot.stage(make(w, 1)); swap(slot);             // active=1
        slot.stage(make(w, 2, /*valid=*/false)); swap(slot);
        check(slot.active() == nullptr, "invalid staged handle leaves active null");
        check(contains(w.destroyed, 2), "invalid handle destroyed immediately");
        check(contains(w.stopped, 1), "previous active (1) was stopped + retired");
        check(!contains(w.started, 2), "invalid handle never started");
        slot.reclaim(destroy_fn);
        slot.destroy_all(destroy_fn);
    }

    // 5) destroy_all drains active + pending + dying.
    {
        World w;
        PluginSlot<MockHandle> slot;
        slot.stage(make(w, 1)); swap(slot);  // active=1
        slot.stage(make(w, 2)); swap(slot);  // 1 → dying, active=2
        slot.stage(make(w, 3));              // 3 pending, not swapped
        slot.destroy_all(destroy_fn);
        check(contains(w.destroyed, 1) && contains(w.destroyed, 2) && contains(w.destroyed, 3),
              "destroy_all disposes active + pending + dying");
        check(slot.active() == nullptr && !slot.has_pending(), "slot empty after destroy_all");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
