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

        std::string path = (g_tmp->path / "dup_move_rt.json").string();
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
    // Test: current schema version round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: current schema version round-trip ===\n");
        vivid::Graph g;
        g.add_node("n1", "Oscillator");
        std::string json;
        check(g.save_to_string(json), "save graph");
        check(json.find("\"schema_version\": 4") != std::string::npos ||
              json.find("\"schema_version\":4") != std::string::npos,
              "saved JSON has schema_version 4");
        vivid::Graph g2;
        check(g2.load_from_string(json.c_str(), json.size()), "reload schema v4 graph");
    }

    // =====================================================================
    // Test: Connection bridge round-trip
    // =====================================================================
    {
        std::fprintf(stderr, "\n=== Test: Connection bridge round-trip ===\n");
        vivid::Graph g;
        check(g.load_from_string(R"({
            "nodes": {
                "lfo": { "type": "LfoFr" },
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
