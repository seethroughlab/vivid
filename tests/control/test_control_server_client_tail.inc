        // =============================================================
        // Re-add a test node for the remaining endpoint tests
        // (the unlink phase reloaded the graph, so node "a" is gone)
        // =============================================================
        {
            auto re_add = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"test_ep"})");
            check(re_add.ok, "re-add TestOp node for endpoint tests");
            phase.store(21);
            while (phase.load() < 22) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // =============================================================
        // Solo set/get
        // =============================================================
        {
            std::fprintf(stderr, "\n  solo set/get\n");

            auto get1 = post(client, base_url, "get_solo");
            check(get1.ok, "get_solo ok");
            if (!get1.j.is_null()) {
                check(get1.j.contains("active") && !get1.j["active"].get<bool>(),
                      "solo not active initially");
            }

            auto set1 = post(client, base_url, "set_solo",
                R"({"node_id":"test_ep"})");
            check(set1.ok, "set_solo on node test_ep ok");

            auto get2 = post(client, base_url, "get_solo");
            check(get2.ok, "get_solo after set ok");
            if (!get2.j.is_null()) {
                check(get2.j["active"].get<bool>(), "solo active after set");
                check(get2.j["node_id"].get<std::string>() == "test_ep", "solo node_id is test_ep");
            }

            // Clear solo
            auto clear = post(client, base_url, "set_solo",
                R"({"node_id":""})");
            check(clear.ok, "clear solo ok");

            auto get3 = post(client, base_url, "get_solo");
            check(get3.ok, "get_solo after clear ok");
            if (!get3.j.is_null()) {
                check(!get3.j["active"].get<bool>(), "solo cleared");
            }
        }

        // =============================================================
        // Param lock set/get
        // =============================================================
        {
            std::fprintf(stderr, "\n  param lock set/get\n");

            auto set_lock = post(client, base_url, "set_param_lock",
                R"({"node_id":"test_ep","param":"scale","flags":1})");
            check(set_lock.ok, "set_param_lock ok");

            auto get_lock = post(client, base_url, "get_param_lock",
                R"({"node_id":"test_ep","param":"scale"})");
            check(get_lock.ok, "get_param_lock ok");
            if (!get_lock.j.is_null() && get_lock.j.contains("result")) {
                auto& res = get_lock.j["result"];
                check(res.contains("flags") && res["flags"].get<int>() == 1,
                      "param lock flags == 1");
            }

            // Clear lock
            auto clear_lock = post(client, base_url, "set_param_lock",
                R"({"node_id":"test_ep","param":"scale","flags":0})");
            check(clear_lock.ok, "clear param lock ok");

            auto get_lock2 = post(client, base_url, "get_param_lock",
                R"({"node_id":"test_ep","param":"scale"})");
            check(get_lock2.ok, "get_param_lock after clear ok");
            if (!get_lock2.j.is_null() && get_lock2.j.contains("result")) {
                auto& res = get_lock2.j["result"];
                check(res.contains("flags") && res["flags"].get<int>() == 0,
                      "param lock flags cleared to 0");
            }
        }

        // =============================================================
        // Preset lifecycle: save, list, recall, rename, remove
        // =============================================================
        {
            std::fprintf(stderr, "\n  preset lifecycle\n");

            // Set param to known value before saving
            post(client, base_url, "set_param",
                R"({"node_id":"test_ep","param":"scale","value":7.5})");

            auto save = post(client, base_url, "save_preset",
                R"({"node_id":"test_ep","name":"test_preset"})");
            check(save.ok, "save_preset ok");

            auto list1 = post(client, base_url, "list_presets",
                R"({"node_id":"test_ep"})");
            check(list1.ok, "list_presets ok");
            if (!list1.j.is_null() && list1.j.contains("result")) {
                auto& res = list1.j["result"];
                bool found = false;
                if (res.contains("presets") && res["presets"].is_array()) {
                    for (const auto& p : res["presets"]) {
                        if (p.is_string() && p.get<std::string>() == "test_preset")
                            found = true;
                    }
                }
                check(found, "test_preset in preset list");
            }

            // Change param, then recall preset
            post(client, base_url, "set_param",
                R"({"node_id":"test_ep","param":"scale","value":0.0})");

            auto recall = post(client, base_url, "recall_preset",
                R"({"node_id":"test_ep","name":"test_preset"})");
            check(recall.ok, "recall_preset ok");

            // Verify param restored
            auto get_p = post(client, base_url, "get_param",
                R"({"node_id":"test_ep","param":"scale"})");
            check(get_p.ok, "get_param after recall ok");
            if (!get_p.j.is_null() && get_p.j.contains("result")) {
                float val = get_p.j["result"]["value"].get<float>();
                check(std::fabs(val - 7.5f) < 0.01f,
                      "param restored to 7.5 after recall");
            }

            // Rename preset
            auto rename = post(client, base_url, "rename_preset",
                R"({"node_id":"test_ep","old_name":"test_preset","new_name":"renamed_preset"})");
            check(rename.ok, "rename_preset ok");

            auto list2 = post(client, base_url, "list_presets",
                R"({"node_id":"test_ep"})");
            if (!list2.j.is_null() && list2.j.contains("result")) {
                auto& res = list2.j["result"];
                bool found_old = false, found_new = false;
                if (res.contains("presets") && res["presets"].is_array()) {
                    for (const auto& p : res["presets"]) {
                        if (p.is_string()) {
                            if (p.get<std::string>() == "test_preset") found_old = true;
                            if (p.get<std::string>() == "renamed_preset") found_new = true;
                        }
                    }
                }
                check(!found_old, "old preset name gone");
                check(found_new, "new preset name present");
            }

            // Update preset
            post(client, base_url, "set_param",
                R"({"node_id":"test_ep","param":"scale","value":9.9})");
            auto update = post(client, base_url, "update_preset",
                R"({"node_id":"test_ep","name":"renamed_preset"})");
            check(update.ok, "update_preset ok");

            // Remove preset
            auto remove = post(client, base_url, "remove_preset",
                R"({"node_id":"test_ep","name":"renamed_preset"})");
            check(remove.ok, "remove_preset ok");

            auto list3 = post(client, base_url, "list_presets",
                R"({"node_id":"test_ep"})");
            if (!list3.j.is_null() && list3.j.contains("result")) {
                auto& res = list3.j["result"];
                bool found = false;
                if (res.contains("presets") && res["presets"].is_array()) {
                    for (const auto& p : res["presets"]) {
                        if (p.is_string() && p.get<std::string>() == "renamed_preset")
                            found = true;
                    }
                }
                check(!found, "preset removed from list");
            }
        }

        // =============================================================
        // Factory presets
        // =============================================================
        {
            std::fprintf(stderr, "\n  factory presets\n");

            auto fp = post(client, base_url, "list_factory_presets",
                R"({"node_id":"test_ep"})");
            check(fp.ok, "list_factory_presets ok");
            if (!fp.j.is_null() && fp.j.contains("result")) {
                check(fp.j["result"].contains("presets"),
                      "factory presets response has presets field");
            }
        }

        // =============================================================
        // Set node layout
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_node_layout\n");

            auto layout = post(client, base_url, "set_node_layout",
                R"({"node_id":"test_ep","x":100,"y":200})");
            check(layout.ok, "set_node_layout ok");
        }

        // =============================================================
        // Set resolution
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_resolution\n");

            auto res = post(client, base_url, "set_resolution",
                R"({"node_id":"test_ep","width":1920,"height":1080})");
            check(res.ok, "set_resolution ok");
        }

        // =============================================================
        // set_analysis
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_analysis\n");

            auto r1 = post(client, base_url, "set_analysis", R"({"enabled":true})");
            check(r1.ok, "set_analysis enabled=true ok");
            if (!r1.j.is_null()) {
                check(r1.j.value("message", "").find("enabled") != std::string::npos,
                      "set_analysis response mentions enabled");
            }

            auto r2 = post(client, base_url, "set_analysis", R"({"enabled":false})");
            check(r2.ok, "set_analysis enabled=false ok");

            auto r3 = post(client, base_url, "set_analysis", R"({})");
            check(!r3.ok, "set_analysis missing enabled fails");

            auto r4 = post(client, base_url, "set_analysis", R"({"enabled":"yes"})");
            check(!r4.ok, "set_analysis non-boolean enabled fails");
        }

        // =============================================================
        // set_connection_remap
        // =============================================================
        {
            std::fprintf(stderr, "\n  set_connection_remap\n");

            // Add a second node and connect to test_ep so we have a connection to remap
            auto add_remap_dst = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"remap_dst"})");
            check(add_remap_dst.ok, "add_node remap_dst ok");

            auto conn = post(client, base_url, "connect",
                R"({"from_addr":"test_ep/out","to_addr":"remap_dst/scale"})");
            check(conn.ok, "connect test_ep/out -> remap_dst/scale ok");

            auto remap = post(client, base_url, "set_connection_remap",
                R"({"from_addr":"test_ep/out","to_addr":"remap_dst/scale","from_min":0.0,"from_max":1.0,"to_min":0.0,"to_max":10.0,"clamp":true})");
            check(remap.ok, "set_connection_remap ok");

            // Error: non-existent connection
            auto r2 = post(client, base_url, "set_connection_remap",
                R"({"from_addr":"nonexist/out","to_addr":"nonexist/in","from_min":0,"from_max":1,"to_min":0,"to_max":1})");
            check(!r2.ok, "set_connection_remap on nonexistent connection fails");

            // Clean up
            post(client, base_url, "disconnect",
                R"({"from_addr":"test_ep/out","to_addr":"remap_dst/scale"})");
            post(client, base_url, "remove_node", R"({"node_id":"remap_dst"})");
        }

        // =============================================================
        // MIDI mapping lifecycle
        // =============================================================
        {
            std::fprintf(stderr, "\n  midi mapping lifecycle\n");

            auto add = post(client, base_url, "add_midi_mapping",
                R"({"node_id":"test_ep","param":"scale","cc":1,"channel":1,"range_min":0.0,"range_max":1.0})");
            check(add.ok, "add_midi_mapping ok");

            auto update = post(client, base_url, "update_midi_mapping",
                R"({"node_id":"test_ep","param":"scale","range_min":0.2,"range_max":0.8})");
            check(update.ok, "update_midi_mapping ok");

            auto remove = post(client, base_url, "remove_midi_mapping",
                R"({"node_id":"test_ep","param":"scale"})");
            check(remove.ok, "remove_midi_mapping ok");

            // Remove again should fail
            auto remove2 = post(client, base_url, "remove_midi_mapping",
                R"({"node_id":"test_ep","param":"scale"})");
            check(!remove2.ok, "remove_midi_mapping already removed fails");

            // Update non-existent should fail
            auto update2 = post(client, base_url, "update_midi_mapping",
                R"({"node_id":"test_ep","param":"scale","range_min":0.0,"range_max":1.0})");
            check(!update2.ok, "update_midi_mapping non-existent fails");
        }

        // =============================================================
        // State machine presets
        // =============================================================
        {
            std::fprintf(stderr, "\n  state machine presets\n");

            auto set1 = post(client, base_url, "set_state_preset",
                R"({"sm_node":"test_ep","state_idx":0,"target_node":"b","name":"sp1"})");
            check(set1.ok, "set_state_preset ok");

            auto inspect = post(client, base_url, "inspect_state_presets",
                R"({"sm_node":"test_ep"})");
            check(inspect.ok, "inspect_state_presets ok");

            // Invalid state_idx (must be 0-7)
            auto set_bad = post(client, base_url, "set_state_preset",
                R"({"sm_node":"test_ep","state_idx":8,"target_node":"b","name":"sp2"})");
            check(!set_bad.ok, "set_state_preset state_idx=8 fails");

            auto rem = post(client, base_url, "remove_state_preset",
                R"({"sm_node":"test_ep","state_idx":0,"target_node":"b"})");
            check(rem.ok, "remove_state_preset ok");

            // Set again and clear
            auto set2 = post(client, base_url, "set_state_preset",
                R"({"sm_node":"test_ep","state_idx":1,"target_node":"b","name":"sp3"})");
            check(set2.ok, "set_state_preset state_idx=1 ok");

            auto clear = post(client, base_url, "clear_state_presets",
                R"({"sm_node":"test_ep"})");
            check(clear.ok, "clear_state_presets ok");

            // Inspect after clear should show nothing
            auto inspect2 = post(client, base_url, "inspect_state_presets",
                R"({"sm_node":"test_ep"})");
            check(inspect2.ok, "inspect_state_presets after clear ok");
        }

        // =============================================================
        // get_discovery_report
        // =============================================================
        {
            std::fprintf(stderr, "\n  get_discovery_report\n");

            auto r = post(client, base_url, "get_discovery_report");
            check(r.ok, "get_discovery_report ok");
            if (!r.j.is_null()) {
                check(r.j.contains("result") && r.j["result"].is_object(),
                      "get_discovery_report has result");
                auto& result = r.j["result"];
                check(result.contains("scopes") && result["scopes"].is_array(),
                      "discovery_report has scopes array");
                check(result.contains("loaded") && result["loaded"].is_array(),
                      "discovery_report has loaded array");
                check(result.contains("skipped") && result["skipped"].is_array(),
                      "discovery_report has skipped array");
            }
        }

        // =============================================================
        // new_graph — MUST BE LAST (destroys graph state)
        // =============================================================
        {
            std::fprintf(stderr, "\n  new_graph\n");

            // Write minimal default_graph.json so new_graph has a template
            std::string config_dir = test_home + "/Library/Application Support/Vivid";
            std::filesystem::create_directories(config_dir);
            {
                std::ofstream ofs(config_dir + "/default_graph.json");
                ofs << R"({"nodes":{"default_a":{"type":"TestOp","params":{"scale":1.0}}},"connections":[]})";
            }

            auto r = post(client, base_url, "new_graph");
            check(r.ok, "new_graph ok");

            // Verify graph was reset
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after new_graph ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array() && nodes.size() == 1,
                      "new_graph produced 1-node default graph");
                if (nodes.is_array() && nodes.size() == 1) {
                    check(nodes[0]["id"].get<std::string>() == "default_a",
                          "new_graph default node is default_a");
                }
            }
        }
