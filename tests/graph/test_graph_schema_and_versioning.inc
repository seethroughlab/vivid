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
        std::string path = (g_tmp->path / "viewport_rt.json").string();
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
        std::string path2 = (g_tmp->path / "viewport_none.json").string();
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
        std::string path = (g_tmp->path / "empty_graph.json").string();
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
        std::string path = (g_tmp->path / "dangling.json").string();
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
        std::string path = (g_tmp->path / "cycle.json").string();
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
        check(g.add_connection("audio_src", "lane_out", "vis_node", "gpu_in"),
              "lane_array→gpu connection accepted by data model");

        const auto& c0 = g.connections()[0];
        check(c0.from_port == "audio_out",   "from_port stored verbatim");
        check(c0.to_port   == "control_in",  "to_port stored verbatim");

        // Port names with unusual characters are also stored verbatim
        check(g.add_connection("audio_src", "out.0", "vis_node", "in.rgb"), "dot-separated port names accepted");
        check(g.connections().size() == 3, "3 connections stored");

        // Round-trip preserves all port names exactly
        std::string path = (g_tmp->path / "port_types.json").string();
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
        check(g1.add_node("a", "ClockFr", {{"bpm", 120.0f}}), "add node a");
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
