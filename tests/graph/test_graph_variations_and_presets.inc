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
                "lfo1": { "type": "Lfo", "params": { "frequency": 2.0 } },
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

    // Tests 16-20 removed in Phase 1: VariationDef replaced by SessionDef.
    // Session CRUD tests live in test_graph_session_schema.inc (included in test_graph.cpp).

    // =====================================================================
    // Test 20: remove_node cascading cleanup (presets, state mappings)
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

        // Add state-preset mappings: sm1 controls presets on osc and filt
        g.set_state_preset("sm1", 0, "osc", "Bright");
        g.set_state_preset("sm1", 0, "filt", "Open");
        check(g.find_state_mapping("sm1") != nullptr, "state mapping exists for sm1");

        // Also add a mapping where osc is a target in another SM's mapping
        g.add_node("sm2", "StateMachine");
        g.set_state_preset("sm2", 0, "osc", "Bright");

        // Remove osc — should cascade to presets and state mappings
        g.remove_node("osc");
        check(g.list_presets("osc").empty(), "osc presets cleaned up");
        check(g.list_presets("filt").size() == 1, "filt presets untouched");

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
    // Test 25b: Graph::ensure_state_mapping
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test 25b: Graph::ensure_state_mapping ===\n");
        vivid::Graph g;
        g.add_node("sm1", "StateMachine");

        check(g.state_preset_mappings().empty(), "25b: no mappings initially");

        g.ensure_state_mapping("sm1");
        check(g.state_preset_mappings().size() == 1, "25b: mapping created");
        check(g.state_preset_mappings()[0].state_machine_node == "sm1", "25b: correct node id");
        check(g.state_preset_mappings()[0].state_presets.empty(), "25b: no presets yet");

        g.ensure_state_mapping("sm1");
        check(g.state_preset_mappings().size() == 1, "25b: idempotent");

        // set_state_preset works on an already-registered SM
        g.add_node("osc", "Osc");
        g.set_state_preset("sm1", 0, "osc", "Bright");
        check(g.state_preset_mappings().size() == 1, "25b: still one mapping after preset bind");
        check(!g.state_preset_mappings()[0].state_presets.empty(), "25b: preset now bound");

        // Round-trip: empty mapping entry (no presets) survives save/load
        vivid::Graph g2;
        g2.add_node("sm2", "StateMachine");
        g2.ensure_state_mapping("sm2");
        std::string rt_path = (g_tmp->path / "ensure_state_mapping_rt.json").string();
        check(g2.save(rt_path.c_str()), "25b: save with empty mapping");
        vivid::Graph g3;
        check(g3.load(rt_path.c_str()), "25b: load with empty mapping");
        check(g3.find_state_mapping("sm2") != nullptr, "25b: empty mapping survives round-trip");
        std::remove(rt_path.c_str());
    }

    // =====================================================================
    // Test 26: Viewport round-trip
    // =====================================================================
    {
