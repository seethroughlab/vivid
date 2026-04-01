// JSON migration regression test.
// Loads each test graph, round-trips through save_to_string → load_from_string,
// and verifies the in-memory Graph structures match field-by-field.
// This test should pass both before and after the yyjson → nlohmann migration.

#include "runtime/graph.h"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, const char* label) {
    // Both NaN → match.  Otherwise within tolerance.
    bool match = (std::isnan(actual) && std::isnan(expected)) ||
                 std::fabs(actual - expected) < 1e-4f;
    if (!match) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", label, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", label);
    }
}

// Read a file into a string.
static std::string read_file(const char* path) {
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// Compare two NodeDef structures recursively.
static void compare_nodes(const vivid::NodeDef& a, const vivid::NodeDef& b,
                           const std::string& ctx) {
    check(a.id == b.id, (ctx + " id").c_str());
    check(a.type == b.type, (ctx + " type").c_str());

    // Float params
    check(a.params.size() == b.params.size(),
          (ctx + " params count").c_str());
    for (auto& [k, v] : a.params) {
        auto it = b.params.find(k);
        if (it == b.params.end()) {
            check(false, (ctx + " param " + k + " missing").c_str());
        } else {
            check_float(it->second, v, (ctx + " param " + k).c_str());
        }
    }

    // String params
    check(a.string_params.size() == b.string_params.size(),
          (ctx + " string_params count").c_str());
    for (auto& [k, v] : a.string_params) {
        auto it = b.string_params.find(k);
        if (it == b.string_params.end()) {
            check(false, (ctx + " string_param " + k + " missing").c_str());
        } else {
            check(it->second == v, (ctx + " string_param " + k).c_str());
        }
    }

    // Layout
    check_float(a.layout_x, b.layout_x, (ctx + " layout_x").c_str());
    check_float(a.layout_y, b.layout_y, (ctx + " layout_y").c_str());

    // Resolution
    check(a.tex_width == b.tex_width, (ctx + " tex_width").c_str());
    check(a.tex_height == b.tex_height, (ctx + " tex_height").c_str());

    // Package provenance
    check(a.pkg_name == b.pkg_name, (ctx + " pkg_name").c_str());
    check(a.pkg_version == b.pkg_version, (ctx + " pkg_version").c_str());

    // Lock flags
    check(a.param_lock_flags.size() == b.param_lock_flags.size(),
          (ctx + " lock_flags count").c_str());

}

static void compare_graphs(const vivid::Graph& a, const vivid::Graph& b,
                            const std::string& label) {
    std::string ctx = "[" + label + "]";

    // Nodes
    check(a.nodes().size() == b.nodes().size(),
          (ctx + " node count").c_str());
    // Build index by id for order-independent comparison
    std::unordered_map<std::string, const vivid::NodeDef*> b_nodes;
    for (auto& n : b.nodes()) b_nodes[n.id] = &n;
    for (auto& na : a.nodes()) {
        auto it = b_nodes.find(na.id);
        if (it == b_nodes.end()) {
            check(false, (ctx + " node " + na.id + " missing").c_str());
        } else {
            compare_nodes(na, *it->second, ctx + "/node/" + na.id);
        }
    }

    // Connections
    check(a.connections().size() == b.connections().size(),
          (ctx + " connection count").c_str());
    for (size_t i = 0; i < std::min(a.connections().size(), b.connections().size()); i++) {
        auto& ca = a.connections()[i];
        auto& cb = b.connections()[i];
        std::string cc = ctx + " conn[" + std::to_string(i) + "]";
        check(ca.from_node == cb.from_node, (cc + " from_node").c_str());
        check(ca.from_port == cb.from_port, (cc + " from_port").c_str());
        check(ca.to_node == cb.to_node, (cc + " to_node").c_str());
        check(ca.to_port == cb.to_port, (cc + " to_port").c_str());
        check_float(ca.from_min, cb.from_min, (cc + " from_min").c_str());
        check_float(ca.from_max, cb.from_max, (cc + " from_max").c_str());
        check_float(ca.to_min, cb.to_min, (cc + " to_min").c_str());
        check_float(ca.to_max, cb.to_max, (cc + " to_max").c_str());
        check(ca.clamp == cb.clamp, (cc + " clamp").c_str());
    }

    // MIDI mappings
    check(a.midi_mappings().size() == b.midi_mappings().size(),
          (ctx + " midi_mapping count").c_str());
    for (size_t i = 0; i < std::min(a.midi_mappings().size(), b.midi_mappings().size()); i++) {
        auto& ma = a.midi_mappings()[i];
        auto& mb = b.midi_mappings()[i];
        std::string mc = ctx + " midi[" + std::to_string(i) + "]";
        check(ma.node_id == mb.node_id, (mc + " node_id").c_str());
        check(ma.param_name == mb.param_name, (mc + " param_name").c_str());
        check(ma.cc_number == mb.cc_number, (mc + " cc_number").c_str());
        check(ma.channel == mb.channel, (mc + " channel").c_str());
        check_float(ma.range_min, mb.range_min, (mc + " range_min").c_str());
        check_float(ma.range_max, mb.range_max, (mc + " range_max").c_str());
    }

    // Filters
    check(a.filters().size() == b.filters().size(),
          (ctx + " filter count").c_str());
    for (size_t i = 0; i < std::min(a.filters().size(), b.filters().size()); i++) {
        auto& fa = a.filters()[i];
        auto& fb = b.filters()[i];
        std::string fc = ctx + " filter[" + std::to_string(i) + "]";
        check(fa.name == fb.name, (fc + " name").c_str());
        check(fa.source == fb.source, (fc + " source").c_str());
        check(fa.time_dependent == fb.time_dependent, (fc + " time_dependent").c_str());
        check(fa.shader == fb.shader, (fc + " shader").c_str());
        check(fa.params.size() == fb.params.size(), (fc + " param count").c_str());
        for (size_t j = 0; j < std::min(fa.params.size(), fb.params.size()); j++) {
            check(fa.params[j].name == fb.params[j].name, (fc + " param name").c_str());
            check_float(fa.params[j].default_value, fb.params[j].default_value,
                       (fc + " param default").c_str());
        }
    }

    // Variations
    check(a.variations().size() == b.variations().size(),
          (ctx + " variation count").c_str());
    for (size_t i = 0; i < std::min(a.variations().size(), b.variations().size()); i++) {
        auto& va = a.variations()[i];
        auto& vb = b.variations()[i];
        std::string vc = ctx + " var[" + std::to_string(i) + "]";
        check(va.name == vb.name, (vc + " name").c_str());
        check(va.params.size() == vb.params.size(), (vc + " params count").c_str());
    }

    // Viewport
    check_float(a.viewport_pan_x, b.viewport_pan_x, (ctx + " viewport_pan_x").c_str());
    check_float(a.viewport_pan_y, b.viewport_pan_y, (ctx + " viewport_pan_y").c_str());
    check_float(a.viewport_zoom, b.viewport_zoom, (ctx + " viewport_zoom").c_str());

    // Sticky notes
    check(a.sticky_notes().size() == b.sticky_notes().size(),
          (ctx + " sticky_note count").c_str());
    for (size_t i = 0; i < std::min(a.sticky_notes().size(), b.sticky_notes().size()); i++) {
        auto& sa = a.sticky_notes()[i];
        auto& sb = b.sticky_notes()[i];
        std::string sc = ctx + " sticky[" + std::to_string(i) + "]";
        check(sa.id == sb.id, (sc + " id").c_str());
        check(sa.text == sb.text, (sc + " text").c_str());
        check_float(sa.x, sb.x, (sc + " x").c_str());
        check_float(sa.y, sb.y, (sc + " y").c_str());
        check(sa.color == sb.color, (sc + " color").c_str());
    }

    // Node presets
    check(a.node_presets().size() == b.node_presets().size(),
          (ctx + " node_presets count").c_str());

    // State preset mappings
    check(a.state_preset_mappings().size() == b.state_preset_mappings().size(),
          (ctx + " state_preset_mappings count").c_str());
}

int main() {
    const char* test_graphs[] = {
        "test_runtime_api.json",
        "test_audio_engine.json",
        "test_audio_robustness.json",
        "test_cross_cadence_lanes.json",
        "test_mixed_runtime_stability.json",
        "test_package_stress.json",
        "test_reload.json",
        "test_lane_broadcast.json",
    };

    // Use SOURCE_DIR macro set by CMake to locate test graphs
    std::string graphs_dir = std::string(SOURCE_DIR) + "/tests/graphs/";

    for (const char* name : test_graphs) {
        std::fprintf(stderr, "\n=== Round-trip: %s ===\n", name);

        std::string path = graphs_dir + name;
        std::string raw_json = read_file(path.c_str());
        if (raw_json.empty()) {
            std::fprintf(stderr, "  FAIL: could not read %s\n", path.c_str());
            failures++;
            continue;
        }

        // Load from file
        vivid::Graph g1;
        if (!g1.load_from_string(raw_json.c_str(), raw_json.size())) {
            std::fprintf(stderr, "  FAIL: initial load of %s\n", name);
            failures++;
            continue;
        }

        // Serialize
        std::string serialized;
        if (!g1.save_to_string(serialized)) {
            std::fprintf(stderr, "  FAIL: save_to_string for %s\n", name);
            failures++;
            continue;
        }
        check(!serialized.empty(), "serialized output non-empty");

        // Re-load from serialized
        vivid::Graph g2;
        if (!g2.load_from_string(serialized.c_str(), serialized.size())) {
            std::fprintf(stderr, "  FAIL: reload from serialized %s\n", name);
            failures++;
            continue;
        }

        // Compare
        compare_graphs(g1, g2, name);
    }

    // Additional: test with a graph built programmatically (exercises more features)
    {
        std::fprintf(stderr, "\n=== Round-trip: programmatic graph ===\n");
        vivid::Graph g;
        g.add_node("osc", "Oscillator", {{"frequency", 440.0f}, {"amplitude", 0.8f}});
        g.add_node("mix", "Mixer", {{"gain", 0.5f}},
                   {{"label", "Main Mix"}});
        g.add_connection("osc", "output", "mix", "input");
        g.add_midi_mapping("osc", "frequency", 74, 1, 20.0f, 2000.0f);

        vivid::VariationDef var;
        var.name = "Bright";
        var.params["osc"]["frequency"] = 880.0f;
        g.add_variation(var);

        vivid::StickyNoteDef sn;
        sn.id = "note1"; sn.text = "Test note"; sn.x = 50.0f; sn.y = 75.0f;
        sn.width = 200.0f; sn.height = 100.0f; sn.color = 2;
        g.add_sticky_note(sn);

        std::string json;
        check(g.save_to_string(json), "save programmatic graph");

        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload programmatic graph");

        compare_graphs(g, g2, "programmatic");
    }

    std::fprintf(stderr, "\n%s (%d failure%s)\n",
                 failures ? "FAILED" : "ALL PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
