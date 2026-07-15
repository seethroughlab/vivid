// ADR-0017/G1 — the canonical document projection + the audio-block equality used for restore
// tiering (persist_undo.h). Pure JSON in/out, so fully headless. The projection is the load-bearing
// correctness boundary: it must strip performance/view state (so playback/pan don't spam undo) while
// keeping document state (so real edits are captured).
#include "persist_undo.h"
#include "test_helpers.h"

using json = nlohmann::json;
using namespace vivid;

namespace {

json sample_session() {
    return json{
        {"version", 3},
        {"window", {{"w", 1280}, {"h", 720}, {"split", 640.0}, {"dock", 200.0}}},
        {"tracks", json::array({
            {{"name", "Lead"}, {"gain", 0.8}, {"active", 2}, {"is_audio", false}, {"kind", "instrument"}, {"id", 1},
             {"state", "OPAQUE-PLUGIN-BYTES"},
             {"clap_effects", json::array({ {{"path", "/x/reverb.clap"}, {"state", "FX-STATE"}} })}},
        })},
        {"graph", {
            {"view", {{"ox", 12.0}, {"oy", -4.0}, {"scale", 1.5}}},
            {"chain", json::array({
                {{"op_type", "Plasma"}, {"id", 0}, {"x", 10.f}, {"y", 20.f},
                 {"base", {0.5, 0.0, 0.5, 0.5}}, {"params", {{"warp", 0.5}, {"hue", 0.0}}}},
                {{"op_type", "Output"}, {"id", 3}, {"x", 90.f}, {"y", 20.f},
                 {"base", {0.0, 0.0, 2.0, 0.0}},
                 {"params", {{"aspect", 0.0}, {"height", 2.0}, {"fit", 0.0},
                             {"preview", 1.0}, {"launch", 0.0}, {"display", 0.0}}}},
            })},
        }},
    };
}

void test_projection_strips_view_and_performance() {
    const json p = canonical_document_projection(sample_session());
    CHECK(!p.contains("window"));                       // window/split/dock gone
    CHECK(!p["graph"].contains("view"));                // pan/zoom gone
    CHECK(!p["tracks"][0].contains("active"));          // launched clip gone
    // Every chain node loses the legacy "base" duplicate.
    for (const auto& n : p["graph"]["chain"]) CHECK(!n.contains("base"));
    // The Output node loses preview/launch/display but KEEPS aspect/height/fit.
    const json& out = p["graph"]["chain"][1]["params"];
    CHECK(!out.contains("preview") && !out.contains("launch") && !out.contains("display"));
    CHECK(out.contains("aspect") && out.contains("height") && out.contains("fit"));
    // Document values are preserved: track gain, node params.
    CHECK(p["tracks"][0]["gain"] == 0.8);
    CHECK(p["graph"]["chain"][0]["params"]["warp"] == 0.5);
    // Opaque plugin state is stripped (non-deterministic getState(), plugin-owned), but the CLAP
    // effect's path (its identity/topology) is kept.
    CHECK(!p["tracks"][0].contains("state"));
    CHECK(!p["tracks"][0]["clap_effects"][0].contains("state"));
    CHECK(p["tracks"][0]["clap_effects"][0]["path"] == "/x/reverb.clap");
}

void test_projection_ignores_plugin_state_churn() {
    // A plugin whose getState() returns different bytes each call must NOT change the projection.
    json a = sample_session();
    json b = sample_session();
    b["tracks"][0]["state"] = "DIFFERENT-BYTES-THIS-FRAME";
    b["tracks"][0]["clap_effects"][0]["state"] = "ALSO-DIFFERENT";
    CHECK(canonical_document_projection(a) == canonical_document_projection(b));
}

void test_projection_idempotent() {
    const json once = canonical_document_projection(sample_session());
    CHECK(canonical_document_projection(once) == once);
}

void test_projection_ignores_pure_performance_and_view_changes() {
    // A launch (active changes) or a pan (view changes) must NOT change the projection — otherwise
    // playback and camera moves would each spawn undo entries / trip the completeness audit.
    json a = sample_session();
    json b = sample_session();
    b["tracks"][0]["active"] = 4;                        // launched a different clip
    b["graph"]["view"]["scale"] = 0.5;                   // zoomed
    b["window"]["split"] = 700.0;                        // dragged the splitter
    CHECK(canonical_document_projection(a) == canonical_document_projection(b));

    // But a real document edit (a param) DOES change the projection.
    json c = sample_session();
    c["graph"]["chain"][0]["params"]["warp"] = 0.9;
    CHECK(canonical_document_projection(a) != canonical_document_projection(c));
}

void test_audio_topology_equal() {
    // Same tracks, only a VALUE differs (gain / a plugin param / a clip note) -> ParamsOnly tier.
    json base = {
        {"tracks", json::array({
            {{"kind","instrument"}, {"id",1}, {"gain",0.8},
             {"fx", json::array({ {{"name","Reverb"}, {"params", json::array({ {{"id",3},{"v",0.5}} })}} })},
             {"audio_graph", {{"nodes", json::array({ {{"id",7},{"kind",0},{"op","Osc"},{"params",{{"cutoff",0.4}}}} })}}}},
        })},
    };
    json values = base;   // change only values
    values["tracks"][0]["gain"] = 0.2;
    values["tracks"][0]["fx"][0]["params"][0]["v"] = 0.9;
    values["tracks"][0]["audio_graph"]["nodes"][0]["params"]["cutoff"] = 0.1;
    CHECK(audio_topology_equal(base, values));          // structure identical -> ParamsOnly
    CHECK(!audio_block_equal(base, values));            // but the block differs -> not Skip

    // A structural change (an added track, a different fx, a new node) -> NOT topology-equal -> Full.
    json add_track = base; add_track["tracks"].push_back({{"kind","audio"},{"id",2}});
    CHECK(!audio_topology_equal(base, add_track));
    json diff_fx = base; diff_fx["tracks"][0]["fx"][0]["name"] = "Delay";
    CHECK(!audio_topology_equal(base, diff_fx));
    json add_node = base; add_node["tracks"][0]["audio_graph"]["nodes"].push_back({{"id",8},{"kind",1},{"op","Gain"}});
    CHECK(!audio_topology_equal(base, add_node));
}

void test_audio_block_equal() {
    const json a = canonical_document_projection(sample_session());
    // A visual-only edit leaves tracks identical -> Skip tier.
    json vis = sample_session();
    vis["graph"]["chain"][0]["params"]["warp"] = 0.1;
    CHECK(audio_block_equal(a, canonical_document_projection(vis)));
    // A gain change touches the tracks block -> not Skip.
    json aud = sample_session();
    aud["tracks"][0]["gain"] = 0.2;
    CHECK(!audio_block_equal(a, canonical_document_projection(aud)));
    // A launch does NOT count as an audio-block change (active is stripped) -> still Skip.
    json launched = sample_session();
    launched["tracks"][0]["active"] = 5;
    CHECK(audio_block_equal(a, canonical_document_projection(launched)));
}

}  // namespace

int main() {
    test_projection_strips_view_and_performance();
    test_projection_ignores_plugin_state_churn();
    test_projection_idempotent();
    test_projection_ignores_pure_performance_and_view_changes();
    test_audio_block_equal();
    test_audio_topology_equal();
    return vivid::test::summary("undo_projection");
}
