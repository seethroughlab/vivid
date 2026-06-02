    // Tests 46-48 (duplicate_variation, move_variation) removed in Phase 1.
    // VariationDef replaced by SessionDef — see test_graph_session_schema.inc.

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
    // Test: current schema version round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: current schema version round-trip ===\n");
        vivid::Graph g;
        g.add_node("n1", "Oscillator");
        std::string json;
        check(g.save_to_string(json), "save graph");
        check(json.find("\"schema_version\": 3") != std::string::npos ||
              json.find("\"schema_version\":3") != std::string::npos,
              "saved JSON has schema_version 3");
        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload schema v3 graph");
    }

    // =====================================================================
    // Test: Connection bridge round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Connection bridge round-trip ===\n");
        vivid::Graph g;
        check(g.load_from_string(R"({
            "nodes": {
                "lfo": { "type": "Lfo" },
                "osc": { "type": "Oscillator" }
            },
            "connections": [
                { "from": "lfo/out", "to": "osc/freq", "bridge": "snapshot" }
            ]
        })"), "load graph with bridge");

        check(g.connections().size() == 1, "1 connection loaded");
        check(g.connections()[0].has_bridge(), "has_bridge() true");
        check(g.connections()[0].bridge == "snapshot", "bridge == snapshot");

        // Round-trip via string
        std::string json;
        check(g.save_to_string(json), "save with bridge");
        check(json.find("\"bridge\"") != std::string::npos, "bridge key in saved JSON");
        check(json.find("\"snapshot\"") != std::string::npos, "snapshot value in saved JSON");

        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload with bridge");
        check(g2.connections().size() == 1, "1 connection after round-trip");
        check(g2.connections()[0].bridge == "snapshot", "bridge survives round-trip");
    }

    // =====================================================================
    // Test: Connection without bridge omits key
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Connection without bridge omits key ===\n");
        vivid::Graph g;
        g.add_node("a", "Foo");
        g.add_node("b", "Bar");
        g.add_connection("a", "out", "b", "in");

        check(!g.connections()[0].has_bridge(), "no bridge on default connection");

        std::string json;
        check(g.save_to_string(json), "save without bridge");
        check(json.find("\"bridge\"") == std::string::npos, "no bridge key in saved JSON");
    }

    // =====================================================================
    // Test: Unknown bridge value round-trips as-is
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Unknown bridge value round-trips ===\n");
        vivid::Graph g;
        check(g.load_from_string(R"({
            "nodes": { "a": { "type": "Foo" }, "b": { "type": "Bar" } },
            "connections": [
                { "from": "a/out", "to": "b/in", "bridge": "bogus" }
            ]
        })"), "load with unknown bridge");
        check(g.connections()[0].bridge == "bogus", "unknown bridge preserved on load");

        std::string json;
        check(g.save_to_string(json), "save with unknown bridge");
        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload unknown bridge");
        check(g2.connections()[0].bridge == "bogus", "unknown bridge survives round-trip");
    }
