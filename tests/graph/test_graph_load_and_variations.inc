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
        check(!g.load("/nonexistent/does_not_exist_12345.json"), "load returns false");
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

        std::string path = (g_tmp->path / "roundtrip.json").string();
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
