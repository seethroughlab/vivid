// ADR-0017/G1 — the undo history cursor (app/undo_manager.h). Pure data, fully headless: exercises
// the baseline/push/undo/redo/coalesce/depth-cap/label semantics ported from vivid-classic.
#include "app/undo_manager.h"
#include "test_helpers.h"

#include <string>

using namespace vivid;

namespace {

void test_baseline_and_undo_redo() {
    UndoManager u;
    CHECK(!u.can_undo());          // empty: nothing to undo
    CHECK(!u.can_redo());

    u.push("S0", false, "");       // baseline (state before the first edit)
    CHECK(u.size() == 1);
    CHECK(!u.can_undo());          // only the baseline: still nothing to revert

    u.push("S1", false, "Edit A");
    u.push("S2", false, "Edit B");
    CHECK(u.can_undo());
    CHECK(!u.can_redo());
    CHECK(u.undo_label() == "Edit B");   // undo would revert the action that produced the top

    std::string out;
    CHECK(u.undo(out) && out == "S1");   // back to the state before Edit B
    CHECK(u.undo_label() == "Edit A");
    CHECK(u.redo_label() == "Edit B");
    CHECK(u.undo(out) && out == "S0");   // back to baseline
    CHECK(!u.can_undo());

    CHECK(u.redo(out) && out == "S1");
    CHECK(u.redo(out) && out == "S2");
    CHECK(!u.can_redo());
}

void test_push_after_undo_truncates_redo() {
    UndoManager u;
    u.push("S0"); u.push("S1", false, "A"); u.push("S2", false, "B");
    std::string out;
    u.undo(out);                   // now at S1, redo -> S2 available
    CHECK(u.can_redo());
    u.push("S1b", false, "C");     // a new edit discards the S2 redo tail
    CHECK(!u.can_redo());
    u.undo(out); CHECK(out == "S1");
}

void test_replace_top_coalesces() {
    UndoManager u;
    u.push("S0");
    u.push("v1", false, "Adjust");
    u.push("v2", true, "Adjust");   // coalesce: replaces the top rather than appending
    u.push("v3", true, "Adjust");
    CHECK(u.size() == 2);           // baseline + one coalesced entry
    std::string out;
    CHECK(u.undo(out) && out == "S0");   // one undo reverts the whole coalesced run
}

void test_depth_cap_evicts_front() {
    UndoManager u(3);               // baseline + 2
    u.push("S0");
    u.push("S1", false, "A");
    u.push("S2", false, "B");
    u.push("S3", false, "C");       // over cap: S0 evicted from the front
    CHECK(u.size() == 3);
    std::string out;
    CHECK(u.undo(out) && out == "S2");
    CHECK(u.undo(out) && out == "S1");
    CHECK(!u.can_undo());           // S0 is gone; S1 is the new floor
}

// UX Phase-2 F1: opening a project re-seeds the baseline (EditGateway::reset_baseline() =
// clear() + push(new baseline)). After a reseed the PRIOR document's history must be unreachable —
// an undo right after Open must not cross the load back into the previous project. This guards the
// primitive the load-path fix leans on (frame.cpp reseed → reset_baseline on project load/new).
void test_reseed_clears_cross_document_history() {
    UndoManager u; std::string out;
    // Project A: baseline + two edits (a dirty session, as before opening another project).
    u.push("A0", false, ""); u.push("A1", false, "Edit A1"); u.push("A2", false, "Edit A2");
    CHECK(u.can_undo());
    // Open project B → reseed: clear the history, seed B as the new baseline (entry 0).
    u.clear();
    u.push("B0", false, "");
    CHECK(u.size() == 1);
    CHECK(!u.can_undo());                 // a freshly opened project has nothing to undo...
    CHECK(!u.can_redo());                 // ...and no stale redo tail from project A
    // An edit in B undoes to B's baseline, never to any project-A snapshot.
    u.push("B1", false, "Edit B1");
    CHECK(u.undo(out) && out == "B0");    // back to B's baseline — not "A2"/"A1"/"A0"
    CHECK(!u.can_undo());
}

}  // namespace

int main() {
    test_baseline_and_undo_redo();
    test_push_after_undo_truncates_redo();
    test_replace_top_coalesces();
    test_depth_cap_evicts_front();
    test_reseed_clears_cross_document_history();
    return vivid::test::summary("undo_manager");
}
