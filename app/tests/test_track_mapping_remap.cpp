// Headless test for the delete-track mapping fix-up (dynamic tracks, Phase B).
// parse_track_source + MappingRegistry::remap_track_sources are pure (mapping.h is
// header-only), so the renumber/drop policy is tested without the engine.
#include "mapping.h"
#include "test_helpers.h"

#include <string>

using vivid::MappingRegistry;
using vivid::parse_track_source;

int main() {
    // parse_track_source: only "track_<n>." sources match.
    int idx = -1; std::string rest;
    CHECK(parse_track_source("track_3.transient", idx, rest)); CHECK(idx == 3); CHECK(rest == ".transient");
    CHECK(parse_track_source("track_0.level", idx, rest));     CHECK(idx == 0); CHECK(rest == ".level");
    CHECK(!parse_track_source("master.level", idx, rest));
    CHECK(!parse_track_source("viz.feedback", idx, rest));
    CHECK(!parse_track_source("track_x.foo", idx, rest));     // no digit after track_

    // Delete track 2: its mapping is dropped; sources above it renumber down; master +
    // lower tracks are untouched.
    MappingRegistry r;
    r.connect("master.level",     "node:0.glow");
    r.connect("track_1.transient", "node:1.warp");
    r.connect("track_2.level",    "node:2.hue");      // sourced from the deleted track -> dropped
    r.connect("track_3.band_low", "node:3.density");  // above -> renumbers to track_2

    const int changed = r.remap_track_sources(2);
    CHECK(changed == 2);                      // one dropped + one renumbered
    CHECK(r.mappings().size() == 3);          // master + track_1 + (track_3->track_2)

    bool renumbered = false, deleted_present = false, t1_ok = true, master_ok = true;
    for (const auto& m : r.mappings()) {
        if (m.dest == "node:3.density") { renumbered = (m.source == "track_2.band_low"); }
        if (m.dest == "node:2.hue")     { deleted_present = true; }
        if (m.dest == "node:1.warp")    { t1_ok = (m.source == "track_1.transient"); }
        if (m.dest == "node:0.glow")    { master_ok = (m.source == "master.level"); }
    }
    CHECK(renumbered);          // track_3.* -> track_2.*
    CHECK(!deleted_present);    // the deleted track's mapping is gone
    CHECK(t1_ok);               // track below the deletion unchanged
    CHECK(master_ok);          // non-track source unchanged

    // Deleting a track with no mappings sourced from/above it changes nothing.
    MappingRegistry r2;
    r2.connect("master.level", "node:0.glow");
    r2.connect("track_0.level", "node:1.warp");
    CHECK(r2.remap_track_sources(5) == 0);
    CHECK(r2.mappings().size() == 2);

    return vivid::test::summary("test_track_mapping_remap");
}
