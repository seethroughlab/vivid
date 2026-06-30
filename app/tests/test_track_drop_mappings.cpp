// Headless test for the stable-id delete-track behavior (Phase E). With stable ids,
// deleting a track DROPS its mappings and leaves every other mapping byte-identical — no
// renumbering. parse_track_source + MappingRegistry::drop_track_sources are header-only/pure.
#include "mapping.h"
#include "test_helpers.h"

#include <string>

using vivid::MappingRegistry;
using vivid::parse_track_source;

int main() {
    // parse_track_source parses "track_<n>" (n is now a stable id — opaque to the parser).
    int n = -1; std::string rest;
    CHECK(parse_track_source("track_7.transient", n, rest)); CHECK(n == 7); CHECK(rest == ".transient");
    CHECK(!parse_track_source("master.level", n, rest));
    CHECK(!parse_track_source("viz.warp", n, rest));

    MappingRegistry r;
    r.connect("master.level",      "node:0.glow");
    r.connect("track_2.transient", "node:1.warp");      // track id 2
    r.connect("track_5.level",     "node:2.hue");        // track id 5 (HIGHER than the deleted id)
    r.connect("track_2.low",       "node:3.density");    // a second mapping from id 2

    const int dropped = r.drop_track_sources(2);
    CHECK(dropped == 2);                 // both id-2 mappings removed
    CHECK(r.mappings().size() == 2);

    bool warp_gone = true, density_gone = true, hue_unchanged = false, master_ok = false;
    for (const auto& m : r.mappings()) {
        if (m.dest == "node:1.warp")    warp_gone = false;
        if (m.dest == "node:3.density") density_gone = false;
        if (m.dest == "node:2.hue")     hue_unchanged = (m.source == "track_5.level");  // NOT renumbered to track_4
        if (m.dest == "node:0.glow")    master_ok = (m.source == "master.level");
    }
    CHECK(warp_gone);
    CHECK(density_gone);
    CHECK(hue_unchanged);   // the crux: a higher-id source is untouched, never shifted down
    CHECK(master_ok);

    // Dropping an id with no mappings is a no-op.
    CHECK(r.drop_track_sources(99) == 0);
    CHECK(r.mappings().size() == 2);

    return vivid::test::summary("test_track_drop_mappings");
}
