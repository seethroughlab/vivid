#include "runtime/control/control_server_internal.h"
#include "runtime/core/editor_window_manager.h"
#include "runtime/audio/vst3_scanner.h"
#ifdef __APPLE__
#include "runtime/audio/au_scanner.h"
#endif

namespace vivid {

// ---------------------------------------------------------------------------
// DispatchContext + handler table (audit 04-R2-F2/F7)
//
// dispatch() routes by method name through a handler registry (kHandlers) built
// once. Each handler is a non-capturing lambda taking the shared DispatchContext
// (all runtime dependencies), the raw body, and the parsed root + validity flag.
// Handlers never modify Graph/RuntimeCore directly — they go through RuntimeAPI;
// topology-mutating commands set pending_topology_change_ (applied between frames
// via apply_pending()). Every method is served from the handler table; unknown
// methods return a json_err.
// ---------------------------------------------------------------------------
struct DispatchContext {
    RuntimeAPI& api;
    Graph& graph;
    RuntimeCore& core;
    OperatorRegistry& registry;
    bool& has_gpu_ops;
    bool& has_audio;
    HotReloader* hot_reloader;
    const std::string& src_dir;
    OperatorSourceDocs& source_docs;
    SourceIndex& source_index;
    PackageManager* package_manager;
    PackageCompiler* package_compiler;
    Settings* settings;
    AudioEngine* audio_engine;
    AssetLibrary* asset_library;
    BuildConsole* build_console;
    GpuContext* gpu_context;
    PackageCatalog* package_catalog;
    const ControlServer* control_server;
    CrashRecoveryManager* crash_recovery_manager;
    EditorWindowManager* editor_window_manager;
};

using DispatchHandler = std::string(*)(DispatchContext& c, const std::string& body,
                                       const nlohmann::json& root, bool root_valid);

// Method → handler registry (audit 04-R2-F1/F2/F7). Built once. Every method is
// served from this table; unknown methods return a json_err. Each handler is a
// non-capturing lambda (→ function pointer).
static const std::unordered_map<std::string, DispatchHandler>& handler_table() {
    static const std::unordered_map<std::string, DispatchHandler> kHandlers = {
        // --- Session: clips, scenes, cue paths (audit 04-R2) ---
        {"update_clip_param", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string() ||
                !root.contains("node_id") || !root["node_id"].is_string())
                return json_err("missing 'track_id', 'clip_id', or 'node_id'");
            const std::string track = root["track_id"].get<std::string>();
            const std::string clip  = root["clip_id"].get<std::string>();
            const std::string node  = root["node_id"].get<std::string>();
            if (root.contains("bypassed") && root["bypassed"].is_boolean())
                return command_result_to_json(
                    c.api.update_clip_bypass(track, clip, node, root["bypassed"].get<bool>()));
            else if (root.contains("string_value") && root["string_value"].is_string() &&
                     root.contains("param") && root["param"].is_string())
                return command_result_to_json(
                    c.api.update_clip_string_param(track, clip, node, root["param"].get<std::string>(),
                                                 root["string_value"].get<std::string>()));
            else if (root.contains("value") && root["value"].is_number() &&
                     root.contains("param") && root["param"].is_string())
                return command_result_to_json(
                    c.api.update_clip_param(track, clip, node, root["param"].get<std::string>(),
                                          static_cast<float>(root["value"].get<double>())));
            else
                return json_err("need 'param'+'value', 'param'+'string_value', or 'bypassed'");
        }},
        {"rename_clip", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'track_id', 'clip_id', or 'name'");
            return command_result_to_json(
                c.api.rename_clip(root["track_id"].get<std::string>(),
                                root["clip_id"].get<std::string>(),
                                root["name"].get<std::string>()));
        }},
        {"set_clip_fade", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string() ||
                !root.contains("fade_bars") || !root["fade_bars"].is_number())
                return json_err("missing 'track_id', 'clip_id', or 'fade_bars'");
            return command_result_to_json(
                c.api.set_clip_fade(root["track_id"].get<std::string>(),
                                  root["clip_id"].get<std::string>(),
                                  root["fade_bars"].get<float>()));
        }},
        {"remove_clip", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string())
                return json_err("missing 'track_id' or 'clip_id'");
            return command_result_to_json(
                c.api.remove_clip(root["track_id"].get<std::string>(), root["clip_id"].get<std::string>()));
        }},
        {"move_clip", [](DispatchContext& c, const std::string&,
                         const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                return json_err("missing 'track_id', 'clip_id', or 'to_index'");
            return command_result_to_json(
                c.api.move_clip(root["track_id"].get<std::string>(),
                              root["clip_id"].get<std::string>(),
                              root["to_index"].get<int>()));
        }},
        {"launch_clip", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string())
                return json_err("missing 'track_id' or 'clip_id'");
            return command_result_to_json(
                c.api.launch_clip(root["track_id"].get<std::string>(), root["clip_id"].get<std::string>()));
        }},
        {"save_scene", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("name") || !root["name"].is_string())
                return json_err("missing 'name'");
            return command_result_to_json(c.api.save_scene(root["name"].get<std::string>()));
        }},
        {"save_scene_from_clips", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("name") || !root["name"].is_string() ||
                !root.contains("assignments") || !root["assignments"].is_object())
                return json_err("missing 'name' or 'assignments' (object of track_id->clip_id)");
            std::vector<std::pair<std::string, std::string>> assignments;
            for (auto& [track_id, clip_id] : root["assignments"].items())
                if (clip_id.is_string())
                    assignments.emplace_back(track_id, clip_id.get<std::string>());
            return command_result_to_json(
                c.api.save_scene_from_clips(root["name"].get<std::string>(), assignments));
        }},
        {"update_scene", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string())
                return json_err("missing 'scene_id'");
            return command_result_to_json(c.api.update_scene(root["scene_id"].get<std::string>()));
        }},
        {"rename_scene", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                return json_err("missing 'scene_id' or 'new_name'");
            return command_result_to_json(
                c.api.rename_scene(root["scene_id"].get<std::string>(), root["new_name"].get<std::string>()));
        }},
        {"remove_scene", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string())
                return json_err("missing 'scene_id'");
            return command_result_to_json(c.api.remove_scene(root["scene_id"].get<std::string>()));
        }},
        {"move_scene", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                return json_err("missing 'scene_id' or 'to_index'");
            return command_result_to_json(
                c.api.move_scene(root["scene_id"].get<std::string>(), root["to_index"].get<int>()));
        }},
        {"set_scene_assignment", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string() ||
                !root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id")  || !root["clip_id"].is_string())
                return json_err("missing 'scene_id', 'track_id', or 'clip_id'");
            return command_result_to_json(
                c.api.set_scene_assignment(root["scene_id"].get<std::string>(),
                                          root["track_id"].get<std::string>(),
                                          root["clip_id"].get<std::string>()));
        }},
        {"set_scene_leave_unchanged", [](DispatchContext& c, const std::string&,
                                         const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string() ||
                !root.contains("track_id") || !root["track_id"].is_string())
                return json_err("missing 'scene_id' or 'track_id'");
            return command_result_to_json(
                c.api.set_scene_leave_unchanged(root["scene_id"].get<std::string>(),
                                               root["track_id"].get<std::string>()));
        }},
        {"clear_scene_assignment", [](DispatchContext& c, const std::string&,
                                      const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string() ||
                !root.contains("track_id") || !root["track_id"].is_string())
                return json_err("missing 'scene_id' or 'track_id'");
            return command_result_to_json(
                c.api.clear_scene_assignment(root["scene_id"].get<std::string>(),
                                            root["track_id"].get<std::string>()));
        }},
        {"create_cue_path", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("name") || !root["name"].is_string())
                return json_err("missing 'name'");
            return command_result_to_json(c.api.create_cue_path(root["name"].get<std::string>()));
        }},
        {"rename_cue_path", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'path_id' or 'name'");
            return command_result_to_json(
                c.api.rename_cue_path(root["path_id"].get<std::string>(),
                                    root["name"].get<std::string>()));
        }},
        {"remove_cue_path", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string())
                return json_err("missing 'path_id'");
            return command_result_to_json(
                c.api.remove_cue_path(root["path_id"].get<std::string>()));
        }},
        {"move_cue_path", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                return json_err("missing 'path_id' or 'to_index'");
            return command_result_to_json(
                c.api.move_cue_path(root["path_id"].get<std::string>(),
                                  root["to_index"].get<int>()));
        }},
        {"add_cue_step", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("scene_id") || !root["scene_id"].is_string())
                return json_err("missing 'path_id' or 'scene_id'");
            return command_result_to_json(
                c.api.add_cue_step(root["path_id"].get<std::string>(),
                                 root["scene_id"].get<std::string>(),
                                 root.value("index", -1)));
        }},
        {"remove_cue_step", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("step_id") || !root["step_id"].is_string())
                return json_err("missing 'path_id' or 'step_id'");
            return command_result_to_json(
                c.api.remove_cue_step(root["path_id"].get<std::string>(),
                                    root["step_id"].get<std::string>()));
        }},
        {"move_cue_step", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("step_id") || !root["step_id"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                return json_err("missing 'path_id', 'step_id', or 'to_index'");
            return command_result_to_json(
                c.api.move_cue_step(root["path_id"].get<std::string>(),
                                  root["step_id"].get<std::string>(),
                                  root["to_index"].get<int>()));
        }},
        {"set_cue_step_advance", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("step_id") || !root["step_id"].is_string() ||
                !root.contains("advance_mode") || !root["advance_mode"].is_string())
                return json_err("missing 'path_id', 'step_id', or 'advance_mode'");
            return command_result_to_json(
                c.api.set_cue_step_advance(root["path_id"].get<std::string>(),
                                         root["step_id"].get<std::string>(),
                                         root["advance_mode"].get<std::string>(),
                                         root.value("bars", 0)));
        }},
        {"launch_cue_step", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("step_id") || !root["step_id"].is_string() ||
                !root.contains("quantize") || !root["quantize"].is_string())
                return json_err("missing 'path_id', 'step_id', or 'quantize'");
            return command_result_to_json(
                c.api.launch_cue_step(root["path_id"].get<std::string>(),
                                    root["step_id"].get<std::string>(),
                                    root["quantize"].get<std::string>()));
        }},
        {"advance_cue_path", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path_id") || !root["path_id"].is_string() ||
                !root.contains("quantize") || !root["quantize"].is_string())
                return json_err("missing 'path_id' or 'quantize'");
            return command_result_to_json(
                c.api.advance_cue_path(root["path_id"].get<std::string>(),
                                     root["quantize"].get<std::string>()));
        }},
        {"stop_cue_path", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return command_result_to_json(
                c.api.stop_cue_path(root.value("path_id", std::string{})));
        }},
        {"queue_clip", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id")  || !root["clip_id"].is_string() ||
                !root.contains("quantize") || !root["quantize"].is_string())
                return json_err("missing 'track_id', 'clip_id', or 'quantize'");
            const float fade_bars = root.contains("fade_bars") && root["fade_bars"].is_number()
                ? static_cast<float>(root["fade_bars"].get<double>()) : 0.0f;
            return command_result_to_json(
                c.api.queue_clip(root["track_id"].get<std::string>(),
                                root["clip_id"].get<std::string>(),
                                root["quantize"].get<std::string>(), fade_bars));
        }},
        {"queue_scene", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("scene_id") || !root["scene_id"].is_string() ||
                !root.contains("quantize") || !root["quantize"].is_string())
                return json_err("missing 'scene_id' or 'quantize'");
            const float fade_bars = root.contains("fade_bars") && root["fade_bars"].is_number()
                ? static_cast<float>(root["fade_bars"].get<double>()) : 0.0f;
            return command_result_to_json(
                c.api.queue_scene(root["scene_id"].get<std::string>(),
                                 root["quantize"].get<std::string>(), fade_bars));
        }},
        {"inspect_clip", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_inspect_clip(c.graph,
                root.value("track_id", ""), root.value("clip_id", ""));
        }},
        {"inspect_scene", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_inspect_scene(c.graph, c.api, root.value("scene_id", ""));
        }},

        {"inspect_session", [](DispatchContext& c, const std::string&,
                               const nlohmann::json&, bool) -> std::string {
            return handle_inspect_session(c.graph, c.api);
        }},
        {"add_node", [](DispatchContext& c, const std::string&,
                        const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string type, node_id, err;
            if (!require_string(root, "type", type, err) ||
                !require_string(root, "node_id", node_id, err))
                return err;
            return command_result_to_json(c.api.add_node(type, node_id));
        }},
        {"remove_node", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            return command_result_to_json(c.api.remove_node(node_id));
        }},
        {"add_midi_mapping", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, param, err;
            int cc = 0, channel = 0;
            float range_min = 0.0f, range_max = 0.0f;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "param", param, err) ||
                !require_int(root, "cc", cc, err) ||
                !require_int(root, "channel", channel, err) ||
                !require_float(root, "range_min", range_min, err) ||
                !require_float(root, "range_max", range_max, err))
                return err;
            return command_result_to_json(
                c.api.add_midi_mapping(node_id, param, cc, channel, range_min, range_max));
        }},

        // --- Read-only queries (no body needed) ---
        {"introspect_nodes", [](DispatchContext& c, const std::string&,
                                const nlohmann::json&, bool) -> std::string {
            return handle_introspect_nodes(c.graph, c.core, c.core.subgraph_modules());
        }},
        {"run_diagnostics", [](DispatchContext& c, const std::string&,
                               const nlohmann::json&, bool) -> std::string {
            return control_server_checks::handle_run_diagnostics(c.graph, c.core, c.registry, c.audio_engine, c.gpu_context, c.package_catalog, c.control_server);
        }},
        {"get_runtime_health", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json&, bool) -> std::string {
            return control_server_checks::handle_get_runtime_health(c.graph, c.core, c.registry, c.audio_engine, c.gpu_context, c.package_catalog, c.control_server);
        }},
        {"get_registry_diagnostics", [](DispatchContext& c, const std::string&,
                                        const nlohmann::json&, bool) -> std::string {
            return handle_get_registry_diagnostics(c.registry);
        }},
        {"get_graph_load_diagnostics", [](DispatchContext& c, const std::string&,
                                          const nlohmann::json&, bool) -> std::string {
            return handle_get_graph_load_diagnostics(c.graph);
        }},
        {"list_source_roots", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json&, bool) -> std::string {
            return handle_list_source_roots(c.source_index);
        }},
        {"operator_map", [](DispatchContext& c, const std::string&,
                            const nlohmann::json&, bool) -> std::string {
            return handle_operator_map(c.registry);
        }},
        {"validate_operators", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json&, bool) -> std::string {
            return handle_validate_operators(c.registry);
        }},
        {"get_discovery_report", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json&, bool) -> std::string {
            return handle_get_discovery_report(c.package_manager);
        }},

        // --- Body-parsing read-only queries ---
        {"inspect_graph", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            std::string detail = "full";
            if (root_valid && root.contains("detail") && root["detail"].is_string())
                detail = root["detail"].get<std::string>();
            return handle_inspect_graph(c.graph, c.core, c.core.subgraph_modules(), detail);
        }},
        {"list_types", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            return handle_list_types(c.registry, c.package_manager, c.source_docs, root_valid ? root : nlohmann::json::object(), c.core.subgraph_modules());
        }},
        {"search_source", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_search_source(c.source_index, root);
        }},
        {"read_source_file", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_read_source_file(c.source_index, root);
        }},
        {"read_source_span", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_read_source_span(c.source_index, root);
        }},
        {"find_symbol", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_find_symbol(c.source_index, root);
        }},
        {"find_references", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_find_references(c.source_index, root);
        }},
        {"get_build_activity", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json& root, bool root_valid) -> std::string {
            return handle_get_build_activity(c.build_console, root_valid ? root : nlohmann::json::object());
        }},
        {"explain_build_failure", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            return handle_explain_build_failure(c.build_console, root_valid ? root : nlohmann::json::object());
        }},
        {"validate_checks", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return control_server_checks::handle_validate_checks(root);
        }},
        {"sample_node_outputs", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_sample_node_outputs(c.graph, c.core, root);
        }},
        {"run_checks", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return control_server_checks::handle_run_checks(c.graph, c.core, c.registry, root);
        }},

        // --- Topology + parameter mutations ---
        {"rename_node", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string old_id, new_id, err;
            if (!require_string(root, "old_id", old_id, err) ||
                !require_string(root, "new_id", new_id, err))
                return err;
            return command_result_to_json(c.api.rename_node(old_id, new_id));
        }},
        {"connect", [](DispatchContext& c, const std::string&,
                       const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string from_addr, to_addr, err;
            if (!require_string(root, "from_addr", from_addr, err) ||
                !require_string(root, "to_addr", to_addr, err))
                return err;
            const bool semantic_defaults = optional_bool(root, "semantic_defaults", false);
            const std::string bridge = optional_string(root, "bridge", "");
            CommandResult cr = c.api.connect(from_addr, to_addr, semantic_defaults, bridge);
            if (!cr.ok)
                return json_err(cr.message);
            nlohmann::json resp = nlohmann::json::object();
            resp["ok"] = true;
            resp["message"] = cr.message;

            // Surface dropped-port mistakes immediately (e.g. connecting
            // to "mixer/input" instead of "mixer/input_0") instead of
            // letting them fail silently until get_graph_errors.
            {
                std::string ffn, ffp, ttn, ttp;
                nlohmann::json warns = nlohmann::json::array();
                bool from_ok = split_addr_local(from_addr, ffn, ffp);
                bool to_ok = split_addr_local(to_addr, ttn, ttp);
                if (from_ok) {
                    std::string w = connect_port_issue(
                        c.registry, c.graph.find_node(ffn), ffp, /*want_output=*/true);
                    if (!w.empty()) warns.push_back(w);
                }
                if (to_ok) {
                    std::string w = connect_port_issue(
                        c.registry, c.graph.find_node(ttn), ttp, /*want_output=*/false);
                    if (!w.empty()) warns.push_back(w);
                }
                // Type-compat warning: only when BOTH endpoints resolve to
                // real ports (params/name-issues are handled above). Mirrors
                // the compiler's drop conditions so it never false-warns on a
                // valid edge. (audit 04-F5)
                VividPortType ft, tt;
                if (from_ok && to_ok &&
                    resolve_exact_port_type(c.registry, c.graph.find_node(ffn), ffp,
                                            /*want_output=*/true, ft) &&
                    resolve_exact_port_type(c.registry, c.graph.find_node(ttn), ttp,
                                            /*want_output=*/false, tt)) {
                    std::string w = connect_type_issue(ft, tt);
                    if (!w.empty()) warns.push_back(w);
                }
                if (!warns.empty()) resp["warnings"] = std::move(warns);
            }

            bool inferred_applied = false;
            if (semantic_defaults) {
                const ConnectionDef* conn = find_connection_by_addr(c.graph, from_addr, to_addr);
                if (conn && conn->has_remap()) {
                    inferred_applied = true;
                    resp["inferred_remap"] = {
                        {"from_min", conn->from_min}, {"from_max", conn->from_max},
                        {"to_min", conn->to_min}, {"to_max", conn->to_max},
                        {"clamp", conn->clamp}
                    };
                }
            }
            resp["inferred_remap_applied"] = inferred_applied;
            return resp.dump();
        }},
        {"disconnect", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string from_addr, to_addr, err;
            if (!require_string(root, "from_addr", from_addr, err) ||
                !require_string(root, "to_addr", to_addr, err))
                return err;
            return command_result_to_json(c.api.disconnect(from_addr, to_addr));
        }},
        {"set_connection_remap", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string from_addr, to_addr, err;
            if (!require_string(root, "from_addr", from_addr, err) ||
                !require_string(root, "to_addr", to_addr, err))
                return err;
            float fmin = optional_float(root, "from_min", 0.0f);
            float fmax = optional_float(root, "from_max", 1.0f);
            float tmin = optional_float(root, "to_min", 0.0f);
            float tmax = optional_float(root, "to_max", 1.0f);
            bool  cval = optional_bool(root, "clamp", false);
            uint8_t curve = root.contains("curve") && root["curve"].is_number_unsigned()
                ? static_cast<uint8_t>(root["curve"].get<unsigned>()) : uint8_t(0);
            return command_result_to_json(
                c.api.set_connection_remap(from_addr, to_addr, fmin, fmax, tmin, tmax, cval, curve));
        }},
        {"set_param", [](DispatchContext& c, const std::string&,
                         const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, param, err;
            float value = 0.0f;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "param", param, err) ||
                !require_float(root, "value", value, err))
                return err;
            return command_result_to_json(c.api.set_param(node_id, param, value));
        }},
        {"set_string_param", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, param, value, err;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "param", param, err) ||
                !require_string(root, "value", value, err))
                return err;
            return command_result_to_json(c.api.set_string_param(node_id, param, value));
        }},
        {"get_param", [](DispatchContext& c, const std::string&,
                         const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, param, err;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "param", param, err))
                return err;
            auto r = c.api.get_param(node_id, param);
            if (r.ok) {
                float v = 0;
                try { v = std::stof(r.message); } catch (...) {}
                return nlohmann::json{{"ok", true}, {"value", static_cast<double>(v)}}.dump();
            }
            return json_err(r.message);
        }},
        {"list_clap_params", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string nid, err;
            if (!require_string(root, "node_id", nid, err)) return err;
            auto* cg = c.core.compiled_graph();
            if (!cg) return json_err("no compiled graph");
            const auto* cn = cg->find_node(nid);
            if (!cn) return json_err("unknown node '" + nid + "'");
            auto fi = cn->file_param_indices.find("_clap_params");
            if (fi == cn->file_param_indices.end() ||
                fi->second >= cn->file_param_storage.size())
                return json_err("node '" + nid + "' is not a CLAP operator");
            const std::string& raw = cn->file_param_storage[fi->second];
            try {
                auto arr = nlohmann::json::parse(raw.empty() ? "[]" : raw);
                return nlohmann::json{{"ok", true}, {"params", arr}}.dump();
            } catch (...) {
                return nlohmann::json{{"ok", true}, {"params", nlohmann::json::array()}}.dump();
            }
        }},
        {"list_au_plugins", [](DispatchContext&, const std::string&,
                               const nlohmann::json&, bool) -> std::string {
#ifdef __APPLE__
            runtime_au_scan_plugins();
            const auto& plugins = runtime_au_get_plugins();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : plugins)
                arr.push_back(nlohmann::json{{"name", p.name}});
            return nlohmann::json{{"ok", true}, {"plugins", arr}}.dump();
#else
            return nlohmann::json{{"ok", true}, {"plugins", nlohmann::json::array()}}.dump();
#endif
        }},

        // --- Migrated batch 04-R2-R2b ---
        {"list_mod_destinations", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            auto r = c.api.list_mod_destinations(node_id);
            return r.ok ? r.message : json_err(r.message);
        }},
        {"list_mod_assignments", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            auto r = c.api.list_mod_assignments(node_id);
            return r.ok ? r.message : json_err(r.message);
        }},
        {"get_graph_errors", [](DispatchContext& c, const std::string&,
                                const nlohmann::json&, bool) -> std::string {
            nlohmann::json res = nlohmann::json::object();
            nlohmann::json errs = nlohmann::json::array();
            if (const auto* cg = c.core.compiled_graph()) {
                for (const auto& cn : cg->nodes) {
                    if (!cn.errored && !cn.missing_operator) continue;
                    nlohmann::json err_obj = {
                        {"node_id", cn.node_id},
                        {"error", cn.missing_operator ? "missing operator" : cn.error_message},
                        {"missing_operator", cn.missing_operator}
                    };
                    if (!cn.missing_operator_reason.empty())
                        err_obj["reason"] = cn.missing_operator_reason;
                    if (!cn.missing_operator_detail.empty())
                        err_obj["detail"] = cn.missing_operator_detail;
                    errs.push_back(std::move(err_obj));
                }
            }
            nlohmann::json dropped = nlohmann::json::array();
            if (const auto* cg = c.core.compiled_graph()) {
                for (const auto& dc : cg->dropped_connections) {
                    dropped.push_back({
                        {"from", dc.from_node + "/" + dc.from_port},
                        {"to", dc.to_node + "/" + dc.to_port},
                        {"reason", dc.reason}
                    });
                }
            }
            res["errors"] = std::move(errs);
            res["dropped_connections"] = std::move(dropped);
            return json_ok(std::move(res));
        }},
        {"queue_state_transition", [](DispatchContext& c, const std::string&,
                                      const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                return json_err("missing 'sm_node'");
            else if (!root.contains("state") || !root["state"].is_number_integer())
                return json_err("missing 'state'");
            std::string q = (root.contains("quantize") && root["quantize"].is_string())
                ? root["quantize"].get<std::string>() : "bar";
            return command_result_to_json(c.api.queue_state_transition(
                root["sm_node"].get<std::string>(),
                root["state"].get<int>(), q));
        }},
        {"set_quantize_clock", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            return command_result_to_json(c.api.set_quantize_clock(node_id));
        }},
        {"set_launch_quantize", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string mode, err;
            if (!require_string(root, "mode", mode, err)) return err;
            return command_result_to_json(c.api.set_launch_quantize(mode));
        }},
        {"set_graph_metronome", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            const float bpm = (root.contains("bpm") && root["bpm"].is_number())
                ? root["bpm"].get<float>() : 120.0f;
            const int beats_per_bar = (root.contains("beats_per_bar") && root["beats_per_bar"].is_number_integer())
                ? static_cast<int>(root["beats_per_bar"].get<int64_t>()) : 4;
            return command_result_to_json(c.api.set_graph_metronome(bpm, beats_per_bar));
        }},
        {"set_analysis", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("enabled") || !root["enabled"].is_boolean())
                return json_err("missing 'enabled' (boolean)");
            bool enabled = root["enabled"].get<bool>();
            c.core.frame_executor().set_analysis_enabled(enabled);
            if (c.audio_engine) c.audio_engine->set_analysis_enabled(enabled);
            if (c.settings) c.settings->show_analysis = enabled;
            return json_ok_msg(enabled ? "analysis enabled" : "analysis disabled");
        }},
        {"save_preset", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'node_id' or 'name'");
            // Optional curation metadata (JSON object) stored verbatim on the preset.
            std::string metadata;
            if (root.contains("metadata") && root["metadata"].is_object())
                metadata = root["metadata"].dump();
            return command_result_to_json(
                c.api.save_preset(root["node_id"].get<std::string>(),
                                  root["name"].get<std::string>(), metadata));
        }},
        {"recall_preset", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'node_id' or 'name'");
            return command_result_to_json(
                c.api.recall_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }},
        {"update_preset", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'node_id' or 'name'");
            return command_result_to_json(
                c.api.update_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }},
        {"remove_preset", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'node_id' or 'name'");
            return command_result_to_json(
                c.api.remove_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }},
        {"rename_preset", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("old_name") || !root["old_name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                return json_err("missing 'node_id', 'old_name', or 'new_name'");
            return command_result_to_json(
                c.api.rename_preset(root["node_id"].get<std::string>(), root["old_name"].get<std::string>(),
                                    root["new_name"].get<std::string>()));
        }},
        {"list_presets", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            return c.api.list_presets_json(node_id);
        }},
        {"list_factory_presets", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            return command_result_to_json(c.api.list_factory_presets(node_id));
        }},
        {"set_param_lock", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("flags") || !root["flags"].is_number())
                return json_err("missing 'node_id', 'param', or 'flags'");
            return command_result_to_json(
                c.api.set_param_lock(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                     static_cast<uint8_t>(root["flags"].get<int64_t>())));
        }},
        {"get_param_lock", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                return json_err("missing 'node_id' or 'param'");
            return command_result_to_json(
                c.api.get_param_lock(root["node_id"].get<std::string>(), root["param"].get<std::string>()));
        }},
        {"set_state_preset", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("sm_node") || !root["sm_node"].is_string() ||
                !root.contains("state_idx") || !root["state_idx"].is_number() ||
                !root.contains("target_node") || !root["target_node"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'sm_node', 'state_idx', 'target_node', or 'name'");
            return command_result_to_json(
                c.api.set_state_preset(root["sm_node"].get<std::string>(),
                                       root["state_idx"].get<int>(),
                                       root["target_node"].get<std::string>(), root["name"].get<std::string>()));
        }},
        {"remove_state_preset", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("sm_node") || !root["sm_node"].is_string() ||
                !root.contains("state_idx") || !root["state_idx"].is_number() ||
                !root.contains("target_node") || !root["target_node"].is_string())
                return json_err("missing 'sm_node', 'state_idx', or 'target_node'");
            return command_result_to_json(
                c.api.remove_state_preset(root["sm_node"].get<std::string>(),
                                          root["state_idx"].get<int>(),
                                          root["target_node"].get<std::string>()));
        }},
        {"clear_state_presets", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string sm_node, err;
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                return json_err("missing 'sm_node'");
            return command_result_to_json(
                c.api.clear_state_presets(root["sm_node"].get<std::string>()));
        }},
        {"ensure_state_mapping", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                return json_err("missing 'sm_node'");
            return command_result_to_json(
                c.api.ensure_state_mapping(root["sm_node"].get<std::string>()));
        }},
        {"inspect_state_presets", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                return json_err("missing 'sm_node'");
            return command_result_to_json(
                c.api.inspect_state_presets(root["sm_node"].get<std::string>()));
        }},
        {"create_track", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("name") || !root["name"].is_string())
                return json_err("missing 'name'");
            return command_result_to_json(c.api.create_track(root["name"].get<std::string>()));
        }},
        {"rename_track", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'track_id' or 'name'");
            return command_result_to_json(
                c.api.rename_track(root["track_id"].get<std::string>(), root["name"].get<std::string>()));
        }},
        {"remove_track", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string())
                return json_err("missing 'track_id'");
            return command_result_to_json(c.api.remove_track(root["track_id"].get<std::string>()));
        }},
        {"move_track", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                return json_err("missing 'track_id' or 'to_index'");
            return command_result_to_json(
                c.api.move_track(root["track_id"].get<std::string>(), root["to_index"].get<int>()));
        }},
        {"assign_nodes_to_track", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("node_ids") || !root["node_ids"].is_array())
                return json_err("missing 'track_id' or 'node_ids'");
            std::vector<std::string> ids;
            for (const auto& v : root["node_ids"]) {
                if (v.is_string()) ids.push_back(v.get<std::string>());
            }
            return command_result_to_json(
                c.api.assign_nodes_to_track(root["track_id"].get<std::string>(), ids));
        }},
        {"unassign_nodes_from_track", [](DispatchContext& c, const std::string&,
                                         const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("node_ids") || !root["node_ids"].is_array())
                return json_err("missing 'track_id' or 'node_ids'");
            std::vector<std::string> ids;
            for (const auto& v : root["node_ids"]) {
                if (v.is_string()) ids.push_back(v.get<std::string>());
            }
            return command_result_to_json(
                c.api.unassign_nodes_from_track(root["track_id"].get<std::string>(), ids));
        }},
        {"save_clip", [](DispatchContext& c, const std::string&,
                         const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                return json_err("missing 'track_id' or 'name'");
            const bool activate = root.contains("activate") &&
                root["activate"].is_boolean() && root["activate"].get<bool>();
            return command_result_to_json(
                c.api.save_clip(root["track_id"].get<std::string>(),
                                root["name"].get<std::string>(), activate));
        }},
        {"update_clip", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("track_id") || !root["track_id"].is_string() ||
                !root.contains("clip_id") || !root["clip_id"].is_string())
                return json_err("missing 'track_id' or 'clip_id'");
            return command_result_to_json(
                c.api.update_clip(root["track_id"].get<std::string>(), root["clip_id"].get<std::string>()));
        }},

        // --- Migrated batch 04-R2-R2 ---
        {"list_au_params", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string nid, err;
            if (!require_string(root, "node_id", nid, err)) return err;
            auto* cg = c.core.compiled_graph();
            if (!cg) return json_err("no compiled graph");
            const auto* cn = cg->find_node(nid);
            if (!cn) return json_err("unknown node '" + nid + "'");
            auto fi = cn->file_param_indices.find("_au_params");
            if (fi == cn->file_param_indices.end() ||
                fi->second >= cn->file_param_storage.size())
                return json_err("node '" + nid + "' is not an AU operator");
            const std::string& raw = cn->file_param_storage[fi->second];
            try {
                auto arr = nlohmann::json::parse(raw.empty() ? "[]" : raw);
                return nlohmann::json{{"ok", true}, {"params", arr}}.dump();
            } catch (...) {
                return nlohmann::json{{"ok", true}, {"params", nlohmann::json::array()}}.dump();
            }
        }},
        {"list_vst3_plugins", [](DispatchContext&, const std::string&,
                                 const nlohmann::json&, bool) -> std::string {
            runtime_vst3_scan_plugins();
            const auto& plugins = runtime_vst3_get_plugins();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : plugins)
                arr.push_back(nlohmann::json{{"name", p.key}});
            return nlohmann::json{{"ok", true}, {"plugins", arr}}.dump();
        }},
        {"list_vst3_params", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string nid, err;
            if (!require_string(root, "node_id", nid, err)) return err;
            auto* cg = c.core.compiled_graph();
            if (!cg) return json_err("no compiled graph");
            const auto* cn = cg->find_node(nid);
            if (!cn) return json_err("unknown node '" + nid + "'");
            auto fi = cn->file_param_indices.find("_vst3_params");
            if (fi == cn->file_param_indices.end() ||
                fi->second >= cn->file_param_storage.size())
                return json_err("node '" + nid + "' is not a VST3 operator");
            const std::string& raw = cn->file_param_storage[fi->second];
            try {
                auto arr = nlohmann::json::parse(raw.empty() ? "[]" : raw);
                return nlohmann::json{{"ok", true}, {"params", arr}}.dump();
            } catch (...) {
                return nlohmann::json{{"ok", true}, {"params", nlohmann::json::array()}}.dump();
            }
        }},
        {"list_vst3_presets", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string nid, err;
            if (!require_string(root, "node_id", nid, err)) return err;
            auto* cg = c.core.compiled_graph();
            if (!cg) return json_err("no compiled graph");
            const auto* cn = cg->find_node(nid);
            if (!cn) return json_err("unknown node '" + nid + "'");
            auto fi = cn->file_param_indices.find("_vst3_presets");
            if (fi == cn->file_param_indices.end() ||
                fi->second >= cn->file_param_storage.size())
                return json_err("node '" + nid + "' is not a VST3 operator");
            const std::string& raw = cn->file_param_storage[fi->second];
            try {
                auto arr = nlohmann::json::parse(raw.empty() ? "[]" : raw);
                return nlohmann::json{{"ok", true}, {"presets", arr}}.dump();
            } catch (...) {
                return nlohmann::json{{"ok", true}, {"presets", nlohmann::json::array()}}.dump();
            }
        }},
        {"open_editor", [](DispatchContext& c, const std::string&,
                           const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.editor_window_manager) return json_err("editor window manager unavailable");
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            bool opened = c.editor_window_manager->open(node_id);
            if (opened) return json_ok_msg("editor opened for " + node_id);
            return json_err("no editor available for " + node_id);
        }},
        {"close_editor", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.editor_window_manager) return json_err("editor window manager unavailable");
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            c.editor_window_manager->close(node_id);
            return R"({"ok":true})";
        }},
        {"is_editor_open", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.editor_window_manager) return json_err("editor window manager unavailable");
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            bool open = c.editor_window_manager->is_open(node_id);
            return nlohmann::json{{"ok", true}, {"open", open}}.dump();
        }},
        {"editor_inject_event", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.editor_window_manager) return json_err("editor window manager unavailable");
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            if (!root.contains("type") || !root["type"].is_number_integer())
                return json_err("missing 'type' (integer 0..4)");
            VividEditorEvent ev{};
            ev.type      = static_cast<VividEditorEventType>(root["type"].get<int>());
            ev.x         = root.value("x",         0.0f);
            ev.y         = root.value("y",         0.0f);
            ev.button    = root.value("button",    0);
            ev.action    = root.value("action",    0);
            ev.scroll_dx = root.value("scroll_dx", 0.0f);
            ev.scroll_dy = root.value("scroll_dy", 0.0f);
            ev.key       = root.value("key",       0);
            ev.scancode  = root.value("scancode",  0);
            ev.codepoint = root.value("codepoint", 0u);
            ev.modifiers = root.value("modifiers", 0);
            bool ok = c.editor_window_manager->inject_event(node_id, ev);
            if (ok) return R"({"ok":true})";
            return json_err("no editor open for " + node_id);
        }},
        {"inspect_editor", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.editor_window_manager) return json_err("editor window manager unavailable");
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            auto maybe_tree = c.editor_window_manager->capture_introspection(node_id);
            if (!maybe_tree)
                return json_err("no editor open (or no widgets drawn) for " + node_id);
            nlohmann::json out;
            out["ok"] = true;
            out["node_id"] = node_id;
            try {
                out["widgets"] = nlohmann::json::parse(*maybe_tree);
            } catch (const std::exception& e) {
                out["widgets"] = nlohmann::json::array();
                out["parse_error"] = e.what();
            }
            return out.dump();
        }},
        {"capture_editor", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.editor_window_manager) return json_err("editor window manager unavailable");
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            auto maybe_png = c.editor_window_manager->capture_surface_png(node_id);
            if (!maybe_png)
                return json_err("no editor open (or capture failed) for " + node_id);
            const auto& png = *maybe_png;
            int w = 0, h = 0;
            parse_png_dimensions(png.data(), png.size(), w, h);
            nlohmann::json out;
            out["ok"]        = true;
            out["width"]     = w;
            out["height"]    = h;
            bool saved = false;
            if (root.contains("save_path") && root["save_path"].is_string()) {
                const std::string save_path = root["save_path"].get<std::string>();
                if (!save_path.empty() && is_safe_capture_image_path(save_path)) {
                    std::ofstream ofs(save_path, std::ios::binary);
                    if (ofs.is_open()) {
                        ofs.write(reinterpret_cast<const char*>(png.data()),
                                  static_cast<std::streamsize>(png.size()));
                        if (ofs.good()) {
                            out["path"] = save_path;
                            saved = true;
                        }
                    }
                }
            }
            if (!saved)
                out["png_base64"] = base64_encode_bytes(png.data(), png.size());
            return out.dump();
        }},
        {"save_graph", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            // Annotate nodes with package provenance before saving
            if (c.package_manager) {
                auto packages = c.package_manager->list();
                std::unordered_map<std::string, std::string> pkg_ver_map;
                for (const auto& p : packages) pkg_ver_map[p.name] = p.version;
                for (auto& node : c.graph.nodes_mut()) {
                    const auto* pkg = c.registry.package_for_type(node.type);
                    if (pkg) {
                        node.pkg_name    = *pkg;
                        node.pkg_version = pkg_ver_map.count(*pkg) ? pkg_ver_map[*pkg] : "";
                    }
                }
            }
            if (root_valid && root.contains("path") && root["path"].is_string())
                return command_result_to_json(c.api.save_as(root["path"].get<std::string>()));
            return command_result_to_json(c.api.save());
        }},
        {"write_project_lockfile", [](DispatchContext& c, const std::string&,
                                      const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!c.package_manager) return json_err("no package manager available");
            const std::string graph_path  = root.value("graph_path", std::string());
            const std::string output_path = root.value("output_path", std::string());
            return command_result_to_json(
                c.api.write_project_lockfile(*c.package_manager, graph_path, output_path));
        }},
        {"verify_project_lockfile", [](DispatchContext& c, const std::string&,
                                       const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!c.package_manager) return json_err("no package manager available");
            const std::string graph_path    = root.value("graph_path", std::string());
            const std::string lockfile_path = root.value("lockfile_path", std::string());
            return unwrap_status_to_json(
                c.api.verify_project_lockfile(*c.package_manager, graph_path, lockfile_path));
        }},
        {"get_project_dependency_status", [](DispatchContext& c, const std::string&,
                                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!c.package_manager) return json_err("no package manager available");
            const std::string graph_path = root.value("graph_path", std::string());
            return unwrap_status_to_json(
                c.api.get_project_dependency_status(*c.package_manager, graph_path));
        }},
        {"load_graph", [](DispatchContext& c, const std::string&,
                          const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("path") || !root["path"].is_string())
                return json_err("load_graph requires 'path' parameter");
            const std::string lockfile_mode =
                root.value("lockfile_mode", std::string());
            CommandResult cr = c.api.load_graph(
                root["path"].get<std::string>(),
                c.has_gpu_ops, c.has_audio, lockfile_mode);
            if (!cr.ok)
                return json_err(cr.message);
            // Disambiguate has_audio=false: surface when the graph has
            // audio operators but the engine failed to start (e.g. device
            // lost), so clients don't read it as "no audio nodes". (04-F1)
            nlohmann::json resp = {{"ok", true}, {"message", cr.message}};
            if (c.core.has_audio_operators() && !c.has_audio)
                resp["audio_unavailable"] = true;
            return resp.dump();
        }},
        {"get_last_crash", [](DispatchContext& c, const std::string&,
                              const nlohmann::json&, bool) -> std::string {
            return handle_get_last_crash(c.crash_recovery_manager);
        }},
        {"clear_last_crash", [](DispatchContext& c, const std::string&,
                                const nlohmann::json&, bool) -> std::string {
            return handle_clear_last_crash(c.crash_recovery_manager);
        }},
        {"load_graph_safe_mode", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            return handle_load_graph_safe_mode(
                root, c.crash_recovery_manager, c.core, c.api, c.has_gpu_ops, c.has_audio);
        }},
        {"set_resolution", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("width") || !root["width"].is_number() ||
                !root.contains("height") || !root["height"].is_number())
                return json_err("missing 'node_id', 'width', or 'height'");
            return command_result_to_json(
                c.api.set_resolution(root["node_id"].get<std::string>(),
                                     root["width"].get<uint32_t>(),
                                     root["height"].get<uint32_t>()));
        }},
        {"set_node_layout", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("x") || !root["x"].is_number() ||
                !root.contains("y") || !root["y"].is_number())
                return json_err("missing 'node_id', 'x', or 'y'");
            return command_result_to_json(
                c.api.set_node_layout(root["node_id"].get<std::string>(),
                                      root["x"].get<float>(),
                                      root["y"].get<float>()));
        }},
        {"set_node_bypassed", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            bool bypassed = false;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_bool(root, "bypassed", bypassed, err))
                return err;
            return command_result_to_json(c.api.set_node_bypassed(node_id, bypassed));
        }},
        {"inspect", [](DispatchContext& c, const std::string&,
                       const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            return command_result_to_json(c.api.inspect(node_id));
        }},
        {"list_nodes", [](DispatchContext& c, const std::string&,
                          const nlohmann::json&, bool) -> std::string {
            return command_result_to_json(c.api.list_nodes());
        }},
        {"remove_midi_mapping", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, param, err;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "param", param, err))
                return err;
            return command_result_to_json(c.api.remove_midi_mapping(node_id, param));
        }},
        {"update_midi_mapping", [](DispatchContext& c, const std::string&,
                                   const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, param, err;
            float range_min = 0.0f, range_max = 0.0f;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "param", param, err) ||
                !require_float(root, "range_min", range_min, err) ||
                !require_float(root, "range_max", range_max, err))
                return err;
            return command_result_to_json(
                c.api.update_midi_mapping(node_id, param, range_min, range_max));
        }},
        {"add_mod_assignment", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, source, destination, err;
            float amount = 0.0f;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "source", source, err) ||
                !require_string(root, "destination", destination, err) ||
                !require_float(root, "amount", amount, err))
                return err;
            std::string polarity = optional_string(root, "polarity", "unipolar");
            std::string curve = optional_string(root, "curve", "linear");
            return command_result_to_json(
                c.api.add_mod_assignment(node_id, source, destination, amount, polarity, curve));
        }},
        {"remove_mod_assignment", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, source, destination, err;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "source", source, err) ||
                !require_string(root, "destination", destination, err))
                return err;
            return command_result_to_json(
                c.api.remove_mod_assignment(node_id, source, destination));
        }},
        {"update_mod_assignment", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, source, destination, err;
            float amount = 0.0f;
            if (!require_string(root, "node_id", node_id, err) ||
                !require_string(root, "source", source, err) ||
                !require_string(root, "destination", destination, err) ||
                !require_float(root, "amount", amount, err))
                return err;
            std::string polarity = optional_string(root, "polarity", "unipolar");
            std::string curve = optional_string(root, "curve", "linear");
            return command_result_to_json(
                c.api.update_mod_assignment(node_id, source, destination, amount, polarity, curve));
        }},
        {"list_mod_sources", [](DispatchContext& c, const std::string&,
                                const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            std::string node_id, err;
            if (!require_string(root, "node_id", node_id, err)) return err;
            auto r = c.api.list_mod_sources(node_id);
            return r.ok ? r.message : json_err(r.message);
        }},

        // --- Migrated batch 04-R2-F1 (final: packages, scaffold, session/sticky, assets) ---
        {"rescan_operators", [](DispatchContext& c, const std::string&,
                                const nlohmann::json&, bool) -> std::string {
            int newly = c.registry.rescan();
            nlohmann::json j;
            j["ok"] = true;
            j["newly_registered"] = newly;
            return j.dump();
        }},
        {"scaffold_operator", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json& root, bool root_valid) -> std::string {
            if (c.src_dir.empty())
                return json_err("scaffold_operator requires --src-dir");
            if (!root_valid)
                return json_err("invalid JSON body");

            if (!root.contains("name") || !root["name"].is_string())
                return json_err("missing 'name'");
            if (!root.contains("kind") || !root["kind"].is_string())
                return json_err("missing 'kind'");

            std::string name = root["name"].get<std::string>();
            std::string kind_str_val = root["kind"].get<std::string>();

            VividOperatorKind kind;
            if (kind_str_val == "control")      kind = VIVID_OP_CONTROL;
            else if (kind_str_val == "audio")   kind = VIVID_OP_AUDIO;
            else if (kind_str_val == "gpu")     kind = VIVID_OP_GPU;
            else return json_err("kind must be 'control', 'audio', or 'gpu'");

            // Optional variant (e.g. "child_op")
            std::string variant;
            if (root.contains("variant") && root["variant"].is_string())
                variant = root["variant"].get<std::string>();

            std::string destination = "auto";
            if (root.contains("destination") && root["destination"].is_string())
                destination = root["destination"].get<std::string>();

            std::string err = OperatorCreator::validate_name(name, c.registry);
            if (!err.empty()) return json_err(err);

            std::vector<PackageInfo> packages =
                c.package_manager ? c.package_manager->list() : std::vector<PackageInfo>{};

            // For an explicit "project" destination with no workspace package
            // yet, auto-create one beside the saved graph then route into it.
            // Freshly-linked packages get source_scope = "user" (not
            // "workspace"), so select_workspace_project_package would reject
            // them; substituting the absolute path lets find_package_by_path
            // match it in resolve_operator_destination.
            //
            // "auto" is intentionally excluded: when no project package exists
            // resolve_operator_destination falls back to core with a warning,
            // which is the desired behaviour for the auto destination.
            const bool auto_core_mode = c.settings &&
                c.settings->operator_clone_destination_mode == "core_explicit";
            if (destination == "project" && !auto_core_mode && c.package_manager &&
                !select_workspace_project_package(packages)) {
                auto created = vivid::ensure_project_package(*c.package_manager, c.graph);
                if (!created.first.empty()) {
                    std::fprintf(stderr,
                        "[vivid] Auto-created project package at %s for new operator '%s'\n",
                        created.first.c_str(), name.c_str());
                    packages = c.package_manager->list();
                    destination = created.first;  // route to the new package by absolute path
                }
                // If creation failed (no saved graph etc.), fall through to
                // resolve_operator_destination which will surface the
                // appropriate warning / fallback to core.
            }

            OperatorDestination resolved;
            std::string resolve_error;
            if (!resolve_operator_destination(destination, c.src_dir, packages, c.settings,
                                              resolved, resolve_error)) {
                return json_err(resolve_error);
            }
            if (!resolved.warning.empty()) {
                std::fprintf(stderr, "[vivid] %s\n", resolved.warning.c_str());
            }

            VividCreateOperatorRequest req;
            req.name = name;
            req.kind = kind;
            req.variant = variant;
            req.destination = destination;

            auto parse_port_type = [&](const std::string& type_str, VividOperatorKind k, VividPortType& out) -> std::string {
                if (k == VIVID_OP_CONTROL) {
                    if      (type_str == "float")         out = VIVID_PORT_SCALAR;
                    else if (type_str == "int")           out = VIVID_PORT_SCALAR;
                    else if (type_str == "bool")          out = VIVID_PORT_SCALAR;
                    // Legacy many-port vocabulary (lane port types retired, 7d.5e):
                    // accepted for back-compat, mapped to the payload type. Multiplicity
                    // is now declared via the port's .multiplicity, not the type string.
                    else if (type_str == "lane_array")    out = VIVID_PORT_SCALAR;
                    else if (type_str == "string")        out = VIVID_PORT_STRING;
                    else if (type_str == "string_values") out = VIVID_PORT_STRING;
                    else if (type_str == "string_lanes")  out = VIVID_PORT_STRING;  // legacy alias
                    else return "unknown control port type '" + type_str + "'";
                } else if (k == VIVID_OP_AUDIO) {
                    if (type_str == "float") out = VIVID_PORT_AUDIO_BUFFER;
                    else return "unknown audio port type '" + type_str + "'";
                } else if (k == VIVID_OP_GPU) {
                    if (type_str == "texture") out = VIVID_PORT_TEXTURE;
                    else return "unknown GPU port type '" + type_str + "'";
                }
                return {};
            };

            auto parse_ports = [&](const char* key, VividPortDirection dir) -> std::string {
                if (!root.contains(key)) return {};
                const auto& arr = root[key];
                if (!arr.is_array()) return std::string(key) + " must be an array";
                for (size_t idx = 0; idx < arr.size(); ++idx) {
                    const auto& elem = arr[idx];
                    if (!elem.contains("name") || !elem["name"].is_string())
                        return std::string(key) + "[" + std::to_string(idx) + "] missing 'name'";
                    std::string ptype = "float";
                    if (elem.contains("type") && elem["type"].is_string()) ptype = elem["type"].get<std::string>();
                    VividPortType vt;
                    std::string perr = parse_port_type(ptype, kind, vt);
                    if (!perr.empty()) return perr;
                    req.ports.push_back({elem["name"].get<std::string>(), vt, dir});
                }
                return {};
            };

            std::string port_err = parse_ports("inputs", VIVID_PORT_INPUT);
            if (!port_err.empty()) return json_err(port_err);
            port_err = parse_ports("outputs", VIVID_PORT_OUTPUT);
            if (!port_err.empty()) return json_err(port_err);

            if (root.contains("params")) {
                const auto& params_arr = root["params"];
                if (!params_arr.is_array())
                    return json_err("params must be an array");
                for (size_t pidx = 0; pidx < params_arr.size(); ++pidx) {
                    const auto& pelem = params_arr[pidx];
                    if (!pelem.contains("name") || !pelem["name"].is_string())
                        return json_err("params[" + std::to_string(pidx) + "] missing 'name'");
                    VividParamSpec ps;
                    ps.name = pelem["name"].get<std::string>();
                    if (pelem.contains("type") && pelem["type"].is_string()) {
                        std::string ts = pelem["type"].get<std::string>();
                        if      (ts == "float") ps.type = VIVID_PARAM_FLOAT;
                        else if (ts == "int")   ps.type = VIVID_PARAM_INT;
                        else if (ts == "bool")  ps.type = VIVID_PARAM_BOOL;
                        else if (ts == "file")  ps.type = VIVID_PARAM_FILE;
                        else if (ts == "text")  ps.type = VIVID_PARAM_TEXT;
                        else return json_err("unknown param type '" + ts + "'");
                    }
                    if (pelem.contains("default") && pelem["default"].is_number()) ps.default_value = pelem["default"].get<float>();
                    if (pelem.contains("min") && pelem["min"].is_number()) ps.min_value = pelem["min"].get<float>();
                    if (pelem.contains("max") && pelem["max"].is_number()) ps.max_value = pelem["max"].get<float>();
                    if (pelem.contains("default_string") && pelem["default_string"].is_string()) ps.default_string = pelem["default_string"].get<std::string>();
                    req.params.push_back(ps);
                }
            }

            auto cr = OperatorCreator::create(req, resolved.root, resolved.package_layout);
            if (!cr.success) return json_err(cr.error);

            // A new .cpp was written into a package directory; poke the host
            // so the file-watcher picks it up without waiting for the safety-net
            // periodic rescan.
            if (resolved.package_layout && c.package_manager)
                c.package_manager->notify_watchers_changed();

            if (c.hot_reloader) {
                if (resolved.package_layout && !resolved.package_name.empty())
                    c.hot_reloader->queue_rebuild("pkg:" + resolved.package_name + ":" + cr.target_name);
                else
                    c.hot_reloader->queue_rebuild(cr.target_name);
            }

            OperatorCreator::open_in_editor(cr.cpp_path);

            nlohmann::json res = nlohmann::json::object();
            res["cpp_path"] = cr.cpp_path;
            res["target_name"] = cr.target_name;
            res["destination_root"] = resolved.root;
            res["destination_is_package"] = resolved.package_layout;
            if (!resolved.package_name.empty())
                res["destination_package"] = resolved.package_name;
            if (!resolved.warning.empty())
                res["destination_warning"] = resolved.warning;
            return json_ok(std::move(res));
        }},
        {"install_package", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.package_manager) {
                return json_err("package manager not available");
            } else if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                if (!root.contains("url") || !root["url"].is_string())
                    return json_err("missing 'url'");
                else {
                    auto ir = c.package_manager->install(root["url"].get<std::string>());
                    if (ir.success) {
                        nlohmann::json res = nlohmann::json::object();
                        res["name"] = ir.info.name;
                        res["version"] = ir.info.version;
                        if (!ir.info.vivid_core.empty())
                            res["vivid_core"] = ir.info.vivid_core;
                        res["operator_count"] = static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size());
                        std::string result = json_ok(std::move(res));
                        // Auto-reload graph if it has missing operators
                        if (const auto* cg = c.core.compiled_graph()) {
                            for (const auto& cn : cg->nodes) {
                                if (cn.missing_operator) {
                                    auto rr = c.api.reload(c.has_gpu_ops, c.has_audio);
                                    if (!rr.ok) {
                                        result = json_err("package installed but runtime refresh failed: " + rr.message);
                                    }
                                    break;
                                }
                            }
                        }
                        return result;
                    } else {
                        return json_err(ir.error);
                    }
                }
            }
        }},
        {"uninstall_package", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.package_manager) {
                return json_err("package manager not available");
            } else if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                if (!root.contains("name") || !root["name"].is_string())
                    return json_err("missing 'name'");
                else {
                    std::string snapshot_json;
                    std::string snapshot_error;
                    if (!capture_live_graph_snapshot(c.graph, snapshot_json, snapshot_error)) {
                        return json_err(snapshot_error);
                    }
                    auto rm = c.package_manager->uninstall(root["name"].get<std::string>());
                    if (rm.success) {
                        c.source_docs.invalidate_package(root["name"].get<std::string>());
                        auto rr = c.api.apply_snapshot_json(snapshot_json, c.has_gpu_ops, c.has_audio);
                        if (!rr.ok)
                            return json_err("package uninstalled but runtime refresh failed: " + rr.message);
                        else
                            return json_ok_msg("uninstalled");
                    } else {
                        return json_err(rm.error.empty() ? "failed to uninstall package" : rm.error);
                    }
                }
            }
        }},
        {"link_package", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.package_manager) {
                return json_err("package manager not available");
            } else if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                if (!root.contains("path") || !root["path"].is_string())
                    return json_err("missing 'path'");
                else {
                    auto ir = c.package_manager->link(root["path"].get<std::string>());
                    if (ir.success) {
                        c.source_docs.invalidate_package(ir.info.name, ir.info.path);
                        nlohmann::json res = nlohmann::json::object();
                        res["name"] = ir.info.name;
                        res["version"] = ir.info.version;
                        if (!ir.info.vivid_core.empty())
                            res["vivid_core"] = ir.info.vivid_core;
                        res["operator_count"] = static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size());
                        res["linked"] = true;
                        std::string result = json_ok(std::move(res));
                        // Auto-reload graph if it has missing operators
                        if (const auto* cg = c.core.compiled_graph()) {
                            for (const auto& cn : cg->nodes) {
                                if (cn.missing_operator) {
                                    auto rr = c.api.reload(c.has_gpu_ops, c.has_audio);
                                    if (!rr.ok) {
                                        result = json_err("package linked but runtime refresh failed: " + rr.message);
                                    }
                                    break;
                                }
                            }
                        }
                        return result;
                    } else {
                        return json_err(ir.error);
                    }
                }
            }
        }},
        {"unlink_package", [](DispatchContext& c, const std::string&,
                              const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.package_manager) {
                return json_err("package manager not available");
            } else if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                if (!root.contains("name") || !root["name"].is_string())
                    return json_err("missing 'name'");
                else {
                    std::string snapshot_json;
                    std::string snapshot_error;
                    if (!capture_live_graph_snapshot(c.graph, snapshot_json, snapshot_error)) {
                        return json_err(snapshot_error);
                    }
                    auto rm = c.package_manager->unlink(root["name"].get<std::string>());
                    if (rm.success) {
                        c.source_docs.invalidate_package(root["name"].get<std::string>());
                        auto rr = c.api.apply_snapshot_json(snapshot_json, c.has_gpu_ops, c.has_audio);
                        if (!rr.ok)
                            return json_err("package unlinked but runtime refresh failed: " + rr.message);
                        else
                            return json_ok_msg("unlinked");
                    } else {
                        return json_err(rm.error.empty() ? "failed to unlink package" : rm.error);
                    }
                }
            }
        }},
        {"rebuild_package", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.package_manager) {
                return json_err("package manager not available");
            } else if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                if (!root.contains("name") || !root["name"].is_string())
                    return json_err("missing 'name'");
                else {
                    const std::string pkg_name = root["name"].get<std::string>();
                    std::string snapshot_json;
                    std::string snapshot_error;
                    if (!capture_live_graph_snapshot(c.graph, snapshot_json, snapshot_error)) {
                        return json_err(snapshot_error);
                    } else {
                        auto ir = c.package_manager->rebuild(pkg_name);
                        if (ir.success) {
                            c.source_docs.invalidate_package(pkg_name,
                                c.package_manager->resolve_package_path(pkg_name));
                            std::error_code build_ec;
                            std::string pkg_path = c.package_manager->resolve_package_path(pkg_name);
                            if (!pkg_path.empty()) {
                                std::string build_root = std::filesystem::canonical(pkg_path, build_ec).string();
                                if (build_ec || build_root.empty())
                                    build_root = pkg_path;
                                c.registry.clear_deferred_probe_handles_for_dir(build_root + "/build");
                            }
                            auto rr = c.api.apply_snapshot_json(snapshot_json, c.has_gpu_ops, c.has_audio);
                            if (!rr.ok) {
                                return json_err("package rebuilt but runtime refresh failed: " + rr.message);
                            } else {
                                c.core.tick(0.0, 0.016, 0);
                                nlohmann::json res = {
                                    {"name", ir.info.name},
                                    {"operator_count", static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size())},
                                    {"linked", ir.info.linked}
                                };
                                return json_ok(std::move(res));
                            }
                        } else {
                            return json_err(ir.error);
                        }
                    }
                }
            }
        }},
        {"list_packages", [](DispatchContext& c, const std::string&,
                             const nlohmann::json&, bool) -> std::string {
            return handle_list_packages(c.package_manager);
        }},
        {"read_package_docs", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            else return handle_read_package_docs(c.package_manager, root);
        }},
        {"list_package_examples", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            else return handle_list_package_examples(c.package_manager, root);
        }},
        {"read_package_example", [](DispatchContext& c, const std::string&,
                                    const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            else return handle_read_package_example(c.package_manager, root);
        }},
        {"operator_docs", [](DispatchContext& c, const std::string&,
                             const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                return handle_operator_docs(c.registry, c.package_manager, c.source_docs, root, c.core.subgraph_modules());
            }
        }},
        {"package_operator_docs", [](DispatchContext& c, const std::string&,
                                     const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) return json_err("invalid JSON body");
            else return handle_package_operator_docs(c.registry, c.package_manager, c.source_docs, root);
        }},
        {"test_package", [](DispatchContext& c, const std::string&,
                            const nlohmann::json& root, bool root_valid) -> std::string {
            if (!c.package_manager || !c.package_compiler) {
                return json_err("package manager/compiler not available");
            } else if (!root_valid) {
                return json_err("invalid JSON body");
            } else {
                if (!root.contains("name") || !root["name"].is_string())
                    return json_err("missing 'name'");
                else {
                    std::string name = root["name"].get<std::string>();
                    auto tr = run_package_tests(name, *c.package_manager,
                                                 *c.package_compiler, c.registry);
                    if (!tr.error.empty()) {
                        return json_err(tr.error);
                    } else {
                        nlohmann::json res = nlohmann::json::object();
                        res["package"] = tr.package_name;
                        res["summary"] = {{"total", tr.total}, {"passed", tr.passed}, {"failed", tr.failed}, {"skipped", tr.skipped}};
                        if (!tr.notes.empty()) res["notes"] = tr.notes;
                        nlohmann::json tests_arr = nlohmann::json::array();
                        for (const auto& t : tr.tests) {
                            nlohmann::json obj = nlohmann::json::object();
                            obj["name"] = t.name;
                            obj["type"] = t.type;
                            obj["status"] = t.status;
                            if (!t.code.empty()) obj["code"] = t.code;
                            if (!t.reason.empty()) obj["reason"] = t.reason;
                            if (!t.output.empty()) obj["output"] = t.output;
                            tests_arr.push_back(std::move(obj));
                        }
                        res["tests"] = std::move(tests_arr);
                        return json_ok(std::move(res));
                    }
                }
            }
        }},
        {"set_solo", [](DispatchContext& c, const std::string&,
                        const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) { return json_err("invalid JSON body"); }
            else {
                std::string node_id_str;
                if (root.contains("node_id") && root["node_id"].is_string())
                    node_id_str = root["node_id"].get<std::string>();
                return command_result_to_json(c.api.set_solo(node_id_str));
            }
        }},
        {"get_solo", [](DispatchContext& c, const std::string&,
                        const nlohmann::json&, bool) -> std::string {
            std::string solo_id = c.api.solo_node_id();
            return nlohmann::json{{"ok", true}, {"node_id", solo_id}, {"active", !solo_id.empty()}}.dump();
        }},
        {"add_sticky_note", [](DispatchContext& c, const std::string&,
                               const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) { return json_err("invalid JSON body"); }
            else {
                if (!root.contains("text") || !root["text"].is_string() ||
                    !root.contains("x") || !root["x"].is_number() ||
                    !root.contains("y") || !root["y"].is_number())
                    return json_err("missing 'text', 'x', or 'y'");
                else {
                    StickyNoteDef note;
                    if (root.contains("id") && root["id"].is_string())
                        note.id = root["id"].get<std::string>();
                    else
                        note.id = "sticky_" + std::to_string(c.graph.sticky_notes().size() + 1);
                    note.text = root["text"].get<std::string>();
                    note.x = root["x"].get<float>();
                    note.y = root["y"].get<float>();
                    if (root.contains("width") && root["width"].is_number()) note.width = root["width"].get<float>();
                    if (root.contains("height") && root["height"].is_number()) note.height = root["height"].get<float>();
                    if (root.contains("color") && root["color"].is_number_integer()) note.color = root["color"].get<int>();
                    std::string assigned_id = note.id;
                    c.graph.add_sticky_note(std::move(note));
                    return json_ok(nlohmann::json{{"id", assigned_id}});
                }
            }
        }},
        {"list_sticky_notes", [](DispatchContext& c, const std::string&,
                                 const nlohmann::json&, bool) -> std::string {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& sn : c.graph.sticky_notes()) {
                arr.push_back({
                    {"id", sn.id}, {"text", sn.text},
                    {"x", static_cast<double>(sn.x)}, {"y", static_cast<double>(sn.y)},
                    {"width", static_cast<double>(sn.width)}, {"height", static_cast<double>(sn.height)},
                    {"color", sn.color}
                });
            }
            return json_ok(std::move(arr));
        }},
        {"update_sticky_note", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) { return json_err("invalid JSON body"); }
            else {
                if (!root.contains("id") || !root["id"].is_string())
                    return json_err("missing 'id'");
                else {
                    auto* sn = c.graph.find_sticky_note(root["id"].get<std::string>());
                    if (!sn)
                        return json_err("sticky note not found");
                    else {
                        if (root.contains("text") && root["text"].is_string()) sn->text = root["text"].get<std::string>();
                        if (root.contains("x") && root["x"].is_number()) sn->x = root["x"].get<float>();
                        if (root.contains("y") && root["y"].is_number()) sn->y = root["y"].get<float>();
                        if (root.contains("width") && root["width"].is_number()) sn->width = root["width"].get<float>();
                        if (root.contains("height") && root["height"].is_number()) sn->height = root["height"].get<float>();
                        if (root.contains("color") && root["color"].is_number_integer()) sn->color = root["color"].get<int>();
                        return json_ok_msg("updated");
                    }
                }
            }
        }},
        {"remove_sticky_note", [](DispatchContext& c, const std::string&,
                                  const nlohmann::json& root, bool root_valid) -> std::string {
            if (!root_valid) { return json_err("invalid JSON body"); }
            else {
                if (!root.contains("id") || !root["id"].is_string())
                    return json_err("missing 'id'");
                else {
                    bool removed = c.graph.remove_sticky_note(root["id"].get<std::string>());
                    if (removed) return json_ok_msg("removed");
                    else return json_err("sticky note not found");
                }
            }
        }},
        {"list_assets", [](DispatchContext& c, const std::string& body,
                           const nlohmann::json&, bool) -> std::string {
            if (!c.asset_library) return json_err("asset library not available");
            auto root = body.empty() ? nlohmann::json::object() : nlohmann::json::parse(body, nullptr, false);
            if (root.is_discarded()) root = nlohmann::json::object();
            return handle_list_assets(*c.asset_library, root);
        }},
        {"inspect_asset", [](DispatchContext& c, const std::string& body,
                             const nlohmann::json&, bool) -> std::string {
            if (!c.asset_library) return json_err("asset library not available");
            auto root = nlohmann::json::parse(body, nullptr, false);
            if (root.is_discarded()) return json_err("invalid JSON body");
            return handle_inspect_asset(*c.asset_library, root);
        }},
        {"import_asset", [](DispatchContext& c, const std::string& body,
                            const nlohmann::json&, bool) -> std::string {
            if (!c.asset_library) return json_err("asset library not available");
            auto root = nlohmann::json::parse(body, nullptr, false);
            if (root.is_discarded()) return json_err("invalid JSON body");
            return handle_import_asset(*c.asset_library, root);
        }},
        {"refresh_assets", [](DispatchContext& c, const std::string&,
                              const nlohmann::json&, bool) -> std::string {
            if (!c.asset_library) return json_err("asset library not available");
            return handle_refresh_assets(*c.asset_library);
        }},
    };
    return kHandlers;
}

std::string dispatch(const std::string& method, const std::string& body,
                     RuntimeAPI& api, Graph& graph,
                     RuntimeCore& core, OperatorRegistry& registry,
                     bool& has_gpu_ops, bool& has_audio,
                     HotReloader* hot_reloader,
                     const std::string& src_dir,
                     OperatorSourceDocs& source_docs,
                     SourceIndex& source_index,
                     PackageManager* package_manager,
                     PackageCompiler* package_compiler,
                     Settings* settings,
                     AudioEngine* audio_engine,
                     AssetLibrary* asset_library,
                     BuildConsole* build_console,
                     GpuContext* gpu_context,
                     PackageCatalog* package_catalog,
                     const ControlServer* control_server,
                     CrashRecoveryManager* crash_recovery_manager,
                     EditorWindowManager* editor_window_manager) {
    DispatchContext c{api, graph, core, registry, has_gpu_ops, has_audio, hot_reloader, src_dir,
                      source_docs, source_index, package_manager, package_compiler, settings,
                      audio_engine, asset_library, build_console, gpu_context, package_catalog,
                      control_server, crash_recovery_manager, editor_window_manager};

    nlohmann::json root;
    bool root_valid = false;
    try { root = nlohmann::json::parse(body); root_valid = true; } catch (...) {}

    auto it = handler_table().find(method);
    if (it != handler_table().end())
        return it->second(c, body, root, root_valid);

    return json_err("unknown method '" + method + "'");
}

} // namespace vivid
