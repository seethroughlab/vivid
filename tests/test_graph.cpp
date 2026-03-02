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
        check_float(conn.scale, 0.5f, "connection scale = 0.5");

        // Also test with explicit length
        vivid::Graph g2;
        std::string json_str = json;
        check(g2.load_from_string(json_str.c_str(), json_str.size()), "load_from_string with explicit len");
        check(g2.nodes().size() == 2, "2 nodes with explicit len");

        // Test failure case
        vivid::Graph g3;
        check(!g3.load_from_string("{ bad json !!!"), "load_from_string fails on bad JSON");
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
