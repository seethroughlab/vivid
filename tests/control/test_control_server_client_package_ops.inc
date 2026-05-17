        // Phase 13b: package endpoints (MCP/API matrix coverage)
        std::fprintf(stderr, "\n--- package endpoints ---\n");
        {
            auto rl = post(client, base_url, "list_packages");
            check(rl.ok, "list_packages ok");
            if (!rl.j.is_null()) {
                auto& result = rl.j["result"];
                auto& pkgs = result["packages"];
                check(pkgs.is_array(), "list_packages returns array");
                bool found = false;
                if (pkgs.is_array()) {
                    for (const auto& p : pkgs) {
                        if (p.contains("name") && p["name"].is_string() &&
                            p["name"].get<std::string>() == "catalog-test-pkg") {
                            found = true;
                            check(p.contains("source_scope") && p["source_scope"].is_string(),
                                  "list_packages exposes source_scope");
                            if (p.contains("source_scope") && p["source_scope"].is_string())
                                check(p["source_scope"].get<std::string>() == "user",
                                      "source_scope is user");
                            break;
                        }
                    }
                }
                check(found, "list_packages contains installed test package");
            }

            auto rc = post(client, base_url, "package_catalog");
            check(rc.ok, "package_catalog ok");
            if (!rc.j.is_null()) {
                auto& pkgs = rc.j["packages"];
                check(pkgs.is_array(), "package_catalog returns packages array");
                bool found = false;
                if (pkgs.is_array()) {
                    for (const auto& p : pkgs) {
                        if (p.contains("name") && p["name"].is_string() &&
                            p["name"].get<std::string>() == "catalog-test-pkg") {
                            found = true;
                            check(p.contains("installed") && p["installed"].is_boolean() &&
                                      p["installed"].get<bool>(),
                                  "package_catalog marks installed package");
                            check(p.contains("installed_version") && p["installed_version"].is_string() &&
                                      p["installed_version"].get<std::string>() == "1.0.0",
                                  "package_catalog includes installed_version");
                            break;
                        }
                    }
                }
                check(found, "package_catalog contains catalog-test-pkg");
            }

            auto ru = post(client, base_url, "check_package_updates", R"({"core_version":"0.9.0"})");
            check(ru.ok, "check_package_updates ok");
            if (!ru.j.is_null()) {
                check(ru.j.contains("updates_available") && ru.j["updates_available"].is_number_integer() &&
                          ru.j["updates_available"].get<int>() >= 1,
                      "check_package_updates reports updates_available");
                auto& pkgs = ru.j["packages"];
                check(pkgs.is_array(), "check_package_updates returns packages array");
                if (pkgs.is_array() && pkgs.size() > 0) {
                    auto& p0 = pkgs[0];
                    check(p0.contains("classification") && p0["classification"].is_string(),
                          "update entry includes classification");
                    check(p0.contains("update_available") && p0["update_available"].is_boolean() &&
                              p0["update_available"].get<bool>(),
                          "update entry marks update_available=true");
                    check(p0.contains("compatible") && p0["compatible"].is_boolean(),
                          "update entry includes compatible flag");
                }
            }

            auto ri = post(client, base_url, "check_package_updates", R"({"core_version":"9.0.0"})");
            check(ri.ok, "check_package_updates incompatible-core ok");
            if (!ri.j.is_null()) {
                check(ri.j.contains("incompatible_updates") &&
                          ri.j["incompatible_updates"].is_number_integer() &&
                          ri.j["incompatible_updates"].get<int>() >= 1,
                      "check_package_updates reports incompatible_updates");
                auto& pkgs = ri.j["packages"];
                if (pkgs.is_array() && pkgs.size() > 0) {
                    auto& p0 = pkgs[0];
                    check(p0.contains("classification") && p0["classification"].is_string() &&
                              p0["classification"].get<std::string>() == "incompatible_update",
                          "incompatible update classification is exposed");
                    check(p0.contains("compatible") && p0["compatible"].is_boolean() &&
                              !p0["compatible"].get<bool>(),
                          "incompatible update marks compatible=false");
                }
            }
        }

        // Phase 13c: scaffold_operator destination e2e
        std::fprintf(stderr, "\n--- scaffold_operator destination ---\n");
        {
            auto rp = post(client, base_url, "scaffold_operator",
                R"({"name":"mcp_pkg_e2e","kind":"control","destination":"package:vivid-scaffold-e2e"})");
            check(rp.ok, "scaffold_operator project destination ok");
            if (!rp.ok && !rp.j.is_null()) {
                if (rp.j.contains("error") && rp.j["error"].is_string()) {
                    std::fprintf(stderr, "  INFO: scaffold_operator project error: %s\n",
                                 rp.j["error"].get<std::string>().c_str());
                }
            }
            if (!rp.j.is_null()) {
                auto& result = rp.j["result"];
                check(result.contains("destination_is_package") &&
                          result["destination_is_package"].is_boolean() &&
                          result["destination_is_package"].get<bool>(),
                      "scaffold_operator project resolved to package");
                check(result.contains("destination_package") &&
                          result["destination_package"].is_string() &&
                          result["destination_package"].get<std::string>() == "vivid-scaffold-e2e",
                      "scaffold_operator reports destination package");
                check(result.contains("destination_root") && result["destination_root"].is_string(),
                      "scaffold_operator reports destination root");
            }
            check(std::filesystem::exists(local_pkg_src + "/src/mcp_pkg_e2e.cpp"),
                  "MCP scaffold wrote operator source into linked package src/");
            {
                std::ifstream ifs(local_pkg_src + "/CMakeLists.txt");
                std::string cmake((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                check(cmake.find("mcp_pkg_e2e") != std::string::npos,
                      "MCP scaffold patched package CMake ops list");
            }

            // Remove linked project package so auto destination falls back to core with warning.
            std::error_code ec;
            std::filesystem::remove(local_pkg_link, ec);
            check(!ec, "removed linked project package for fallback test");

            auto rc = post(client, base_url, "scaffold_operator",
                R"({"name":"mcp_core_fallback","kind":"control","destination":"auto"})");
            check(rc.ok, "scaffold_operator auto fallback ok");
            if (!rc.ok && !rc.j.is_null()) {
                if (rc.j.contains("error") && rc.j["error"].is_string()) {
                    std::fprintf(stderr, "  INFO: scaffold_operator auto fallback error: %s\n",
                                 rc.j["error"].get<std::string>().c_str());
                }
            }
            if (!rc.j.is_null()) {
                auto& result = rc.j["result"];
                check(result.contains("destination_is_package") &&
                          result["destination_is_package"].is_boolean() &&
                          !result["destination_is_package"].get<bool>(),
                      "auto fallback resolved to core destination");
                check(result.contains("destination_warning") &&
                          result["destination_warning"].is_string() &&
                          result["destination_warning"].get<std::string>().size() > 0,
                      "auto fallback returns destination warning");
                check(result.contains("destination_root") &&
                          result["destination_root"].is_string() &&
                          result["destination_root"].get<std::string>() == scaffold_core_src,
                      "auto fallback destination root is configured core src");
            }
            check(std::filesystem::exists(scaffold_core_src +
                                         "/operators/control/mcp_core_fallback/mcp_core_fallback.cpp"),
                  "fallback-to-core scaffold wrote core operator source");
        }

        // Cleanup temp file
        std::filesystem::remove(tmp_path);

        // Phase 14: Variations removed in Phase 1 (HTTP variation endpoints removed).
        // set_quantize_clock still valid:
        {
            auto r8 = post(client, base_url, "set_quantize_clock", R"({"node_id":"a"})");
            check(r8.ok, "set_quantize_clock a ok");
        }

        // --- input validation: recording path traversal ---
        std::fprintf(stderr, "\n--- recording path traversal rejection ---\n");
        {
            ScopedTempDir sandbox("control_server_paths");
            const std::string valid_recording_path = sandbox.file_str("ok.mov");
            // No capture coordinator in this test harness, so start_recording routes
            // through the path-validation guard before reaching the coordinator check.
            // A traversal path must be rejected with ok:false before coordinator dispatch.
            auto r1 = post(client, base_url, "start_recording",
                R"({"path":"../../../tmp/evil.mov"})");
            check(!r1.ok, "start_recording traversal path rejected");

            auto r2 = post(client, base_url, "start_recording",
                std::string("{\"path\":\"") + valid_recording_path + "\"}");
            // No coordinator attached, so this either times out or errors — but must NOT
            // be the traversal rejection (i.e. it should not fail with the traversal guard).
            // We verify the path guard passed by checking the response is not the
            // hard-coded "invalid recording path" error.
            if (!r2.j.is_null()) {
                std::string err_str = r2.j.value("error", "");
                check(err_str != "invalid recording path", "valid path passes traversal guard");
            }
        }

        // --- add_node: old "id" field rejected, "node_id" accepted ---
        std::fprintf(stderr, "\n--- add_node field name validation ---\n");
        {
            auto r_bad = post(client, base_url, "add_node",
                R"({"type":"TestOp","id":"should_fail"})");
            check(!r_bad.ok, "add_node with old 'id' field rejected");

            auto r_good = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"validation_test_node"})");
            check(r_good.ok, "add_node with 'node_id' field accepted");
        }

        // ---------------------------------------------------------------
        // Sticky note CRUD via control server
        // ---------------------------------------------------------------
        std::fprintf(stderr, "\n--- sticky note CRUD ---\n");
        {
            auto r = post(client, base_url, "add_sticky_note",
                R"({"text":"Hello world","x":100,"y":200,"width":250,"height":150,"color":1})");
            check(r.ok, "add_sticky_note ok");
            std::string note_id;
            if (!r.j.is_null()) {
                if (r.j.contains("result") && r.j["result"].contains("id") &&
                    r.j["result"]["id"].is_string())
                    note_id = r.j["result"]["id"].get<std::string>();
                check(!note_id.empty(), "add_sticky_note returns id");
            }

            auto r2 = post(client, base_url, "add_sticky_note",
                R"({"id":"custom_note","text":"Second","x":300,"y":400})");
            check(r2.ok, "add_sticky_note with custom id ok");

            auto list = post(client, base_url, "list_sticky_notes");
            check(list.ok, "list_sticky_notes ok");
            if (!list.j.is_null()) {
                auto& result_v = list.j["result"];
                check(result_v.is_array() && result_v.size() == 2,
                      "list_sticky_notes returns 2 notes");
            }

            auto upd = post(client, base_url, "update_sticky_note",
                R"({"id":"custom_note","text":"Updated text","color":3})");
            check(upd.ok, "update_sticky_note ok");

            auto list2 = post(client, base_url, "list_sticky_notes");
            if (!list2.j.is_null()) {
                auto& result_v = list2.j["result"];
                if (result_v.is_array()) {
                    for (const auto& item : result_v) {
                        if (item.value("id", "") == "custom_note") {
                            check(item.contains("text") &&
                                      item["text"].get<std::string>() == "Updated text",
                                  "sticky note text updated");
                            check(item.contains("color") &&
                                      item["color"].get<int>() == 3,
                                  "sticky note color updated");
                        }
                    }
                }
            }

            auto rem = post(client, base_url, "remove_sticky_note",
                R"({"id":"custom_note"})");
            check(rem.ok, "remove_sticky_note ok");

            auto list3 = post(client, base_url, "list_sticky_notes");
            if (!list3.j.is_null()) {
                auto& result_v = list3.j["result"];
                check(result_v.is_array() && result_v.size() == 1,
                      "1 sticky note after remove");
            }

            auto rem_bad = post(client, base_url, "remove_sticky_note",
                R"({"id":"nonexistent"})");
            check(!rem_bad.ok, "remove_sticky_note nonexistent fails");

            // Clean up
            auto rem2 = post(client, base_url, "remove_sticky_note",
                std::string("{\"id\":\"") + note_id + "\"}");
            check(rem2.ok, "remove remaining sticky note ok");
        }

        // Final pass: package mutation safety with a live linked package node.
        std::fprintf(stderr, "\n--- live package mutation safety ---\n");
        {
            auto link = post(client, base_url, "link_package",
                std::string("{\"path\":\"") + live_pkg_src + "\"}");
            check(link.ok, "link_package vivid-live-pkg ok");
            if (!link.j.is_null()) {
                auto& result = link.j["result"];
                check(result.contains("name") && result["name"].is_string() &&
                          result["name"].get<std::string>() == "vivid-live-pkg",
                      "link_package returns vivid-live-pkg");
                check(result.contains("linked") && result["linked"].is_boolean() &&
                          result["linked"].get<bool>(),
                      "link_package marks package as linked");
            }

            auto pkg_docs = post(client, base_url, "package_operator_docs",
                R"({"name":"vivid-live-pkg"})");
            check(pkg_docs.ok, "package_operator_docs vivid-live-pkg ok");
            if (!pkg_docs.j.is_null()) {
                auto& result = pkg_docs.j["result"];
                check(result.contains("operators") && result["operators"].is_array() &&
                          result["operators"].size() == 1,
                      "package_operator_docs returns linked package operator");
                if (result.contains("operators") && result["operators"].is_array() &&
                    result["operators"].size() == 1) {
                    auto& op = result["operators"][0];
                    check(op.contains("name") && op["name"].is_string() &&
                              op["name"].get<std::string>() == "PkgLiveOp",
                          "package_operator_docs returns operator name");
                    check(op.contains("package") && op["package"].is_string() &&
                              op["package"].get<std::string>() == "vivid-live-pkg",
                          "package_operator_docs includes package name");
                    check(op.contains("has_docs") && op["has_docs"].is_boolean() &&
                              op["has_docs"].get<bool>(),
                          "package_operator_docs marks has_docs true");
                    check(op.contains("brief") && op["brief"].is_string() &&
                              op["brief"].get<std::string>() == "Linked package fixture operator.",
                          "package_operator_docs merges brief");
                    check(op.contains("outputs") && op["outputs"].is_array() &&
                              op["outputs"].size() == 1,
                          "package_operator_docs returns outputs");
                    if (op.contains("outputs") && op["outputs"].is_array() &&
                        op["outputs"].size() == 1) {
                        auto& out0 = op["outputs"][0];
                        check(out0.contains("doc") && out0["doc"].is_string() &&
                                  out0["doc"].get<std::string>() == "Live package scalar output.",
                              "package_operator_docs merges output doc");
                    }
                }
            }

            auto add_live = post(client, base_url, "add_node",
                R"({"type":"PkgLiveOp","node_id":"pkg_live"})");
            check(add_live.ok, "add_node pkg_live ok");
            phase.store(15);
            while (phase.load() < 16) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto intro_v1 = post(client, base_url, "introspect_nodes");
            check(intro_v1.ok, "introspect_nodes after add live package ok");
            if (!intro_v1.j.is_null()) {
                auto node = introspect_node_by_id(intro_v1.j, "pkg_live");
                check(!node.is_null(), "pkg_live appears in introspect_nodes");
                if (!node.is_null()) {
                    auto& health = node["health"];
                    auto& node_outputs = node["outputs"];
                    check(health.contains("missing_operator") &&
                              health["missing_operator"].is_boolean() &&
                              !health["missing_operator"].get<bool>(),
                          "pkg_live is resolved before rebuild");
                    if (node_outputs.is_array() && node_outputs.size() > 0) {
                        auto& out0 = node_outputs[0];
                        check(out0.contains("scalar") &&
                                  std::fabs(out0["scalar"].get<double>() - 3.0) < 1e-4,
                              "pkg_live output starts at 3");
                    } else {
                        check(false, "pkg_live exposes output after initial link");
                    }
                }
            }

            write_live_pkg_source(7.0f);
            auto rebuild = post(client, base_url, "rebuild_package",
                R"({"name":"vivid-live-pkg"})");
            check(rebuild.ok, "rebuild_package vivid-live-pkg ok");
            phase.store(17);
            while (phase.load() < 18) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto intro_v2 = post(client, base_url, "introspect_nodes");
            check(intro_v2.ok, "introspect_nodes after rebuild ok");
            if (!intro_v2.j.is_null()) {
                auto node = introspect_node_by_id(intro_v2.j, "pkg_live");
                check(!node.is_null(), "pkg_live remains after rebuild");
                if (!node.is_null()) {
                    auto& health = node["health"];
                    auto& node_outputs = node["outputs"];
                    check(health.contains("missing_operator") &&
                              health["missing_operator"].is_boolean() &&
                              !health["missing_operator"].get<bool>(),
                          "pkg_live stays resolved after rebuild");
                    if (node_outputs.is_array() && node_outputs.size() > 0) {
                        auto& out0 = node_outputs[0];
                        check(out0.contains("scalar") &&
                                  std::fabs(out0["scalar"].get<double>() - 7.0) < 1e-4,
                              "pkg_live output refreshes to rebuilt value");
                    } else {
                        check(false, "pkg_live exposes output after rebuild");
                    }
                }
            }

            auto unlink = post(client, base_url, "unlink_package",
                R"({"name":"vivid-live-pkg"})");
            check(unlink.ok, "unlink_package vivid-live-pkg ok");
            phase.store(19);
            while (phase.load() < 20) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto intro_missing = post(client, base_url, "introspect_nodes");
            check(intro_missing.ok, "introspect_nodes after unlink ok");
            if (!intro_missing.j.is_null()) {
                auto node = introspect_node_by_id(intro_missing.j, "pkg_live");
                check(!node.is_null(), "pkg_live remains in graph after unlink");
                if (!node.is_null()) {
                    auto& health = node["health"];
                    check(health.contains("missing_operator") &&
                              health["missing_operator"].is_boolean() &&
                              health["missing_operator"].get<bool>(),
                          "pkg_live becomes missing operator after unlink");
                }
            }

            auto list_missing = post(client, base_url, "list_nodes");
            check(list_missing.ok, "list_nodes after unlink ok");

            auto inspect_missing = post(client, base_url, "inspect_graph");
            check(inspect_missing.ok, "inspect_graph after unlink ok");
            if (!inspect_missing.j.is_null()) {
                auto node = inspect_graph_node_by_id(inspect_missing.j, "pkg_live");
                check(!node.is_null(), "pkg_live remains inspectable after unlink");
            }

            auto rl2 = post(client, base_url, "list_packages");
            check(rl2.ok, "list_packages after unlink ok");
            if (!rl2.j.is_null()) {
                auto& result = rl2.j["result"];
                auto& pkgs = result["packages"];
                bool found = false;
                if (pkgs.is_array()) {
                    for (const auto& p : pkgs) {
                        if (p.contains("name") && p["name"].is_string() &&
                            p["name"].get<std::string>() == "vivid-live-pkg") {
                            found = true;
                            break;
                        }
                    }
                }
                check(!found, "vivid-live-pkg removed from package list after unlink");
            }
        }
