// ADR-0027: the shared PopupMenu component. Its whole point is that draw and click hit-test compute rows
// from ONE function (row_rect), so they can't drift. This pins that: the builders produce the right items
// + payload, and hit_row agrees with row_rect at every row (the invariant the old per-menu draw/hit pairs
// couldn't guarantee). Pure geometry — no GUI — so it runs in the headless tier.
#include "ui/popup_menu.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using vivid::ui::PopupMenu;

int main() {
    // --- builder: the per-track characteristics menu ------------------------------------------------
    PopupMenu master = vivid::ui::popup_track_chars(100.f, 200.f, /*track_index=*/-1, "Master");
    assert(master.open);
    assert(master.kind == PopupMenu::Kind::TrackChars);
    assert(master.a == -1);                                   // payload = the (master) track index
    assert(master.accent == PopupMenu::Teal);
    assert(static_cast<int>(master.items.size()) == vivid::ui::kNumCharsMaster);   // master: 5 (no notes)
    assert(std::strstr(master.header.c_str(), "Master") != nullptr);

    PopupMenu track = vivid::ui::popup_track_chars(10.f, 20.f, /*track_index=*/2, "Bass");
    assert(static_cast<int>(track.items.size()) == vivid::ui::kNumChars);           // a real track: 8
    assert(track.a == 2);
    assert(std::strstr(track.header.c_str(), "Bass") != nullptr);
    for (int j = 0; j < static_cast<int>(track.items.size()); ++j) assert(track.items[j].id == j);  // id = kChars index

    // --- builder: the map-source picker -------------------------------------------------------------
    PopupMenu map = vivid::ui::popup_map_sources(50.f, 60.f, /*node_id=*/7);
    assert(map.kind == PopupMenu::Kind::MapSource);
    assert(map.a == 7);                                       // payload = the audio-graph node id
    assert(map.accent == PopupMenu::Gold);
    assert(static_cast<int>(map.items.size()) == vivid::ui::kNumMapSources);

    // --- the core invariant: hit_row agrees with row_rect at every row ------------------------------
    for (int j = 0; j < static_cast<int>(track.items.size()); ++j) {
        const vivid::ui::Rect r = track.row_rect(j);
        const float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;   // centre of the drawn row
        assert(track.hit_row(cx, cy) == j);                        // ...hit-tests back to that row
    }
    // just above the first row and well below the last miss
    assert(track.hit_row(track.x + 5.f, track.y - 5.f) == -1);
    assert(track.hit_row(track.x + 5.f, track.y + track.items.size() * track.row_h + 5.f) == -1);
    // far to the side misses (outside the width)
    assert(track.hit_row(track.x + track.width + 20.f, track.y + track.row_h * 0.5f) == -1);

    // a closed menu never hits, even at a valid point
    track.close();
    assert(!track.open);
    assert(track.hit_row(track.row_rect(0).x + 1.f, track.row_rect(0).y + 1.f) == -1);

    std::puts("test_popup_menu: OK");
    return 0;
}
