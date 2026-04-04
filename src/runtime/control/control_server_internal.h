#pragma once

#include "runtime/assets/asset_library.h"
#include "runtime/control/control_server.h"
#include "runtime/control/control_server_checks.h"
#include "runtime/graph/subgraph_module.h"
#include "ui/graph/graph_snapshot.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_loader.h"
#include "runtime/operators/operator_creator.h"
#include "runtime/operators/operator_source_docs.h"
#include "runtime/core/hot_reload.h"
#include "runtime/core/undo_manager.h"
#include "runtime/packages/package_manager.h"
#include "runtime/packages/package_compiler.h"
#include "runtime/packages/package_test_runner.h"
#include "runtime/packages/package_catalog.h"
#include "runtime/platform/app_update_manager.h"
#include "runtime/core/settings.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/operator_destination_policy.h"
#include "operator_api/types.h"
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXHttpServer.h>
#include <cassert>
#include <deque>
#include <future>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <thread>
#include <chrono>


namespace vivid {

class SubgraphModuleRegistry;

// ---------------------------------------------------------------------------
// Timeout constants (seconds)
// ---------------------------------------------------------------------------
inline constexpr int kCaptureTimeoutSec           = 5;
inline constexpr int kInterfaceCaptureTimeoutSec   = 10;
inline constexpr int kRecordingTimeoutSec          = 10;
inline constexpr int kAnalysisTimeoutSec           = 10;
inline constexpr int kDefaultDispatchTimeoutSec    = 5;
inline constexpr int kSampleNodeOutputsTimeoutSec  = 30;
inline constexpr int kTestPackageTimeoutSec        = 60;

// ---------------------------------------------------------------------------
// Enum → string helpers
// ---------------------------------------------------------------------------

inline const char* lane_behavior_str(VividLaneBehavior lb) {
    switch (lb) {
        case VIVID_LANE_POINTWISE:  return "pointwise";
        case VIVID_LANE_STRUCTURAL: return "structural";
        case VIVID_LANE_REDUCTION:  return "reduction";
        case VIVID_LANE_KERNEL:     return "kernel";
        default: return "unknown";
    }
}

inline const char* lane_behavior_help_str(VividLaneBehavior lb) {
    switch (lb) {
        case VIVID_LANE_POINTWISE:
            return "Processes each lane independently and preserves per-lane structure. "
                   "Use this in poly chains when you want one stateful copy per note or lane.";
        case VIVID_LANE_STRUCTURAL:
            return "Creates, reorders, or reshapes lane structure. "
                   "Use this to generate or transform polyphonic note/gate lane arrays.";
        case VIVID_LANE_REDUCTION:
            return "Consumes multiple lanes and collapses them into fewer outputs. "
                   "Use this when summing or mixing voices back to a smaller channel count.";
        case VIVID_LANE_KERNEL:
            return "Processes neighborhoods of lanes together. "
                   "Use this for cross-lane operations that depend on nearby lane values.";
        default:
            return "Lane behavior is unknown.";
    }
}

inline const char* kind_str(VividOperatorKind k) {
    switch (k) {
        case VIVID_OP_CONTROL: return "control";
        case VIVID_OP_AUDIO:   return "audio";
        case VIVID_OP_GPU:     return "gpu";
        default: return "unknown";
    }
}

inline const char* param_type_str(VividParamType t) {
    switch (t) {
        case VIVID_PARAM_FLOAT: return "float";
        case VIVID_PARAM_INT:   return "int";
        case VIVID_PARAM_BOOL:  return "bool";
        case VIVID_PARAM_FILE:  return "file";
        case VIVID_PARAM_TEXT:  return "text";
        default: return "unknown";
    }
}

inline const char* port_type_str(VividPortType t) {
    switch (t) {
        case VIVID_PORT_SCALAR:         return "float";
        case VIVID_PORT_AUDIO_BUFFER:         return "audio";
        case VIVID_PORT_LANE_ARRAY:        return "lane_array";
        case VIVID_PORT_STRING:        return "string";
        case VIVID_PORT_STRING_LANES: return "string_lanes";
        case VIVID_PORT_TEXTURE:       return "texture";
        default:
            if (vivid_is_custom_port_type(t)) return "custom";
            return "unknown";
    }
}

inline const char* transport_str(VividPortTransport t) {
    switch (t) {
        case VIVID_PORT_TRANSPORT_SIGNAL:        return "scalar";
        case VIVID_PORT_TRANSPORT_AUDIO_BUFFER:  return "audio_buffer";
        case VIVID_PORT_TRANSPORT_LANE_ARRAY:        return "lane_array";
        case VIVID_PORT_TRANSPORT_STRING:        return "string";
        case VIVID_PORT_TRANSPORT_STRING_LANES: return "string_lanes";
        case VIVID_PORT_TRANSPORT_TEXTURE:       return "texture";
        case VIVID_PORT_TRANSPORT_CUSTOM_VALUE:  return "custom_value";
        case VIVID_PORT_TRANSPORT_CUSTOM_REF:    return "custom_ref";
        default: return "unknown";
    }
}

inline void add_port_registry_metadata(nlohmann::json& port_obj,
                                       const VividPortDescriptor& pd) {
    if (!vivid_is_custom_port_type(pd.type)) return;

    VividPortTypeInfo info{};
    const bool registered = vivid_lookup_port_type(pd.type, &info) == 1;
    port_obj["custom_type_registered"] = registered;
    if (!registered) return;

    port_obj["audio_safe"] = (info.audio_safe != 0);
    if (info.package_name && *info.package_name)
        port_obj["registry_package_name"] = info.package_name;
    if (info.description && *info.description)
        port_obj["registry_description"] = info.description;
}

inline void add_port_descriptor_metadata(nlohmann::json& port_obj,
                                         const VividPortDescriptor& pd) {
    if (pd.channels > 0)
        port_obj["channels"] = pd.channels;
    if (pd.direction == VIVID_PORT_INPUT && pd.type == VIVID_PORT_SCALAR)
        port_obj["default"] = static_cast<double>(pd.default_value);
    if (pd.semantic_tag && *pd.semantic_tag)
        port_obj["semantic_tag"] = pd.semantic_tag;
    if (pd.semantic_shape && *pd.semantic_shape)
        port_obj["semantic_shape"] = pd.semantic_shape;
    if (pd.semantic_intent && *pd.semantic_intent)
        port_obj["semantic_intent"] = pd.semantic_intent;
    if (pd.description && *pd.description)
        port_obj["description"] = pd.description;
    add_port_registry_metadata(port_obj, pd);
}

inline nlohmann::json build_param_descriptor_json(const VividParamDescriptor& pd) {
    nlohmann::json p = nlohmann::json::object();
    p["name"] = pd.name;
    p["type"] = param_type_str(pd.type);
    p["default"] = static_cast<double>(pd.default_value);
    p["min"] = static_cast<double>(pd.min_value);
    p["max"] = static_cast<double>(pd.max_value);
    if (pd.semantic_tag && *pd.semantic_tag)
        p["semantic_tag"] = pd.semantic_tag;
    if (pd.semantic_shape && *pd.semantic_shape)
        p["semantic_shape"] = pd.semantic_shape;
    if (pd.semantic_unit && *pd.semantic_unit)
        p["semantic_unit"] = pd.semantic_unit;
    if (pd.semantic_intent && *pd.semantic_intent)
        p["semantic_intent"] = pd.semantic_intent;
    if (pd.description && *pd.description)
        p["description"] = pd.description;
    if (pd.default_string && *pd.default_string)
        p["default_string"] = pd.default_string;
    if (pd.group && *pd.group)
        p["group"] = pd.group;
    if (pd.choice_count > 0 && pd.choice_labels) {
        nlohmann::json choices = nlohmann::json::array();
        for (uint32_t c = 0; c < pd.choice_count; ++c)
            choices.push_back(pd.choice_labels[c]);
        p["choices"] = std::move(choices);
    }
    return p;
}

inline nlohmann::json build_port_descriptor_json(const VividPortDescriptor& pd) {
    nlohmann::json p = nlohmann::json::object();
    p["name"] = pd.name;
    p["type"] = port_type_str(pd.type);
    p["transport"] = transport_str(pd.transport);
    if (pd.type_name && *pd.type_name)
        p["type_name"] = pd.type_name;
    if (pd.stable_type_id && *pd.stable_type_id)
        p["stable_type_id"] = pd.stable_type_id;
    if (pd.payload_size > 0)
        p["payload_size"] = pd.payload_size;
    add_port_descriptor_metadata(p, pd);
    return p;
}

inline void merge_doc_text_field(nlohmann::json& out, const nlohmann::json& doc,
                                 const char* target_key, const char* source_key) {
    if (doc.contains(source_key) && doc[source_key].is_string() && !doc[source_key].get<std::string>().empty())
        out[target_key] = doc[source_key];
}

inline std::unordered_map<std::string, nlohmann::json> index_docs_by_name(const nlohmann::json& arr) {
    std::unordered_map<std::string, nlohmann::json> out;
    if (!arr.is_array()) return out;
    for (const auto& item : arr) {
        if (!item.contains("name") || !item["name"].is_string()) continue;
        out[item["name"].get<std::string>()] = item;
    }
    return out;
}

inline nlohmann::json resolve_operator_source_doc(OperatorSourceDocs& source_docs,
                                                  OperatorRegistry& registry,
                                                  PackageManager* package_manager,
                                                  const std::string& type_name,
                                                  const std::string& forced_package = "") {
    std::string package_name = forced_package;
    if (package_name.empty()) {
        if (const auto* pkg = registry.package_for_type(type_name))
            package_name = *pkg;
    }

    if (!package_name.empty() && package_manager && package_manager->is_installed(package_name)) {
        return source_docs.resolve_package(package_name,
                                           package_manager->resolve_package_path(package_name),
                                           type_name);
    }
    return source_docs.resolve_core(type_name);
}

inline nlohmann::json build_operator_docs_response(const VividOperatorDescriptor& desc,
                                                   const nlohmann::json* doc,
                                                   const std::string& package_name = "") {
    nlohmann::json op = nlohmann::json::object();
    op["name"] = desc.name;
    op["kind"] = kind_str(vivid_operator_kind(&desc));
    op["time_dependent"] = (desc.time_dependent != 0);
    op["lane_behavior"] = lane_behavior_str(desc.lane_behavior);
    op["lane_behavior_help"] = lane_behavior_help_str(desc.lane_behavior);
    if (!package_name.empty())
        op["package"] = package_name;

    bool has_docs = false;
    std::unordered_map<std::string, nlohmann::json> param_docs;
    std::unordered_map<std::string, nlohmann::json> input_docs;
    std::unordered_map<std::string, nlohmann::json> output_docs;
    if (doc && doc->is_object()) {
        has_docs = doc->value("has_docs", false);
        if (doc->contains("brief") && (*doc)["brief"].is_string())
            op["brief"] = (*doc)["brief"];
        merge_doc_text_field(op, *doc, "body", "body");
        if (!op.contains("body"))
            merge_doc_text_field(op, *doc, "body", "description");
        if (!op.contains("source_path"))
            merge_doc_text_field(op, *doc, "source_path", "source_path");
        if (!op.contains("source_path"))
            merge_doc_text_field(op, *doc, "source_path", "source_file");
        for (const char* key : {"tips", "related", "recipes", "pitfalls", "best_used_with", "common_companions"}) {
            if (doc->contains(key) && (*doc)[key].is_array())
                op[key] = (*doc)[key];
        }
        if (doc->contains("operator_family") && (*doc)["operator_family"].is_string())
            op["operator_family"] = (*doc)["operator_family"];
        param_docs = index_docs_by_name(doc->value("params", nlohmann::json::array()));
        input_docs = index_docs_by_name(doc->value("inputs", nlohmann::json::array()));
        output_docs = index_docs_by_name(doc->value("outputs", nlohmann::json::array()));
    }
    op["has_docs"] = has_docs;

    nlohmann::json params_arr = nlohmann::json::array();
    for (uint32_t i = 0; i < desc.param_count; ++i) {
        const auto& pd = desc.params[i];
        nlohmann::json p = build_param_descriptor_json(pd);
        auto it = param_docs.find(pd.name);
        if (it != param_docs.end() && it->second.contains("doc") && it->second["doc"].is_string())
            p["doc"] = it->second["doc"];
        params_arr.push_back(std::move(p));
    }
    op["params"] = std::move(params_arr);

    nlohmann::json inputs_arr = nlohmann::json::array();
    nlohmann::json outputs_arr = nlohmann::json::array();
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        const auto& pd = desc.ports[i];
        nlohmann::json p = build_port_descriptor_json(pd);
        auto& doc_map = (pd.direction == VIVID_PORT_INPUT) ? input_docs : output_docs;
        auto it = doc_map.find(pd.name);
        if (it != doc_map.end() && it->second.contains("doc") && it->second["doc"].is_string())
            p["doc"] = it->second["doc"];
        if (pd.direction == VIVID_PORT_INPUT)
            inputs_arr.push_back(std::move(p));
        else
            outputs_arr.push_back(std::move(p));
    }
    op["inputs"] = std::move(inputs_arr);
    op["outputs"] = std::move(outputs_arr);
    return op;
}

// Build operator-docs-style JSON from a subgraph module definition + its
// synthetic OperatorInfo.  The output matches the shape of
// build_operator_docs_response so consumers need not distinguish.
inline nlohmann::json build_module_docs_response(const SubgraphModuleDef& mod,
                                                  const ui::OperatorInfo& info) {
    nlohmann::json op = nlohmann::json::object();
    op["name"] = mod.name;
    op["kind"] = "module";
    op["is_module"] = true;
    if (!mod.description.empty()) op["brief"] = mod.description;
    if (!mod.category.empty()) op["category"] = mod.category;
    op["has_docs"] = false;

    nlohmann::json params_arr = nlohmann::json::array();
    for (const auto& pi : info.params) {
        nlohmann::json p = nlohmann::json::object();
        p["name"] = pi.name;
        p["type"] = param_type_str(pi.type);
        p["default"] = static_cast<double>(pi.default_value);
        p["min"] = static_cast<double>(pi.min_value);
        p["max"] = static_cast<double>(pi.max_value);
        if (!pi.group.empty()) p["group"] = pi.group;
        if (!pi.description.empty()) p["description"] = pi.description;
        if (!pi.semantic_tag.empty()) p["semantic_tag"] = pi.semantic_tag;
        if (!pi.semantic_shape.empty()) p["semantic_shape"] = pi.semantic_shape;
        if (!pi.semantic_unit.empty()) p["semantic_unit"] = pi.semantic_unit;
        if (!pi.semantic_intent.empty()) p["semantic_intent"] = pi.semantic_intent;
        if (!pi.choice_labels.empty()) {
            nlohmann::json choices = nlohmann::json::array();
            for (const auto& c : pi.choice_labels)
                choices.push_back(c);
            p["choices"] = std::move(choices);
        }
        params_arr.push_back(std::move(p));
    }
    op["params"] = std::move(params_arr);

    nlohmann::json inputs_arr = nlohmann::json::array();
    nlohmann::json outputs_arr = nlohmann::json::array();
    for (const auto& pi : info.ports) {
        nlohmann::json p = nlohmann::json::object();
        p["name"] = pi.name;
        p["type"] = port_type_str(pi.type);
        if (pi.direction == VIVID_PORT_INPUT)
            inputs_arr.push_back(std::move(p));
        else
            outputs_arr.push_back(std::move(p));
    }
    op["inputs"] = std::move(inputs_arr);
    op["outputs"] = std::move(outputs_arr);

    // Include factory preset names if any
    if (!mod.presets.empty()) {
        nlohmann::json presets_arr = nlohmann::json::array();
        for (const auto& pr : mod.presets)
            presets_arr.push_back(pr.name);
        op["factory_presets"] = std::move(presets_arr);
    }

    return op;
}

inline const char* update_class_str(PackageUpdateClass c) {
    switch (c) {
        case PackageUpdateClass::UpToDate: return "up_to_date";
        case PackageUpdateClass::CompatibleUpdate: return "compatible_update";
        case PackageUpdateClass::IncompatibleUpdate: return "incompatible_update";
        case PackageUpdateClass::RemoteOlderOrEqual: return "remote_older_or_equal";
        case PackageUpdateClass::InvalidVersionData: return "invalid_version_data";
        default: return "unknown";
    }
}

// ---------------------------------------------------------------------------
// JSON response helpers
// ---------------------------------------------------------------------------

inline std::string json_ok(nlohmann::json result) {
    return nlohmann::json{{"ok", true}, {"result", std::move(result)}}.dump();
}

inline std::string json_ok_msg(const std::string& msg) {
    return nlohmann::json{{"ok", true}, {"message", msg}}.dump();
}

inline std::string json_err(const std::string& msg) {
    return nlohmann::json{{"ok", false}, {"error", msg}}.dump();
}

inline std::string command_result_to_json(const CommandResult& r) {
    return r.ok ? json_ok_msg(r.message) : json_err(r.message);
}

inline bool split_addr_local(const std::string& addr, std::string& node, std::string& port) {
    size_t slash = addr.find('/');
    if (slash == std::string::npos) return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

inline const ConnectionDef* find_connection_by_addr(const Graph& graph,
                                                    const std::string& from_addr,
                                                    const std::string& to_addr) {
    std::string fn, fp, tn, tp;
    if (!split_addr_local(from_addr, fn, fp) || !split_addr_local(to_addr, tn, tp))
        return nullptr;
    for (const auto& c : graph.connections()) {
        if (c.from_node == fn && c.from_port == fp && c.to_node == tn && c.to_port == tp)
            return &c;
    }
    return nullptr;
}

inline bool is_safe_package_name(const std::string& name) {
    return name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           name.find("..") == std::string::npos;
}

inline bool is_safe_recording_path(const std::string& path) {
    if (path.empty()) return false;
    // Must be absolute
    if (path[0] != '/') return false;
    // Must not contain .. components
    if (path.find("..") != std::string::npos) return false;
    // Must end with an allowed extension
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    return ext == ".mov" || ext == ".mp4";
}

inline bool is_safe_capture_image_path(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] != '/') return false;
    if (path.find("..") != std::string::npos) return false;
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    return ext == ".png";
}

inline bool response_is_ok(const std::string& response_json) {
    try {
        auto doc = nlohmann::json::parse(response_json);
        return doc.contains("ok") && doc["ok"].is_boolean() && doc["ok"].get<bool>();
    } catch (...) { return false; }
}

inline bool capture_live_graph_snapshot(Graph& graph, std::string& out_json,
                                        std::string& out_error) {
    if (!graph.save_to_string(out_json)) {
        out_error = "failed to serialize current graph before package mutation";
        return false;
    }
    return true;
}

inline bool is_undo_tracked_method(const std::string& method) {
    return method == "add_node" ||
           method == "remove_node" ||
           method == "connect" ||
           method == "disconnect" ||
           method == "set_connection_remap" ||
           method == "set_param" ||
           method == "set_string_param" ||
           method == "set_resolution" ||
           method == "set_node_layout" ||
           method == "add_midi_mapping" ||
           method == "remove_midi_mapping" ||
           method == "update_midi_mapping" ||
           method == "save_variation" ||
           method == "recall_variation" ||
           method == "remove_variation" ||
           method == "rename_variation" ||
           method == "duplicate_variation" ||
           method == "move_variation" ||
           method == "update_variation" ||
           method == "queue_variation" ||
           method == "set_quantize_clock" ||
           method == "set_analysis" ||
           method == "save_preset" ||
           method == "recall_preset" ||
           method == "update_preset" ||
           method == "remove_preset" ||
           method == "rename_preset" ||
           method == "set_param_lock" ||
           method == "set_state_preset" ||
           method == "remove_state_preset" ||
           method == "clear_state_presets" ||
           method == "add_sticky_note" ||
           method == "remove_sticky_note" ||
           method == "update_sticky_note" ||
           method == "load_graph";
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

// Query handlers (defined in control_server_query.cpp)
std::string handle_inspect_graph(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules = nullptr);
nlohmann::json sample_node_outputs_snapshot(const CompiledNode& ns, bool include_lanes);
std::string handle_sample_node_outputs(Graph& graph, RuntimeCore& core, const nlohmann::json& root);
std::string handle_introspect_nodes(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules = nullptr);
std::string handle_get_graph_load_diagnostics(const Graph& graph);
std::string handle_list_types(OperatorRegistry& registry, PackageManager* package_manager, OperatorSourceDocs& source_docs, const nlohmann::json& root, const SubgraphModuleRegistry* modules = nullptr);
std::string handle_operator_docs(OperatorRegistry& registry, PackageManager* package_manager, OperatorSourceDocs& source_docs, const nlohmann::json& root, const SubgraphModuleRegistry* modules = nullptr);
std::string handle_get_registry_diagnostics(OperatorRegistry& registry);

// Dispatch router (defined in control_server_dispatch.cpp)
std::string dispatch(const std::string& method, const std::string& body,
                            RuntimeAPI& api, Graph& graph,
                            RuntimeCore& core, OperatorRegistry& registry,
                            bool& has_gpu_ops, bool& has_audio,
                            HotReloader* hot_reloader,
                            const std::string& src_dir,
                            OperatorSourceDocs& source_docs,
                            PackageManager* package_manager,
                            PackageCompiler* package_compiler,
                            Settings* settings,
                            AudioEngine* audio_engine,
                            AssetLibrary* asset_library = nullptr);

// Asset library handlers (defined in control_server_assets.cpp)
std::string handle_list_assets(AssetLibrary& lib, const nlohmann::json& root);
std::string handle_inspect_asset(AssetLibrary& lib, const nlohmann::json& root);
std::string handle_import_asset(AssetLibrary& lib, const nlohmann::json& root);
std::string handle_refresh_assets(AssetLibrary& lib);

} // namespace vivid
