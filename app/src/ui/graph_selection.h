#pragma once
#include "ui/layout.h"   // Rect (pure, header-only — no renderer/GPU deps)
#include <set>
#include <vector>

// ADR-0033 Phase 1 — the shared multi-select state for BOTH graph editors (the visuals `NodeGraph`
// and the per-track `AudioNodeGraph`). Deliberately dependency-free (no renderer / model / GPU) so it
// is a standalone, headlessly-testable unit — the selection + marquee-intersect math lives in exactly
// one place and is pinned by a unit test (see app/tests/test_graph_selection.cpp), mirroring how
// ui/node_view.h isolates the camera math.
//
// KEYED BY STABLE ID. The set holds stable node ids (AdapterNode.id — the visuals VisualNode.id or the
// audio graph node id), NOT array indices. Ids survive node deletion, so a multi-selection stays
// correct across removals; each editor resolves id<->index at its own call sites. `primary()` is the
// anchor / inspector target (the single node the detail dock edits), always a member of the set unless
// the set is empty. Selection is VIEW-STATE: never persisted, never undoable.
namespace vivid::ui {

// A laid-out node for marquee resolution: its stable id and its world-space card rect.
struct SelItem { int id; Rect rect; };

// Do two world-space rects overlap? (Half-open on the far edge, matching ui::hit.) Pure + free so the
// marquee-intersect predicate has one definition the test can pin directly.
inline bool rects_overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

// Normalize a rect that may have been dragged up/left (negative w/h) into positive extents.
inline Rect normalize_rect(const Rect& r) {
    Rect n = r;
    if (n.w < 0.f) { n.x += n.w; n.w = -n.w; }
    if (n.h < 0.f) { n.y += n.h; n.h = -n.h; }
    return n;
}

class GraphSelection {
public:
    void clear() { ids_.clear(); primary_ = -1; }

    // Collapse to a single node (the plain-click case). id < 0 clears.
    void replace(int id) {
        ids_.clear();
        if (id >= 0) { ids_.insert(id); primary_ = id; }
        else primary_ = -1;
    }

    // Add a node to the selection, making it the new primary (idempotent on membership).
    void add(int id) {
        if (id < 0) return;
        ids_.insert(id);
        primary_ = id;
    }

    // Flip membership (Shift/Cmd-click). Adding makes it primary; removing the primary re-anchors to
    // any remaining member (or -1 when empty).
    void toggle(int id) {
        if (id < 0) return;
        if (ids_.erase(id)) {
            if (primary_ == id) primary_ = ids_.empty() ? -1 : *ids_.begin();
        } else {
            ids_.insert(id);
            primary_ = id;
        }
    }

    bool contains(int id) const { return ids_.count(id) != 0; }
    int  primary() const { return primary_; }
    // Re-anchor without changing membership; ignored if `id` is not a member.
    void set_primary(int id) { if (contains(id)) primary_ = id; }

    int  size() const { return static_cast<int>(ids_.size()); }
    bool empty() const { return ids_.empty(); }
    const std::set<int>& ids() const { return ids_; }

    // Resolve a marquee (rubber-band) gesture against the laid-out nodes. `additive` (Cmd held) unions
    // with the existing selection; otherwise the marquee replaces it. Corners may be inverted — they
    // are normalized here. The last node added becomes the primary so the inspector follows the gesture.
    void resolve_marquee(const std::vector<SelItem>& nodes, const Rect& marquee, bool additive) {
        if (!additive) clear();
        const Rect m = normalize_rect(marquee);
        for (const SelItem& n : nodes) {
            if (rects_overlap(n.rect, m)) { ids_.insert(n.id); primary_ = n.id; }
        }
        if (primary_ < 0 && !ids_.empty()) primary_ = *ids_.begin();
    }

private:
    std::set<int> ids_;
    int primary_ = -1;
};

}  // namespace vivid::ui
