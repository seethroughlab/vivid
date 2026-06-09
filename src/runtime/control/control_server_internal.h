#pragma once

#include "runtime/assets/asset_library.h"
#include "runtime/control/control_server.h"
#include "runtime/control/control_server_checks.h"
#include "runtime/control/graph_file_io.h"
#include "runtime/core/crash_recovery.h"
#include "runtime/graph/subgraph_module.h"
#include "ui/graph/graph_snapshot.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/operators/operator_registry.h"
#include "operator_api/data_driven_filter.h"
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
#include "runtime/core/build_console.h"
#include "runtime/core/settings.h"
#include "runtime/core/source_index.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/operator_destination_policy.h"
#include "runtime/operators/project_package.h"
#include "operator_api/types.h"
#include "operator_api/type_id.h"  // vivid_is_custom_port_type
#include "operator_api/type_id.h"
#include "operator_api/port_type_registry.h"
// Split-out helper headers (Audit 04-R2-F3). Ordering matters: enums → json →
// validation, because validation.h uses json_err from json.h. The descriptor
// builders below use the enum→string converters from enums.h.
#include "runtime/control/control_server_enums.h"
#include "runtime/control/control_server_json.h"
#include "runtime/control/control_server_validation.h"
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
class EditorWindowManager;

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
// JSON descriptor builders (operator-doc-specific)
//
// The enum→string converters (*_str), JSON response helpers (json_ok/_err/…),
// and connect-address/field validation helpers now live in
// control_server_enums.h / control_server_json.h / control_server_validation.h,
// included above (Audit 04-R2-F3).
// ---------------------------------------------------------------------------

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

inline void add_param_descriptor_visibility(nlohmann::json& p,
                                            const VividParamDescriptor& pd) {
    if (pd.visible_when_op == VIVID_PARAM_VIS_ALWAYS || !pd.visible_when_param ||
        !*pd.visible_when_param || !pd.visible_when_values ||
        pd.visible_when_value_count == 0) {
        return;
    }
    nlohmann::json values = nlohmann::json::array();
    for (uint32_t i = 0; i < pd.visible_when_value_count; ++i)
        values.push_back(pd.visible_when_values[i]);
    p["visible_when"] = {
        {"param", pd.visible_when_param},
        {"op", param_visibility_op_str(pd.visible_when_op)},
        {"values", std::move(values)},
    };
}

inline void add_param_info_visibility(nlohmann::json& p,
                                      const ui::ParamInfo& pi) {
    if (pi.visible_when_op == VIVID_PARAM_VIS_ALWAYS ||
        pi.visible_when_param.empty() || pi.visible_when_values.empty()) {
        return;
    }
    nlohmann::json values = nlohmann::json::array();
    for (int32_t v : pi.visible_when_values)
        values.push_back(v);
    p["visible_when"] = {
        {"param", pi.visible_when_param},
        {"op", param_visibility_op_str(pi.visible_when_op)},
        {"values", std::move(values)},
    };
}

inline nlohmann::json build_param_descriptor_json(const VividParamDescriptor& pd) {
    nlohmann::json p = nlohmann::json::object();
    p["name"] = pd.name;
    p["type"] = param_type_str(pd.type);
    p["default"] = static_cast<double>(pd.default_value);
    p["min"] = static_cast<double>(pd.min_value);
    p["max"] = static_cast<double>(pd.max_value);
    if (pd.display_hint != VIVID_DISPLAY_DEFAULT)
        p["display_hint"] = display_hint_str(pd.display_hint);
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
    if (pd.asset_kind && *pd.asset_kind)
        p["asset_kind"] = pd.asset_kind;
    if (pd.widget_id && *pd.widget_id)
        p["widget_id"] = pd.widget_id;
    if (pd.widget_span > 0)
        p["widget_span"] = pd.widget_span;
    add_param_descriptor_visibility(p, pd);
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
    // Value-model envelope (lane-value clean-break) — multiplicity declared on the
    // port; value_type honors explicit override else derives from the payload type.
    p["value_type"] = value_type_str(value_type_for_port(pd));
    p["multiplicity"] = multiplicity_str(multiplicity_for_port(pd));
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
    op["kind"] = kind_str(vivid_operator_domain(&desc));
    op["time_dependent"] = (desc.time_dependent != 0);
    op["multiplicity_behavior"] = multiplicity_behavior_str(desc.multiplicity_behavior);
    op["multiplicity_behavior_help"] = multiplicity_behavior_help_str(desc.multiplicity_behavior);
    if (!package_name.empty())
        op["package"] = package_name;
    // v3 metadata: human-facing label, search keywords, one-line summary.
    // display_name is always present (auto-derived from name when descriptor
    // doesn't supply one); keywords/summary only when set.
    op["display_name"] = (desc.display_name && *desc.display_name)
        ? std::string(desc.display_name)
        : vivid::default_display_name(desc.name ? desc.name : "");
    if (desc.keywords && desc.keyword_count > 0) {
        nlohmann::json kw = nlohmann::json::array();
        for (uint32_t i = 0; i < desc.keyword_count; ++i)
            if (desc.keywords[i]) kw.push_back(desc.keywords[i]);
        if (!kw.empty()) op["keywords"] = std::move(kw);
    }
    if (desc.summary && *desc.summary) op["summary"] = desc.summary;

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
        if (pi.display_hint != VIVID_DISPLAY_DEFAULT)
            p["display_hint"] = display_hint_str(pi.display_hint);
        if (!pi.group.empty()) p["group"] = pi.group;
        if (!pi.description.empty()) p["description"] = pi.description;
        if (!pi.semantic_tag.empty()) p["semantic_tag"] = pi.semantic_tag;
        if (!pi.semantic_shape.empty()) p["semantic_shape"] = pi.semantic_shape;
        if (!pi.semantic_unit.empty()) p["semantic_unit"] = pi.semantic_unit;
        if (!pi.semantic_intent.empty()) p["semantic_intent"] = pi.semantic_intent;
        if (!pi.widget_id.empty()) p["widget_id"] = pi.widget_id;
        if (pi.widget_span > 0) p["widget_span"] = pi.widget_span;
        add_param_info_visibility(p, pi);
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

// Base64 encoder. Mirrors the one in capture_coordinator.cpp so dispatch
// handlers can return png_base64 responses without depending on the
// capture coordinator subsystem.
inline std::string base64_encode_bytes(const uint8_t* data, size_t len) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kTable[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kTable[n & 0x3F] : '=');
    }
    return out;
}

// Parse PNG dimensions from the IHDR chunk. Returns false if data is
// too small or doesn't look like a PNG. Width/height sit at offsets
// 16 and 20 (big-endian uint32) after the 8-byte signature + 4-byte
// IHDR length + 4-byte "IHDR" type.
inline bool parse_png_dimensions(const uint8_t* data, size_t size,
                                 int& out_width, int& out_height) {
    if (size < 24) return false;
    auto read_be32 = [](const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8)  |  uint32_t(p[3]);
    };
    out_width  = static_cast<int>(read_be32(data + 16));
    out_height = static_cast<int>(read_be32(data + 20));
    return true;
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
           method == "set_graph_metronome" ||
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
           method == "load_graph" ||
           method == "create_track" ||
           method == "rename_track" ||
           method == "remove_track" ||
           method == "move_track" ||
           method == "assign_nodes_to_track" ||
           method == "unassign_nodes_from_track" ||
           method == "save_clip" ||
           method == "update_clip" ||
           method == "rename_clip" ||
           method == "remove_clip" ||
           method == "move_clip" ||
           method == "launch_clip" ||
           method == "save_scene" ||
           method == "update_scene" ||
           method == "rename_scene" ||
           method == "remove_scene" ||
           method == "move_scene" ||
           method == "set_scene_assignment" ||
           method == "set_scene_leave_unchanged" ||
           method == "clear_scene_assignment" ||
           method == "create_cue_path" ||
           method == "rename_cue_path" ||
           method == "remove_cue_path" ||
           method == "move_cue_path" ||
           method == "add_cue_step" ||
           method == "remove_cue_step" ||
           method == "move_cue_step" ||
           method == "set_cue_step_advance";
           // queue_clip, queue_scene, launch_cue_step, advance_cue_path, and
           // stop_cue_path are real-time performance commands — not undo-tracked
}

// Human-readable label for a method, stored with its undo snapshot so /undo and
// /redo can report what they reverted ("Add node"). Generic transform: underscores
// to spaces, leading capital — covers every tracked method without a hand map.
inline std::string undo_label_for_method(const std::string& method) {
    if (method.empty()) return {};
    std::string out = method;
    for (auto& c : out) {
        if (c == '_') c = ' ';
    }
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 32);
    return out;
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

// Query handlers (defined in control_server_query.cpp)
std::string handle_inspect_graph(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules = nullptr, const std::string& detail = "full");
std::string handle_inspect_session(const Graph& graph, const RuntimeAPI& runtime_api);
std::string handle_inspect_clip(const Graph& graph, const std::string& track_id, const std::string& clip_id);
std::string handle_inspect_scene(const Graph& graph, const RuntimeAPI& runtime_api, const std::string& scene_id);
nlohmann::json make_audio_node_debug_json(const CompiledNode& ns);
nlohmann::json sample_node_outputs_snapshot(const CompiledNode& ns, bool include_lanes);
std::string handle_sample_node_outputs(Graph& graph, RuntimeCore& core, const nlohmann::json& root);
std::string handle_introspect_nodes(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules = nullptr);
std::string handle_get_graph_load_diagnostics(const Graph& graph);
std::string handle_operator_map(OperatorRegistry& registry);
std::string handle_get_discovery_report(PackageManager* package_manager);
std::string handle_list_types(OperatorRegistry& registry, PackageManager* package_manager, OperatorSourceDocs& source_docs, const nlohmann::json& root, const SubgraphModuleRegistry* modules = nullptr);
std::string handle_operator_docs(OperatorRegistry& registry, PackageManager* package_manager, OperatorSourceDocs& source_docs, const nlohmann::json& root, const SubgraphModuleRegistry* modules = nullptr);
std::string handle_list_packages(PackageManager* package_manager);
std::string handle_read_package_docs(PackageManager* package_manager, const nlohmann::json& root);
std::string handle_list_package_examples(PackageManager* package_manager, const nlohmann::json& root);
std::string handle_read_package_example(PackageManager* package_manager, const nlohmann::json& root);
std::string handle_package_operator_docs(OperatorRegistry& registry, PackageManager* package_manager, OperatorSourceDocs& source_docs, const nlohmann::json& root);
std::string handle_package_catalog(PackageCatalog* package_catalog);
std::string handle_check_package_updates(PackageCatalog* package_catalog, PackageManager* package_manager, const nlohmann::json& root);
std::string handle_check_core_updates(AppUpdateManager* app_update_manager, const nlohmann::json& root);
std::string handle_list_source_roots(SourceIndex& source_index);
std::string handle_search_source(SourceIndex& source_index, const nlohmann::json& root);
std::string handle_read_source_file(SourceIndex& source_index, const nlohmann::json& root);
std::string handle_read_source_span(SourceIndex& source_index, const nlohmann::json& root);
std::string handle_find_symbol(SourceIndex& source_index, const nlohmann::json& root);
std::string handle_find_references(SourceIndex& source_index, const nlohmann::json& root);
std::string handle_get_build_activity(BuildConsole* build_console, const nlohmann::json& root);
std::string handle_explain_build_failure(BuildConsole* build_console, const nlohmann::json& root);
std::string handle_get_registry_diagnostics(OperatorRegistry& registry);
std::string handle_validate_operators(OperatorRegistry& registry);

// Crash-recovery handlers (defined in control_server_crash.cpp, Phase 5)
std::string handle_get_last_crash(CrashRecoveryManager* crm);
std::string handle_clear_last_crash(CrashRecoveryManager* crm);
std::string handle_load_graph_safe_mode(const nlohmann::json& root,
                                        CrashRecoveryManager* crm,
                                        RuntimeCore& core,
                                        RuntimeAPI& api,
                                        bool& has_gpu_ops,
                                        bool& has_audio);

// Dispatch router (defined in control_server_dispatch.cpp)
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
                            AssetLibrary* asset_library = nullptr,
                            BuildConsole* build_console = nullptr,
                            GpuContext* gpu_context = nullptr,
                            PackageCatalog* package_catalog = nullptr,
                            const ControlServer* control_server = nullptr,
                            CrashRecoveryManager* crash_recovery_manager = nullptr,
                            EditorWindowManager* editor_window_manager = nullptr);

// Asset library handlers (defined in control_server_assets.cpp)
std::string handle_list_assets(AssetLibrary& lib, const nlohmann::json& root);
std::string handle_inspect_asset(AssetLibrary& lib, const nlohmann::json& root);
std::string handle_import_asset(AssetLibrary& lib, const nlohmann::json& root);
std::string handle_refresh_assets(AssetLibrary& lib);

} // namespace vivid
