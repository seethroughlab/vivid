        // Phase 3: set_param — set a/scale=9
        std::fprintf(stderr, "\n--- set_param ---\n");
        {
            auto r = post(client, base_url, "set_param",
                R"({"node_id":"a","param":"scale","value":9.0})");
            check(r.ok, "set_param a/scale=9 ok");
        }

        // Phase 4: get_param — verify a/scale=9
        std::fprintf(stderr, "\n--- get_param ---\n");
        {
            auto r = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(r.ok, "get_param a/scale ok");
            if (!r.j.is_null()) {
                check(r.j.contains("value") &&
                          std::fabs(r.j["value"].get<double>() - 9.0) < 1e-4,
                      "value is 9.0");
            }
        }

        // Phase 4b: undo/redo via MCP control methods
        std::fprintf(stderr, "\n--- undo/redo ---\n");
        {
            auto ru = post(client, base_url, "undo");
            check(ru.ok, "undo ok");
            auto rg = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(rg.ok, "get_param after undo ok");
            if (!rg.j.is_null()) {
                check(rg.j.contains("value") &&
                          std::fabs(rg.j["value"].get<double>() - 3.0) < 1e-4,
                      "value restored to 3.0 after undo");
            }

            auto rr = post(client, base_url, "redo");
            check(rr.ok, "redo ok");
            auto rg2 = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(rg2.ok, "get_param after redo ok");
            if (!rg2.j.is_null()) {
                check(rg2.j.contains("value") &&
                          std::fabs(rg2.j["value"].get<double>() - 9.0) < 1e-4,
                      "value restored to 9.0 after redo");
            }
        }

        // set_string_param
        std::fprintf(stderr, "\n--- set_string_param ---\n");
        {
            // TestOp has no string/file params, so this should fail
            auto r = post(client, base_url, "set_string_param",
                R"({"node_id":"a","param":"scale","value":"hello"})");
            check(!r.ok, "set_string_param on float param fails");

            auto r2 = post(client, base_url, "set_string_param",
                R"({"node_id":"nonexistent","param":"x","value":"y"})");
            check(!r2.ok, "set_string_param on nonexistent node fails");

            auto r3 = post(client, base_url, "set_string_param", R"({})");
            check(!r3.ok, "set_string_param with empty body fails");
        }

        // sample_node_outputs
        std::fprintf(stderr, "\n--- sample_node_outputs ---\n");
        {
            auto r = post(client, base_url, "sample_node_outputs",
                R"({"node_id":"a","duration_seconds":0.05,"interval_ms":10})");
            check(r.ok, "sample_node_outputs ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["node_id"].get<std::string>() == "a", "sample result node_id=a");
                check(result["type"].get<std::string>() == "TestOp", "sample result type=TestOp");
                check(result["kind"].get<std::string>() == "control", "sample result kind=control");
                check(result["sample_count"].get<int>() >= 1, "sample_count >= 1");
                check(result["samples"].is_array() && result["samples"].size() >= 1,
                      "samples array non-empty");
                if (result["samples"].is_array() && result["samples"].size() > 1) {
                    double first_t = result["samples"][0]["time_seconds"].get<double>();
                    double last_t = result["samples"][result["samples"].size() - 1]["time_seconds"].get<double>();
                    check(last_t > first_t,
                          "sample_node_outputs collects samples across runtime ticks");
                }
                if (result["samples"].is_array() && result["samples"].size() > 0) {
                    auto& s0 = result["samples"][0];
                    check(s0.contains("time_seconds") && s0["time_seconds"].is_number(),
                          "sample has time_seconds");
                    check(s0.contains("outputs") && s0["outputs"].is_object(),
                          "sample has outputs object");
                    if (s0["outputs"].contains("out")) {
                        check(s0["outputs"]["out"].contains("scalar"),
                              "sample output has scalar field");
                    }
                }
            }

            // Error: nonexistent node
            auto r2 = post(client, base_url, "sample_node_outputs",
                R"({"node_id":"nonexistent","duration_seconds":0.01,"interval_ms":10})");
            check(!r2.ok, "sample_node_outputs on nonexistent node fails");
        }

        // Phase 5: add_node — add TestOp as "c"
        std::fprintf(stderr, "\n--- add_node ---\n");
        {
            auto r = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"c"})");
            check(r.ok, "add_node TestOp c ok");
        }

        // Phase 6: connect — a/out -> c/scale
        std::fprintf(stderr, "\n--- connect ---\n");
        {
            auto r = post(client, base_url, "connect",
                R"({"from_addr":"a/out","to_addr":"c/scale"})");
            check(r.ok, "connect a/out -> c/scale ok");
        }

        // Signal main thread to apply_pending
        phase.store(1);
        // Wait for main thread to apply
        while (phase.load() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Phase 7: inspect_graph — should now have 3 nodes, 2 connections
        std::fprintf(stderr, "\n--- inspect_graph (after add+connect) ---\n");
        {
            auto r = post(client, base_url, "inspect_graph");
            check(r.ok, "inspect_graph ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                auto& conns = result["connections"];
                check(nodes.is_array() && nodes.size() == 3, "3 nodes");
                check(conns.is_array() && conns.size() == 2, "2 connections");
            }
        }

        // Phase 8: disconnect — a/out -> c/scale
        std::fprintf(stderr, "\n--- disconnect ---\n");
        {
            auto r = post(client, base_url, "disconnect",
                R"({"from_addr":"a/out","to_addr":"c/scale"})");
            check(r.ok, "disconnect a/out -> c/scale ok");
        }

        // Phase 9: remove_node — remove "c"
        std::fprintf(stderr, "\n--- remove_node ---\n");
        {
            auto r = post(client, base_url, "remove_node",
                R"({"node_id":"c"})");
            check(r.ok, "remove_node c ok");
        }

        // Signal main thread to apply_pending again
        phase.store(3);
        while (phase.load() < 4) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        // Phase 10: inspect_graph — back to 2 nodes
        std::fprintf(stderr, "\n--- inspect_graph (after remove) ---\n");
        {
            auto r = post(client, base_url, "inspect_graph");
            check(r.ok, "inspect_graph ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array() && nodes.size() == 2, "back to 2 nodes");
            }
        }

        // Phase 10b: semantic-default connect remap
        std::fprintf(stderr, "\n--- semantic default connect remap ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"MsSourceOp","node_id":"ms1"})");
            check(add_src.ok, "add_node MsSourceOp ms1 ok");

            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"SecDestOp","node_id":"s1"})");
            check(add_dst.ok, "add_node SecDestOp s1 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec","semantic_defaults":true})");
            check(c.ok, "connect ms1/ms -> s1/sec with semantic_defaults ok");
            if (!c.j.is_null()) {
                std::string m = c.j.value("message", "");
                check(m.find("semantic default remap applied") != std::string::npos,
                      "connect response reports semantic remap applied");
                check(c.j.contains("inferred_remap_applied") &&
                          c.j["inferred_remap_applied"].is_boolean() &&
                          c.j["inferred_remap_applied"].get<bool>(),
                      "connect response inferred_remap_applied=true");
                check(c.j.contains("inferred_remap") && c.j["inferred_remap"].is_object(),
                      "connect response includes inferred_remap object");
            }
        }

        phase.store(9);
        while (phase.load() < 10) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after semantic connect ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "ms1/ms" ||
                            conn.value("to", "") != "s1/sec") continue;
                        found = true;
                        check(conn.contains("from_min") &&
                                  std::fabs(conn["from_min"].get<double>() - 0.0) < 1e-6,
                              "semantic remap from_min=0");
                        check(conn.contains("from_max") &&
                                  std::fabs(conn["from_max"].get<double>() - 2000.0) < 1e-6,
                              "semantic remap from_max=2000");
                        check(conn.contains("to_min") &&
                                  std::fabs(conn["to_min"].get<double>() - 0.0) < 1e-6,
                              "semantic remap to_min=0");
                        check(conn.contains("to_max") &&
                                  std::fabs(conn["to_max"].get<double>() - 2.0) < 1e-6,
                              "semantic remap to_max=2");
                        break;
                    }
                }
                check(found, "semantic remap connection present in inspect_graph");
            }
        }

        // Inferred remap must be user-overridable via set_connection_remap.
        {
            auto r = post(client, base_url, "set_connection_remap",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec","from_min":10.0,"from_max":20.0,"to_min":1.0,"to_max":2.0,"clamp":true})");
            check(r.ok, "set_connection_remap overrides inferred remap");
        }

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after remap override ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "ms1/ms" ||
                            conn.value("to", "") != "s1/sec") continue;
                        found = true;
                        check(conn.contains("from_min") &&
                                  std::fabs(conn["from_min"].get<double>() - 10.0) < 1e-6,
                              "override remap from_min=10");
                        check(conn.contains("from_max") &&
                                  std::fabs(conn["from_max"].get<double>() - 20.0) < 1e-6,
                              "override remap from_max=20");
                        check(conn.contains("to_min") &&
                                  std::fabs(conn["to_min"].get<double>() - 1.0) < 1e-6,
                              "override remap to_min=1");
                        check(conn.contains("to_max") &&
                                  std::fabs(conn["to_max"].get<double>() - 2.0) < 1e-6,
                              "override remap to_max=2");
                        check(conn.contains("clamp") && conn["clamp"].is_boolean() &&
                                  conn["clamp"].get<bool>(),
                              "override remap clamp=true");
                        break;
                    }
                }
                check(found, "semantic remap connection present after override");
            }
        }

        {
            auto d = post(client, base_url, "disconnect",
                R"({"from_addr":"ms1/ms","to_addr":"s1/sec"})");
            check(d.ok, "disconnect semantic remap connection ok");

            auto rm_src = post(client, base_url, "remove_node",
                R"({"node_id":"ms1"})");
            check(rm_src.ok, "remove_node ms1 ok");

            auto rm_dst = post(client, base_url, "remove_node",
                R"({"node_id":"s1"})");
            check(rm_dst.ok, "remove_node s1 ok");
        }

        // Phase 10c: semantic-default coercion only for explicit conversion pairs.
        std::fprintf(stderr, "\n--- semantic explicit coercion contract ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"hz1"})");
            check(add_src.ok, "add_node TestOp hz1 ok");

            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"SecDestOp","node_id":"s2"})");
            check(add_dst.ok, "add_node SecDestOp s2 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"hz1/out","to_addr":"s2/sec","semantic_defaults":true})");
            check(c.ok, "connect hz1/out -> s2/sec with semantic_defaults ok");
            if (!c.j.is_null()) {
                std::string m = c.j.value("message", "");
                check(m.find("semantic default remap applied") == std::string::npos,
                      "non-contract coercion does not apply remap");
                check(c.j.contains("inferred_remap_applied") &&
                          c.j["inferred_remap_applied"].is_boolean() &&
                          !c.j["inferred_remap_applied"].get<bool>(),
                      "connect response inferred_remap_applied=false for non-contract pair");
                check(!c.j.contains("inferred_remap"),
                      "connect response omits inferred_remap object for non-contract pair");
            }
        }

        phase.store(11);
        while (phase.load() < 12) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after non-contract semantic connect ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "hz1/out" ||
                            conn.value("to", "") != "s2/sec") continue;
                        found = true;
                        check(!conn.contains("from_min"),
                              "non-contract coercion leaves from_min unset");
                        check(!conn.contains("from_max"),
                              "non-contract coercion leaves from_max unset");
                        check(!conn.contains("to_min"),
                              "non-contract coercion leaves to_min unset");
                        check(!conn.contains("to_max"),
                              "non-contract coercion leaves to_max unset");
                        break;
                    }
                }
                check(found, "non-contract coercion connection present in inspect_graph");
            }
        }

        {
            auto d = post(client, base_url, "disconnect",
                R"({"from_addr":"hz1/out","to_addr":"s2/sec"})");
            check(d.ok, "disconnect non-contract semantic connection ok");

            auto rm_src = post(client, base_url, "remove_node",
                R"({"node_id":"hz1"})");
            check(rm_src.ok, "remove_node hz1 ok");

            auto rm_dst = post(client, base_url, "remove_node",
                R"({"node_id":"s2"})");
            check(rm_dst.ok, "remove_node s2 ok");
        }

        // Unknown tags and untagged params must remain tolerated with no implicit remap.
        std::fprintf(stderr, "\n--- semantic unknown/untagged tolerance ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"UnknownTagSourceOp","node_id":"ux1"})");
            check(add_src.ok, "add_node UnknownTagSourceOp ux1 ok");
            auto add_dst = post(client, base_url, "add_node",
                R"({"type":"UntaggedDestOp","node_id":"ud1"})");
            check(add_dst.ok, "add_node UntaggedDestOp ud1 ok");

            auto c = post(client, base_url, "connect",
                R"({"from_addr":"ux1/out","to_addr":"ud1/value","semantic_defaults":true})");
            check(c.ok, "connect unknown-tag -> untagged with semantic_defaults ok");
            if (!c.j.is_null()) {
                check(c.j.contains("inferred_remap_applied") &&
                          c.j["inferred_remap_applied"].is_boolean() &&
                          !c.j["inferred_remap_applied"].get<bool>(),
                      "unknown/untagged connection has no inferred remap");
                check(!c.j.contains("inferred_remap"),
                      "unknown/untagged connection omits inferred_remap payload");
            }
        }

        phase.store(13);
        while (phase.load() < 14) std::this_thread::sleep_for(std::chrono::milliseconds(5));

        {
            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after unknown/untagged connect ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& conns = result["connections"];
                bool found = false;
                if (conns.is_array()) {
                    for (const auto& conn : conns) {
                        if (conn.value("from", "") != "ux1/out" ||
                            conn.value("to", "") != "ud1/value") continue;
                        found = true;
                        check(!conn.contains("from_min"),
                              "unknown/untagged leaves from_min unset");
                        check(!conn.contains("from_max"),
                              "unknown/untagged leaves from_max unset");
                        check(!conn.contains("to_min"),
                              "unknown/untagged leaves to_min unset");
                        check(!conn.contains("to_max"),
                              "unknown/untagged leaves to_max unset");
                        break;
                    }
                }
                check(found, "unknown/untagged connection present in inspect_graph");
            }
        }

        {
            auto d = post(client, base_url, "disconnect",
                R"({"from_addr":"ux1/out","to_addr":"ud1/value"})");
            check(d.ok, "disconnect unknown/untagged connection ok");
            auto rm_src = post(client, base_url, "remove_node",
                R"({"node_id":"ux1"})");
            check(rm_src.ok, "remove_node ux1 ok");
            auto rm_dst = post(client, base_url, "remove_node",
                R"({"node_id":"ud1"})");
            check(rm_dst.ok, "remove_node ud1 ok");
        }

        // Phase 11: save_graph
        std::fprintf(stderr, "\n--- save_graph ---\n");
        std::string tmp_path = build_dir + "/test_cs_saved.json";
        std::string load_graph_path = build_dir + "/test_cs_loaded.json";
        {
            auto r = post(client, base_url, "save_graph",
                R"({"path":")" + tmp_path + R"("})");
            check(r.ok, "save_graph ok");
            check(std::filesystem::exists(tmp_path), "saved file exists");
        }
        {
            std::ofstream ofs(load_graph_path);
            ofs <<
                "{\n"
                "  \"nodes\": {\n"
                "    \"loaded_only\": { \"type\": \"TestOp\", \"params\": { \"scale\": 4.25 } }\n"
                "  }\n"
                "}\n";
        }

        // Phase 12: load_graph
        std::fprintf(stderr, "\n--- load_graph ---\n");
        {
            auto r = post(client, base_url, "load_graph",
                R"({"path":")" + load_graph_path + R"("})");
            check(r.ok, "load_graph ok");

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after load_graph ok");
            if (!ig.j.is_null()) {
                check(!inspect_graph_node_by_id(ig.j, "loaded_only").is_null(),
                      "load_graph switched to requested file");
                check(inspect_graph_node_by_id(ig.j, "a").is_null(),
                      "load_graph no longer shows previous graph nodes");
            }

            // Undo history must reset on file load.
            auto u = post(client, base_url, "undo");
            check(!u.ok, "undo after load_graph reports no history");

            // Baseline should still be tracked: mutate then undo returns to loaded state.
            auto sp = post(client, base_url, "set_param",
                R"({"node_id":"loaded_only","param":"scale","value":8.0})");
            check(sp.ok, "set_param after load_graph ok");
            auto u2 = post(client, base_url, "undo");
            check(u2.ok, "undo after post-load mutation ok");
            auto gp = post(client, base_url, "get_param",
                R"({"node_id":"loaded_only","param":"scale"})");
            check(gp.ok, "get_param after post-load undo ok");
            if (!gp.j.is_null()) {
                check(gp.j.contains("value") &&
                          std::fabs(gp.j["value"].get<double>() - 4.25) < 1e-4,
                      "post-load undo restored loaded baseline value");
            }
        }

        // Phase 13: error cases
        std::fprintf(stderr, "\n--- error cases ---\n");
        {
            auto r1 = post(client, base_url, "bogus_method");
            check(!r1.ok, "unknown method -> ok=false");

            auto r2 = post(client, base_url, "get_param",
                R"({"node_id":"nonexistent","param":"scale"})");
            check(!r2.ok, "unknown node -> ok=false");

            auto r3 = post(client, base_url, "set_param", "not json at all{{{");
            check(!r3.ok, "invalid JSON -> ok=false");
        }
