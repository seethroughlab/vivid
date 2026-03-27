#include "runtime/graph.h"
#include <cstdio>
#include <cmath>
#include <fstream>
#include <string>
#include <cstdlib>
#include <unistd.h>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, const char* msg) {
    if (std::fabs(actual - expected) > 1e-4f) {
        std::fprintf(stderr, "  FAIL: %s (expected %f, got %f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%f)\n", msg, actual);
    }
}

static std::string write_temp(const char* name, const char* content) {
    std::string path = std::string("/tmp/vivid_test_") + name + ".json";
    std::ofstream f(path);
    f << content;
    return path;
}

int main() {
    // =====================================================================
    // Test 1: Load valid graph
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Load valid graph ===\n");
        std::string path = write_temp("valid", R"({
            "nodes": {
                "a": { "type": "Foo", "params": { "scale": 2.5, "offset": 10 } },
                "b": { "type": "Bar" }
            },
            "connections": [
                { "from": "a/out", "to": "b/in" }
            ]
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load succeeds");
        check(g.nodes().size() == 2, "2 nodes loaded");
        check(g.connections().size() == 1, "1 connection loaded");

        const auto* na = g.find_node("a");
        check(na != nullptr, "node a found");
        if (na) {
            check(na->type == "Foo", "node a type = Foo");
            check(na->params.size() == 2, "node a has 2 params");
            auto it = na->params.find("scale");
            check(it != na->params.end(), "scale param exists");
            if (it != na->params.end())
                check_float(it->second, 2.5f, "scale = 2.5");
        }

        const auto& conn = g.connections()[0];
        check(conn.from_node == "a" && conn.from_port == "out", "connection from a/out");
        check(conn.to_node == "b" && conn.to_port == "in", "connection to b/in");
    }

    // =====================================================================
    // Test 2: Load with layout
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Load with layout ===\n");
        std::string path = write_temp("layout", R"({
            "nodes": {
                "a": { "type": "Foo", "layout": { "x": 100.5, "y": 200.0 } }
            },
            "connections": []
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load succeeds");
        const auto* na = g.find_node("a");
        check(na != nullptr, "node a found");
        if (na) {
            check(na->has_layout(), "has_layout() returns true");
            check_float(na->layout_x, 100.5f, "layout_x = 100.5");
            check_float(na->layout_y, 200.0f, "layout_y = 200.0");
        }
    }

    // =====================================================================
    // Test 3: Load with resolution
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: Load with resolution ===\n");
        std::string path = write_temp("resolution", R"({
            "nodes": {
                "a": { "type": "Foo", "resolution": [1920, 1080] }
            },
            "connections": []
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load succeeds");
        const auto* na = g.find_node("a");
        check(na != nullptr, "node a found");
        if (na) {
            check(na->tex_width == 1920, "tex_width = 1920");
            check(na->tex_height == 1080, "tex_height = 1080");
        }
    }

    // =====================================================================
    // Test 4: Load non-existent file
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: Load non-existent file ===\n");
        vivid::Graph g;
        check(!g.load("/tmp/vivid_test_does_not_exist_12345.json"), "load returns false");
    }

    // =====================================================================
    // Test 5: Load malformed JSON
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: Load malformed JSON ===\n");
        std::string path = write_temp("malformed", "{ this is not json }}}");

        vivid::Graph g;
        check(!g.load(path.c_str()), "load returns false for malformed JSON");
    }

    // =====================================================================
    // Test 6: Load missing type field
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Load missing type field ===\n");
        std::string path = write_temp("notype", R"({
            "nodes": {
                "a": { "params": { "x": 1 } }
            },
            "connections": []
        })");

        vivid::Graph g;
        check(!g.load(path.c_str()), "load returns false for missing type");
    }

    // =====================================================================
    // Test 7: source_path()
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: source_path() ===\n");
        std::string path = write_temp("srcpath", R"({
            "nodes": {},
            "connections": []
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load succeeds");
        check(g.source_path() == path, "source_path matches");
    }

    // =====================================================================
    // Test 8: add_node
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: add_node ===\n");
        vivid::Graph g;
        check(g.add_node("a", "Foo", {{"x", 1.0f}}), "add_node a succeeds");
        check(g.nodes().size() == 1, "1 node after add");
        check(!g.add_node("a", "Bar"), "duplicate id rejected");
        check(g.nodes().size() == 1, "still 1 node");
    }

    // =====================================================================
    // Test 9: remove_node
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: remove_node ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");
        g.add_node("b", "Bar");
        g.add_connection("a", "out", "b", "in");
        check(g.connections().size() == 1, "1 connection before remove");

        check(g.remove_node("a"), "remove_node a succeeds");
        check(g.nodes().size() == 1, "1 node remains");
        check(g.connections().empty(), "connections cleaned up");
        check(!g.remove_node("nonexistent"), "remove non-existent returns false");
    }

    // =====================================================================
    // Test 10: add_connection
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: add_connection ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");
        g.add_node("b", "Bar");
        check(g.add_connection("a", "out", "b", "in"), "add_connection succeeds");
        check(!g.add_connection("a", "out", "b", "in"), "duplicate connection rejected");
        check(g.connections().size() == 1, "still 1 connection");
    }

    // =====================================================================
    // Test 11: remove_connection
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 11: remove_connection ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");
        g.add_node("b", "Bar");
        g.add_connection("a", "out", "b", "in");
        check(g.remove_connection("a", "out", "b", "in"), "remove_connection succeeds");
        check(g.connections().empty(), "0 connections after remove");
        check(!g.remove_connection("a", "out", "b", "in"), "remove non-existent returns false");
    }

    // =====================================================================
    // Test 12: find_node
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 12: find_node ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");

        // Mutable version
        vivid::NodeDef* mut = g.find_node("a");
        check(mut != nullptr, "find_node mutable returns non-null");
        check(g.find_node("missing") == nullptr, "find_node mutable returns nullptr for missing");

        // Const version
        const vivid::Graph& cg = g;
        const vivid::NodeDef* cn = cg.find_node("a");
        check(cn != nullptr, "find_node const returns non-null");
        check(cg.find_node("missing") == nullptr, "find_node const returns nullptr for missing");
    }

    // =====================================================================
    // Test 13: Save/load round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 13: Save/load round-trip ===\n");
        vivid::Graph g1;
        g1.add_node("a", "Foo", {{"scale", 2.5f}, {"offset", 10.0f}});
        g1.add_node("b", "Bar", {});

        // Set layout on a
        auto* na = g1.find_node("a");
        na->layout_x = 150.0f;
        na->layout_y = 300.0f;

        // Set resolution on b
        auto* nb = g1.find_node("b");
        nb->tex_width = 1920;
        nb->tex_height = 1080;

        g1.add_connection("a", "out", "b", "in");

        std::string path = "/tmp/vivid_test_roundtrip.json";
        check(g1.save(path.c_str()), "save succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "load succeeds");

        check(g2.nodes().size() == 2, "2 nodes after round-trip");
        check(g2.connections().size() == 1, "1 connection after round-trip");

        const auto* a2 = g2.find_node("a");
        check(a2 != nullptr, "node a found after round-trip");
        if (a2) {
            check(a2->type == "Foo", "type preserved");
            auto sit = a2->params.find("scale");
            check(sit != a2->params.end(), "scale param preserved");
            if (sit != a2->params.end())
                check_float(sit->second, 2.5f, "scale value preserved");
            auto oit = a2->params.find("offset");
            check(oit != a2->params.end(), "offset param preserved");
            if (oit != a2->params.end())
                check_float(oit->second, 10.0f, "offset value preserved");
            check(a2->has_layout(), "layout preserved");
            check_float(a2->layout_x, 150.0f, "layout_x preserved");
            check_float(a2->layout_y, 300.0f, "layout_y preserved");
        }

        const auto* b2 = g2.find_node("b");
        check(b2 != nullptr, "node b found after round-trip");
        if (b2) {
            check(b2->tex_width == 1920, "tex_width preserved");
            check(b2->tex_height == 1080, "tex_height preserved");
        }

        const auto& conn = g2.connections()[0];
        check(conn.from_node == "a" && conn.from_port == "out", "connection from preserved");
        check(conn.to_node == "b" && conn.to_port == "in", "connection to preserved");
    }

    // =====================================================================
    // Test 14: NaN layout round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 14: NaN layout round-trip ===\n");
        vivid::Graph g1;
        g1.add_node("a", "Foo");

        const auto* na = g1.find_node("a");
        check(!na->has_layout(), "new node has no layout (NaN)");
        check(std::isnan(na->layout_x), "layout_x is NaN");

        std::string path = "/tmp/vivid_test_nan_layout.json";
        check(g1.save(path.c_str()), "save succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "load succeeds");

        const auto* a2 = g2.find_node("a");
        check(a2 != nullptr, "node a found after round-trip");
        if (a2) {
            check(!a2->has_layout(), "no layout after round-trip");
            check(std::isnan(a2->layout_x), "layout_x still NaN");
        }
    }

    // =====================================================================
    // Test 15: MIDI mappings round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 15: MIDI mappings round-trip ===\n");

        // Load graph with midi_mappings in JSON
        std::string path = write_temp("midi_map", R"({
            "nodes": {
                "lfo1": { "type": "LFO", "params": { "frequency": 2.0 } },
                "gain1": { "type": "Gain" }
            },
            "connections": [],
            "midi_mappings": [
                { "node": "lfo1", "param": "frequency", "cc": 74, "range_min": 0.1, "range_max": 10.0 },
                { "node": "gain1", "param": "level", "cc": 7, "channel": 1, "range_min": 0.0, "range_max": 1.0 }
            ]
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load with midi_mappings succeeds");
        check(g.midi_mappings().size() == 2, "2 midi mappings loaded");

        const auto* mm0 = g.find_midi_mapping("lfo1", "frequency");
        check(mm0 != nullptr, "find lfo1/frequency mapping");
        if (mm0) {
            check(mm0->cc_number == 74, "cc = 74");
            check(mm0->channel == 0, "channel = 0 (omni)");
            check_float(mm0->range_min, 0.1f, "range_min = 0.1");
            check_float(mm0->range_max, 10.0f, "range_max = 10.0");
        }

        const auto* mm1 = g.find_midi_mapping("gain1", "level");
        check(mm1 != nullptr, "find gain1/level mapping");
        if (mm1) {
            check(mm1->cc_number == 7, "cc = 7");
            check(mm1->channel == 1, "channel = 1");
        }

        // Add a mapping via API
        g.add_midi_mapping("lfo1", "depth", 1, 0, 0.0f, 1.0f);
        check(g.midi_mappings().size() == 3, "3 mappings after add");

        // Update range
        check(g.update_midi_mapping("lfo1", "frequency", 0.5f, 5.0f), "update succeeds");
        const auto* updated = g.find_midi_mapping("lfo1", "frequency");
        if (updated) {
            check_float(updated->range_min, 0.5f, "updated range_min");
            check_float(updated->range_max, 5.0f, "updated range_max");
        }

        // Remove a mapping
        check(g.remove_midi_mapping("gain1", "level"), "remove succeeds");
        check(g.midi_mappings().size() == 2, "2 mappings after remove");
        check(g.find_midi_mapping("gain1", "level") == nullptr, "removed mapping gone");

        // remove_node cleans up mappings
        g.remove_node("lfo1");
        check(g.midi_mappings().empty(), "mappings cleaned up after node removal");

        // Save/load round-trip
        vivid::Graph g2;
        g2.add_node("synth1", "Synth");
        g2.add_midi_mapping("synth1", "cutoff", 71, 2, 100.0f, 8000.0f);

        std::string rt_path = "/tmp/vivid_test_midi_rt.json";
        check(g2.save(rt_path.c_str()), "save with midi mapping");

        vivid::Graph g3;
        check(g3.load(rt_path.c_str()), "reload succeeds");
        check(g3.midi_mappings().size() == 1, "1 mapping after reload");
        const auto* rt_mm = g3.find_midi_mapping("synth1", "cutoff");
        check(rt_mm != nullptr, "mapping found after reload");
        if (rt_mm) {
            check(rt_mm->cc_number == 71, "cc preserved");
            check(rt_mm->channel == 2, "channel preserved");
            check_float(rt_mm->range_min, 100.0f, "range_min preserved");
            check_float(rt_mm->range_max, 8000.0f, "range_max preserved");
        }
        std::remove(rt_path.c_str());
    }

    // =====================================================================
    // Test 16: Variation CRUD
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 16: Variation CRUD ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo", {{"scale", 1.0f}});
        g.add_node("b", "Bar", {{"gain", 0.5f}});

        // add_variation
        vivid::VariationDef v1;
        v1.name = "Intro";
        v1.params["a"] = {{"scale", 2.0f}};
        v1.params["b"] = {{"gain", 0.8f}};
        g.add_variation(std::move(v1));
        check(g.variations().size() == 1, "1 variation after add");

        vivid::VariationDef v2;
        v2.name = "Drop";
        v2.params["a"] = {{"scale", 5.0f}};
        g.add_variation(std::move(v2));
        check(g.variations().size() == 2, "2 variations after second add");

        // find_variation — const
        const vivid::Graph& cg = g;
        const vivid::VariationDef* found = cg.find_variation("Intro");
        check(found != nullptr, "find_variation const found");
        check(cg.find_variation("nope") == nullptr, "find_variation const not-found");

        // find_variation — mutable
        vivid::VariationDef* mut = g.find_variation("Intro");
        check(mut != nullptr, "find_variation mutable found");
        check(g.find_variation("nope") == nullptr, "find_variation mutable not-found");

        // find_variation_index
        check(g.find_variation_index("Intro") == 0, "find_variation_index Intro = 0");
        check(g.find_variation_index("Drop") == 1, "find_variation_index Drop = 1");
        check(g.find_variation_index("nope") == -1, "find_variation_index not-found = -1");

        // rename_variation — success
        check(g.rename_variation("Intro", "Verse"), "rename Intro -> Verse succeeds");
        check(g.find_variation("Verse") != nullptr, "renamed variation found");
        check(g.find_variation("Intro") == nullptr, "old name gone");

        // rename_variation — name conflict
        check(!g.rename_variation("Verse", "Drop"), "rename to existing name fails");

        // rename_variation — not found
        check(!g.rename_variation("nope", "x"), "rename non-existent fails");

        // remove_variation — success + adjusts active index
        g.set_active_variation(1); // "Drop" is at index 1
        check(g.remove_variation("Verse"), "remove Verse succeeds"); // index 0 removed
        check(g.variations().size() == 1, "1 variation after remove");
        check(g.active_variation() == 0, "active_variation decremented from 1 to 0");

        // remove_variation — not found
        check(!g.remove_variation("nope"), "remove non-existent fails");

        // variations() accessor
        check(g.variations()[0].name == "Drop", "remaining variation is Drop");
    }

    // =====================================================================
    // Test 17: active_variation + quantize_clock_node
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 17: active_variation + quantize_clock_node ===\n");
        vivid::Graph g;

        // Initial state
        check(g.active_variation() == -1, "initial active_variation = -1");
        check(g.quantize_clock_node().empty(), "initial quantize_clock_node empty");

        // set/get round-trip
        g.set_active_variation(2);
        check(g.active_variation() == 2, "active_variation set to 2");

        g.set_quantize_clock_node("clock1");
        check(g.quantize_clock_node() == "clock1", "quantize_clock_node = clock1");

        // Removing variation at active index sets active to -1
        vivid::VariationDef v1; v1.name = "A";
        vivid::VariationDef v2; v2.name = "B";
        vivid::VariationDef v3; v3.name = "C";
        g.add_variation(std::move(v1));
        g.add_variation(std::move(v2));
        g.add_variation(std::move(v3));
        g.set_active_variation(1); // "B"
        g.remove_variation("B");
        check(g.active_variation() == -1, "active = -1 after removing active variation");

        // Removing variation before active index decrements active
        g.set_active_variation(1); // "C" is now at index 1
        g.remove_variation("A");   // remove index 0
        check(g.active_variation() == 0, "active decremented after removing earlier variation");
    }

    // =====================================================================
    // Test 18: Variation JSON load
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 18: Variation JSON load ===\n");
        std::string path = write_temp("variations_load", R"({
            "nodes": {
                "osc": { "type": "Osc", "params": { "freq": 440.0 } },
                "filt": { "type": "Filter", "params": { "cutoff": 1000.0 } }
            },
            "connections": [],
            "variations": [
                {
                    "name": "Bright",
                    "params": {
                        "osc": { "freq": 880.0 },
                        "filt": { "cutoff": 5000.0 }
                    }
                },
                {
                    "name": "Dark",
                    "params": {
                        "osc": { "freq": 220.0 },
                        "filt": { "cutoff": 200.0 }
                    }
                }
            ],
            "active_variation": 1,
            "quantize_clock": "clock1"
        })");

        vivid::Graph g;
        check(g.load(path.c_str()), "load with variations succeeds");
        check(g.variations().size() == 2, "2 variations loaded");
        check(g.variations()[0].name == "Bright", "variation 0 name = Bright");
        check(g.variations()[1].name == "Dark", "variation 1 name = Dark");

        // Check nested param values
        const auto& bright = g.variations()[0];
        auto osc_it = bright.params.find("osc");
        check(osc_it != bright.params.end(), "Bright has osc params");
        if (osc_it != bright.params.end()) {
            auto freq_it = osc_it->second.find("freq");
            check(freq_it != osc_it->second.end(), "Bright/osc has freq");
            if (freq_it != osc_it->second.end())
                check_float(freq_it->second, 880.0f, "Bright osc freq = 880");
        }

        const auto& dark = g.variations()[1];
        auto filt_it = dark.params.find("filt");
        check(filt_it != dark.params.end(), "Dark has filt params");
        if (filt_it != dark.params.end()) {
            auto cutoff_it = filt_it->second.find("cutoff");
            check(cutoff_it != filt_it->second.end(), "Dark/filt has cutoff");
            if (cutoff_it != filt_it->second.end())
                check_float(cutoff_it->second, 200.0f, "Dark filt cutoff = 200");
        }

        check(g.active_variation() == 1, "active_variation = 1");
        check(g.quantize_clock_node() == "clock1", "quantize_clock = clock1");
    }

    // =====================================================================
    // Test 19: Variation JSON save/load round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 19: Variation JSON save/load round-trip ===\n");

        // Build a graph with 2 variations programmatically
        vivid::Graph g1;
        g1.add_node("a", "Foo", {{"scale", 1.0f}});
        g1.add_node("b", "Bar", {{"gain", 0.5f}});

        vivid::VariationDef v1;
        v1.name = "Intro";
        v1.params["a"] = {{"scale", 3.0f}};
        v1.params["b"] = {{"gain", 0.9f}};
        g1.add_variation(std::move(v1));

        vivid::VariationDef v2;
        v2.name = "Chorus";
        v2.params["a"] = {{"scale", 7.0f}};
        g1.add_variation(std::move(v2));

        g1.set_active_variation(0);
        g1.set_quantize_clock_node("clk1");

        std::string path = "/tmp/vivid_test_var_roundtrip.json";
        check(g1.save(path.c_str()), "save with variations succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "reload succeeds");
        check(g2.variations().size() == 2, "2 variations after round-trip");
        check(g2.variations()[0].name == "Intro", "variation 0 name preserved");
        check(g2.variations()[1].name == "Chorus", "variation 1 name preserved");

        // Check nested param values survived
        const auto& intro = g2.variations()[0];
        auto a_it = intro.params.find("a");
        check(a_it != intro.params.end(), "Intro has node a params");
        if (a_it != intro.params.end()) {
            auto s_it = a_it->second.find("scale");
            check(s_it != a_it->second.end(), "Intro/a has scale");
            if (s_it != a_it->second.end())
                check_float(s_it->second, 3.0f, "Intro a/scale = 3.0");
        }

        check(g2.active_variation() == 0, "active_variation preserved = 0");
        check(g2.quantize_clock_node() == "clk1", "quantize_clock preserved = clk1");

        // Also verify a graph with 0 variations saves/loads cleanly
        vivid::Graph g3;
        g3.add_node("x", "X");
        std::string path2 = "/tmp/vivid_test_var_empty.json";
        check(g3.save(path2.c_str()), "save empty variations succeeds");

        vivid::Graph g4;
        check(g4.load(path2.c_str()), "reload empty variations succeeds");
        check(g4.variations().empty(), "0 variations after round-trip");
        check(g4.active_variation() == -1, "active_variation = -1 with no variations");
        check(g4.quantize_clock_node().empty(), "quantize_clock empty with no variations");

        std::remove(path.c_str());
        std::remove(path2.c_str());
    }

    // =====================================================================
    // Test 20: remove_node cascading cleanup (presets, variations, state mappings)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 20: remove_node cascading cleanup ===\n");
        vivid::Graph g;
        g.add_node("sm1", "StateMachine");
        g.add_node("osc", "Osc");
        g.add_node("filt", "Filter");

        // Add presets for osc and filt
        g.save_preset("osc", {"Bright", {{"freq", 880.0f}}, {}});
        g.save_preset("filt", {"Open", {{"cutoff", 5000.0f}}, {}});
        check(g.list_presets("osc").size() == 1, "osc has 1 preset");
        check(g.list_presets("filt").size() == 1, "filt has 1 preset");

        // Add a variation referencing both nodes
        vivid::VariationDef v;
        v.name = "Main";
        v.params["osc"] = {{"freq", 440.0f}};
        v.params["filt"] = {{"cutoff", 1000.0f}};
        v.string_params["osc"] = {{"waveform", "saw"}};
        g.add_variation(std::move(v));
        check(g.variations()[0].params.size() == 2, "variation has 2 node entries");

        // Add state-preset mappings: sm1 controls presets on osc and filt
        g.set_state_preset("sm1", 0, "osc", "Bright");
        g.set_state_preset("sm1", 0, "filt", "Open");
        check(g.find_state_mapping("sm1") != nullptr, "state mapping exists for sm1");

        // Also add a mapping where osc is a target in another SM's mapping
        g.add_node("sm2", "StateMachine");
        g.set_state_preset("sm2", 0, "osc", "Bright");

        // Remove osc — should cascade to presets, variations, and state mappings
        g.remove_node("osc");
        check(g.list_presets("osc").empty(), "osc presets cleaned up");
        check(g.list_presets("filt").size() == 1, "filt presets untouched");
        check(g.variations()[0].params.size() == 1, "variation has 1 node entry after removal");
        check(g.variations()[0].params.count("filt") == 1, "variation still has filt");
        check(g.variations()[0].params.count("osc") == 0, "variation no longer has osc");
        check(g.variations()[0].string_params.count("osc") == 0, "variation string_params no longer has osc");

        // sm1 mapping should still exist but osc removed from targets
        const auto* sm1_map = g.find_state_mapping("sm1");
        check(sm1_map != nullptr, "sm1 mapping still exists");
        if (sm1_map) {
            check(sm1_map->state_presets[0].count("osc") == 0, "osc removed from sm1 targets");
            check(sm1_map->state_presets[0].count("filt") == 1, "filt still in sm1 targets");
        }

        // sm2 mapping should have osc removed from targets
        const auto* sm2_map = g.find_state_mapping("sm2");
        check(sm2_map != nullptr, "sm2 mapping still exists");
        if (sm2_map) {
            check(sm2_map->state_presets[0].count("osc") == 0, "osc removed from sm2 targets");
        }

        // Remove the state machine node — its mapping should be removed entirely
        g.remove_node("sm1");
        check(g.find_state_mapping("sm1") == nullptr, "sm1 mapping removed when sm1 node deleted");
        check(g.find_state_mapping("sm2") != nullptr, "sm2 mapping unaffected");
    }

    // =====================================================================
    // Test 21: load_from_string
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 21: load_from_string ===\n");

        const char* json = R"({
            "nodes": {
                "osc": { "type": "Oscillator", "params": { "frequency": 440.0 } },
                "gain": { "type": "Gain", "params": { "level": 0.8 } }
            },
            "connections": [
                { "from": "osc/output", "to": "gain/input", "scale": 0.5 }
            ]
        })";

        vivid::Graph g;
        check(g.load_from_string(json), "load_from_string succeeds");
        check(g.nodes().size() == 2, "2 nodes loaded from string");
        check(g.connections().size() == 1, "1 connection loaded from string");
        check(g.source_path().empty(), "source_path is empty for string load");

        const auto* osc = g.find_node("osc");
        check(osc != nullptr, "osc node found");
        if (osc) {
            check(osc->type == "Oscillator", "osc type = Oscillator");
            auto it = osc->params.find("frequency");
            check(it != osc->params.end(), "frequency param exists");
            if (it != osc->params.end())
                check_float(it->second, 440.0f, "frequency = 440.0");
        }

        const auto& conn = g.connections()[0];
        check(conn.from_node == "osc" && conn.from_port == "output", "connection from osc/output");
        check(conn.to_node == "gain" && conn.to_port == "input", "connection to gain/input");
        check_float(conn.to_max, 0.5f, "connection to_max = 0.5 (backward compat from scale)");

        // Also test with explicit length
        vivid::Graph g2;
        std::string json_str = json;
        check(g2.load_from_string(json_str.c_str(), json_str.size()), "load_from_string with explicit len");
        check(g2.nodes().size() == 2, "2 nodes with explicit len");

        // Test failure case
        vivid::Graph g3;
        check(!g3.load_from_string("{ bad json !!!"), "load_from_string fails on bad JSON");
    }

    // =====================================================================
    // Test 22: Connection remap round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 22: Connection remap round-trip ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");
        g.add_node("b", "Bar");
        g.add_connection("a", "out", "b", "in");

        // Default connection has no remap
        check(!g.connections()[0].has_remap(), "default connection has no remap");

        // Set remap
        check(g.set_connection_remap("a", "out", "b", "in",
            0.0f, 10.0f, -1.0f, 1.0f, true), "set_connection_remap succeeds");

        const auto& conn = g.connections()[0];
        check(conn.has_remap(), "has_remap after set");
        check_float(conn.from_min, 0.0f, "remap from_min = 0");
        check_float(conn.from_max, 10.0f, "remap from_max = 10");
        check_float(conn.to_min, -1.0f, "remap to_min = -1");
        check_float(conn.to_max, 1.0f, "remap to_max = 1");
        check(conn.clamp == true, "remap clamp = true");

        // Round-trip
        std::string path = "/tmp/vivid_test_remap_rt.json";
        check(g.save(path.c_str()), "save with remap");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load with remap");
        check(g2.connections().size() == 1, "1 connection after remap round-trip");

        const auto& c2 = g2.connections()[0];
        check(c2.has_remap(), "remap survives round-trip");
        check_float(c2.from_min, 0.0f, "rt from_min");
        check_float(c2.from_max, 10.0f, "rt from_max");
        check_float(c2.to_min, -1.0f, "rt to_min");
        check_float(c2.to_max, 1.0f, "rt to_max");
        check(c2.clamp == true, "rt clamp");

        // set_connection_remap on non-existent connection
        check(!g.set_connection_remap("x", "y", "z", "w",
            0.0f, 1.0f, 0.0f, 1.0f, false), "remap non-existent connection fails");

        std::remove(path.c_str());
    }

    // =====================================================================
    // Test 23: Filter CRUD + round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 23: Filter CRUD + round-trip ===\n");
        vivid::Graph g;

        // Add filter
        vivid::FilterDef f;
        f.name = "blur";
        f.source = "GaussianBlur";
        f.time_dependent = false;
        f.params.push_back({"radius", 5.0f, 0.0f, 50.0f});
        f.shader = "fn main() {}";
        g.add_filter(std::move(f));

        // find_filter — non-null
        const vivid::FilterDef* found = g.find_filter("blur");
        check(found != nullptr, "find_filter blur");
        if (found) {
            check(found->name == "blur", "filter name = blur");
            check(found->source == "GaussianBlur", "filter source = GaussianBlur");
            check(found->time_dependent == false, "filter not time_dependent");
            check(found->params.size() == 1, "filter has 1 param");
            if (!found->params.empty()) {
                check(found->params[0].name == "radius", "param name = radius");
                check_float(found->params[0].default_value, 5.0f, "param default = 5");
                check_float(found->params[0].min_value, 0.0f, "param min = 0");
                check_float(found->params[0].max_value, 50.0f, "param max = 50");
            }
            check(found->shader == "fn main() {}", "shader matches");
        }

        // find_filter — non-existent
        check(g.find_filter("nonexistent") == nullptr, "find_filter nonexistent = null");

        // Const overload
        const vivid::Graph& cg = g;
        check(cg.find_filter("blur") != nullptr, "find_filter const works");
        check(cg.find_filter("nope") == nullptr, "find_filter const nope = null");

        // Mutable overload
        vivid::FilterDef* mfilt = g.find_filter("blur");
        check(mfilt != nullptr, "find_filter mutable works");

        // update_filter_shader
        g.update_filter_shader("blur", "fn updated() {}");
        check(g.find_filter("blur")->shader == "fn updated() {}", "shader updated");

        // remove_filter
        check(g.remove_filter("blur"), "remove_filter blur succeeds");
        check(g.find_filter("blur") == nullptr, "blur gone after remove");
        check(!g.remove_filter("nonexistent"), "remove_filter nonexistent fails");

        // Round-trip with time_dependent filter
        vivid::FilterDef f2;
        f2.name = "glow";
        f2.source = "Glow";
        f2.time_dependent = true;
        f2.params.push_back({"intensity", 0.8f, 0.0f, 2.0f});
        f2.shader = "@fragment fn frag() -> vec4f { return vec4f(1.0); }";
        g.add_filter(std::move(f2));

        std::string path = "/tmp/vivid_test_filter_rt.json";
        g.add_node("x", "X"); // need at least one node for valid graph
        check(g.save(path.c_str()), "save with filter");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load with filter");
        check(g2.filters().size() == 1, "1 filter after round-trip");

        const vivid::FilterDef* rt = g2.find_filter("glow");
        check(rt != nullptr, "glow found after round-trip");
        if (rt) {
            check(rt->source == "Glow", "rt source = Glow");
            check(rt->time_dependent == true, "rt time_dependent = true");
            check(rt->params.size() == 1, "rt 1 param");
            if (!rt->params.empty()) {
                check(rt->params[0].name == "intensity", "rt param name = intensity");
                check_float(rt->params[0].default_value, 0.8f, "rt param default = 0.8");
            }
            check(rt->shader == "@fragment fn frag() -> vec4f { return vec4f(1.0); }", "rt shader preserved");
        }

        std::remove(path.c_str());
    }

    // =====================================================================
    // Test 24: Per-node preset CRUD
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 24: Per-node preset CRUD ===\n");
        vivid::Graph g;
        g.add_node("osc", "Osc");

        // save_preset
        g.save_preset("osc", {"Bright", {{"freq", 880.0f}}, {}});
        auto names = g.list_presets("osc");
        check(names.size() == 1, "1 preset after save");
        check(names[0] == "Bright", "preset name = Bright");

        // Second preset
        g.save_preset("osc", {"Dark", {{"freq", 220.0f}}, {}});
        names = g.list_presets("osc");
        check(names.size() == 2, "2 presets after second save");

        // find_preset — const
        const vivid::Graph& cg = g;
        const vivid::OperatorPreset* cp = cg.find_preset("osc", "Bright");
        check(cp != nullptr, "find_preset const Bright");
        if (cp) {
            auto it = cp->params.find("freq");
            check(it != cp->params.end(), "preset has freq param");
            if (it != cp->params.end())
                check_float(it->second, 880.0f, "preset freq = 880");
        }

        // find_preset — mutable
        vivid::OperatorPreset* mp = g.find_preset("osc", "Bright");
        check(mp != nullptr, "find_preset mutable Bright");

        // find_preset — non-existent
        check(g.find_preset("osc", "nonexistent") == nullptr, "find_preset nonexistent = null");

        // Overwrite existing preset
        g.save_preset("osc", {"Bright", {{"freq", 999.0f}}, {}});
        names = g.list_presets("osc");
        check(names.size() == 2, "still 2 presets after overwrite");
        const auto* ow = g.find_preset("osc", "Bright");
        if (ow) {
            auto it = ow->params.find("freq");
            if (it != ow->params.end())
                check_float(it->second, 999.0f, "overwritten freq = 999");
        }

        // rename_preset — success
        check(g.rename_preset("osc", "Dark", "Mellow"), "rename Dark -> Mellow");
        check(g.find_preset("osc", "Mellow") != nullptr, "Mellow found");
        check(g.find_preset("osc", "Dark") == nullptr, "Dark gone");

        // rename_preset — name conflict
        check(!g.rename_preset("osc", "Mellow", "Bright"), "rename to existing name fails");

        // rename_preset — not found
        check(!g.rename_preset("osc", "nonexistent", "x"), "rename nonexistent fails");

        // remove_preset — success
        check(g.remove_preset("osc", "Mellow"), "remove Mellow succeeds");
        check(g.list_presets("osc").size() == 1, "1 preset after remove");

        // remove_preset — not found
        check(!g.remove_preset("osc", "nonexistent"), "remove nonexistent fails");

        // list_presets for unknown node
        check(g.list_presets("unknown_node").empty(), "list_presets unknown = empty");

        // Round-trip
        g.save_preset("osc", {"Low", {{"freq", 110.0f}}, {{"waveform", "sine"}}});
        std::string path = "/tmp/vivid_test_preset_rt.json";
        check(g.save(path.c_str()), "save with presets");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load with presets");
        auto rt_names = g2.list_presets("osc");
        check(rt_names.size() == 2, "2 presets after round-trip");
        const auto* rt_low = g2.find_preset("osc", "Low");
        check(rt_low != nullptr, "Low found after round-trip");
        if (rt_low) {
            auto it = rt_low->params.find("freq");
            if (it != rt_low->params.end())
                check_float(it->second, 110.0f, "rt freq = 110");
            auto sit = rt_low->string_params.find("waveform");
            check(sit != rt_low->string_params.end(), "rt string_param waveform exists");
            if (sit != rt_low->string_params.end())
                check(sit->second == "sine", "rt waveform = sine");
        }

        std::remove(path.c_str());
    }

    // =====================================================================
    // Test 25: State-preset mapping CRUD + round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 25: State-preset mapping CRUD + round-trip ===\n");
        vivid::Graph g;
        g.add_node("sm1", "StateMachine");
        g.add_node("osc", "Osc");
        g.add_node("filt", "Filter");

        // set_state_preset — first target at state 0
        g.set_state_preset("sm1", 0, "osc", "Bright");
        const auto* sm = g.find_state_mapping("sm1");
        check(sm != nullptr, "state mapping exists after set");
        if (sm) {
            check(sm->state_presets.size() >= 1, "state_presets has at least 1 entry");
            check(sm->state_presets[0].count("osc") == 1, "osc in state 0");
            check(sm->state_presets[0].at("osc") == "Bright", "osc preset = Bright");
        }

        // Second target at same state index
        g.set_state_preset("sm1", 0, "filt", "Open");
        sm = g.find_state_mapping("sm1");
        if (sm) {
            check(sm->state_presets[0].size() == 2, "2 targets at state 0");
        }

        // Second state index
        g.set_state_preset("sm1", 1, "osc", "Dark");
        sm = g.find_state_mapping("sm1");
        if (sm) {
            check(sm->state_presets.size() >= 2, "state_presets has 2 entries");
            check(sm->state_presets[1].count("osc") == 1, "osc in state 1");
            check(sm->state_presets[1].at("osc") == "Dark", "osc preset at state 1 = Dark");
        }

        // remove_state_preset — removes one target
        check(g.remove_state_preset("sm1", 0, "osc"), "remove osc from state 0");
        sm = g.find_state_mapping("sm1");
        if (sm) {
            check(sm->state_presets[0].count("osc") == 0, "osc removed from state 0");
            check(sm->state_presets[0].count("filt") == 1, "filt still in state 0");
        }

        // remove_state_preset — non-existent target
        check(!g.remove_state_preset("sm1", 0, "nonexistent"), "remove nonexistent target fails");

        // clear_state_presets
        g.clear_state_presets("sm1");
        check(g.find_state_mapping("sm1") == nullptr, "mapping gone after clear");

        // find_state_mapping on non-existent
        check(g.find_state_mapping("nonexistent") == nullptr, "find nonexistent mapping = null");

        // Round-trip
        g.set_state_preset("sm1", 0, "osc", "Bright");
        g.set_state_preset("sm1", 0, "filt", "Open");
        g.set_state_preset("sm1", 1, "osc", "Dark");

        std::string path = "/tmp/vivid_test_state_rt.json";
        check(g.save(path.c_str()), "save with state mappings");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load with state mappings");

        const auto* sm2 = g2.find_state_mapping("sm1");
        check(sm2 != nullptr, "state mapping found after round-trip");
        if (sm2) {
            check(sm2->state_presets.size() >= 2, "rt: 2 state entries");
            check(sm2->state_presets[0].count("osc") == 1, "rt: osc in state 0");
            check(sm2->state_presets[0].at("osc") == "Bright", "rt: osc=Bright at state 0");
            check(sm2->state_presets[0].count("filt") == 1, "rt: filt in state 0");
            check(sm2->state_presets[0].at("filt") == "Open", "rt: filt=Open at state 0");
            check(sm2->state_presets[1].count("osc") == 1, "rt: osc in state 1");
            check(sm2->state_presets[1].at("osc") == "Dark", "rt: osc=Dark at state 1");
        }

        std::remove(path.c_str());
    }

    // =====================================================================
    // Test 26: Viewport round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 26: Viewport round-trip ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");

        // Fresh graph: no viewport
        check(!g.has_viewport(), "fresh graph has no viewport");

        // Set viewport
        g.set_viewport(100.0f, 200.0f, 1.5f);
        check(g.has_viewport(), "has_viewport after set");
        check_float(g.viewport_pan_x, 100.0f, "viewport pan_x = 100");
        check_float(g.viewport_pan_y, 200.0f, "viewport pan_y = 200");
        check_float(g.viewport_zoom, 1.5f, "viewport zoom = 1.5");

        // Round-trip
        std::string path = "/tmp/vivid_test_viewport_rt.json";
        check(g.save(path.c_str()), "save with viewport");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load with viewport");
        check(g2.has_viewport(), "viewport preserved after round-trip");
        check_float(g2.viewport_pan_x, 100.0f, "rt pan_x");
        check_float(g2.viewport_pan_y, 200.0f, "rt pan_y");
        check_float(g2.viewport_zoom, 1.5f, "rt zoom");

        // Graph with no viewport round-trips cleanly
        vivid::Graph g3;
        g3.add_node("b", "Bar");
        std::string path2 = "/tmp/vivid_test_viewport_none.json";
        check(g3.save(path2.c_str()), "save without viewport");
        vivid::Graph g4;
        check(g4.load(path2.c_str()), "load without viewport");
        check(!g4.has_viewport(), "no viewport after round-trip");

        std::remove(path.c_str());
        std::remove(path2.c_str());
    }

    // =====================================================================
    // Test 27: Empty graph operations
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 27: Empty graph operations ===\n");

        // Fresh graph has zero nodes and connections
        vivid::Graph g;
        check(g.nodes().empty(), "fresh graph: no nodes");
        check(g.connections().empty(), "fresh graph: no connections");

        // Remove from empty graph fails gracefully
        check(!g.remove_node("nonexistent"), "remove_node on empty graph = false");
        check(!g.remove_connection("a", "out", "b", "in"), "remove_connection on empty graph = false");
        check(g.find_node("x") == nullptr, "find_node on empty graph = null");

        // Load/save round-trip with zero nodes
        std::string path = "/tmp/vivid_test_empty_graph.json";
        check(g.save(path.c_str()), "save empty graph succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "load empty graph succeeds");
        check(g2.nodes().empty(), "0 nodes after empty round-trip");
        check(g2.connections().empty(), "0 connections after empty round-trip");
        std::remove(path.c_str());

        // load_from_string with explicit empty node/connection arrays
        vivid::Graph g3;
        check(g3.load_from_string(R"({"nodes":{},"connections":[]})"),
              "load_from_string empty graph succeeds");
        check(g3.nodes().empty(), "0 nodes from empty string");
        check(g3.connections().empty(), "0 connections from empty string");
    }

    // =====================================================================
    // Test 28: Dangling wire (connections to non-existent nodes)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 28: Dangling wire ===\n");

        // The Graph data model is permissive — it stores connections without
        // validating that the referenced node IDs exist.  Cycle detection and
        // port-type validation are the runtime's responsibility, not the
        // data model's.
        vivid::Graph g;
        // No nodes added — add_connection stores the wire anyway
        check(g.add_connection("ghost_a", "out", "ghost_b", "in"),
              "dangling connection accepted by data model");
        check(g.connections().size() == 1, "1 dangling connection stored");
        check(g.connections()[0].from_node == "ghost_a", "dangling from_node stored");
        check(g.connections()[0].to_node   == "ghost_b", "dangling to_node stored");

        // Round-trip: dangling connections survive save/load
        std::string path = "/tmp/vivid_test_dangling.json";
        check(g.save(path.c_str()), "save with dangling connection succeeds");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load with dangling connection succeeds");
        check(g2.connections().size() == 1, "dangling connection survives round-trip");
        std::remove(path.c_str());

        // Removing the non-existent "from" node is a no-op but doesn't crash
        check(!g.remove_node("ghost_a"), "remove non-existent node = false");
        check(g.connections().size() == 1, "dangling connection still present");
    }

    // =====================================================================
    // Test 29: Cycle in data model (Graph is cycle-permissive)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 29: Cycle in data model ===\n");

        // The Graph data model allows cycles — they are detected and handled
        // at the runtime level (kahn_sort / topo_sort).
        vivid::Graph g;
        g.add_node("a", "Foo");
        g.add_node("b", "Bar");
        g.add_node("c", "Baz");

        // Build A→B→C→A cycle
        check(g.add_connection("a", "out", "b", "in"),  "A→B added");
        check(g.add_connection("b", "out", "c", "in"),  "B→C added");
        check(g.add_connection("c", "out", "a", "in"),  "C→A added (cycle)");
        check(g.connections().size() == 3, "3 connections in cyclic graph");

        // Self-loop
        check(g.add_connection("a", "out2", "a", "in2"), "self-loop accepted");
        check(g.connections().size() == 4, "4 connections after self-loop");

        // Duplicate cycle connection is rejected
        check(!g.add_connection("a", "out", "b", "in"), "duplicate cyclic edge rejected");
        check(g.connections().size() == 4, "still 4 connections");

        // Save/load round-trip for cyclic graph
        std::string path = "/tmp/vivid_test_cycle.json";
        check(g.save(path.c_str()), "save cyclic graph succeeds");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load cyclic graph succeeds");
        check(g2.connections().size() == 4, "cycle connections survive round-trip");
        std::remove(path.c_str());
    }

    // =====================================================================
    // Test 30: Port-type agnostic (Graph stores any port name strings)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 30: Port-type agnostic connections ===\n");

        // The Graph data model has no concept of port types — it stores
        // arbitrary string identifiers.  Type compatibility is validated by
        // the runtime when it resolves operator descriptors.
        vivid::Graph g;
        g.add_node("audio_src", "AudioOsc");
        g.add_node("vis_node", "ColorRamp");

        // Semantically mismatched ports — accepted at data level
        check(g.add_connection("audio_src", "audio_out", "vis_node", "control_in"),
              "audio→control connection accepted by data model");
        check(g.add_connection("audio_src", "spread_out", "vis_node", "gpu_in"),
              "spread→gpu connection accepted by data model");

        const auto& c0 = g.connections()[0];
        check(c0.from_port == "audio_out",   "from_port stored verbatim");
        check(c0.to_port   == "control_in",  "to_port stored verbatim");

        // Port names with unusual characters are also stored verbatim
        check(g.add_connection("audio_src", "out.0", "vis_node", "in.rgb"), "dot-separated port names accepted");
        check(g.connections().size() == 3, "3 connections stored");

        // Round-trip preserves all port names exactly
        std::string path = "/tmp/vivid_test_port_types.json";
        check(g.save(path.c_str()), "save port-agnostic graph");
        vivid::Graph g2;
        check(g2.load(path.c_str()), "load port-agnostic graph");
        check(g2.connections().size() == 3, "all connections survive round-trip");
        check(g2.connections()[0].from_port == "audio_out",  "rt from_port preserved");
        check(g2.connections()[0].to_port   == "control_in", "rt to_port preserved");
        std::remove(path.c_str());
    }

    // =====================================================================
    // Test 31: save_to_string round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 31: save_to_string round-trip ===\n");

        vivid::Graph g1;
        check(g1.add_node("a", "Clock", {{"bpm", 120.0f}}), "add node a");
        check(g1.add_node("b", "Math", {{"scale", 2.0f}}), "add node b");
        check(g1.add_connection("a", "beat_phase", "b", "input"), "add connection");
        check(g1.add_midi_mapping("b", "scale", 12, 1, 0.0f, 2.0f), "add midi mapping");

        std::string json;
        check(g1.save_to_string(json), "save_to_string succeeds");
        check(!json.empty(), "serialized JSON is non-empty");

        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "load_from_string serialized JSON succeeds");
        check(g2.nodes().size() == 2, "round-trip node count preserved");
        check(g2.connections().size() == 1, "round-trip connection count preserved");
        check(g2.midi_mappings().size() == 1, "round-trip midi mapping count preserved");
        const auto* na = g2.find_node("a");
        check(na != nullptr, "round-trip node a exists");
        if (na) {
            auto it = na->params.find("bpm");
            check(it != na->params.end(), "round-trip node a bpm exists");
            if (it != na->params.end())
                check_float(it->second, 120.0f, "round-trip node a bpm preserved");
        }
    }

    // =====================================================================
    // Test 32: non-string "from" in connection skipped without crash
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 32: non-string connection address skipped ===\n");
        const char* json = R"({
            "nodes": { "a": { "type": "Foo" }, "b": { "type": "Bar" } },
            "connections": [
                { "from": 42, "to": "a/out" },
                { "from": "a/out", "to": 99 },
                { "from": "a/out", "to": "b/in" }
            ]
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load with non-string from/to succeeds");
        check(g.connections().size() == 1, "only valid connection stored");
    }

    // =====================================================================
    // Test 33: active_variation out of bounds clamped to -1
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 33: active_variation out of bounds reset to -1 ===\n");
        const char* json = R"({
            "nodes": { "a": { "type": "Foo" } },
            "connections": [],
            "active_variation": 99
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load with bad active_variation succeeds");
        check(g.active_variation() == -1, "active_variation clamped to -1");
    }

    // =====================================================================
    // Test 34: MIDI mapping with out-of-range cc skipped
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 34: MIDI cc out of range skipped ===\n");
        const char* json = R"({
            "nodes": { "a": { "type": "Foo" } },
            "connections": [],
            "midi_mappings": [
                { "node": "a", "param": "x", "cc": 200 },
                { "node": "a", "param": "y", "cc": 10 }
            ]
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load with bad midi cc succeeds");
        check(g.midi_mappings().size() == 1, "out-of-range cc mapping skipped");
        if (!g.midi_mappings().empty())
            check(g.midi_mappings()[0].cc_number == 10, "valid mapping retained");
    }

    // =====================================================================
    // Test 35: MIDI mapping with out-of-range channel skipped
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 35: MIDI channel out of range skipped ===\n");
        const char* json = R"({
            "nodes": { "a": { "type": "Foo" } },
            "connections": [],
            "midi_mappings": [
                { "node": "a", "param": "x", "cc": 10, "channel": 99 },
                { "node": "a", "param": "y", "cc": 11, "channel": 0 }
            ]
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load with bad midi channel succeeds");
        check(g.midi_mappings().size() == 1, "out-of-range channel mapping skipped");
    }

    // =====================================================================
    // Test 36: duplicate node IDs — only first kept
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 36: duplicate node IDs — only first kept ===\n");
        const char* json = R"({
            "nodes": {
                "a": { "type": "Foo" },
                "b": { "type": "Bar" }
            },
            "connections": []
        })";
        // We can't test key collision inside a JSON object (second key silently
        // overwrites in most parsers), but we can verify that if parse_doc is
        // called on a graph that already has a node, it starts fresh.  The real
        // duplicate-ID guard is tested by direct API:
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load base graph");
        check(g.nodes().size() == 2, "2 distinct nodes loaded");

        // Programmatic duplicate prevention (add_node path)
        check(!g.add_node("a", "Baz"), "add_node rejects duplicate id");
        check(g.nodes().size() == 2, "node count unchanged after rejected duplicate");
        const auto* na = g.find_node("a");
        check(na && na->type == "Foo", "original node a preserved");
    }

    // =====================================================================
    // Test 37: duplicate connections on load deduplicated
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 37: duplicate connections deduplicated on load ===\n");
        const char* json = R"({
            "nodes": { "a": { "type": "Foo" }, "b": { "type": "Bar" } },
            "connections": [
                { "from": "a/out", "to": "b/in" },
                { "from": "a/out", "to": "b/in" },
                { "from": "a/out", "to": "b/in" }
            ]
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load with duplicate connections succeeds");
        check(g.connections().size() == 1, "duplicate connections collapsed to one");
    }

    // =====================================================================
    // Test 38: tex_width/tex_height exceeding 8192 zeroed out
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 38: oversized resolution zeroed ===\n");
        const char* json = R"({
            "nodes": {
                "a": { "type": "Foo", "resolution": [999999, 999999] },
                "b": { "type": "Bar", "resolution": [1920, 1080] }
            },
            "connections": []
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "load with oversized resolution succeeds");
        const auto* na = g.find_node("a");
        const auto* nb = g.find_node("b");
        check(na != nullptr, "node a exists");
        check(nb != nullptr, "node b exists");
        if (na) {
            check(na->tex_width == 0,  "oversized tex_width zeroed");
            check(na->tex_height == 0, "oversized tex_height zeroed");
        }
        if (nb) {
            check(nb->tex_width  == 1920, "valid tex_width preserved");
            check(nb->tex_height == 1080, "valid tex_height preserved");
        }
    }

    // --- Tests 39–45: M10 versioning ---

    // Test 39: absent schema_version → backward compat, treated as 1
    {
        const char* json = R"({"nodes": {"a": {"type": "Foo"}}, "connections": []})";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "absent schema_version loads ok");
        check(g.schema_version == 1, "absent schema_version defaults to 1");
    }

    // Test 40: schema_version = 1 (current) → loads successfully
    {
        const char* json = R"({"schema_version": 1, "nodes": {"a": {"type": "Foo"}}, "connections": []})";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "schema_version 1 loads ok");
        check(g.schema_version == 1, "schema_version 1 stored");
    }

    // Test 41: schema_version from the future → hard reject
    {
        const char* json = R"({"schema_version": 99, "nodes": {}, "connections": []})";
        vivid::Graph g;
        check(!g.load_from_string(json, 0), "future schema_version rejected");
    }

    // Test 42: vivid_version stored on load
    {
        const char* json = R"({"vivid_version": "1.2.3", "nodes": {"a": {"type": "Foo"}}, "connections": []})";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "graph with vivid_version loads ok");
        check(g.vivid_version == "1.2.3", "vivid_version stored");
    }

    // Test 43: pkg sub-object round-trips through save/load
    {
        const char* json = R"({
            "nodes": {
                "kick1": {
                    "type": "audio/drum_kick",
                    "pkg": {"name": "vivid-drums", "version": "0.2.0"}
                }
            },
            "connections": []
        })";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "graph with pkg node loads ok");
        const auto* n = g.find_node("kick1");
        check(n != nullptr, "node kick1 found");
        if (n) {
            check(n->pkg_name == "vivid-drums", "pkg_name loaded");
            check(n->pkg_version == "0.2.0",    "pkg_version loaded");
        }

        std::string out;
        check(g.save_to_string(out), "save_to_string succeeds");
        check(out.find("\"vivid-drums\"") != std::string::npos, "pkg name in saved JSON");
        check(out.find("\"0.2.0\"") != std::string::npos,       "pkg version in saved JSON");
        check(out.find("schema_version") != std::string::npos,  "schema_version in saved JSON");
        check(out.find("vivid_version") != std::string::npos,   "vivid_version in saved JSON");

        vivid::Graph g2;
        check(g2.load_from_string(out.c_str(), out.size()), "reloaded from saved JSON");
        const auto* n2 = g2.find_node("kick1");
        if (n2) {
            check(n2->pkg_name == "vivid-drums", "pkg_name preserved after round-trip");
            check(n2->pkg_version == "0.2.0",    "pkg_version preserved after round-trip");
        }
    }

    // Test 44: node without pkg has empty provenance fields
    {
        const char* json = R"({"nodes": {"a": {"type": "Foo"}}, "connections": []})";
        vivid::Graph g;
        check(g.load_from_string(json, 0), "graph without pkg loads ok");
        const auto* n = g.find_node("a");
        if (n) {
            check(n->pkg_name.empty(),    "pkg_name empty for core node");
            check(n->pkg_version.empty(), "pkg_version empty for core node");
        }
    }

    // Test 45: load_diagnostics cleared on each load
    {
        const char* json = R"({"nodes": {"a": {"type": "Foo"}}, "connections": []})";
        vivid::Graph g;
        vivid::Graph::LoadDiagnostic d;
        d.node_id = "x"; d.pkg_name = "p";
        d.saved_version = "1.0.0"; d.installed_version = "2.0.0";
        d.classification = "incompatible_update";
        g.load_diagnostics.push_back(d);
        check(g.load_from_string(json, 0), "reload succeeds");
        check(g.load_diagnostics.empty(), "load_diagnostics cleared on load");
    }

    // =====================================================================
    // Test 46: duplicate_variation
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 46: duplicate_variation ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo", {{"scale", 1.0f}});

        vivid::VariationDef v1;
        v1.name = "A";
        v1.params["a"] = {{"scale", 2.0f}};
        v1.string_params["a"] = {{"file", "hello.wav"}};
        g.add_variation(std::move(v1));

        vivid::VariationDef v2;
        v2.name = "B";
        v2.params["a"] = {{"scale", 5.0f}};
        g.add_variation(std::move(v2));

        // Duplicate A as A_copy — should insert after A
        check(g.duplicate_variation("A", "A_copy"), "duplicate A -> A_copy succeeds");
        check(g.variations().size() == 3, "3 variations after duplicate");
        check(g.variations()[0].name == "A", "index 0 = A");
        check(g.variations()[1].name == "A_copy", "index 1 = A_copy (inserted after source)");
        check(g.variations()[2].name == "B", "index 2 = B");

        // Deep copy check: params and string_params copied
        const auto& dup = g.variations()[1];
        auto a_it = dup.params.find("a");
        check(a_it != dup.params.end(), "A_copy has node a params");
        if (a_it != dup.params.end()) {
            auto s_it = a_it->second.find("scale");
            check(s_it != a_it->second.end(), "A_copy has scale param");
            if (s_it != a_it->second.end())
                check_float(s_it->second, 2.0f, "A_copy a/scale = 2.0");
        }
        auto sp_it = dup.string_params.find("a");
        check(sp_it != dup.string_params.end(), "A_copy has string_params for node a");
        if (sp_it != dup.string_params.end()) {
            auto f_it = sp_it->second.find("file");
            check(f_it != sp_it->second.end() && f_it->second == "hello.wav",
                  "A_copy string_param file = hello.wav");
        }

        // Name conflict
        check(!g.duplicate_variation("A", "B"), "duplicate with name conflict fails");

        // Not found
        check(!g.duplicate_variation("nope", "X"), "duplicate non-existent fails");

        // Active index adjustment: active after insertion point shifts
        g.set_active_variation(2); // "B"
        check(g.duplicate_variation("A", "A2"), "duplicate A -> A2");
        check(g.active_variation() == 3, "active shifted from 2 to 3 after insert at 1");

        // Active index unchanged when inserting after active
        g.set_active_variation(0); // "A"
        check(g.duplicate_variation("B", "B2"), "duplicate B -> B2");
        check(g.active_variation() == 0, "active stays 0 when inserting after it");
    }

    // =====================================================================
    // Test 47: move_variation
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 47: move_variation ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");

        vivid::VariationDef v1; v1.name = "A";
        vivid::VariationDef v2; v2.name = "B";
        vivid::VariationDef v3; v3.name = "C";
        g.add_variation(std::move(v1));
        g.add_variation(std::move(v2));
        g.add_variation(std::move(v3));

        // Move C (idx 2) to idx 0
        g.set_active_variation(2); // "C"
        check(g.move_variation("C", 0), "move C to 0 succeeds");
        check(g.variations()[0].name == "C", "C now at 0");
        check(g.variations()[1].name == "A", "A now at 1");
        check(g.variations()[2].name == "B", "B now at 2");
        check(g.active_variation() == 0, "active tracks C to 0");

        // Move C (idx 0) to idx 2
        check(g.move_variation("C", 2), "move C to 2 succeeds");
        check(g.variations()[0].name == "A", "A at 0");
        check(g.variations()[1].name == "B", "B at 1");
        check(g.variations()[2].name == "C", "C at 2");
        check(g.active_variation() == 2, "active tracks C to 2");

        // Move to same position — no-op
        check(g.move_variation("B", 1), "move to same pos succeeds");
        check(g.variations()[1].name == "B", "B still at 1");

        // Active adjustment: active=0(A), move B(1) to 0 — active should shift to 1
        g.set_active_variation(0); // "A"
        check(g.move_variation("B", 0), "move B to 0");
        check(g.variations()[0].name == "B", "B at 0");
        check(g.variations()[1].name == "A", "A at 1");
        check(g.active_variation() == 1, "active A shifted from 0 to 1");

        // Invalid index
        check(!g.move_variation("A", 99), "move to out-of-range fails");
        check(!g.move_variation("A", -1), "move to negative fails");

        // Not found
        check(!g.move_variation("nope", 0), "move non-existent fails");
    }

    // =====================================================================
    // Test 48: duplicate + move variation save/load round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 48: duplicate + move variation save/load round-trip ===\n");
        vivid::Graph g1;
        g1.add_node("a", "Foo", {{"scale", 1.0f}});

        vivid::VariationDef v1; v1.name = "A"; v1.params["a"] = {{"scale", 1.0f}};
        vivid::VariationDef v2; v2.name = "B"; v2.params["a"] = {{"scale", 2.0f}};
        g1.add_variation(std::move(v1));
        g1.add_variation(std::move(v2));
        g1.duplicate_variation("A", "A_copy");
        g1.move_variation("B", 0);
        g1.set_active_variation(1); // "A" after move

        std::string path = "/tmp/vivid_test_dup_move_rt.json";
        check(g1.save(path.c_str()), "save after dup+move succeeds");

        vivid::Graph g2;
        check(g2.load(path.c_str()), "reload succeeds");
        check(g2.variations().size() == 3, "3 variations after round-trip");
        check(g2.variations()[0].name == "B", "order preserved: B at 0");
        check(g2.variations()[1].name == "A", "order preserved: A at 1");
        check(g2.variations()[2].name == "A_copy", "order preserved: A_copy at 2");
        check(g2.active_variation() == 1, "active_variation preserved");

        std::remove(path.c_str());
    }

    // =====================================================================
    // Test: Sticky note CRUD
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Sticky note CRUD ===\n");
        vivid::Graph g;
        g.load_from_string(R"({"nodes":{},"connections":[]})");

        check(g.sticky_notes().empty(), "no sticky notes initially");

        vivid::StickyNoteDef note;
        note.id = "sn1"; note.text = "Hello"; note.x = 10; note.y = 20;
        note.width = 200; note.height = 120; note.color = 2;
        g.add_sticky_note(note);
        check(g.sticky_notes().size() == 1, "1 sticky note after add");
        check(g.find_sticky_note("sn1") != nullptr, "find_sticky_note returns non-null");
        check(g.find_sticky_note("sn1")->text == "Hello", "sticky note text matches");
        check(g.find_sticky_note("sn1")->color == 2, "sticky note color matches");

        // Update via add (same id)
        vivid::StickyNoteDef note2;
        note2.id = "sn1"; note2.text = "Updated"; note2.x = 30; note2.y = 40;
        note2.width = 300; note2.height = 150; note2.color = 4;
        g.add_sticky_note(note2);
        check(g.sticky_notes().size() == 1, "still 1 sticky note after update-via-add");
        check(g.find_sticky_note("sn1")->text == "Updated", "text updated");

        // Add a second
        vivid::StickyNoteDef note3;
        note3.id = "sn2"; note3.text = "Second";
        g.add_sticky_note(note3);
        check(g.sticky_notes().size() == 2, "2 sticky notes");

        // Remove
        check(g.remove_sticky_note("sn1"), "remove sn1 succeeds");
        check(g.sticky_notes().size() == 1, "1 sticky note after remove");
        check(!g.remove_sticky_note("sn1"), "remove sn1 again fails");
        check(g.find_sticky_note("sn1") == nullptr, "sn1 not found after remove");
        check(g.find_sticky_note("sn2") != nullptr, "sn2 still present");
    }

    // =====================================================================
    // Test: Sticky note serialization round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Sticky note serialization ===\n");
        vivid::Graph g;
        g.load_from_string(R"({"nodes":{},"connections":[]})");

        vivid::StickyNoteDef sn;
        sn.id = "note_a"; sn.text = "**bold** test"; sn.x = 100.5f; sn.y = 200.0f;
        sn.width = 250.0f; sn.height = 150.0f; sn.color = 3;
        g.add_sticky_note(sn);

        std::string json;
        check(g.save_to_string(json), "save_to_string succeeds");

        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload succeeds");
        check(g2.sticky_notes().size() == 1, "1 sticky note after reload");
        const auto* loaded = g2.find_sticky_note("note_a");
        check(loaded != nullptr, "note_a found after reload");
        if (loaded) {
            check(loaded->text == "**bold** test", "text preserved");
            check_float(loaded->x, 100.5f, "x preserved");
            check_float(loaded->y, 200.0f, "y preserved");
            check_float(loaded->width, 250.0f, "width preserved");
            check_float(loaded->height, 150.0f, "height preserved");
            check(loaded->color == 3, "color preserved");
        }
    }

    // =====================================================================
    // Test: Load graph without sticky_notes key (backward compat)
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Sticky note backward compat ===\n");
        vivid::Graph g;
        check(g.load_from_string(R"({"nodes":{"a":{"type":"Foo"}},"connections":[]})"),
              "load without sticky_notes succeeds");
        check(g.sticky_notes().empty(), "no sticky notes from old graph");
    }

    // =====================================================================
    // Test: Schema version 2 round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Schema version 2 round-trip ===\n");
        vivid::Graph g;
        g.add_node("n1", "Oscillator");
        std::string json;
        check(g.save_to_string(json), "save graph");
        // Verify saved JSON contains schema_version 2
        check(json.find("\"schema_version\": 2") != std::string::npos ||
              json.find("\"schema_version\":2") != std::string::npos,
              "saved JSON has schema_version 2");
        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload schema v2 graph");
    }

    // =====================================================================
    // Test: embedded_ops parsed and injected as flat params
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: embedded_ops round-trip ===\n");
        const char* json = R"({
            "nodes": {
                "p1": {
                    "type": "Particles",
                    "params": { "count": 8, "envelope_enabled": 1, "envelope_amount": 0.75 },
                    "embedded_ops": {
                        "envelope": {
                            "type": "Envelope",
                            "params": { "attack": 0.1, "decay": 0.5 }
                        }
                    }
                }
            },
            "connections": []
        })";
        vivid::Graph g;
        check(g.load_from_string(json), "graph with embedded_ops loads");
        const auto* ndef = g.find_node("p1");
        check(ndef != nullptr, "node p1 found");
        if (ndef) {
            // Check embedded_ops stored as child NodeDef
            check(ndef->embedded_ops.count("envelope") == 1,
                  "embedded_ops contains envelope");
            const auto& child = ndef->embedded_ops.at("envelope");
            check(child.type == "Envelope", "child type is Envelope");
            auto atk_it = child.params.find("attack");
            check(atk_it != child.params.end() && atk_it->second == 0.1f,
                  "child has attack param");
            // Check flat params were injected from child
            auto it_atk = ndef->params.find("envelope_attack");
            check(it_atk != ndef->params.end() && it_atk->second == 0.1f,
                  "envelope_attack injected as flat param");
            // enabled and amount are host params, not embedded op fields
            auto it_en = ndef->params.find("envelope_enabled");
            check(it_en != ndef->params.end() && it_en->second == 1.0f,
                  "envelope_enabled is a host param");
        }
    }

    // --- Cleanup temp files ---
    std::remove("/tmp/vivid_test_valid.json");
    std::remove("/tmp/vivid_test_layout.json");
    std::remove("/tmp/vivid_test_resolution.json");
    std::remove("/tmp/vivid_test_malformed.json");
    std::remove("/tmp/vivid_test_notype.json");
    std::remove("/tmp/vivid_test_srcpath.json");
    std::remove("/tmp/vivid_test_roundtrip.json");
    std::remove("/tmp/vivid_test_nan_layout.json");
    std::remove("/tmp/vivid_test_midi_map.json");
    std::remove("/tmp/vivid_test_variations_load.json");

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
