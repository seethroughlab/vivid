// ADR-0030 Phase 1: host-owned authored base for plugin nodes. These tests exercise the base cache
// on the plugin handles directly — no plugin is loaded, so they are deterministic and fast. They
// prove the mechanics the session param API leans on: an authored set records the base, an
// un-authored param falls back (has_base==0), a plugin-GUI performEdit authors the base through the
// REAL component handler, and a preset load forgets the cache so the plugin's values become base
// again. macOS/app-ON only (the handles pull in the VST3 SDK); not part of the portable Linux tier.
#include "audio/vst3_host_common.h"
#include "audio/clap_host.h"
#include "test_helpers.h"

#include <cmath>

using namespace vivid::test;
using vivid::session::Vst3Handle;
using vivid::session::ClapHandle;
using Steinberg::Vst::ParamID;
using Steinberg::Vst::ParamValue;

// Populate a Vst3Handle's param table with `n` synthetic params (no plugin), then size the base
// cache exactly as vst3_cache_params() does. Ids are 100,101,... so index != id (catches id/index
// confusion in base_index_of).
static void seed_vst3(Vst3Handle& h, int n) {
    h.params.clear();
    for (int i = 0; i < n; ++i) {
        Vst3Handle::ParamEntry e{};
        e.id = static_cast<ParamID>(100 + i);
        e.min_plain = 0.0; e.max_plain = 1.0; e.default_plain = 0.0; e.step_count = 0;
        e.name = "p" + std::to_string(i);
        h.params.push_back(std::move(e));
    }
    h.base_size_to_params();
}

static void test_vst3_base_cache() {
    Vst3Handle h;                       // all plugin pointers null; destroy() is null-safe
    seed_vst3(h, 4);

    // Fresh table: nothing authored → the reader would fall back to the plugin's live value.
    CHECK(h.host_base.size() == 4);
    CHECK(h.has_base.size() == 4);
    for (int i = 0; i < 4; ++i) CHECK(h.has_base[i] == 0);

    // base_index_of maps stable ParamID → index (id 102 is at index 2).
    CHECK(h.base_index_of(102) == 2);
    CHECK(h.base_index_of(999) == -1);

    // An authored set (what session_audio_graph_node_param_set does) records the base + flags it.
    h.base_author(2, 0.42f);
    CHECK(h.has_base[2] == 1);
    CHECK_NEAR(h.host_base[2], 0.42f, 1e-6);
    CHECK(h.has_base[0] == 0 && h.has_base[1] == 0 && h.has_base[3] == 0);   // others untouched

    // A plugin-GUI knob turn arrives through the REAL handler. Wire the callback exactly as
    // vst3_cache_params() does, then drive performEdit and confirm it authors the base.
    h.component_handler.on_authored_edit = [&h](ParamID pid, ParamValue v) {
        h.base_author(h.base_index_of(pid), static_cast<float>(v));
    };
    h.component_handler.performEdit(/*id*/ 100, /*normalized*/ 0.9);   // param index 0
    CHECK(h.has_base[0] == 1);
    CHECK_NEAR(h.host_base[0], 0.9f, 1e-6);

    // performEdit for an unknown id is a no-op (index -1), not a crash or a stray author.
    h.component_handler.performEdit(/*id*/ 424242, 0.5);
    CHECK(h.has_base[1] == 0);

    // ADR-0034: the audio-thread base mirror tracks base_author. Authored params read back through
    // abase_load; un-authored params return false (not modulatable until captured on wire).
    float ab = -1.f;
    CHECK(h.abase_load(0, ab) && std::fabs(ab - 0.9f) < 1e-6f);    // authored via performEdit
    CHECK(h.abase_load(2, ab) && std::fabs(ab - 0.42f) < 1e-6f);   // authored via base_author
    CHECK(!h.abase_load(1, ab));                                   // never authored

    // A preset load forgets every cached base — the reader falls back to the plugin's (freshly
    // loaded) values until the user authors again. host_base bytes may linger but has_base gates it.
    h.base_forget_all();
    for (int i = 0; i < 4; ++i) CHECK(h.has_base[i] == 0);
    CHECK(!h.abase_load(0, ab) && !h.abase_load(2, ab));           // mirror cleared too

    // Re-caching params (restartComponent path) resets the cache to the new table size, unauthored.
    seed_vst3(h, 2);
    CHECK(h.has_base.size() == 2);
    CHECK(h.has_base[0] == 0 && h.has_base[1] == 0);

    // Out-of-range author is ignored (no growth, no crash).
    h.base_author(7, 0.5f);
    CHECK(h.has_base.size() == 2);
}

static void seed_clap(ClapHandle& h, int n) {
    h.params.clear();
    for (int i = 0; i < n; ++i)
        h.params.push_back({ static_cast<clap_id>(200 + i), /*min*/ -1.0, /*max*/ 1.0, /*def*/ 0.0,
                             "c" + std::to_string(i), /*module*/ "", /*flags*/ 0u });
    h.base_size_to_params();
}

static void test_clap_base_cache() {
    ClapHandle h;
    seed_clap(h, 3);

    CHECK(h.host_base.size() == 3);
    for (int i = 0; i < 3; ++i) CHECK(h.has_base[i] == 0);

    // CLAP base is stored in PLAIN units (session setter clamps to [min,max] before authoring).
    h.base_author(1, -0.25);
    CHECK(h.has_base[1] == 1);
    CHECK_NEAR(h.host_base[1], -0.25, 1e-9);
    CHECK(h.has_base[0] == 0 && h.has_base[2] == 0);

    // ADR-0034: the audio-thread base mirror tracks it (plain units for CLAP).
    double ab = 0.0;
    CHECK(h.abase_load(1, ab) && std::fabs(ab - (-0.25)) < 1e-9);
    CHECK(!h.abase_load(0, ab));

    h.base_forget_all();
    for (int i = 0; i < 3; ++i) CHECK(h.has_base[i] == 0);
    CHECK(!h.abase_load(1, ab));   // mirror cleared
}

int main() {
    test_vst3_base_cache();
    test_clap_base_cache();
    return summary("plugin_param_base");
}
