        // Phase 1: inspect_graph — initial state
        std::fprintf(stderr, "\n--- inspect_graph (initial) ---\n");
        {
            auto r = post(client, base_url, "inspect_graph");
            check(r.ok, "inspect_graph ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                auto& conns = result["connections"];
                check(nodes.is_array() && nodes.size() == 2, "2 nodes");
                check(conns.is_array() && conns.size() == 1, "1 connection");
                check(result.contains("meta") && result["meta"].is_object(), "inspect_graph returns graph meta");
                if (result.contains("meta") && result["meta"].is_object()) {
                    auto& meta = result["meta"];
                    check(meta.contains("content_kind") && meta["content_kind"] == "instrument",
                          "inspect_graph meta exposes content_kind");
                    check(meta.contains("domains") && meta["domains"].is_array() &&
                              meta["domains"].size() == 2,
                          "inspect_graph meta exposes canonical domains");
                    check(meta.contains("preview_controls") && meta["preview_controls"].is_array() &&
                              meta["preview_controls"].size() == 2,
                          "inspect_graph meta exposes raw preview_controls");
                }

                // Check that params have values
                auto& first_node = nodes[0];
                auto& params = first_node["params"];
                check(params.is_array() && params.size() > 0, "params have values");
                if (params.is_array() && params.size() > 0) {
                    auto& p0 = params[0];
                    check(p0.contains("type") && p0["type"].is_string(), "inspect_graph params expose type");
                    check(p0.contains("semantic_tag") && p0["semantic_tag"].is_string() &&
                              p0["semantic_tag"].get<std::string>() == "frequency_hz",
                          "inspect_graph params expose semantic_tag");
                    check(p0.contains("semantic_shape") && p0["semantic_shape"].is_string() &&
                              p0["semantic_shape"].get<std::string>() == "scalar",
                          "inspect_graph params expose semantic_shape");
                    check(p0.contains("semantic_unit") && p0["semantic_unit"].is_string() &&
                              p0["semantic_unit"].get<std::string>() == "Hz",
                          "inspect_graph params expose semantic_unit");
                    check(p0.contains("semantic_intent") && p0["semantic_intent"].is_string() &&
                              p0["semantic_intent"].get<std::string>() == "test_scale",
                          "inspect_graph params expose semantic_intent");
                    check(p0.contains("widget_id") && p0["widget_id"].is_string() &&
                              p0["widget_id"].get<std::string>() == "tests.vivid.scale_widget",
                          "inspect_graph params expose widget_id");
                    check(p0.contains("widget_span") && p0["widget_span"].is_number_unsigned() &&
                              p0["widget_span"].get<uint32_t>() == 1,
                          "inspect_graph params expose widget_span");
                }
            }
        }

        // Phase 1b: introspect_nodes — per-node perception payload
        std::fprintf(stderr, "\n--- introspect_nodes ---\n");
        {
            auto r = post(client, base_url, "introspect_nodes");
            check(r.ok, "introspect_nodes ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "introspect_nodes schema_version=1");

                auto& result = r.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array(), "introspect_nodes returns nodes array");
                check(nodes.is_array() && nodes.size() == 2, "introspect_nodes returns 2 nodes");

                auto& first_node = nodes[0];
                check(first_node.contains("node_id") && first_node["node_id"].is_string(), "introspection node has node_id");
                check(first_node.contains("type") && first_node["type"].is_string(), "introspection node has type");
                check(first_node.contains("kind") && first_node["kind"].is_string(), "introspection node has kind");
                check(first_node.contains("health") && first_node["health"].is_object(), "introspection node has health object");
                check(first_node.contains("params") && first_node["params"].is_object(), "introspection node has params object");
                check(first_node.contains("param_meta") && first_node["param_meta"].is_array(), "introspection node has param_meta array");
                if (first_node.contains("param_meta") && first_node["param_meta"].is_array() &&
                    first_node["param_meta"].size() > 0) {
                    auto& pm0 = first_node["param_meta"][0];
                    check(pm0.contains("semantic_tag") && pm0["semantic_tag"].is_string() &&
                              pm0["semantic_tag"].get<std::string>() == "frequency_hz",
                          "introspection param_meta exposes semantic_tag");
                    check(pm0.contains("semantic_shape") && pm0["semantic_shape"].is_string() &&
                              pm0["semantic_shape"].get<std::string>() == "scalar",
                          "introspection param_meta exposes semantic_shape");
                    check(pm0.contains("semantic_unit") && pm0["semantic_unit"].is_string() &&
                              pm0["semantic_unit"].get<std::string>() == "Hz",
                          "introspection param_meta exposes semantic_unit");
                    check(pm0.contains("semantic_intent") && pm0["semantic_intent"].is_string() &&
                              pm0["semantic_intent"].get<std::string>() == "test_scale",
                          "introspection param_meta exposes semantic_intent");
                    check(pm0.contains("widget_id") && pm0["widget_id"].is_string() &&
                              pm0["widget_id"].get<std::string>() == "tests.vivid.scale_widget",
                          "introspection param_meta exposes widget_id");
                    check(pm0.contains("widget_span") && pm0["widget_span"].is_number_unsigned() &&
                              pm0["widget_span"].get<uint32_t>() == 1,
                          "introspection param_meta exposes widget_span");
                }
                check(first_node.contains("inputs") && first_node["inputs"].is_array(), "introspection node has inputs array");
                check(first_node.contains("outputs") && first_node["outputs"].is_array(), "introspection node has outputs array");
                check(first_node.contains("env_metrics") && first_node["env_metrics"].is_object(),
                      "introspection node has env_metrics object");
                check(first_node.contains("incoming_wires") && first_node["incoming_wires"].is_number_integer(),
                      "introspection node has incoming_wires");
                check(first_node.contains("outgoing_wires") && first_node["outgoing_wires"].is_number_integer(),
                      "introspection node has outgoing_wires");
            }
        }

        // Phase 1c: run_diagnostics — graph-level perception findings
        std::fprintf(stderr, "\n--- run_diagnostics ---\n");
        {
            auto r = post(client, base_url, "run_diagnostics");
            check(r.ok, "run_diagnostics ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 2,
                      "run_diagnostics schema_version=2");

                auto& result = r.j["result"];
                auto& summary = result["summary"];
                auto& findings = result["findings"];
                auto& hints = result["hints"];

                check(summary.is_object(), "run_diagnostics has summary");
                check(findings.is_array(), "run_diagnostics has findings array");
                check(hints.is_array(), "run_diagnostics has hints array");

                if (summary.is_object()) {
                    check(summary.contains("critical") && summary["critical"].is_number_integer(), "summary has critical count");
                    check(summary.contains("warning") && summary["warning"].is_number_integer(), "summary has warning count");
                    check(summary.contains("info") && summary["info"].is_number_integer(), "summary has info count");
                    if (summary.contains("critical") && summary.contains("warning") && summary.contains("info") &&
                        summary["critical"].is_number_integer() && summary["warning"].is_number_integer() &&
                        summary["info"].is_number_integer()) {
                        check(summary["critical"].get<int>() == 0 &&
                              summary["warning"].get<int>() == 0 &&
                              summary["info"].get<int>() == 0,
                              "healthy fixture graph yields minimal diagnostics");
                    }
                }

                // Health telemetry
                check(r.j.contains("health") && r.j["health"].is_object(), "run_diagnostics has health");
                if (r.j.contains("health") && r.j["health"].is_object()) {
                    auto& health = r.j["health"];
                    check(health.contains("audio") && health["audio"].is_object(), "health has audio");
                    check(health.contains("graph") && health["graph"].is_object(), "health has graph");
                    check(health.contains("gpu") && health["gpu"].is_object(), "health has gpu");

                    if (health["audio"].is_object()) {
                        auto& ah = health["audio"];
                        check(ah.contains("xruns") && ah["xruns"].is_number_integer(), "audio health has xruns");
                        check(ah.contains("load") && ah["load"].is_number(), "audio health has load");
                        check(ah.contains("running") && ah["running"].is_boolean(), "audio health has running");
                        check(ah.contains("top_nodes") && ah["top_nodes"].is_array(),
                              "audio health has top_nodes");
                        check(ah.contains("top_lane_state_nodes") && ah["top_lane_state_nodes"].is_array(),
                              "audio health has top_lane_state_nodes");
                        if (ah.contains("top_nodes") && ah["top_nodes"].is_array() &&
                            ah["top_nodes"].size() > 0) {
                            const auto& n0 = ah["top_nodes"][0];
                            check(n0.contains("node_id") && n0["node_id"].is_string(),
                                  "top_nodes entry has node_id");
                            check(n0.contains("ema_block_us") && n0["ema_block_us"].is_number_integer(),
                                  "top_nodes entry has ema_block_us");
                        }
                    }
                    if (health["graph"].is_object()) {
                        auto& gh = health["graph"];
                        check(gh.contains("compiled_nodes") && gh["compiled_nodes"].is_number_integer(), "graph health has compiled_nodes");
                        check(gh.contains("frame_nodes") && gh["frame_nodes"].is_number_integer(), "graph health has frame_nodes");
                        check(gh.contains("audio_nodes") && gh["audio_nodes"].is_number_integer(), "graph health has audio_nodes");
                    }
                    if (health["gpu"].is_object()) {
                        check(health["gpu"].contains("shader_errors") && health["gpu"]["shader_errors"].is_number_integer(),
                              "gpu health has shader_errors");
                        check(health["gpu"].contains("device_lost") && health["gpu"]["device_lost"].is_boolean(),
                              "gpu health has device_lost (Phase 3 addition)");
                    }
                    // Phase 3 additions: severity rollup + findings + new fields.
                    check(health.contains("severity") && health["severity"].is_string(),
                          "health has severity (Phase 3)");
                    check(health.contains("findings") && health["findings"].is_array(),
                          "health has findings (Phase 3)");
                    if (health["audio"].is_object()) {
                        check(health["audio"].contains("lane_overflow_count") &&
                              health["audio"]["lane_overflow_count"].is_number_integer(),
                              "audio health has lane_overflow_count (Phase 3)");
                    }
                }
            }
        }

        // Phase 4: get_runtime_health endpoint — same health body, no result/findings extras.
        std::fprintf(stderr, "\n--- get_runtime_health ---\n");
        {
            auto r = post(client, base_url, "get_runtime_health");
            check(r.ok, "get_runtime_health ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 2,
                      "get_runtime_health schema_version=2");
                check(r.j.contains("health") && r.j["health"].is_object(),
                      "get_runtime_health has health");
                if (r.j["health"].is_object()) {
                    auto& health = r.j["health"];
                    check(health.contains("severity") && health["severity"].is_string(),
                          "health has severity string");
                    check(health.contains("findings") && health["findings"].is_array(),
                          "health has findings array");
                    check(health.contains("audio") && health["audio"].is_object(),
                          "health has audio sub-block");
                    check(health.contains("graph") && health["graph"].is_object(),
                          "health has graph sub-block");
                    check(health.contains("gpu") && health["gpu"].is_object(),
                          "health has gpu sub-block");
                }
                // Endpoint is read-only — no `result` field.
                check(!r.j.contains("result"),
                      "get_runtime_health envelope omits diagnostics-only result block");
            }
        }

        // Deterministic ordering regression: repeated diagnostics return stable finding id order.
        {
            auto r1 = post(client, base_url, "run_diagnostics");
            auto r2 = post(client, base_url, "run_diagnostics");
            check(r1.ok && r2.ok, "run_diagnostics repeat calls ok");
            std::string ids1, ids2;
            if (!r1.j.is_null()) {
                auto& result = r1.j["result"];
                auto& findings = result["findings"];
                if (findings.is_array()) {
                    for (const auto& f : findings) {
                        if (!ids1.empty()) ids1 += ",";
                        ids1 += f.value("id", "");
                        ids1 += "@";
                        ids1 += f.value("node_id", "");
                    }
                }
            }
            if (!r2.j.is_null()) {
                auto& result = r2.j["result"];
                auto& findings = result["findings"];
                if (findings.is_array()) {
                    for (const auto& f : findings) {
                        if (!ids2.empty()) ids2 += ",";
                        ids2 += f.value("id", "");
                        ids2 += "@";
                        ids2 += f.value("node_id", "");
                    }
                }
            }
            check(ids1 == ids2, "run_diagnostics findings order deterministic");
            std::string audio_ids1, audio_ids2;
            if (!r1.j.is_null()) {
                auto audio = r1.j["health"]["audio"];
                auto top = audio.value("top_nodes", nlohmann::json::array());
                for (const auto& n : top) {
                    if (!audio_ids1.empty()) audio_ids1 += ",";
                    audio_ids1 += n.value("node_id", "");
                }
            }
            if (!r2.j.is_null()) {
                auto audio = r2.j["health"]["audio"];
                auto top = audio.value("top_nodes", nlohmann::json::array());
                for (const auto& n : top) {
                    if (!audio_ids2.empty()) audio_ids2 += ",";
                    audio_ids2 += n.value("node_id", "");
                }
            }
            check(audio_ids1 == audio_ids2, "run_diagnostics top_nodes order deterministic");
        }
        // Broken fixture regression: intentionally disconnected node should emit expected warning.
        {
            auto add_missing = post(client, base_url, "add_node",
                R"({"type":"TestOp","node_id":"missing_fixture"})");
            check(add_missing.ok, "add_node broken fixture ok");
            phase.store(5);
            while (phase.load() < 6) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto r = post(client, base_url, "run_diagnostics");
            check(r.ok, "run_diagnostics on broken fixture ok");
            bool found_missing = false;
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& summary = result["summary"];
                auto& findings = result["findings"];
                if (summary.is_object()) {
                    check(summary.contains("warning") && summary["warning"].is_number_integer() &&
                              summary["warning"].get<int>() >= 1,
                          "broken fixture emits warning diagnostics");
                }
                if (findings.is_array()) {
                    for (const auto& f : findings) {
                        if (f.value("id", "") == "isolated_node" &&
                            f.value("node_id", "") == "missing_fixture") {
                            found_missing = true;
                            break;
                        }
                    }
                }
            }
            check(found_missing, "broken fixture has isolated_node finding for missing_fixture");

            auto remove_missing = post(client, base_url, "remove_node",
                R"({"node_id":"missing_fixture"})");
            check(remove_missing.ok, "remove_node missing fixture ok");
            phase.store(7);
            while (phase.load() < 8) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after broken fixture cleanup ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& nodes = result["nodes"];
                check(nodes.is_array() && nodes.size() == 2,
                      "broken fixture cleanup restored 2-node graph");
            }
        }

        // Phase 1d: validate_checks / run_checks
        std::fprintf(stderr, "\n--- checks ---\n");
        {
            auto r = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"node_count_is_two","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"no_missing_ops","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type"}]})");
            check(r.ok, "validate_checks ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "validate_checks schema_version=1");
                auto& result = r.j["result"];
                check(result.contains("valid") && result["valid"].is_boolean() && result["valid"].get<bool>(),
                      "validate_checks valid=true");
                check(result.contains("error_count") && result["error_count"].is_number_integer() &&
                          result["error_count"].get<int>() == 0,
                      "validate_checks error_count=0");
            }
        }
        {
            auto r = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"dup","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"dup","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(r.ok, "validate_checks duplicate-id request ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result.contains("valid") && result["valid"].is_boolean() && !result["valid"].get<bool>(),
                      "validate_checks duplicate-id valid=false");
                check(result.contains("error_count") && result["error_count"].is_number_integer() &&
                          result["error_count"].get<int>() > 0,
                      "validate_checks duplicate-id has error_count>0");
            }
            auto rbad = post(client, base_url, "validate_checks", R"({"foo":[]})");
            check(!rbad.ok, "validate_checks missing checks array -> ok=false");
        }
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"node_count_is_two","type":"state_check","path":"graph.node_count","op":"==","value":2,"severity":"critical"},{"id":"no_missing_ops","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type","severity":"critical"}]})");
            check(r.ok, "run_checks ok");
            if (!r.j.is_null()) {
                check(r.j.contains("schema_version") && r.j["schema_version"].is_number_integer() &&
                          r.j["schema_version"].get<int>() == 1,
                      "run_checks schema_version=1");
                auto& result = r.j["result"];
                check(result.contains("all_passed") && result["all_passed"].is_boolean() &&
                          result["all_passed"].get<bool>(),
                      "run_checks all_passed=true");
                check(result.contains("all_critical_passed") && result["all_critical_passed"].is_boolean() &&
                          result["all_critical_passed"].get<bool>(),
                      "run_checks all_critical_passed=true");
                check(result.contains("summary") && result["summary"].is_object(), "run_checks has summary");
                check(result.contains("results") && result["results"].is_array() &&
                          result["results"].size() == 2,
                      "run_checks returns 2 results");
            }
        }
        // Deterministic check result ordering regression: results sorted by id regardless of input order.
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"z_second","type":"state_check","path":"graph.node_count","op":"==","value":2},{"id":"a_first","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(r.ok, "run_checks deterministic-order request ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& rows = result["results"];
                check(rows.is_array() && rows.size() == 2,
                      "run_checks deterministic-order returns 2 results");
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0].value("id", "") == "a_first",
                          "run_checks results sorted id[0]=a_first");
                    check(rows[1].value("id", "") == "z_second",
                          "run_checks results sorted id[1]=z_second");
                }
            }
        }

        // Perception API regression: read-only endpoints must not mutate graph state.
        {
            auto gp_before = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(gp_before.ok, "pre-mutation baseline get_param ok");
            double before = 0.0;
            if (!gp_before.j.is_null()) {
                if (gp_before.j.contains("value") && gp_before.j["value"].is_number())
                    before = gp_before.j["value"].get<double>();
            }

            auto i = post(client, base_url, "introspect_nodes");
            auto d = post(client, base_url, "run_diagnostics");
            auto vc = post(client, base_url, "validate_checks",
                R"({"checks":[{"id":"baseline","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            auto rc = post(client, base_url, "run_checks",
                R"({"checks":[{"id":"baseline","type":"state_check","path":"graph.node_count","op":"==","value":2}]})");
            check(i.ok && d.ok && vc.ok && rc.ok, "perception endpoints all return ok");

            auto gp_after = post(client, base_url, "get_param",
                R"({"node_id":"a","param":"scale"})");
            check(gp_after.ok, "post-perception get_param ok");
            if (!gp_after.j.is_null()) {
                check(gp_after.j.contains("value") && gp_after.j["value"].is_number() &&
                          std::fabs(gp_after.j["value"].get<double>() - before) < 1e-6,
                      "perception endpoints do not mutate parameter state");
            }

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph after perception endpoints ok");
            if (!ig.j.is_null()) {
                auto& result = ig.j["result"];
                auto& nodes = result["nodes"];
                auto& conns = result["connections"];
                check(nodes.is_array() && nodes.size() == 2,
                      "node count unchanged after perception endpoints");
                check(conns.is_array() && conns.size() == 1,
                      "connection count unchanged after perception endpoints");
            }
        }

        // get_graph_load_diagnostics
        std::fprintf(stderr, "\n--- get_graph_load_diagnostics ---\n");
        {
            auto r = post(client, base_url, "get_graph_load_diagnostics");
            check(r.ok, "get_graph_load_diagnostics ok");
            if (!r.j.is_null()) {
                check(r.j.contains("result") && r.j["result"].is_object(),
                      "get_graph_load_diagnostics has result");
                auto& result = r.j["result"];
                check(result.contains("graph_load_diagnostics") && result["graph_load_diagnostics"].is_array(),
                      "result has graph_load_diagnostics array");
                check(result["graph_load_diagnostics"].size() == 0,
                      "healthy graph has empty load diagnostics");
            }
        }

        // Extended checks: comparison operators
        std::fprintf(stderr, "\n--- checks: comparison operators ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"neq","type":"state_check","path":"graph.node_count","op":"!=","value":99},
                    {"id":"gt","type":"state_check","path":"graph.node_count","op":">","value":1},
                    {"id":"gte","type":"state_check","path":"graph.node_count","op":">=","value":2},
                    {"id":"lt","type":"state_check","path":"graph.node_count","op":"<","value":99},
                    {"id":"lte","type":"state_check","path":"graph.node_count","op":"<=","value":2}
                ]})");
            check(r.ok, "run_checks comparison ops ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["all_passed"].get<bool>(), "all comparison ops pass");
                check(result["results"].size() == 5, "5 comparison results");
            }
        }

        // Extended checks: between operator
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"a_between_pass","type":"state_check","path":"graph.node_count","op":"between","value":[1,5]},
                    {"id":"b_between_fail","type":"state_check","path":"graph.node_count","op":"between","value":[10,20]}
                ]})");
            check(r.ok, "run_checks between ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(!result["all_passed"].get<bool>(), "between: not all passed (one should fail)");
                auto& rows = result["results"];
                // Results sorted by id: a_between_pass, b_between_fail
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0]["passed"].get<bool>(), "between [1,5] passes for node_count=2");
                    check(!rows[1]["passed"].get<bool>(), "between [10,20] fails for node_count=2");
                }
            }
        }

        // Extended checks: exists / not_exists
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"a_exists_ok","type":"state_check","path":"nodes.a.kind","op":"exists"},
                    {"id":"b_not_exists_ok","type":"state_check","path":"nodes.zzz.kind","op":"not_exists"},
                    {"id":"c_exists_fail","type":"state_check","path":"nodes.zzz.kind","op":"exists"}
                ]})");
            check(r.ok, "run_checks exists/not_exists ok");
            if (!r.j.is_null()) {
                auto& rows = r.j["result"]["results"];
                // Results sorted by id: a_exists_ok, b_not_exists_ok, c_exists_fail
                if (rows.is_array() && rows.size() == 3) {
                    check(rows[0]["passed"].get<bool>(), "exists on nodes.a.kind passes");
                    check(rows[1]["passed"].get<bool>(), "not_exists on nodes.zzz.kind passes");
                    check(!rows[2]["passed"].get<bool>(), "exists on nodes.zzz.kind fails");
                }
            }
        }

        // Extended checks: state paths
        std::fprintf(stderr, "\n--- checks: state paths ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"kind","type":"state_check","path":"nodes.a.kind","op":"==","value":"control"},
                    {"id":"health_err","type":"state_check","path":"nodes.a.health.errored","op":"==","value":false},
                    {"id":"health_miss","type":"state_check","path":"nodes.a.health.missing_operator","op":"==","value":false},
                    {"id":"param_val","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.0},
                    {"id":"output_val","type":"state_check","path":"nodes.a.outputs.out.scalar","op":"==","value":6.0},
                    {"id":"a_out_wires","type":"state_check","path":"nodes.a.outgoing_wires","op":"==","value":1},
                    {"id":"b_in_wires","type":"state_check","path":"nodes.b.incoming_wires","op":"==","value":1}
                ]})");
            check(r.ok, "run_checks state paths ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["all_passed"].get<bool>(), "all state path checks pass");
                check(result["results"].size() == 7, "7 state path results");
            }
        }

        // Extended checks: tolerance
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"a_tol_pass","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.01,"tolerance":0.1},
                    {"id":"b_tol_fail","type":"state_check","path":"nodes.a.params.scale","op":"==","value":4.0,"tolerance":0.1}
                ]})");
            check(r.ok, "run_checks tolerance ok");
            if (!r.j.is_null()) {
                auto& rows = r.j["result"]["results"];
                // Results sorted by id: a_tol_pass, b_tol_fail
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0]["passed"].get<bool>(), "tolerance 0.1: 3.0 == 3.01 passes");
                    check(!rows[1]["passed"].get<bool>(), "tolerance 0.1: 3.0 == 4.0 fails");
                }
            }
        }

        // Extended checks: when guards
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"guard_pass","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.0,
                     "when":{"path":"nodes.a.kind","op":"==","value":"control"}},
                    {"id":"guard_skip","type":"state_check","path":"nodes.a.params.scale","op":"==","value":3.0,
                     "when":{"path":"nodes.a.kind","op":"==","value":"audio"}}
                ]})");
            check(r.ok, "run_checks when guards ok");
            if (!r.j.is_null()) {
                auto& rows = r.j["result"]["results"];
                if (rows.is_array() && rows.size() == 2) {
                    check(rows[0]["passed"].get<bool>(), "guard met: check passes");
                    check(!rows[0].value("skipped", true), "guard met: check not skipped");
                    check(rows[1].value("skipped", false), "guard not met: check skipped");
                }
            }
        }

        // Extended checks: failing checks with severity reporting
        std::fprintf(stderr, "\n--- checks: failure reporting ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"crit_fail","type":"state_check","path":"graph.node_count","op":"==","value":999,"severity":"critical","message":"custom failure msg"},
                    {"id":"warn_fail","type":"state_check","path":"graph.node_count","op":"==","value":999,"severity":"warning"}
                ]})");
            check(r.ok, "run_checks failure reporting ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(!result["all_passed"].get<bool>(), "all_passed=false when checks fail");
                check(!result["all_critical_passed"].get<bool>(), "all_critical_passed=false when critical fails");
                auto& summary = result["summary"];
                check(summary["critical_failed"].get<int>() == 1, "critical_failed=1");
                check(summary["warning_failed"].get<int>() == 1, "warning_failed=1");
                auto& rows = result["results"];
                if (rows.is_array() && rows.size() == 2) {
                    check(!rows[0]["passed"].get<bool>(), "critical check reports passed=false");
                    check(rows[0]["severity"].get<std::string>() == "critical", "result has severity=critical");
                    check(rows[0]["message"].get<std::string>() == "custom failure msg", "result has custom message");
                }
            }
        }
        // Warning-only failure preserves all_critical_passed
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"warn_only","type":"state_check","path":"graph.node_count","op":"==","value":999,"severity":"warning"}
                ]})");
            check(r.ok, "run_checks warning-only failure ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(!result["all_passed"].get<bool>(), "all_passed=false for warning failure");
                check(result["all_critical_passed"].get<bool>(), "all_critical_passed=true when only warning fails");
            }
        }

        // Extended checks: diagnostic_check finding_present
        // (missing_fixture was removed, so use finding_absent for isolated_node on current graph)
        // We'll test count_by_severity ops on the healthy graph
        std::fprintf(stderr, "\n--- checks: diagnostic_check ops ---\n");
        {
            auto r = post(client, base_url, "run_checks",
                R"({"checks":[
                    {"id":"count_eq_zero","type":"diagnostic_check","op":"count_by_severity_eq","check_severity":"critical","value":0},
                    {"id":"count_lte","type":"diagnostic_check","op":"count_by_severity_lte","check_severity":"warning","value":100},
                    {"id":"count_gte","type":"diagnostic_check","op":"count_by_severity_gte","check_severity":"warning","value":0},
                    {"id":"finding_absent_ok","type":"diagnostic_check","op":"finding_absent","finding_id":"missing_operator_type"}
                ]})");
            check(r.ok, "run_checks diagnostic ops ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result["all_passed"].get<bool>(), "all diagnostic checks pass on healthy graph");
            }
        }

        // Phase 2: list_types
        std::fprintf(stderr, "\n--- list_types ---\n");
        {
            auto r = post(client, base_url, "list_types");
            check(r.ok, "list_types ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                auto& types = result["types"];
                check(types.is_array() && types.size() > 0, "has types");
                if (types.is_array()) {
                    json t0;
                    json ms_type;
                    json sec_type;
                    json unknown_type;
                    json untagged_type;
                    json custom_type;
                    for (const auto& t : types) {
                        if (!t.contains("name") || !t["name"].is_string()) continue;
                        std::string tn = t["name"].get<std::string>();
                        if (tn == "TestOp") {
                            t0 = t;
                        } else if (tn == "MsSourceOp") {
                            ms_type = t;
                        } else if (tn == "SecDestOp") {
                            sec_type = t;
                        } else if (tn == "UnknownTagSourceOp") {
                            unknown_type = t;
                        } else if (tn == "UntaggedDestOp") {
                            untagged_type = t;
                        } else if (tn == "ExportCustomPortOp") {
                            custom_type = t;
                        }
                    }
                    check(!t0.is_null(), "contains TestOp");
                    check(t0.contains("brief") && t0["brief"].is_string() &&
                              t0["brief"].get<std::string>() ==
                                  "Fixture operator docs for MCP/control-server tests.",
                          "list_types merges doc brief");
                    check(t0.contains("has_docs") && t0["has_docs"].is_boolean() &&
                              t0["has_docs"].get<bool>(),
                          "list_types marks documented operator");
                    check(t0.contains("operator_family") && t0["operator_family"].is_string() &&
                              t0["operator_family"].get<std::string>() == "test-fixture",
                          "list_types exposes operator_family");
                    check(t0.contains("multiplicity_behavior_help") && t0["multiplicity_behavior_help"].is_string() &&
                              !t0["multiplicity_behavior_help"].get<std::string>().empty(),
                          "list_types exposes multiplicity_behavior_help");
                    check(!t0.contains("params") && !t0.contains("outputs"),
                          "list_types stays compact and omits rich descriptor arrays");
                    check(!ms_type.is_null(), "contains MsSourceOp");
                    check(!sec_type.is_null(), "contains SecDestOp");
                    check(!unknown_type.is_null(), "contains UnknownTagSourceOp");
                    check(!untagged_type.is_null(), "contains UntaggedDestOp");
                    check(!custom_type.is_null(), "contains ExportCustomPortOp");
                }
            }
        }

        // Phase 2a: operator_docs
        std::fprintf(stderr, "\n--- operator_docs ---\n");
        {
            auto r = post(client, base_url, "operator_docs",
                R"({"name":"TestOp"})");
            check(r.ok, "operator_docs TestOp ok");
            if (!r.j.is_null()) {
                auto& op = r.j["result"];
                check(op.contains("has_docs") && op["has_docs"].is_boolean() &&
                          op["has_docs"].get<bool>(),
                      "operator_docs reports docs present");
                check(op.contains("brief") && op["brief"].is_string() &&
                          op["brief"].get<std::string>() ==
                              "Fixture operator docs for MCP/control-server tests.",
                      "operator_docs merges brief");
                check(op.contains("body") && op["body"].is_string() &&
                          op["body"].get<std::string>() == "Detailed body for the TestOp fixture.",
                      "operator_docs merges body");
                check(op.contains("source_path") && op["source_path"].is_string() &&
                          op["source_path"].get<std::string>() == "operators/control/test_op/test_op.cpp",
                      "operator_docs merges source_path");
                check(op.contains("multiplicity_behavior_help") && op["multiplicity_behavior_help"].is_string() &&
                          !op["multiplicity_behavior_help"].get<std::string>().empty(),
                      "operator_docs includes multiplicity_behavior_help");
                auto& params_obj = op["params"];
                check(params_obj.is_array() && params_obj.size() == 1, "operator_docs returns params");
                if (params_obj.is_array() && params_obj.size() == 1) {
                    auto& p0 = params_obj[0];
                    check(p0.contains("doc") && p0["doc"].is_string() &&
                              p0["doc"].get<std::string>() == "Scale multiplier for the output.",
                          "operator_docs merges param doc");
                    check(p0.contains("widget_id") && p0["widget_id"].is_string() &&
                              p0["widget_id"].get<std::string>() == "tests.vivid.scale_widget",
                          "operator_docs exposes param widget id");
                    check(p0.contains("widget_span") && p0["widget_span"].is_number_unsigned() &&
                              p0["widget_span"].get<uint32_t>() == 1,
                          "operator_docs exposes param widget span");
                }
                auto& outputs_obj = op["outputs"];
                check(outputs_obj.is_array() && outputs_obj.size() == 1, "operator_docs returns outputs");
                if (outputs_obj.is_array() && outputs_obj.size() == 1) {
                    auto& p0 = outputs_obj[0];
                    check(p0.contains("doc") && p0["doc"].is_string() &&
                              p0["doc"].get<std::string>() == "Scaled scalar output.",
                          "operator_docs merges output doc");
                    check(p0.contains("semantic_shape") && p0["semantic_shape"].is_string() &&
                              p0["semantic_shape"].get<std::string>() == "scalar",
                          "operator_docs preserves port semantic_shape");
                }
            }

            auto fallback = post(client, base_url, "operator_docs",
                R"({"name":"ExportCustomPortOp"})");
            check(fallback.ok, "operator_docs descriptor-only fallback ok");
            if (!fallback.j.is_null()) {
                auto& op = fallback.j["result"];
                check(op.contains("has_docs") && op["has_docs"].is_boolean() &&
                          !op["has_docs"].get<bool>(),
                      "operator_docs descriptor-only fallback marks has_docs false");
                check(op.contains("outputs") && op["outputs"].is_array() &&
                          op["outputs"].size() == 1,
                      "operator_docs descriptor-only fallback still returns outputs");
            }
        }

        // Phase 2b: get_registry_diagnostics
        std::fprintf(stderr, "\n--- get_registry_diagnostics ---\n");
        {
            auto r = post(client, base_url, "get_registry_diagnostics");
            check(r.ok, "get_registry_diagnostics ok");
            if (!r.j.is_null()) {
                auto& result = r.j["result"];
                check(result.contains("schema_version") && result["schema_version"].is_number_integer() &&
                          result["schema_version"].get<int>() == 1,
                      "registry diagnostics schema_version=1");
                auto& custom_types = result["custom_port_types"];
                auto& abi_mismatches = result["abi_mismatch_diagnostics"];
                auto& loader_failures = result["loader_failure_diagnostics"];
                check(custom_types.is_array() && custom_types.size() > 0,
                      "registry diagnostics exposes custom_port_types");
                check(abi_mismatches.is_array() && abi_mismatches.size() > 0,
                      "registry diagnostics exposes ABI mismatch diagnostics");
                check(loader_failures.is_array() && loader_failures.size() > 0,
                      "registry diagnostics exposes loader failure diagnostics");
                if (custom_types.is_array()) {
                    bool found_test_custom_ref = false;
                    for (const auto& item : custom_types) {
                        if (!item.contains("stable_type_id") || !item["stable_type_id"].is_string()) continue;
                        if (item["stable_type_id"].get<std::string>() !=
                                "seethroughlab.vivid.test_custom_ref") continue;
                        found_test_custom_ref = true;
                        check(item.contains("transport") && item["transport"].is_string() &&
                                  item["transport"].get<std::string>() == "custom_ref",
                              "registry diagnostics preserves custom port transport");
                        check(item.contains("audio_safe") && item["audio_safe"].is_boolean(),
                              "registry diagnostics preserves audio_safe");
                        break;
                    }
                    check(found_test_custom_ref, "registry diagnostics includes TestCustomRef");
                }
                if (abi_mismatches.is_array() && abi_mismatches.size() > 0) {
                    auto& first = abi_mismatches[0];
                    check(first.contains("plugin_abi") && first["plugin_abi"].is_number_unsigned(),
                          "ABI diagnostic includes plugin_abi");
                    check(first.contains("runtime_abi") && first["runtime_abi"].is_number_unsigned() &&
                              first["runtime_abi"].get<uint64_t>() == 999,
                          "ABI diagnostic includes mocked runtime ABI");
                }
                if (loader_failures.is_array() && loader_failures.size() > 0) {
                    bool found_bad_custom = false;
                    for (const auto& item : loader_failures) {
                        if (!item.contains("plugin_name") || !item["plugin_name"].is_string()) continue;
                        if (item["plugin_name"].get<std::string>() != "test_op_bad_custom_type.dylib")
                            continue;
                        found_bad_custom = true;
                        check(item.contains("code") && item["code"].is_string() &&
                                  item["code"].get<std::string>() == "custom_type_registration_failed",
                              "loader failure diagnostic exposes structured code");
                        check(item.contains("message") && item["message"].is_string() &&
                                  item["message"].get<std::string>().find("register") != std::string::npos,
                              "loader failure diagnostic exposes useful message");
                    }
                    check(found_bad_custom, "registry diagnostics include bad custom-type loader failure");
                }
            }
        }

        // operator_map
        std::fprintf(stderr, "\n--- operator_map ---\n");
        {
            auto r = post(client, base_url, "operator_map");
            check(r.ok, "operator_map ok");
            if (!r.j.is_null()) {
                check(r.j.contains("result") && r.j["result"].is_array(),
                      "operator_map result is array");
                auto& result = r.j["result"];
                bool found_test_op = false;
                bool found_abi_mismatch = false;
                for (const auto& entry : result) {
                    check(entry.contains("type") && entry["type"].is_string(),
                          "operator_map entry has type");
                    check(entry.contains("status") && entry["status"].is_string(),
                          "operator_map entry has status");
                    if (entry["type"].get<std::string>() == "TestOp" &&
                        entry["status"].get<std::string>() == "loaded")
                        found_test_op = true;
                    if (entry.contains("status") && entry["status"].is_string() &&
                        entry["status"].get<std::string>() == "abi_mismatch")
                        found_abi_mismatch = true;
                }
                check(found_test_op, "operator_map contains TestOp with status=loaded");
                check(found_abi_mismatch, "operator_map contains abi_mismatch entry");
            }
        }

        // Module query-surface parity
        std::fprintf(stderr, "\n--- module query surfaces ---\n");
        {
            auto add_src = post(client, base_url, "add_node",
                R"({"type":"StringSourceOp","node_id":"module_src"})");
            check(add_src.ok, "add_node module_src ok");

            auto add_mod = post(client, base_url, "add_node",
                R"({"type":"QuerySurfaceModule","node_id":"mod1"})");
            check(add_mod.ok, "add_node QuerySurfaceModule mod1 ok");

            auto connect_echo = post(client, base_url, "connect",
                R"({"from_addr":"module_src/out","to_addr":"mod1/echo_in"})");
            check(connect_echo.ok, "connect module_src/out -> mod1/echo_in ok");

            auto connect_list = post(client, base_url, "connect",
                R"({"from_addr":"module_src/list","to_addr":"mod1/echo_list_in"})");
            check(connect_list.ok, "connect module_src/list -> mod1/echo_list_in ok");

            auto connect_mod = post(client, base_url, "connect",
                R"({"from_addr":"a/out","to_addr":"mod1/mod_in"})");
            check(connect_mod.ok, "connect a/out -> mod1/mod_in ok");

            auto add_mod_assignment = post(client, base_url, "add_mod_assignment",
                R"({"node_id":"mod1","source":"motion","destination":"scale","amount":1.0,"polarity":"unipolar","curve":"linear"})");
            check(add_mod_assignment.ok, "add_mod_assignment motion -> scale ok");

            phase.store(23);
            while (phase.load() < 24) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto ig = post(client, base_url, "inspect_graph");
            check(ig.ok, "inspect_graph with module ok");
            if (!ig.j.is_null()) {
                auto mod = inspect_graph_node_by_id(ig.j, "mod1");
                check(!mod.is_null(), "inspect_graph returns authored module node");
                if (!mod.is_null()) {
                    check(mod.contains("type") && mod["type"].is_string() &&
                              mod["type"].get<std::string>() == "QuerySurfaceModule",
                          "module node reports module type");
                    auto scale = find_named_entry(mod.value("params", json::array()), "scale");
                    auto label = find_named_entry(mod.value("params", json::array()), "label");
                    check(!scale.is_null(), "module numeric param present");
                    check(!label.is_null(), "module text param present");
                    if (!scale.is_null()) {
                        check(scale.contains("value") && scale["value"].is_number() &&
                                  std::fabs(scale["value"].get<double>() - 4.0) < 1e-4,
                              "module numeric param exposes authored base value when modulated");
                        check(scale.contains("min") && scale["min"].is_number() &&
                                  std::fabs(scale["min"].get<double>() - 0.0) < 1e-6,
                              "module numeric param exposes min");
                        check(scale.contains("max") && scale["max"].is_number() &&
                                  std::fabs(scale["max"].get<double>() - 100.0) < 1e-6,
                              "module numeric param exposes max");
                        check(scale.contains("default") && scale["default"].is_number() &&
                                  std::fabs(scale["default"].get<double>() - 4.0) < 1e-6,
                              "module numeric param exposes default");
                    }
                    if (!label.is_null()) {
                        check(label.contains("string_value") && label["string_value"].is_string() &&
                                  label["string_value"].get<std::string>() == "module-label",
                              "module text param exposes string_value");
                    }

                    auto echo_in = find_named_entry(mod.value("inputs", json::array()), "echo_in");
                    auto echo_list_in = find_named_entry(mod.value("inputs", json::array()), "echo_list_in");
                    auto scaled = find_named_entry(mod.value("outputs", json::array()), "scaled");
                    auto label_out = find_named_entry(mod.value("outputs", json::array()), "label_out");
                    auto label_list = find_named_entry(mod.value("outputs", json::array()), "label_list");
                    auto echo_out = find_named_entry(mod.value("outputs", json::array()), "echo_out");
                    auto echo_list_out = find_named_entry(mod.value("outputs", json::array()), "echo_list_out");

                    check(!echo_in.is_null(), "module input port present");
                    check(!echo_list_in.is_null(), "module string-values input port present");
                    check(!scaled.is_null(), "module scalar output port present");
                    check(!label_out.is_null(), "module string output port present");
                    check(!label_list.is_null(), "module string-values output port present");
                    check(!echo_out.is_null(), "module echoed string output present");
                    check(!echo_list_out.is_null(), "module echoed string-values output present");

                    if (!echo_in.is_null()) {
                        check(echo_in.contains("current_string") && echo_in["current_string"].is_string() &&
                                  echo_in["current_string"].get<std::string>() == "alpha",
                              "module input exposes current_string");
                    }
                    if (!echo_list_in.is_null()) {
                        check(echo_list_in.contains("string_values") && echo_list_in["string_values"].is_array() &&
                                  echo_list_in["string_values"].size() == 3,
                              "module input exposes string_values");
                    }
                    if (!scaled.is_null()) {
                        check(scaled.contains("current_value") && scaled["current_value"].is_number() &&
                                  std::fabs(scaled["current_value"].get<double>() - 20.0) < 1e-4,
                              "module scalar output exposes modulated live output");
                    }
                    if (!label_out.is_null()) {
                        check(label_out.contains("current_string") && label_out["current_string"].is_string() &&
                                  label_out["current_string"].get<std::string>() == "module-label",
                              "module string output exposes current_string");
                    }
                    if (!label_list.is_null()) {
                        check(label_list.contains("string_values") && label_list["string_values"].is_array() &&
                                  label_list["string_values"].size() == 3,
                              "module string-values output exposes string_values");
                    }
                    if (!echo_out.is_null()) {
                        check(echo_out.contains("current_string") && echo_out["current_string"].is_string() &&
                                  echo_out["current_string"].get<std::string>() == "alpha",
                              "module echoed output exposes current_string");
                    }
                    if (!echo_list_out.is_null()) {
                        check(echo_list_out.contains("string_values") && echo_list_out["string_values"].is_array() &&
                                  echo_list_out["string_values"].size() == 3,
                              "module echoed output exposes string_values");
                    }
                }
            }

            auto intro = post(client, base_url, "introspect_nodes");
            check(intro.ok, "introspect_nodes with module ok");
            if (!intro.j.is_null()) {
                auto mod = introspect_node_by_id(intro.j, "mod1");
                check(!mod.is_null(), "introspect_nodes returns module instance");
                if (!mod.is_null()) {
                    check(mod.contains("is_module") && mod["is_module"].is_boolean() &&
                              mod["is_module"].get<bool>(),
                          "module introspection marks is_module");
                    check(mod.contains("kind") && mod["kind"].is_string() &&
                              mod["kind"].get<std::string>() == "module",
                          "module introspection kind=module");
                    check(mod.contains("params") && mod["params"].is_object(),
                          "module introspection has params object");
                    if (mod.contains("params") && mod["params"].is_object()) {
                        auto params = mod["params"];
                        check(params.contains("scale") && params["scale"].is_number() &&
                                  std::fabs(params["scale"].get<double>() - 4.0) < 1e-4,
                              "module introspection exposes authored base value when modulated");
                        check(params.contains("label") && params["label"].is_string() &&
                                  params["label"].get<std::string>() == "module-label",
                              "module introspection exposes string param");
                    }
                    auto meta = find_named_entry(mod.value("param_meta", json::array()), "scale");
                    check(!meta.is_null(), "module introspection param_meta present");
                    if (!meta.is_null()) {
                        check(meta.contains("semantic_intent") && meta["semantic_intent"].is_string() &&
                                  meta["semantic_intent"].get<std::string>() == "module_scale",
                              "module introspection exposes param metadata");
                    }

                    auto echo_in = find_named_entry(mod.value("inputs", json::array()), "echo_in");
                    auto echo_list_in = find_named_entry(mod.value("inputs", json::array()), "echo_list_in");
                    auto scaled = find_named_entry(mod.value("outputs", json::array()), "scaled");
                    auto label_out = find_named_entry(mod.value("outputs", json::array()), "label_out");
                    auto label_list = find_named_entry(mod.value("outputs", json::array()), "label_list");
                    auto echo_out = find_named_entry(mod.value("outputs", json::array()), "echo_out");
                    auto echo_list_out = find_named_entry(mod.value("outputs", json::array()), "echo_list_out");

                    if (!echo_in.is_null()) {
                        check(echo_in.contains("string") && echo_in["string"].is_string() &&
                                  echo_in["string"].get<std::string>() == "alpha",
                              "module introspection input exposes string");
                        check(echo_in.contains("connected_wires") && echo_in["connected_wires"].is_number_integer() &&
                                  echo_in["connected_wires"].get<int>() == 1,
                              "module introspection input exposes wire count");
                    }
                    if (!echo_list_in.is_null()) {
                        check(echo_list_in.contains("string_values") && echo_list_in["string_values"].is_object() &&
                                  echo_list_in["string_values"].value("length", -1) == 3,
                              "module introspection input exposes string_values length");
                    }
                    if (!scaled.is_null()) {
                        check(scaled.contains("scalar") && scaled["scalar"].is_number() &&
                                  std::fabs(scaled["scalar"].get<double>() - 20.0) < 1e-4,
                              "module introspection output exposes modulated live scalar");
                    }
                    if (!label_out.is_null()) {
                        check(label_out.contains("string") && label_out["string"].is_string() &&
                                  label_out["string"].get<std::string>() == "module-label",
                              "module introspection output exposes string");
                    }
                    if (!label_list.is_null()) {
                        check(label_list.contains("string_values") && label_list["string_values"].is_object() &&
                                  label_list["string_values"].value("length", -1) == 3,
                              "module introspection output exposes string_values length");
                    }
                    if (!echo_out.is_null()) {
                        check(echo_out.contains("string") && echo_out["string"].is_string() &&
                                  echo_out["string"].get<std::string>() == "alpha",
                              "module introspection echoed output exposes string");
                    }
                    if (!echo_list_out.is_null()) {
                        check(echo_list_out.contains("string_values") && echo_list_out["string_values"].is_object() &&
                                  echo_list_out["string_values"].value("length", -1) == 3,
                              "module introspection echoed output exposes string_values length");
                    }
                }
            }

            auto remove_mod = post(client, base_url, "remove_node",
                R"({"node_id":"mod1"})");
            check(remove_mod.ok, "remove_node mod1 ok");
            auto remove_src = post(client, base_url, "remove_node",
                R"({"node_id":"module_src"})");
            check(remove_src.ok, "remove_node module_src ok");

            phase.store(25);
            while (phase.load() < 26) std::this_thread::sleep_for(std::chrono::milliseconds(5));

            auto cleanup = post(client, base_url, "inspect_graph");
            check(cleanup.ok, "inspect_graph after module cleanup ok");
            if (!cleanup.j.is_null()) {
                check(inspect_graph_node_by_id(cleanup.j, "mod1").is_null(),
                      "module cleanup removes authored module node");
                check(inspect_graph_node_by_id(cleanup.j, "module_src").is_null(),
                      "module cleanup removes external source node");
            }
        }
