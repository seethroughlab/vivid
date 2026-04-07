        std::fprintf(stderr, "\n=== Test 14: NaN layout round-trip ===\n");
        vivid::Graph g1;
        g1.add_node("a", "Foo");

        const auto* na = g1.find_node("a");
        check(!na->has_layout(), "new node has no layout (NaN)");
        check(std::isnan(na->layout_x), "layout_x is NaN");

        std::string path = (g_tmp->path / "nan_layout.json").string();
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
                "lfo1": { "type": "LfoFr", "params": { "frequency": 2.0 } },
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

        std::string rt_path = (g_tmp->path / "midi_rt.json").string();
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
    // Test 17: active_variation + quantize_clock_node + metronome
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 17: active_variation + quantize_clock_node + metronome ===\n");
        vivid::Graph g;

        // Initial state
        check(g.active_variation() == -1, "initial active_variation = -1");
        check(g.quantize_clock_node().empty(), "initial quantize_clock_node empty");
        check_float(g.metronome().bpm, 120.0f, "initial metronome bpm = 120");
        check(g.metronome().beats_per_bar == 4, "initial beats_per_bar = 4");

        // set/get round-trip
        g.set_active_variation(2);
        check(g.active_variation() == 2, "active_variation set to 2");

        g.set_quantize_clock_node("clock1");
        check(g.quantize_clock_node() == "clock1", "quantize_clock_node = clock1");

        vivid::GraphMetronomeDef metronome;
        metronome.bpm = 96.0f;
        metronome.beats_per_bar = 7;
        g.set_metronome(metronome);
        check_float(g.metronome().bpm, 96.0f, "metronome bpm = 96");
        check(g.metronome().beats_per_bar == 7, "beats_per_bar = 7");

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
            "quantize_clock": "clock1",
            "metronome": { "enabled": true, "bpm": 92.0, "beats_per_bar": 3 }
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
        check_float(g.metronome().bpm, 92.0f, "metronome bpm = 92");
        check(g.metronome().beats_per_bar == 3, "metronome beats_per_bar = 3");
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
        vivid::GraphMetronomeDef metronome;
        metronome.bpm = 135.0f;
        metronome.beats_per_bar = 5;
        g1.set_metronome(metronome);

        std::string path = (g_tmp->path / "var_roundtrip.json").string();
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
        check_float(g2.metronome().bpm, 135.0f, "metronome bpm preserved");
        check(g2.metronome().beats_per_bar == 5, "metronome beats_per_bar preserved");

        // Also verify a graph with 0 variations saves/loads cleanly
        vivid::Graph g3;
        g3.add_node("x", "X");
        std::string path2 = (g_tmp->path / "var_empty.json").string();
        check(g3.save(path2.c_str()), "save empty variations succeeds");

        vivid::Graph g4;
        check(g4.load(path2.c_str()), "reload empty variations succeeds");
        check(g4.variations().empty(), "0 variations after round-trip");
        check(g4.active_variation() == -1, "active_variation = -1 with no variations");
        check(g4.quantize_clock_node().empty(), "quantize_clock empty with no variations");
        check_float(g4.metronome().bpm, 120.0f, "default metronome bpm preserved");
        check(g4.metronome().beats_per_bar == 4, "default metronome beats_per_bar preserved");

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
                { "from": "osc/output", "to": "gain/input", "to_max": 0.5 }
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
        check_float(conn.to_max, 0.5f, "connection to_max = 0.5");

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
        std::string path = (g_tmp->path / "remap_rt.json").string();
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
    // Test 23: Legacy filter graph formats are rejected
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 23: Legacy filter graph formats are rejected ===\n");
        vivid::Graph g;

        const char* legacy_filters_json = R"JSON({
            "schema_version": 3,
            "nodes": {
                "blur1": { "type": "Blur" }
            },
            "filters": [
                { "name": "Blur", "shader": "@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0); }" }
            ]
        })JSON";
        check(!g.load_from_string(legacy_filters_json), "graphs with top-level filters are rejected");

        const char* legacy_wgsl_filter_node_json = R"JSON({
            "schema_version": 3,
            "nodes": {
                "filter1": { "type": "WGSLFilter", "string_params": { "filter": "Blur" } }
            }
        })JSON";
        check(!g.load_from_string(legacy_wgsl_filter_node_json),
              "graphs with WGSLFilter nodes are rejected");
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
        std::string path = (g_tmp->path / "preset_rt.json").string();
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

        std::string path = (g_tmp->path / "state_rt.json").string();
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
