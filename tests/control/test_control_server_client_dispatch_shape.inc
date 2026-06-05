// Dispatch response-envelope shape guard (audit 04-R2).
//
// Pins the HTTP response contract that the dispatch handler-table refactor
// (extract_field + registry) must preserve: every result is {ok:bool},
// successes carry result/message, failures carry a string "error", and an
// unknown method is reported (not dropped). Included by test_control_server.cpp
// inside the client thread (uses `client`, `base_url`, `post`, `check`).
{
    std::fprintf(stderr, "\n--- dispatch response-envelope shape (04-R2 guard) ---\n");

    // Valid read-only call → {ok:true}
    auto r_ok = post(client, base_url, "inspect_graph");
    check(r_ok.j.contains("ok") && r_ok.j["ok"].is_boolean() && r_ok.ok,
          "inspect_graph → ok:true envelope");

    // Unknown method → {ok:false, error:"...unknown method..."}
    auto r_unknown = post(client, base_url, "definitely_not_a_real_method_xyz");
    check(!r_unknown.ok && r_unknown.j.contains("error") && r_unknown.j["error"].is_string(),
          "unknown method → ok:false + string error");
    check(r_unknown.j.value("error", std::string()).find("unknown method") != std::string::npos,
          "unknown-method error names it");

    // Missing required fields → {ok:false, error}
    auto r_missing = post(client, base_url, "add_node", "{}");
    check(!r_missing.ok && r_missing.j.contains("error") && r_missing.j["error"].is_string(),
          "add_node missing fields → ok:false + string error");

    // Wrong field type (cc as string) → {ok:false, error}
    auto r_badtype = post(client, base_url, "add_midi_mapping",
        R"({"node_id":"x","param":"y","cc":"not_a_number","channel":1,"range_min":0,"range_max":1})");
    check(!r_badtype.ok && r_badtype.j.contains("error"),
          "add_midi_mapping bad-type cc → ok:false + error");

    // Malformed JSON body → {ok:false, error}
    auto r_badjson = post(client, base_url, "add_node", "{not valid json");
    check(!r_badjson.ok && r_badjson.j.contains("error"),
          "malformed JSON body → ok:false + error");
}
