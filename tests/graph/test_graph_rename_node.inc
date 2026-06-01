// Tests for Graph::rename_node — the atomic node-id rename sweep.
{
    std::fprintf(stderr, "\n=== Test: rename_node atomic reference sweep ===\n");

    vivid::Graph g;
    g.add_node("src", "Oscillator");
    g.add_node("dst", "Gain");
    g.add_connection("src", "output", "dst", "input");

    // A preset, a MIDI mapping, a mod assignment, the quantize-clock ref, and a
    // session track + clip all referencing "src".
    vivid::OperatorPreset preset;
    preset.name = "p1";
    preset.params["frequency"] = 2.0f;
    g.save_preset("src", preset);

    g.add_midi_mapping("src", "frequency", 74, 0, 0.0f, 1.0f);

    vivid::ModAssignmentDef mod;
    mod.source = "lfo"; mod.destination = "amp"; mod.amount = 0.5f;
    g.add_mod_assignment("src", mod);

    g.set_quantize_clock_node("src");

    std::string tr = g.create_track("T");
    g.assign_nodes_to_track(tr, {"src"});
    std::unordered_map<std::string, std::unordered_map<std::string, float>> cp;
    cp["src"]["frequency"] = 3.0f;
    std::string clip = g.save_clip(tr, "c1", cp, {});

    // --- rename ---
    check(g.rename_node("src", "lead"), "rename src -> lead succeeds");
    check(g.find_node("src") == nullptr, "old id 'src' gone");
    check(g.find_node("lead") != nullptr, "new id 'lead' present");

    bool conn_ok = false, no_old_conn = true;
    for (const auto& c : g.connections()) {
        if (c.from_node == "lead" && c.to_node == "dst") conn_ok = true;
        if (c.from_node == "src" || c.to_node == "src") no_old_conn = false;
    }
    check(conn_ok, "connection from_node remapped to 'lead'");
    check(no_old_conn, "no connection references old 'src'");

    check(g.list_presets("lead").size() == 1, "preset moved to 'lead'");
    check(g.list_presets("src").empty(), "no preset under 'src'");

    check(g.find_midi_mapping("lead", "frequency") != nullptr, "midi mapping under 'lead'");
    check(g.find_midi_mapping("src", "frequency") == nullptr, "no midi mapping under 'src'");

    check(g.find_mod_assignments("lead") != nullptr, "mod assignments under 'lead'");
    check(g.find_mod_assignments("src") == nullptr, "no mod assignments under 'src'");

    check(g.quantize_clock_node() == "lead", "quantize clock remapped to 'lead'");

    const auto* trk = g.find_track(tr);
    bool owns_lead = false, owns_src = false;
    if (trk) {
        for (const auto& id : trk->owned_node_ids) {
            if (id == "lead") owns_lead = true;
            if (id == "src")  owns_src = true;
        }
    }
    check(owns_lead, "track owns 'lead'");
    check(!owns_src, "track no longer owns 'src'");

    const auto* cl = g.find_clip(tr, clip);
    check(cl && cl->params.count("lead") == 1, "clip params re-keyed to 'lead'");
    check(cl && cl->params.count("src") == 0, "clip params no longer under 'src'");

    // --- rejections (the validity / uniqueness guard) ---
    check(!g.rename_node("lead", "dst"), "reject rename to an existing id");
    check(!g.rename_node("lead", ""), "reject empty id");
    check(!g.rename_node("nope", "x"), "reject unknown old id");
    check(!g.rename_node("lead", "a/b"), "reject invalid charset (slash)");
    check(!g.rename_node("lead", "has space"), "reject invalid charset (space)");
    check(!g.rename_node("lead", "lead"), "reject unchanged id");
    check(g.find_node("lead") != nullptr, "node still 'lead' after rejected renames");
    check(g.find_node("dst") != nullptr, "'dst' untouched by rejected renames");

    // --- subgraph member rejection ---
    {
        vivid::Graph gs;
        gs.add_node("mod", "Module");
        gs.add_node("inner", "Osc");
        // Mark 'inner' as a member owned by 'mod'.
        gs.nodes_mut().back().subgraph_owner = "mod";
        check(!gs.rename_node("inner", "inner2"), "reject renaming a subgraph member");
        check(!gs.rename_node("mod", "mod2"), "reject renaming a module owner");
    }

    // --- JSON round-trip ---
    std::string path = (g_tmp->path / "rename.json").string();
    check(g.save(path.c_str()), "save after rename");
    vivid::Graph g2;
    check(g2.load(path.c_str()), "load after rename");
    check(g2.find_node("lead") != nullptr, "'lead' present after round-trip");
    check(g2.find_node("src") == nullptr, "'src' absent after round-trip");
    bool rt_conn = false;
    for (const auto& c : g2.connections())
        if (c.from_node == "lead" && c.to_node == "dst") rt_conn = true;
    check(rt_conn, "connection intact after round-trip");
}
