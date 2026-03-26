#include "runtime/control_server.h"
#include "runtime/capture_coordinator.h"
#include "runtime/compiled_graph.h"
#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/operator_registry.h"
#include "runtime/operator_loader.h"
#include "runtime/operator_creator.h"
#include "runtime/hot_reload.h"
#include "runtime/undo_manager.h"
#include "runtime/package_manager.h"
#include "runtime/package_compiler.h"
#include "runtime/package_test_runner.h"
#include "runtime/package_catalog.h"
#include "runtime/app_update_manager.h"
#include "runtime/settings.h"
#include "runtime/operator_destination_policy.h"
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

// ---------------------------------------------------------------------------
// Timeout constants (seconds)
// ---------------------------------------------------------------------------
static constexpr int kCaptureTimeoutSec           = 5;
static constexpr int kInterfaceCaptureTimeoutSec   = 10;
static constexpr int kRecordingTimeoutSec          = 10;
static constexpr int kAnalysisTimeoutSec           = 10;
static constexpr int kDefaultDispatchTimeoutSec    = 5;
static constexpr int kSampleNodeOutputsTimeoutSec  = 30;
static constexpr int kTestPackageTimeoutSec        = 60;

// ---------------------------------------------------------------------------
// Enum → string helpers
// ---------------------------------------------------------------------------

static const char* env_str(VividExecutionEnv e) {
    switch (e) {
        case VIVID_ENV_FRAME: return "control";
        case VIVID_ENV_AUDIO: return "audio";
        case VIVID_ENV_GPU:   return "gpu";
        default: return "unknown";
    }
}

static const char* param_type_str(VividParamType t) {
    switch (t) {
        case VIVID_PARAM_FLOAT: return "float";
        case VIVID_PARAM_INT:   return "int";
        case VIVID_PARAM_BOOL:  return "bool";
        case VIVID_PARAM_FILE:  return "file";
        case VIVID_PARAM_TEXT:  return "text";
        default: return "unknown";
    }
}

static const char* port_type_str(VividPortType t) {
    switch (t) {
        case VIVID_PORT_SIGNAL:         return "float";
        case VIVID_PORT_AUDIO:         return "audio";
        case VIVID_PORT_SPREAD:        return "spread";
        case VIVID_PORT_STRING:        return "string";
        case VIVID_PORT_STRING_SPREAD: return "string_spread";
        case VIVID_PORT_TEXTURE:       return "texture";
        default:
            if (vivid_is_custom_port_type(t)) return "custom";
            return "unknown";
    }
}

static const char* transport_str(VividPortTransport t) {
    switch (t) {
        case VIVID_PORT_TRANSPORT_SIGNAL:        return "scalar";
        case VIVID_PORT_TRANSPORT_AUDIO_BUFFER:  return "audio_buffer";
        case VIVID_PORT_TRANSPORT_SPREAD:        return "spread";
        case VIVID_PORT_TRANSPORT_STRING:        return "string";
        case VIVID_PORT_TRANSPORT_STRING_SPREAD: return "string_spread";
        case VIVID_PORT_TRANSPORT_TEXTURE:       return "texture";
        case VIVID_PORT_TRANSPORT_CUSTOM_VALUE:  return "custom_value";
        case VIVID_PORT_TRANSPORT_CUSTOM_REF:    return "custom_ref";
        default: return "unknown";
    }
}

static void add_port_registry_metadata(nlohmann::json& port_obj,
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

static const char* update_class_str(PackageUpdateClass c) {
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

static std::string json_ok(nlohmann::json result) {
    return nlohmann::json{{"ok", true}, {"result", std::move(result)}}.dump();
}

static std::string json_ok_msg(const std::string& msg) {
    return nlohmann::json{{"ok", true}, {"message", msg}}.dump();
}

static std::string json_err(const std::string& msg) {
    return nlohmann::json{{"ok", false}, {"error", msg}}.dump();
}

static std::string command_result_to_json(const CommandResult& r) {
    return r.ok ? json_ok_msg(r.message) : json_err(r.message);
}

static bool split_addr_local(const std::string& addr, std::string& node, std::string& port) {
    size_t slash = addr.find('/');
    if (slash == std::string::npos) return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

static const ConnectionDef* find_connection_by_addr(const Graph& graph,
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

static bool is_safe_package_name(const std::string& name) {
    return name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           name.find("..") == std::string::npos;
}

static bool is_safe_recording_path(const std::string& path) {
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

static bool is_safe_capture_image_path(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] != '/') return false;
    if (path.find("..") != std::string::npos) return false;
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot);
    return ext == ".png";
}

static bool response_is_ok(const std::string& response_json) {
    try {
        auto doc = nlohmann::json::parse(response_json);
        return doc.contains("ok") && doc["ok"].is_boolean() && doc["ok"].get<bool>();
    } catch (...) { return false; }
}

static bool capture_live_graph_snapshot(Graph& graph, std::string& out_json,
                                        std::string& out_error) {
    if (!graph.save_to_string(out_json)) {
        out_error = "failed to serialize current graph before package mutation";
        return false;
    }
    return true;
}

static bool is_undo_tracked_method(const std::string& method) {
    return method == "add_node" ||
           method == "remove_node" ||
           method == "connect" ||
           method == "disconnect" ||
           method == "set_connection_remap" ||
           method == "set_param" ||
           method == "set_string_param" ||
           method == "set_resolution" ||
           method == "set_cadence_override" ||
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

static std::string handle_inspect_graph(Graph& graph, Scheduler& scheduler) {
    const auto* cg = scheduler.compiled_graph();
    std::unordered_map<std::string, const CompiledNode*> state_map;
    if (cg) {
        for (const auto& cn : cg->nodes)
            state_map[cn.node_id] = &cn;
    }

    nlohmann::json result = nlohmann::json::object();

    // -- Nodes --
    nlohmann::json nodes_arr = nlohmann::json::array();
    for (const auto& ndef : graph.nodes()) {
        nlohmann::json node = nlohmann::json::object();
        node["id"] = ndef.id;
        node["type"] = ndef.type;

        auto sit = state_map.find(ndef.id);
        const CompiledNode* ns = (sit != state_map.end()) ? sit->second : nullptr;
        const VividOperatorDescriptor* desc =
            (ns && ns->loader) ? ns->loader->descriptor() : nullptr;

        // Params (with live values from scheduler)
        nlohmann::json params_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t i = 0; i < desc->param_count; ++i) {
                const auto& pd = desc->params[i];
                nlohmann::json p = nlohmann::json::object();
                p["name"] = pd.name;
                p["type"] = param_type_str(pd.type);
                float value = pd.default_value;
                if (ns) {
                    auto pi = ns->param_indices.find(pd.name);
                    if (pi != ns->param_indices.end())
                        value = ns->param_values[pi->second];
                }
                p["value"] = static_cast<double>(value);
                p["min"] = static_cast<double>(pd.min_value);
                p["max"] = static_cast<double>(pd.max_value);
                p["default"] = static_cast<double>(pd.default_value);
                if (pd.semantic_tag)
                    p["semantic_tag"] = pd.semantic_tag;
                if (pd.semantic_shape)
                    p["semantic_shape"] = pd.semantic_shape;
                if (pd.semantic_unit)
                    p["semantic_unit"] = pd.semantic_unit;
                if (pd.semantic_intent)
                    p["semantic_intent"] = pd.semantic_intent;
                if (pd.choice_count > 0 && pd.choice_labels) {
                    nlohmann::json choices = nlohmann::json::array();
                    for (uint32_t c = 0; c < pd.choice_count; ++c)
                        choices.push_back(pd.choice_labels[c]);
                    p["choices"] = std::move(choices);
                }
                if ((pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT) && ns) {
                    auto fi = ns->file_param_indices.find(pd.name);
                    if (fi != ns->file_param_indices.end()) {
                        p["string_value"] = ns->file_param_storage[fi->second];
                    }
                }
                params_arr.push_back(std::move(p));
            }
        }
        node["params"] = std::move(params_arr);

        // Ports split into inputs / outputs
        nlohmann::json inputs_arr = nlohmann::json::array();
        nlohmann::json outputs_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                const auto& pd = desc->ports[i];
                nlohmann::json p = nlohmann::json::object();
                p["name"] = pd.name;
                p["type"] = port_type_str(pd.type);
                p["transport"] = transport_str(pd.transport);
                if (pd.type_name)
                    p["type_name"] = pd.type_name;
                if (pd.stable_type_id)
                    p["stable_type_id"] = pd.stable_type_id;
                if (pd.payload_size > 0)
                    p["payload_size"] = pd.payload_size;

                if (pd.direction == VIVID_PORT_OUTPUT && ns) {
                    auto oi = ns->output_port_indices.find(pd.name);
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_values.size()) {
                        p["current_value"] = static_cast<double>(ns->output_values[oi->second]);
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_string_values.size() &&
                        !ns->output_string_values[oi->second].empty()) {
                        p["current_string"] = ns->output_string_values[oi->second];
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_spreads.size() &&
                        !ns->output_spreads[oi->second].empty()) {
                        nlohmann::json spread_arr = nlohmann::json::array();
                        for (float sv : ns->output_spreads[oi->second])
                            spread_arr.push_back(static_cast<double>(sv));
                        p["spread"] = std::move(spread_arr);
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_string_spreads.size() &&
                        !ns->output_string_spreads[oi->second].empty()) {
                        nlohmann::json spread_arr = nlohmann::json::array();
                        for (const auto& sv : ns->output_string_spreads[oi->second])
                            spread_arr.push_back(sv);
                        p["string_spread"] = std::move(spread_arr);
                    }
                }

                if (pd.direction == VIVID_PORT_INPUT && ns) {
                    auto ii = ns->input_port_indices.find(pd.name);
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_values.size()) {
                        p["current_value"] = static_cast<double>(ns->input_values[ii->second]);
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_string_values.size() &&
                        !ns->input_string_values[ii->second].empty()) {
                        p["current_string"] = ns->input_string_values[ii->second];
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_spreads.size() &&
                        !ns->input_spreads[ii->second].empty()) {
                        nlohmann::json spread_arr = nlohmann::json::array();
                        for (float sv : ns->input_spreads[ii->second])
                            spread_arr.push_back(static_cast<double>(sv));
                        p["spread"] = std::move(spread_arr);
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_string_spreads.size() &&
                        !ns->input_string_spreads[ii->second].empty()) {
                        nlohmann::json spread_arr = nlohmann::json::array();
                        for (const auto& sv : ns->input_string_spreads[ii->second])
                            spread_arr.push_back(sv);
                        p["string_spread"] = std::move(spread_arr);
                    }
                }

                if (pd.direction == VIVID_PORT_INPUT)
                    inputs_arr.push_back(std::move(p));
                else
                    outputs_arr.push_back(std::move(p));
            }
        }
        node["inputs"] = std::move(inputs_arr);
        node["outputs"] = std::move(outputs_arr);

        nodes_arr.push_back(std::move(node));
    }
    result["nodes"] = std::move(nodes_arr);

    // -- Connections --
    nlohmann::json conns_arr = nlohmann::json::array();
    for (const auto& conn : graph.connections()) {
        nlohmann::json c = nlohmann::json::object();
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        c["from"] = from_addr;
        c["to"] = to_addr;
        if (conn.has_remap()) {
            c["from_min"] = conn.from_min;
            c["from_max"] = conn.from_max;
            c["to_min"] = conn.to_min;
            c["to_max"] = conn.to_max;
            if (conn.clamp)
                c["clamp"] = true;
        }
        conns_arr.push_back(std::move(c));
    }
    result["connections"] = std::move(conns_arr);

    return json_ok(std::move(result));
}

static const CompiledNode* find_node_state(const Scheduler& scheduler,
                                            const std::string& node_id) {
    const auto* cg = scheduler.compiled_graph();
    if (!cg) return nullptr;
    return cg->find_node(node_id);
}

static nlohmann::json sample_node_outputs_snapshot(const CompiledNode& ns,
                                                    bool include_spreads) {
    nlohmann::json outputs_obj = nlohmann::json::object();
    const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;
    if (!desc) return outputs_obj;

    for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
        const auto& pd = desc->ports[pi];
        if (pd.direction != VIVID_PORT_OUTPUT) continue;

        nlohmann::json out = nlohmann::json::object();
        out["kind"] = port_type_str(pd.type);
        out["transport"] = transport_str(pd.transport);
        if (pd.type_name)
            out["type_name"] = pd.type_name;
        if (pd.stable_type_id)
            out["stable_type_id"] = pd.stable_type_id;

        auto oit = ns.output_port_indices.find(pd.name);
        if (oit != ns.output_port_indices.end()) {
            const uint32_t oi = oit->second;
            if (oi < ns.output_values.size()) {
                out["scalar"] = static_cast<double>(ns.output_values[oi]);
            }
            if (oi < ns.output_string_values.size() &&
                !ns.output_string_values[oi].empty()) {
                out["string"] = ns.output_string_values[oi];
            }
            if (include_spreads && oi < ns.output_spreads.size() &&
                !ns.output_spreads[oi].empty()) {
                nlohmann::json spread_arr = nlohmann::json::array();
                for (float sv : ns.output_spreads[oi]) {
                    spread_arr.push_back(static_cast<double>(sv));
                }
                out["spread"] = std::move(spread_arr);
            }
            if (include_spreads && oi < ns.output_string_spreads.size() &&
                !ns.output_string_spreads[oi].empty()) {
                nlohmann::json spread_arr = nlohmann::json::array();
                for (const auto& sv : ns.output_string_spreads[oi]) {
                    spread_arr.push_back(sv);
                }
                out["string_spread"] = std::move(spread_arr);
            }
        }

        outputs_obj[pd.name] = std::move(out);
    }

    return outputs_obj;
}

static std::string handle_sample_node_outputs(Graph& graph, Scheduler& scheduler,
                                              const nlohmann::json& root) {
    if (!root.contains("node_id") || !root["node_id"].is_string()) return json_err("missing 'node_id'");
    std::string node_id = root["node_id"].get<std::string>();

    double duration_seconds = 8.0;
    int interval_ms = 250;
    bool include_spreads = true;

    if (root.contains("duration_seconds") && root["duration_seconds"].is_number())
        duration_seconds = root["duration_seconds"].get<double>();
    if (root.contains("interval_ms") && root["interval_ms"].is_number())
        interval_ms = root["interval_ms"].get<int>();
    if (root.contains("include_spreads") && root["include_spreads"].is_boolean())
        include_spreads = root["include_spreads"].get<bool>();

    duration_seconds = std::clamp(duration_seconds, 0.0, 60.0);
    interval_ms = std::clamp(interval_ms, 10, 5000);

    const CompiledNode* initial = find_node_state(scheduler, node_id);
    if (!initial) return json_err("node not found");
    if (!initial->loader || !initial->loader->descriptor()) {
        return json_err("node has no live descriptor");
    }

    nlohmann::json result = nlohmann::json::object();
    result["node_id"] = node_id;
    result["type"] = initial->type_name;
    result["domain"] = initial->is_gpu ? "gpu" : (initial->active_cadence == vivid::Cadence::Audio ? "audio" : "control");
    result["duration_seconds"] = duration_seconds;
    result["interval_ms"] = interval_ms;
    result["include_spreads"] = include_spreads;

    nlohmann::json samples_arr = nlohmann::json::array();
    const auto start = std::chrono::steady_clock::now();
    const auto end = start + std::chrono::duration<double>(duration_seconds);
    auto next_sample = start;
    int sample_count = 0;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const CompiledNode* ns = find_node_state(scheduler, node_id);
        if (!ns) {
            return json_err("node disappeared during sampling");
        }

        nlohmann::json sample = nlohmann::json::object();
        const double t = std::chrono::duration<double>(now - start).count();
        sample["time_seconds"] = t;
        sample["outputs"] = sample_node_outputs_snapshot(*ns, include_spreads);
        samples_arr.push_back(std::move(sample));
        ++sample_count;

        if (now >= end) break;
        next_sample += std::chrono::milliseconds(interval_ms);
        std::this_thread::sleep_until(next_sample);
    }

    result["sample_count"] = sample_count;
    result["samples"] = std::move(samples_arr);
    return json_ok(std::move(result));
}

static std::string handle_introspect_nodes(Graph& graph, Scheduler& scheduler) {
    std::unordered_map<std::string, const NodeDef*> def_map;
    for (const auto& ndef : graph.nodes())
        def_map[ndef.id] = &ndef;
    std::unordered_map<std::string, int> incoming_wires;
    std::unordered_map<std::string, int> outgoing_wires;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> incoming_port_wires;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> outgoing_port_wires;
    for (const auto& conn : graph.connections()) {
        incoming_wires[conn.to_node]++;
        outgoing_wires[conn.from_node]++;
        incoming_port_wires[conn.to_node][conn.to_port]++;
        outgoing_port_wires[conn.from_node][conn.from_port]++;
    }

    nlohmann::json result_obj = nlohmann::json::object();
    nlohmann::json nodes_arr = nlohmann::json::array();

    const auto* cg = scheduler.compiled_graph();
    if (!cg) {
        result_obj["nodes"] = std::move(nodes_arr);
        return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
    }
    const auto& nodes = cg->nodes;
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        const auto& ns = nodes[ni];
        nlohmann::json node = nlohmann::json::object();
        node["node_id"] = ns.node_id;
        node["node_index"] = static_cast<int64_t>(ni);

        std::string type_name = ns.type_name;
        if (type_name.empty()) {
            auto dit = def_map.find(ns.node_id);
            if (dit != def_map.end() && dit->second)
                type_name = dit->second->type;
        }
        node["type"] = type_name;
        node["domain"] = ns.is_gpu ? "gpu" : (ns.active_cadence == vivid::Cadence::Audio ? "audio" : "control");
        node["cadence_capability"] = (ns.cadence_capability == VIVID_CADENCE_AUDIO_CAPABLE) ? "audio_capable" : "frame_only";
        {
            const auto* ndef = graph.find_node(ns.node_id);
            node["cadence_override"] = ndef ? static_cast<int>(ndef->cadence_override) : 0;
        }
        node["incoming_wires"] = static_cast<int64_t>(incoming_wires[ns.node_id]);
        node["outgoing_wires"] = static_cast<int64_t>(outgoing_wires[ns.node_id]);

        // Health
        nlohmann::json health = nlohmann::json::object();
        health["errored"] = (ns.errored || ns.missing_operator);
        health["message"] = ns.error_message;
        health["missing_operator"] = ns.missing_operator;
        node["health"] = std::move(health);

        const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;

        // Current params
        nlohmann::json params_obj = nlohmann::json::object();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                if (pi < ns.param_values.size())
                    params_obj[pd.name] = static_cast<double>(ns.param_values[pi]);
            }
            for (const auto& [name, idx] : ns.file_param_indices) {
                if (idx < ns.file_param_storage.size())
                    params_obj[name] = ns.file_param_storage[idx];
            }
        } else {
            auto dit = def_map.find(ns.node_id);
            if (dit != def_map.end() && dit->second) {
                for (const auto& [k, v] : dit->second->params)
                    params_obj[k] = static_cast<double>(v);
                for (const auto& [k, v] : dit->second->string_params)
                    params_obj[k] = v;
            }
        }
        node["params"] = std::move(params_obj);

        // Param metadata
        nlohmann::json param_meta_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                nlohmann::json pm = nlohmann::json::object();
                pm["name"] = pd.name;
                pm["kind"] = param_type_str(pd.type);
                pm["default"] = static_cast<double>(pd.default_value);
                pm["min"] = static_cast<double>(pd.min_value);
                pm["max"] = static_cast<double>(pd.max_value);
                if (pd.semantic_tag)
                    pm["semantic_tag"] = pd.semantic_tag;
                if (pd.semantic_shape)
                    pm["semantic_shape"] = pd.semantic_shape;
                if (pd.semantic_unit)
                    pm["semantic_unit"] = pd.semantic_unit;
                if (pd.semantic_intent)
                    pm["semantic_intent"] = pd.semantic_intent;
                param_meta_arr.push_back(std::move(pm));
            }
        }
        node["param_meta"] = std::move(param_meta_arr);

        // Input summary
        nlohmann::json inputs_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                const auto& pd = desc->ports[pi];
                if (pd.direction != VIVID_PORT_INPUT) continue;

                nlohmann::json in = nlohmann::json::object();
                in["name"] = pd.name;
                in["kind"] = port_type_str(pd.type);
                in["transport"] = transport_str(pd.transport);
                if (pd.type_name)
                    in["type_name"] = pd.type_name;
                if (pd.stable_type_id)
                    in["stable_type_id"] = pd.stable_type_id;
                if (pd.payload_size > 0)
                    in["payload_size"] = static_cast<int64_t>(pd.payload_size);
                in["connected_wires"] = static_cast<int64_t>(incoming_port_wires[ns.node_id][pd.name]);

                auto iit = ns.input_port_indices.find(pd.name);
                if (iit != ns.input_port_indices.end()) {
                    uint32_t ii = iit->second;
                    if (ii < ns.input_values.size()) {
                        in["scalar"] = static_cast<double>(ns.input_values[ii]);
                    }
                    if (ii < ns.input_string_values.size() &&
                        !ns.input_string_values[ii].empty()) {
                        in["string"] = ns.input_string_values[ii];
                    }
                    if (ii < ns.input_spreads.size()) {
                        in["spread"] = nlohmann::json{{"length", static_cast<int64_t>(ns.input_spreads[ii].size())}};
                    }
                    if (ii < ns.input_string_spreads.size()) {
                        in["string_spread"] = nlohmann::json{{"length", static_cast<int64_t>(ns.input_string_spreads[ii].size())}};
                    }
                }
                inputs_arr.push_back(std::move(in));
            }
        }
        node["inputs"] = std::move(inputs_arr);

        // Output summary
        nlohmann::json outputs_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                const auto& pd = desc->ports[pi];
                if (pd.direction != VIVID_PORT_OUTPUT) continue;

                nlohmann::json out = nlohmann::json::object();
                out["name"] = pd.name;
                out["kind"] = port_type_str(pd.type);
                out["transport"] = transport_str(pd.transport);
                if (pd.type_name)
                    out["type_name"] = pd.type_name;
                if (pd.stable_type_id)
                    out["stable_type_id"] = pd.stable_type_id;
                if (pd.payload_size > 0)
                    out["payload_size"] = static_cast<int64_t>(pd.payload_size);
                out["connected_wires"] = static_cast<int64_t>(outgoing_port_wires[ns.node_id][pd.name]);

                auto oit = ns.output_port_indices.find(pd.name);
                if (oit != ns.output_port_indices.end()) {
                    uint32_t oi = oit->second;
                    if (oi < ns.output_values.size())
                        out["scalar"] = static_cast<double>(ns.output_values[oi]);
                    if (oi < ns.output_string_values.size() &&
                        !ns.output_string_values[oi].empty()) {
                        out["string"] = ns.output_string_values[oi];
                    }
                    if (oi < ns.output_spreads.size()) {
                        out["spread"] = nlohmann::json{{"length", static_cast<int64_t>(ns.output_spreads[oi].size())}};
                    }
                    if (oi < ns.output_string_spreads.size()) {
                        out["string_spread"] = nlohmann::json{{"length", static_cast<int64_t>(ns.output_string_spreads[oi].size())}};
                    }
                }

                if (pd.type == VIVID_PORT_TEXTURE && ns.gpu_tex_width > 0 && ns.gpu_tex_height > 0) {
                    out["width"] = ns.gpu_tex_width;
                    out["height"] = ns.gpu_tex_height;
                }
                outputs_arr.push_back(std::move(out));
            }
        }
        node["outputs"] = std::move(outputs_arr);

        // Domain metrics (lightweight first pass)
        nlohmann::json domain_metrics = nlohmann::json::object();
        if (ns.is_gpu) {
            nlohmann::json gpu = nlohmann::json::object();
            gpu["width"] = ns.gpu_tex_width;
            gpu["height"] = ns.gpu_tex_height;
            gpu["has_texture"] = (ns.gpu_texture != nullptr);
            gpu["aux_texture_count"] = static_cast<int64_t>(ns.aux_gpu_texture_views.size());
            domain_metrics["gpu"] = std::move(gpu);
        } else if (ns.active_cadence == vivid::Cadence::Audio) {
            nlohmann::json audio = nlohmann::json::object();
            audio["output_port_count"] = ns.output_port_count;
            audio["input_port_count"] = ns.input_port_count;
            auto rms_it = ns.output_port_indices.find("rms");
            if (rms_it != ns.output_port_indices.end() &&
                rms_it->second < ns.output_values.size()) {
                audio["rms"] = static_cast<double>(ns.output_values[rms_it->second]);
            }
            auto peak_it = ns.output_port_indices.find("peak");
            if (peak_it != ns.output_port_indices.end() &&
                peak_it->second < ns.output_values.size()) {
                audio["peak"] = static_cast<double>(ns.output_values[peak_it->second]);
            }
            auto wave_it = ns.output_port_indices.find("waveform");
            if (wave_it != ns.output_port_indices.end() &&
                wave_it->second < ns.output_spreads.size()) {
                const auto& wave = ns.output_spreads[wave_it->second];
                audio["waveform_length"] = static_cast<int64_t>(wave.size());
                nlohmann::json preview = nlohmann::json::array();
                size_t preview_count = wave.size();
                if (preview_count > 32) preview_count = 32;
                for (size_t wi = 0; wi < preview_count; ++wi) {
                    preview.push_back(static_cast<double>(wave[wi]));
                }
                audio["waveform_preview"] = std::move(preview);
            }
            domain_metrics["audio"] = std::move(audio);
        } else {
            nlohmann::json control = nlohmann::json::object();
            int64_t spread_out_nonempty = 0;
            int64_t scalar_out_nonzero = 0;
            for (const auto& sp : ns.output_spreads)
                if (!sp.empty()) spread_out_nonempty++;
            for (float v : ns.output_values)
                if (v != 0.0f) scalar_out_nonzero++;
            control["non_empty_spread_outputs"] = spread_out_nonempty;
            control["non_zero_scalar_outputs"] = scalar_out_nonzero;
            domain_metrics["control"] = std::move(control);
        }
        node["domain_metrics"] = std::move(domain_metrics);

        nodes_arr.push_back(std::move(node));
    }

    result_obj["nodes"] = std::move(nodes_arr);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

static int severity_rank(const std::string& severity) {
    if (severity == "critical") return 0;
    if (severity == "warning") return 1;
    return 2;
}

struct DiagnosticFinding {
    std::string id;
    std::string severity;
    std::string node_id;
    std::string message;
    std::string suggestion;
};

static std::vector<DiagnosticFinding> collect_diagnostics(
        Graph& graph, Scheduler& scheduler, OperatorRegistry& registry) {
    std::unordered_map<std::string, int> incoming_wires;
    std::unordered_map<std::string, int> outgoing_wires;
    for (const auto& conn : graph.connections()) {
        incoming_wires[conn.to_node]++;
        outgoing_wires[conn.from_node]++;
    }

    std::vector<DiagnosticFinding> findings;
    findings.reserve(32);

    const auto* cg = scheduler.compiled_graph();
    if (!cg) return findings;
    const auto& nodes = cg->nodes;
    for (const auto& ns : nodes) {
        const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;
        std::string type_name = ns.type_name;
        if (type_name.empty() && desc && desc->name) type_name = desc->name;

        if (ns.missing_operator) {
            std::string suggestion = "Install or link the package providing this operator type, then reload.";
            if (registry.has_abi_mismatch_diagnostics()) {
                suggestion = "Install/link may be fine but plugin ABI appears incompatible. "
                             "Rebuild vivid and rerun package rebuild.";
            }
            findings.push_back({
                "missing_operator_type",
                "critical",
                ns.node_id,
                "Operator type is unresolved; missing-operator placeholder is active.",
                suggestion
            });
        }

        if (ns.errored) {
            findings.push_back({
                "node_runtime_error",
                "critical",
                ns.node_id,
                std::string("Node is in errored state: ") + ns.error_message,
                "Fix compile/runtime error in this operator; graph currently uses stale/broken output."
            });
        }

        if (ns.active_cadence == vivid::Cadence::Audio && type_name == "audio_out" && incoming_wires[ns.node_id] == 0) {
            findings.push_back({
                "audio_sink_disconnected",
                "critical",
                ns.node_id,
                "Audio sink node has no incoming connections.",
                "Connect an audio-producing node to audio_out inputs."
            });
        }

        if (!ns.missing_operator && incoming_wires[ns.node_id] == 0 && outgoing_wires[ns.node_id] == 0) {
            findings.push_back({
                "isolated_node",
                "warning",
                ns.node_id,
                "Node is fully disconnected from the graph.",
                "Connect it to upstream/downstream nodes or remove it if unused."
            });
        }

        bool found_non_finite = false;
        for (float v : ns.param_values) {
            if (!std::isfinite(v)) { found_non_finite = true; break; }
        }
        if (!found_non_finite) {
            for (float v : ns.output_values) {
                if (!std::isfinite(v)) { found_non_finite = true; break; }
            }
        }
        if (!found_non_finite) {
            for (const auto& sp : ns.output_spreads) {
                for (float v : sp) {
                    if (!std::isfinite(v)) { found_non_finite = true; break; }
                }
                if (found_non_finite) break;
            }
        }
        if (found_non_finite) {
            findings.push_back({
                "non_finite_values",
                "warning",
                ns.node_id,
                "NaN or Inf value detected in node runtime state.",
                "Clamp or sanitize values; check divisions, logs, and numeric domain assumptions."
            });
        }

        if (ns.active_cadence == vivid::Cadence::Audio) {
            auto peak_it = ns.output_port_indices.find("peak");
            if (peak_it != ns.output_port_indices.end() && peak_it->second < ns.output_values.size()) {
                float peak = ns.output_values[peak_it->second];
                if (std::isfinite(peak) && peak > 1.05f) {
                    findings.push_back({
                        "audio_peak_clipping_risk",
                        "warning",
                        ns.node_id,
                        "Audio peak exceeds 1.05; clipping risk likely.",
                        "Reduce gain, add limiting, or remap modulation depth."
                    });
                }
            }
        }
    }

    std::sort(findings.begin(), findings.end(), [](const DiagnosticFinding& a, const DiagnosticFinding& b) {
        int ar = severity_rank(a.severity);
        int br = severity_rank(b.severity);
        if (ar != br) return ar < br;
        if (a.id != b.id) return a.id < b.id;
        if (a.node_id != b.node_id) return a.node_id < b.node_id;
        if (a.message != b.message) return a.message < b.message;
        return a.suggestion < b.suggestion;
    });
    return findings;
}

static std::string handle_run_diagnostics(Graph& graph, Scheduler& scheduler, OperatorRegistry& registry) {
    std::vector<DiagnosticFinding> findings = collect_diagnostics(graph, scheduler, registry);

    nlohmann::json summary = nlohmann::json::object();
    int64_t critical_count = 0;
    int64_t warning_count = 0;
    int64_t info_count = 0;
    for (const auto& f : findings) {
        if (f.severity == "critical") critical_count++;
        else if (f.severity == "warning") warning_count++;
        else info_count++;
    }
    summary["critical"] = critical_count;
    summary["warning"] = warning_count;
    summary["info"] = info_count;

    nlohmann::json findings_arr = nlohmann::json::array();
    for (const auto& f : findings) {
        findings_arr.push_back({
            {"id", f.id}, {"severity", f.severity}, {"node_id", f.node_id},
            {"message", f.message}, {"suggestion", f.suggestion}
        });
    }

    // Hint list: dedupe by finding id, keep highest-priority instance.
    nlohmann::json hints_arr = nlohmann::json::array();
    std::unordered_set<std::string> seen_hint_ids;
    for (const auto& f : findings) {
        if (seen_hint_ids.find(f.id) != seen_hint_ids.end()) continue;
        seen_hint_ids.insert(f.id);
        hints_arr.push_back({{"id", f.id}, {"severity", f.severity}, {"suggestion", f.suggestion}});
    }

    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["summary"] = std::move(summary);
    result_obj["findings"] = std::move(findings_arr);
    result_obj["hints"] = std::move(hints_arr);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

static std::string handle_get_graph_load_diagnostics(const Graph& graph) {
    nlohmann::json diags_arr = nlohmann::json::array();
    for (const auto& d : graph.load_diagnostics) {
        diags_arr.push_back({
            {"node_id", d.node_id}, {"pkg_name", d.pkg_name},
            {"saved_version", d.saved_version}, {"installed_version", d.installed_version},
            {"classification", d.classification}
        });
    }

    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["graph_load_diagnostics"] = std::move(diags_arr);
    return nlohmann::json{{"ok", true}, {"result", std::move(result_obj)}}.dump();
}

struct CheckValue {
    enum class Kind { Missing, Number, Bool, String };
    Kind kind = Kind::Missing;
    double number = 0.0;
    bool boolean = false;
    std::string string;
};

static CheckValue cv_number(double n) { CheckValue v; v.kind = CheckValue::Kind::Number; v.number = n; return v; }
static CheckValue cv_bool(bool b) { CheckValue v; v.kind = CheckValue::Kind::Bool; v.boolean = b; return v; }
static CheckValue cv_string(const std::string& s) { CheckValue v; v.kind = CheckValue::Kind::String; v.string = s; return v; }

static bool parse_check_value(const nlohmann::json& v, CheckValue& out) {
    if (v.is_null()) return false;
    if (v.is_number()) {
        out = cv_number(v.get<double>());
        return true;
    }
    if (v.is_boolean()) {
        out = cv_bool(v.get<bool>());
        return true;
    }
    if (v.is_string()) {
        out = cv_string(v.get<std::string>());
        return true;
    }
    return false;
}

static void add_json_check_value(nlohmann::json& obj,
                                 const char* key, const CheckValue& v) {
    if (v.kind == CheckValue::Kind::Number)
        obj[key] = v.number;
    else if (v.kind == CheckValue::Kind::Bool)
        obj[key] = v.boolean;
    else if (v.kind == CheckValue::Kind::String)
        obj[key] = v.string;
}

static bool check_is_true(const CheckValue& v) {
    if (v.kind == CheckValue::Kind::Bool) return v.boolean;
    if (v.kind == CheckValue::Kind::Number) return v.number != 0.0;
    if (v.kind == CheckValue::Kind::String) return !v.string.empty();
    return false;
}

static bool eval_compare(const CheckValue& actual, const std::string& op,
                         const CheckValue& expected, double tolerance,
                         const CheckValue* between_max = nullptr) {
    if (op == "exists") return actual.kind != CheckValue::Kind::Missing;
    if (op == "not_exists") return actual.kind == CheckValue::Kind::Missing;
    if (actual.kind == CheckValue::Kind::Missing) return false;

    if (op == "between") {
        if (!between_max) return false;
        if (actual.kind != CheckValue::Kind::Number ||
            expected.kind != CheckValue::Kind::Number ||
            between_max->kind != CheckValue::Kind::Number) return false;
        double lo = expected.number;
        double hi = between_max->number;
        if (lo > hi) std::swap(lo, hi);
        return actual.number >= (lo - tolerance) && actual.number <= (hi + tolerance);
    }

    if (actual.kind == CheckValue::Kind::Number && expected.kind == CheckValue::Kind::Number) {
        double a = actual.number;
        double b = expected.number;
        if (op == "==") return std::fabs(a - b) <= tolerance;
        if (op == "!=") return std::fabs(a - b) > tolerance;
        if (op == ">") return a > b;
        if (op == ">=") return a >= b;
        if (op == "<") return a < b;
        if (op == "<=") return a <= b;
        return false;
    }
    if (actual.kind == CheckValue::Kind::Bool && expected.kind == CheckValue::Kind::Bool) {
        if (op == "==") return actual.boolean == expected.boolean;
        if (op == "!=") return actual.boolean != expected.boolean;
        return false;
    }
    if (actual.kind == CheckValue::Kind::String && expected.kind == CheckValue::Kind::String) {
        if (op == "==") return actual.string == expected.string;
        if (op == "!=") return actual.string != expected.string;
        return false;
    }
    return false;
}

static bool resolve_state_path(Graph& graph, Scheduler& scheduler,
                               const std::unordered_map<std::string, int>& incoming_wires,
                               const std::unordered_map<std::string, int>& outgoing_wires,
                               const std::string& path, CheckValue& out) {
    if (path == "graph.node_count") {
        out = cv_number(static_cast<double>(graph.nodes().size()));
        return true;
    }
    const std::string prefix = "nodes.";
    if (path.rfind(prefix, 0) != 0) return false;

    size_t node_end = path.find('.', prefix.size());
    if (node_end == std::string::npos) return false;
    std::string node_id = path.substr(prefix.size(), node_end - prefix.size());
    std::string rest = path.substr(node_end + 1);

    const auto* cg = scheduler.compiled_graph();
    if (!cg) return false;
    const CompiledNode* node = cg->find_node(node_id);
    if (!node) return false;

    if (rest == "domain") {
        out = cv_string(node->is_gpu ? "gpu" : (node->active_cadence == vivid::Cadence::Audio ? "audio" : "control"));
        return true;
    }
    if (rest == "incoming_wires") {
        auto it = incoming_wires.find(node_id);
        out = cv_number(static_cast<double>(it == incoming_wires.end() ? 0 : it->second));
        return true;
    }
    if (rest == "outgoing_wires") {
        auto it = outgoing_wires.find(node_id);
        out = cv_number(static_cast<double>(it == outgoing_wires.end() ? 0 : it->second));
        return true;
    }
    if (rest == "health.errored") {
        out = cv_bool(node->errored || node->missing_operator);
        return true;
    }
    if (rest == "health.missing_operator") {
        out = cv_bool(node->missing_operator);
        return true;
    }
    if (rest == "health.message") {
        out = cv_string(node->error_message);
        return true;
    }
    if (rest == "domain_metrics.audio.rms") {
        auto it = node->output_port_indices.find("rms");
        if (node->active_cadence != vivid::Cadence::Audio || it == node->output_port_indices.end() || it->second >= node->output_values.size())
            return false;
        out = cv_number(node->output_values[it->second]);
        return true;
    }
    if (rest == "domain_metrics.audio.peak") {
        auto it = node->output_port_indices.find("peak");
        if (node->active_cadence != vivid::Cadence::Audio || it == node->output_port_indices.end() || it->second >= node->output_values.size())
            return false;
        out = cv_number(node->output_values[it->second]);
        return true;
    }
    if (rest == "domain_metrics.audio.waveform_length") {
        auto it = node->output_port_indices.find("waveform");
        if (node->active_cadence != vivid::Cadence::Audio || it == node->output_port_indices.end() || it->second >= node->output_spreads.size())
            return false;
        out = cv_number(static_cast<double>(node->output_spreads[it->second].size()));
        return true;
    }

    const std::string param_prefix = "params.";
    if (rest.rfind(param_prefix, 0) == 0) {
        std::string pname = rest.substr(param_prefix.size());
        auto it = node->param_indices.find(pname);
        if (it != node->param_indices.end() && it->second < node->param_values.size()) {
            out = cv_number(node->param_values[it->second]);
            return true;
        }
        auto fit = node->file_param_indices.find(pname);
        if (fit != node->file_param_indices.end() && fit->second < node->file_param_storage.size()) {
            out = cv_string(node->file_param_storage[fit->second]);
            return true;
        }
        return false;
    }

    const std::string output_prefix = "outputs.";
    if (rest.rfind(output_prefix, 0) == 0) {
        size_t sep = rest.find('.', output_prefix.size());
        if (sep == std::string::npos) return false;
        std::string pname = rest.substr(output_prefix.size(), sep - output_prefix.size());
        std::string tail = rest.substr(sep + 1);
        auto it = node->output_port_indices.find(pname);
        if (it == node->output_port_indices.end()) return false;
        uint32_t pi = it->second;
        if (tail == "scalar" && pi < node->output_values.size()) {
            out = cv_number(node->output_values[pi]);
            return true;
        }
        if (tail == "spread.length" && pi < node->output_spreads.size()) {
            out = cv_number(static_cast<double>(node->output_spreads[pi].size()));
            return true;
        }
        return false;
    }
    return false;
}

struct ParsedCheck {
    std::string id;
    std::string type;
    std::string op;
    std::string severity = "warning";
    std::string message;
    std::string path;
    double tolerance = 0.0;
    int64_t for_frames = 1;
    int64_t after_frame = 0;

    bool has_value = false;
    CheckValue value;
    bool has_between_max = false;
    CheckValue between_max;

    bool has_when = false;
    std::string when_path;
    std::string when_op;
    bool has_when_value = false;
    CheckValue when_value;

    std::string finding_id;
    std::string check_diag_severity;
};

static bool parse_check_def(const nlohmann::json& obj, ParsedCheck& out, std::string& err) {
    if (!obj.is_object()) { err = "check must be an object"; return false; }
    if (!obj.contains("id") || !obj["id"].is_string()) { err = "check missing 'id'"; return false; }
    if (!obj.contains("type") || !obj["type"].is_string()) { err = "check missing 'type'"; return false; }
    if (!obj.contains("op") || !obj["op"].is_string()) { err = "check missing 'op'"; return false; }
    out.id = obj["id"].get<std::string>();
    out.type = obj["type"].get<std::string>();
    out.op = obj["op"].get<std::string>();
    if (obj.contains("severity") && obj["severity"].is_string()) out.severity = obj["severity"].get<std::string>();
    if (obj.contains("message") && obj["message"].is_string()) out.message = obj["message"].get<std::string>();
    if (obj.contains("tolerance") && obj["tolerance"].is_number()) out.tolerance = obj["tolerance"].get<double>();
    if (obj.contains("for_frames") && obj["for_frames"].is_number_integer()) out.for_frames = obj["for_frames"].get<int64_t>();
    if (obj.contains("after_frame") && obj["after_frame"].is_number_integer()) out.after_frame = obj["after_frame"].get<int64_t>();

    if (out.type == "state_check") {
        if (!obj.contains("path") || !obj["path"].is_string()) { err = "state_check missing 'path'"; return false; }
        out.path = obj["path"].get<std::string>();
        if (out.op != "exists" && out.op != "not_exists") {
            if (out.op == "between") {
                if (obj.contains("value") && obj["value"].is_array() && obj["value"].size() == 2) {
                    out.has_value = parse_check_value(obj["value"][0], out.value);
                    out.has_between_max = parse_check_value(obj["value"][1], out.between_max);
                } else {
                    if (obj.contains("min")) out.has_value = parse_check_value(obj["min"], out.value);
                    if (obj.contains("max")) out.has_between_max = parse_check_value(obj["max"], out.between_max);
                }
                if (!out.has_value || !out.has_between_max) {
                    err = "state_check 'between' requires numeric min/max (or value[2])";
                    return false;
                }
            } else {
                if (obj.contains("value")) out.has_value = parse_check_value(obj["value"], out.value);
                if (!out.has_value) { err = "state_check missing scalar 'value'"; return false; }
            }
        }
    } else if (out.type == "diagnostic_check") {
        if (obj.contains("check_severity") && obj["check_severity"].is_string())
            out.check_diag_severity = obj["check_severity"].get<std::string>();
        else if (obj.contains("severity") && obj["severity"].is_string())
            out.check_diag_severity = obj["severity"].get<std::string>();
        if (obj.contains("finding_id") && obj["finding_id"].is_string())
            out.finding_id = obj["finding_id"].get<std::string>();
        if (out.finding_id.empty() && obj.contains("check_diagnostics_ids") &&
            obj["check_diagnostics_ids"].is_array() && !obj["check_diagnostics_ids"].empty()) {
            const auto& first = obj["check_diagnostics_ids"][0];
            if (first.is_string()) out.finding_id = first.get<std::string>();
        }
        if (out.op == "count_by_severity_eq" ||
            out.op == "count_by_severity_lte" ||
            out.op == "count_by_severity_gte") {
            if (obj.contains("value")) out.has_value = parse_check_value(obj["value"], out.value);
            if (!out.has_value || out.value.kind != CheckValue::Kind::Number) {
                err = "diagnostic_check count op requires numeric 'value'";
                return false;
            }
            if (out.check_diag_severity.empty()) {
                err = "diagnostic_check count op requires 'check_severity'";
                return false;
            }
        } else if (out.op == "finding_present" || out.op == "finding_absent") {
            if (out.finding_id.empty()) {
                err = "diagnostic_check finding op requires 'finding_id'";
                return false;
            }
        } else {
            err = "unsupported diagnostic_check op";
            return false;
        }
    } else {
        err = "check 'type' must be 'state_check' or 'diagnostic_check'";
        return false;
    }

    if (obj.contains("when")) {
        const auto& when = obj["when"];
        if (!when.is_object()) { err = "'when' must be object"; return false; }
        if (!when.contains("path") || !when["path"].is_string() ||
            !when.contains("op") || !when["op"].is_string()) {
            err = "'when' requires 'path' and 'op'";
            return false;
        }
        out.has_when = true;
        out.when_path = when["path"].get<std::string>();
        out.when_op = when["op"].get<std::string>();
        if (when.contains("value")) out.has_when_value = parse_check_value(when["value"], out.when_value);
    }

    if (out.for_frames < 1) {
        err = "'for_frames' must be >= 1";
        return false;
    }
    if (out.after_frame < 0) {
        err = "'after_frame' must be >= 0";
        return false;
    }
    return true;
}

static std::string handle_validate_checks(const nlohmann::json& root) {
    if (!root.contains("checks") || !root["checks"].is_array()) return json_err("missing 'checks' array");
    const auto& checks = root["checks"];

    nlohmann::json errs = nlohmann::json::array();
    int64_t error_count = 0;

    std::unordered_set<std::string> seen_ids;
    for (size_t idx = 0; idx < checks.size(); ++idx) {
        ParsedCheck pc;
        std::string err;
        if (!parse_check_def(checks[idx], pc, err)) {
            errs.push_back({{"index", static_cast<int64_t>(idx)}, {"message", err}});
            error_count++;
            continue;
        }
        if (!seen_ids.insert(pc.id).second) {
            errs.push_back({{"index", static_cast<int64_t>(idx)}, {"id", pc.id}, {"message", "duplicate check id"}});
            error_count++;
        }
    }

    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["valid"] = (error_count == 0);
    result_obj["error_count"] = error_count;
    result_obj["errors"] = std::move(errs);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

static std::string handle_run_checks(Graph& graph, Scheduler& scheduler, OperatorRegistry& registry, const nlohmann::json& root) {
    if (!root.contains("checks") || !root["checks"].is_array()) return json_err("missing 'checks' array");
    const auto& checks = root["checks"];

    std::vector<ParsedCheck> parsed;
    parsed.reserve(checks.size());
    std::unordered_set<std::string> seen_ids;
    for (size_t idx = 0; idx < checks.size(); ++idx) {
        ParsedCheck pc;
        std::string err;
        if (!parse_check_def(checks[idx], pc, err))
            return json_err("invalid check at index " + std::to_string(idx) + ": " + err);
        if (!seen_ids.insert(pc.id).second)
            return json_err("duplicate check id: " + pc.id);
        parsed.push_back(std::move(pc));
    }
    std::sort(parsed.begin(), parsed.end(), [](const ParsedCheck& a, const ParsedCheck& b) {
        return a.id < b.id;
    });

    std::unordered_map<std::string, int> incoming_wires;
    std::unordered_map<std::string, int> outgoing_wires;
    for (const auto& conn : graph.connections()) {
        incoming_wires[conn.to_node]++;
        outgoing_wires[conn.from_node]++;
    }
    std::vector<DiagnosticFinding> findings = collect_diagnostics(graph, scheduler, registry);

    nlohmann::json results = nlohmann::json::array();

    int64_t passed = 0, failed = 0, skipped = 0;
    int64_t critical_failed = 0, warning_failed = 0, info_failed = 0;
    bool all_passed = true;
    bool all_critical_passed = true;

    for (const auto& c : parsed) {
        bool r_passed = false;
        bool r_skipped = false;
        CheckValue actual;
        CheckValue expected = c.value;
        std::string message = c.message;

        if (c.for_frames > 1) {
            r_skipped = true;
            message = message.empty() ? "for_frames > 1 not yet supported in single-snapshot run" : message;
        }

        if (!r_skipped && c.has_when) {
            CheckValue guard_actual;
            if (!resolve_state_path(graph, scheduler, incoming_wires, outgoing_wires, c.when_path, guard_actual)) {
                r_skipped = true;
                message = message.empty() ? "guard path not found" : message;
            } else {
                CheckValue guard_expect = c.has_when_value ? c.when_value : cv_bool(true);
                if (!eval_compare(guard_actual, c.when_op, guard_expect, 0.0)) {
                    r_skipped = true;
                    message = message.empty() ? "guard condition not met" : message;
                }
            }
        }

        if (!r_skipped && c.type == "state_check") {
            bool has_actual = resolve_state_path(graph, scheduler, incoming_wires, outgoing_wires, c.path, actual);
            if (!has_actual) actual.kind = CheckValue::Kind::Missing;
            if (c.op == "between")
                r_passed = eval_compare(actual, c.op, c.value, c.tolerance, &c.between_max);
            else
                r_passed = eval_compare(actual, c.op, c.value, c.tolerance, nullptr);
        } else if (!r_skipped && c.type == "diagnostic_check") {
            if (c.op == "count_by_severity_eq" ||
                c.op == "count_by_severity_lte" ||
                c.op == "count_by_severity_gte") {
                int64_t count = 0;
                for (const auto& f : findings) if (f.severity == c.check_diag_severity) count++;
                actual = cv_number(static_cast<double>(count));
                int64_t target = static_cast<int64_t>(c.value.number);
                if (c.op == "count_by_severity_eq") r_passed = (count == target);
                else if (c.op == "count_by_severity_lte") r_passed = (count <= target);
                else r_passed = (count >= target);
            } else {
                bool found = false;
                for (const auto& f : findings) {
                    if (f.id == c.finding_id) { found = true; break; }
                }
                actual = cv_bool(found);
                r_passed = (c.op == "finding_present") ? found : !found;
            }
        }

        nlohmann::json row = nlohmann::json::object();
        row["id"] = c.id;
        row["type"] = c.type;
        row["severity"] = c.severity;
        row["passed"] = r_skipped ? false : r_passed;
        row["skipped"] = r_skipped;
        row["op"] = c.op;
        if (!c.path.empty()) row["path"] = c.path;
        if (!message.empty()) row["message"] = message;
        if (c.has_value) add_json_check_value(row, "expected", expected);
        if (c.op == "between" && c.has_between_max) add_json_check_value(row, "expected_max", c.between_max);
        add_json_check_value(row, "actual", actual);
        results.push_back(std::move(row));

        if (r_skipped) {
            skipped++;
        } else if (r_passed) {
            passed++;
        } else {
            failed++;
            all_passed = false;
            if (c.severity == "critical") { critical_failed++; all_critical_passed = false; }
            else if (c.severity == "warning") warning_failed++;
            else info_failed++;
        }
    }

    nlohmann::json summary = {
        {"passed", passed}, {"failed", failed}, {"skipped", skipped},
        {"critical_failed", critical_failed}, {"warning_failed", warning_failed},
        {"info_failed", info_failed}
    };
    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["all_passed"] = all_passed;
    result_obj["all_critical_passed"] = all_critical_passed;
    result_obj["summary"] = std::move(summary);
    result_obj["results"] = std::move(results);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

static std::string handle_list_types(OperatorRegistry& registry) {
    nlohmann::json result = nlohmann::json::object();
    nlohmann::json types_arr = nlohmann::json::array();

    for (const auto& name : registry.type_names()) {
        auto* loader = registry.find(name);
        if (!loader) continue;
        const auto* desc = loader->descriptor();
        if (!desc) continue;

        nlohmann::json t = nlohmann::json::object();
        t["name"] = desc->name;
        t["domain"] = env_str(desc->execution_env);

        // Params
        nlohmann::json params_arr = nlohmann::json::array();
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            const auto& pd = desc->params[i];
            nlohmann::json p = nlohmann::json::object();
            p["name"] = pd.name;
            p["type"] = param_type_str(pd.type);
            p["default"] = static_cast<double>(pd.default_value);
            p["min"] = static_cast<double>(pd.min_value);
            p["max"] = static_cast<double>(pd.max_value);
            if (pd.semantic_tag)
                p["semantic_tag"] = pd.semantic_tag;
            if (pd.semantic_shape)
                p["semantic_shape"] = pd.semantic_shape;
            if (pd.semantic_unit)
                p["semantic_unit"] = pd.semantic_unit;
            if (pd.semantic_intent)
                p["semantic_intent"] = pd.semantic_intent;
            params_arr.push_back(std::move(p));
        }
        t["params"] = std::move(params_arr);

        // Ports split into inputs / outputs
        nlohmann::json inputs_arr = nlohmann::json::array();
        nlohmann::json outputs_arr = nlohmann::json::array();
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            const auto& pd = desc->ports[i];
            nlohmann::json p = nlohmann::json::object();
            p["name"] = pd.name;
            p["type"] = port_type_str(pd.type);
            p["transport"] = transport_str(pd.transport);
            if (pd.type_name)
                p["type_name"] = pd.type_name;
            if (pd.stable_type_id)
                p["stable_type_id"] = pd.stable_type_id;
            if (pd.payload_size > 0)
                p["payload_size"] = pd.payload_size;
            add_port_registry_metadata(p, pd);
            if (pd.direction == VIVID_PORT_INPUT)
                inputs_arr.push_back(std::move(p));
            else
                outputs_arr.push_back(std::move(p));
        }
        t["inputs"] = std::move(inputs_arr);
        t["outputs"] = std::move(outputs_arr);

        types_arr.push_back(std::move(t));
    }

    result["types"] = std::move(types_arr);
    return json_ok(std::move(result));
}

static std::string handle_get_registry_diagnostics(OperatorRegistry& registry) {
    nlohmann::json result = nlohmann::json::object();
    result["schema_version"] = 1;

    uint32_t type_count = 0;
    vivid_list_port_types(nullptr, &type_count);
    std::vector<VividPortTypeInfo> port_types(type_count);
    if (type_count > 0)
        vivid_list_port_types(port_types.data(), &type_count);
    port_types.resize(type_count);
    std::sort(port_types.begin(), port_types.end(),
              [](const VividPortTypeInfo& a, const VividPortTypeInfo& b) {
                  const char* a_id = a.stable_type_id ? a.stable_type_id : "";
                  const char* b_id = b.stable_type_id ? b.stable_type_id : "";
                  int cmp = std::strcmp(a_id, b_id);
                  if (cmp != 0) return cmp < 0;
                  return a.type_id < b.type_id;
              });

    nlohmann::json types_arr = nlohmann::json::array();
    for (const auto& info : port_types) {
        nlohmann::json item = nlohmann::json::object();
        item["type_id"] = info.type_id;
        item["transport"] = transport_str(info.transport);
        item["payload_size"] = info.payload_size;
        item["type_name"] = info.type_name;
        item["stable_type_id"] = info.stable_type_id;
        item["audio_safe"] = (info.audio_safe != 0);
        if (info.package_name && *info.package_name)
            item["package_name"] = info.package_name;
        if (info.description && *info.description)
            item["description"] = info.description;
        types_arr.push_back(std::move(item));
    }
    result["custom_port_types"] = std::move(types_arr);

    auto mismatches = registry.abi_mismatch_diagnostics();
    std::sort(mismatches.begin(), mismatches.end(),
              [](const AbiMismatchDiagnostic& a, const AbiMismatchDiagnostic& b) {
                  if (a.package_name != b.package_name) return a.package_name < b.package_name;
                  if (a.plugin_name != b.plugin_name) return a.plugin_name < b.plugin_name;
                  return a.plugin_path < b.plugin_path;
              });
    nlohmann::json mismatches_arr = nlohmann::json::array();
    for (const auto& diag : mismatches) {
        nlohmann::json item = nlohmann::json::object();
        item["plugin_path"] = diag.plugin_path;
        item["plugin_name"] = diag.plugin_name;
        if (!diag.package_name.empty())
            item["package_name"] = diag.package_name;
        item["plugin_abi"] = diag.plugin_abi;
        item["runtime_abi"] = diag.runtime_abi;
        mismatches_arr.push_back(std::move(item));
    }
    result["abi_mismatch_diagnostics"] = std::move(mismatches_arr);

    auto loader_failures = registry.loader_failure_diagnostics();
    std::sort(loader_failures.begin(), loader_failures.end(),
              [](const LoaderFailureDiagnostic& a, const LoaderFailureDiagnostic& b) {
                  if (a.code != b.code) return a.code < b.code;
                  return a.plugin_path < b.plugin_path;
              });
    nlohmann::json failures_arr = nlohmann::json::array();
    for (const auto& diag : loader_failures) {
        nlohmann::json item = nlohmann::json::object();
        item["plugin_path"] = diag.plugin_path;
        item["plugin_name"] = diag.plugin_name;
        if (!diag.package_name.empty())
            item["package_name"] = diag.package_name;
        item["code"] = diag.code;
        item["message"] = diag.message;
        failures_arr.push_back(std::move(item));
    }
    result["loader_failure_diagnostics"] = std::move(failures_arr);

    return json_ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Dispatch — route method name to handler
// ---------------------------------------------------------------------------

static std::string dispatch(const std::string& method, const std::string& body,
                            RuntimeAPI& api, Graph& graph,
                            Scheduler& scheduler, OperatorRegistry& registry,
                            bool& has_gpu_ops, bool& has_audio,
                            const std::string& src_dir, HotReloader* hot_reloader,
                            PackageManager* package_manager,
                            PackageCompiler* package_compiler,
                            const Settings* settings) {
    // Read-only queries (no body needed)
    if (method == "inspect_graph") return handle_inspect_graph(graph, scheduler);
    if (method == "introspect_nodes") return handle_introspect_nodes(graph, scheduler);
    if (method == "run_diagnostics") return handle_run_diagnostics(graph, scheduler, registry);
    if (method == "list_types")    return handle_list_types(registry);
    if (method == "get_registry_diagnostics") return handle_get_registry_diagnostics(registry);
    if (method == "get_graph_load_diagnostics") return handle_get_graph_load_diagnostics(graph);

    // Parse body JSON (may be empty for some commands)
    nlohmann::json root;
    bool root_valid = false;
    try { root = nlohmann::json::parse(body); root_valid = true; }
    catch (...) {}

    std::string result;

    if (method == "validate_checks") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_validate_checks(root);
    } else if (method == "sample_node_outputs") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_sample_node_outputs(graph, scheduler, root);
    } else if (method == "run_checks") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_run_checks(graph, scheduler, registry, root);
    } else if (method == "add_node") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("type") || !root["type"].is_string() ||
                !root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'type' or 'node_id'");
            else
                result = command_result_to_json(
                    api.add_node(root["type"].get<std::string>(), root["node_id"].get<std::string>()));
        }
    } else if (method == "remove_node") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.remove_node(root["node_id"].get<std::string>()));
        }
    } else if (method == "connect") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("from_addr") || !root["from_addr"].is_string() ||
                !root.contains("to_addr") || !root["to_addr"].is_string())
                result = json_err("missing 'from_addr' or 'to_addr'");
            else {
                const std::string from_addr = root["from_addr"].get<std::string>();
                const std::string to_addr = root["to_addr"].get<std::string>();
                const bool semantic_defaults = root.contains("semantic_defaults") &&
                    root["semantic_defaults"].is_boolean() && root["semantic_defaults"].get<bool>();
                CommandResult cr = api.connect(from_addr, to_addr, semantic_defaults);
                if (!cr.ok) {
                    result = json_err(cr.message);
                } else {
                    nlohmann::json resp = nlohmann::json::object();
                    resp["ok"] = true;
                    resp["message"] = cr.message;

                    bool inferred_applied = false;
                    if (semantic_defaults) {
                        const ConnectionDef* conn = find_connection_by_addr(graph, from_addr, to_addr);
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
                    result = resp.dump();
                }
            }
        }
    } else if (method == "disconnect") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("from_addr") || !root["from_addr"].is_string() ||
                !root.contains("to_addr") || !root["to_addr"].is_string())
                result = json_err("missing 'from_addr' or 'to_addr'");
            else
                result = command_result_to_json(
                    api.disconnect(root["from_addr"].get<std::string>(), root["to_addr"].get<std::string>()));
        }
    } else if (method == "set_connection_remap") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("from_addr") || !root["from_addr"].is_string() ||
                !root.contains("to_addr") || !root["to_addr"].is_string())
                result = json_err("missing 'from_addr' or 'to_addr'");
            else {
                float fmin = root.contains("from_min") && root["from_min"].is_number() ? root["from_min"].get<float>() : 0.0f;
                float fmax = root.contains("from_max") && root["from_max"].is_number() ? root["from_max"].get<float>() : 1.0f;
                float tmin = root.contains("to_min") && root["to_min"].is_number() ? root["to_min"].get<float>() : 0.0f;
                float tmax = root.contains("to_max") && root["to_max"].is_number() ? root["to_max"].get<float>() : 1.0f;
                bool  cval = root.contains("clamp") && root["clamp"].is_boolean() ? root["clamp"].get<bool>() : false;
                result = command_result_to_json(
                    api.set_connection_remap(root["from_addr"].get<std::string>(), root["to_addr"].get<std::string>(),
                                              fmin, fmax, tmin, tmax, cval));
            }
        }
    } else if (method == "set_param") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("value") || !root["value"].is_number())
                result = json_err("missing 'node_id', 'param', or 'value'");
            else
                result = command_result_to_json(
                    api.set_param(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                  root["value"].get<float>()));
        }
    } else if (method == "set_string_param") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("value") || !root["value"].is_string())
                result = json_err("missing 'node_id', 'param', or 'value' (string)");
            else
                result = command_result_to_json(
                    api.set_string_param(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                         root["value"].get<std::string>()));
        }
    } else if (method == "get_param") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                result = json_err("missing 'node_id' or 'param'");
            else {
                auto r = api.get_param(root["node_id"].get<std::string>(), root["param"].get<std::string>());
                if (r.ok) {
                    float v = 0;
                    try { v = std::stof(r.message); } catch (...) {}
                    result = nlohmann::json{{"ok", true}, {"value", static_cast<double>(v)}}.dump();
                } else {
                    result = json_err(r.message);
                }
            }
        }
    } else if (method == "save_graph") {
        // Annotate nodes with package provenance before saving
        if (package_manager) {
            auto packages = package_manager->list();
            std::unordered_map<std::string, std::string> pkg_ver_map;
            for (const auto& p : packages) pkg_ver_map[p.name] = p.version;
            for (auto& node : graph.nodes_mut()) {
                const auto* pkg = registry.package_for_type(node.type);
                if (pkg) {
                    node.pkg_name    = *pkg;
                    node.pkg_version = pkg_ver_map.count(*pkg) ? pkg_ver_map[*pkg] : "";
                }
            }
        }
        if (root_valid && root.contains("path") && root["path"].is_string()) {
            result = command_result_to_json(api.save_as(root["path"].get<std::string>()));
        } else {
            result = command_result_to_json(api.save());
        }
    } else if (method == "load_graph") {
        if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("path") || !root["path"].is_string()) {
                result = json_err("load_graph requires 'path' parameter");
            } else {
                result = command_result_to_json(
                    api.load_graph(root["path"].get<std::string>(), has_gpu_ops, has_audio));
            }
        }
    } else if (method == "set_resolution") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("width") || !root["width"].is_number() ||
                !root.contains("height") || !root["height"].is_number())
                result = json_err("missing 'node_id', 'width', or 'height'");
            else
                result = command_result_to_json(
                    api.set_resolution(root["node_id"].get<std::string>(),
                                       root["width"].get<uint32_t>(),
                                       root["height"].get<uint32_t>()));
        }
    } else if (method == "set_cadence_override") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("cadence") || !root["cadence"].is_number_integer())
                result = json_err("missing 'node_id' or 'cadence' (0=auto, 1=frame, 2=audio)");
            else
                result = command_result_to_json(
                    api.set_cadence_override(root["node_id"].get<std::string>(),
                                             static_cast<uint8_t>(root["cadence"].get<int>())));
        }
    } else if (method == "set_node_layout") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("x") || !root["x"].is_number() ||
                !root.contains("y") || !root["y"].is_number())
                result = json_err("missing 'node_id', 'x', or 'y'");
            else
                result = command_result_to_json(
                    api.set_node_layout(root["node_id"].get<std::string>(),
                                        root["x"].get<float>(),
                                        root["y"].get<float>()));
        }
    } else if (method == "inspect") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.inspect(root["node_id"].get<std::string>()));
        }
    } else if (method == "list_nodes") {
        result = command_result_to_json(api.list_nodes());
    } else if (method == "add_midi_mapping") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("cc") || !root["cc"].is_number() ||
                !root.contains("channel") || !root["channel"].is_number() ||
                !root.contains("range_min") || !root["range_min"].is_number() ||
                !root.contains("range_max") || !root["range_max"].is_number())
                result = json_err("missing or invalid params for add_midi_mapping");
            else
                result = command_result_to_json(
                    api.add_midi_mapping(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                         root["cc"].get<int>(),
                                         root["channel"].get<int>(),
                                         root["range_min"].get<float>(),
                                         root["range_max"].get<float>()));
        }
    } else if (method == "remove_midi_mapping") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                result = json_err("missing 'node_id' or 'param'");
            else
                result = command_result_to_json(
                    api.remove_midi_mapping(root["node_id"].get<std::string>(), root["param"].get<std::string>()));
        }
    } else if (method == "update_midi_mapping") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("range_min") || !root["range_min"].is_number() ||
                !root.contains("range_max") || !root["range_max"].is_number())
                result = json_err("missing or invalid params for update_midi_mapping");
            else
                result = command_result_to_json(
                    api.update_midi_mapping(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                            root["range_min"].get<float>(),
                                            root["range_max"].get<float>()));
        }
    } else if (method == "get_graph_errors") {
        nlohmann::json res = nlohmann::json::object();
        nlohmann::json errs = nlohmann::json::array();
        if (const auto* cg = scheduler.compiled_graph()) {
            for (const auto& cn : cg->nodes) {
                if (!cn.errored) continue;
                errs.push_back({{"node_id", cn.node_id}, {"error", cn.error_message}});
            }
        }
        res["errors"] = std::move(errs);
        result = json_ok(std::move(res));
    } else if (method == "save_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.save_variation(root["name"].get<std::string>()));
        }
    } else if (method == "recall_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.recall_variation(root["name"].get<std::string>()));
        }
    } else if (method == "remove_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.remove_variation(root["name"].get<std::string>()));
        }
    } else if (method == "rename_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("old_name") || !root["old_name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                result = json_err("missing 'old_name' or 'new_name'");
            else
                result = command_result_to_json(
                    api.rename_variation(root["old_name"].get<std::string>(), root["new_name"].get<std::string>()));
        }
    } else if (method == "duplicate_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                result = json_err("missing 'name' or 'new_name'");
            else
                result = command_result_to_json(
                    api.duplicate_variation(root["name"].get<std::string>(), root["new_name"].get<std::string>()));
        }
    } else if (method == "move_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                result = json_err("missing 'name' or 'to_index'");
            else
                result = command_result_to_json(
                    api.move_variation(root["name"].get<std::string>(), root["to_index"].get<int64_t>()));
        }
    } else if (method == "update_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.update_variation(root["name"].get<std::string>()));
        }
    } else if (method == "list_variations") {
        result = command_result_to_json(api.list_variations());
    } else if (method == "queue_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string q = (root.contains("quantize") && root["quantize"].is_string())
                    ? root["quantize"].get<std::string>() : "instant";
                result = command_result_to_json(api.queue_variation(root["name"].get<std::string>(), q));
            }
        }
    } else if (method == "set_quantize_clock") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.set_quantize_clock(root["node_id"].get<std::string>()));
        }
    } else if (method == "save_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.save_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "recall_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.recall_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "update_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.update_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "remove_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.remove_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "rename_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("old_name") || !root["old_name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                result = json_err("missing 'node_id', 'old_name', or 'new_name'");
            else
                result = command_result_to_json(
                    api.rename_preset(root["node_id"].get<std::string>(), root["old_name"].get<std::string>(),
                                      root["new_name"].get<std::string>()));
        }
    } else if (method == "list_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.list_presets(root["node_id"].get<std::string>()));
        }
    } else if (method == "list_factory_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.list_factory_presets(root["node_id"].get<std::string>()));
        }
    } else if (method == "set_param_lock") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("flags") || !root["flags"].is_number())
                result = json_err("missing 'node_id', 'param', or 'flags'");
            else
                result = command_result_to_json(
                    api.set_param_lock(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                       static_cast<uint8_t>(root["flags"].get<int64_t>())));
        }
    } else if (method == "get_param_lock") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                result = json_err("missing 'node_id' or 'param'");
            else
                result = command_result_to_json(
                    api.get_param_lock(root["node_id"].get<std::string>(), root["param"].get<std::string>()));
        }
    } else if (method == "set_state_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string() ||
                !root.contains("state_idx") || !root["state_idx"].is_number() ||
                !root.contains("target_node") || !root["target_node"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'sm_node', 'state_idx', 'target_node', or 'name'");
            else
                result = command_result_to_json(
                    api.set_state_preset(root["sm_node"].get<std::string>(),
                                         root["state_idx"].get<int>(),
                                         root["target_node"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "remove_state_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string() ||
                !root.contains("state_idx") || !root["state_idx"].is_number() ||
                !root.contains("target_node") || !root["target_node"].is_string())
                result = json_err("missing 'sm_node', 'state_idx', or 'target_node'");
            else
                result = command_result_to_json(
                    api.remove_state_preset(root["sm_node"].get<std::string>(),
                                            root["state_idx"].get<int>(),
                                            root["target_node"].get<std::string>()));
        }
    } else if (method == "clear_state_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                result = json_err("missing 'sm_node'");
            else
                result = command_result_to_json(
                    api.clear_state_presets(root["sm_node"].get<std::string>()));
        }
    } else if (method == "inspect_state_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                result = json_err("missing 'sm_node'");
            else
                result = command_result_to_json(
                    api.inspect_state_presets(root["sm_node"].get<std::string>()));
        }
    } else if (method == "scaffold_operator") {
        result = [&]() -> std::string {
            if (src_dir.empty())
                return json_err("scaffold_operator requires --src-dir");
            if (!root_valid)
                return json_err("invalid JSON body");

            if (!root.contains("name") || !root["name"].is_string())
                return json_err("missing 'name'");
            // Accept both "env" (preferred) and "domain" (legacy) keys
            std::string env_key = root.contains("env") ? "env" : "domain";
            if (!root.contains(env_key) || !root[env_key].is_string())
                return json_err("missing 'env' (or 'domain')");

            std::string name = root["name"].get<std::string>();
            std::string env_str_val = root[env_key].get<std::string>();

            VividExecutionEnv env;
            if (env_str_val == "control")      env = VIVID_ENV_FRAME;
            else if (env_str_val == "audio")   env = VIVID_ENV_AUDIO;
            else if (env_str_val == "gpu")     env = VIVID_ENV_GPU;
            else return json_err("env must be 'control', 'audio', or 'gpu'");

            // Optional variant (e.g. "composite")
            std::string variant;
            if (root.contains("variant") && root["variant"].is_string())
                variant = root["variant"].get<std::string>();

            std::string destination = "auto";
            if (root.contains("destination") && root["destination"].is_string())
                destination = root["destination"].get<std::string>();

            std::string err = OperatorCreator::validate_name(name, registry);
            if (!err.empty()) return json_err(err);

            const std::vector<PackageInfo> packages =
                package_manager ? package_manager->list() : std::vector<PackageInfo>{};
            OperatorDestination resolved;
            std::string resolve_error;
            if (!resolve_operator_destination(destination, src_dir, packages, settings,
                                              resolved, resolve_error)) {
                return json_err(resolve_error);
            }
            if (!resolved.warning.empty()) {
                std::fprintf(stderr, "[vivid] %s\n", resolved.warning.c_str());
            }

            VividCreateOperatorRequest req;
            req.name = name;
            req.env = env;
            req.variant = variant;
            req.destination = destination;

            auto parse_port_type = [&](const std::string& type_str, VividExecutionEnv e, VividPortType& out) -> std::string {
                if (e == VIVID_ENV_FRAME) {
                    if      (type_str == "float")         out = VIVID_PORT_SIGNAL;
                    else if (type_str == "int")           out = VIVID_PORT_SIGNAL;
                    else if (type_str == "bool")          out = VIVID_PORT_SIGNAL;
                    else if (type_str == "spread")        out = VIVID_PORT_SPREAD;
                    else if (type_str == "string")        out = VIVID_PORT_STRING;
                    else if (type_str == "string_spread") out = VIVID_PORT_STRING_SPREAD;
                    else return "unknown control port type '" + type_str + "'";
                } else if (e == VIVID_ENV_AUDIO) {
                    if (type_str == "float") out = VIVID_PORT_AUDIO;
                    else return "unknown audio port type '" + type_str + "'";
                } else if (e == VIVID_ENV_GPU) {
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
                    std::string err = parse_port_type(ptype, env, vt);
                    if (!err.empty()) return err;
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

            if (hot_reloader) {
                if (resolved.package_layout && !resolved.package_name.empty())
                    hot_reloader->queue_rebuild("pkg:" + resolved.package_name + ":" + cr.target_name);
                else
                    hot_reloader->queue_rebuild(cr.target_name);
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
        }();
    } else if (method == "install_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("url") || !root["url"].is_string())
                result = json_err("missing 'url'");
            else {
                auto ir = package_manager->install(root["url"].get<std::string>());
                if (ir.success) {
                    nlohmann::json res = nlohmann::json::object();
                    res["name"] = ir.info.name;
                    res["version"] = ir.info.version;
                    if (!ir.info.vivid_core.empty())
                        res["vivid_core"] = ir.info.vivid_core;
                    res["operator_count"] = static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size());
                    result = json_ok(std::move(res));
                    // Auto-reload graph if it has missing operators
                    if (const auto* cg = scheduler.compiled_graph()) {
                        for (const auto& cn : cg->nodes) {
                            if (cn.missing_operator) {
                                auto rr = api.reload(has_gpu_ops, has_audio);
                                if (!rr.ok) {
                                    result = json_err("package installed but runtime refresh failed: " + rr.message);
                                }
                                break;
                            }
                        }
                    }
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "uninstall_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string snapshot_json;
                std::string snapshot_error;
                if (!capture_live_graph_snapshot(graph, snapshot_json, snapshot_error)) {
                    result = json_err(snapshot_error);
                } else if (package_manager->uninstall(root["name"].get<std::string>())) {
                    auto rr = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
                    if (!rr.ok)
                        result = json_err("package uninstalled but runtime refresh failed: " + rr.message);
                    else
                        result = json_ok_msg("uninstalled");
                } else {
                    result = json_err("failed to uninstall package");
                }
            }
        }
    } else if (method == "link_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("path") || !root["path"].is_string())
                result = json_err("missing 'path'");
            else {
                auto ir = package_manager->link(root["path"].get<std::string>());
                if (ir.success) {
                    nlohmann::json res = nlohmann::json::object();
                    res["name"] = ir.info.name;
                    res["version"] = ir.info.version;
                    if (!ir.info.vivid_core.empty())
                        res["vivid_core"] = ir.info.vivid_core;
                    res["operator_count"] = static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size());
                    res["linked"] = true;
                    result = json_ok(std::move(res));
                    // Auto-reload graph if it has missing operators
                    if (const auto* cg = scheduler.compiled_graph()) {
                        for (const auto& cn : cg->nodes) {
                            if (cn.missing_operator) {
                                auto rr = api.reload(has_gpu_ops, has_audio);
                                if (!rr.ok) {
                                    result = json_err("package linked but runtime refresh failed: " + rr.message);
                                }
                                break;
                            }
                        }
                    }
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "unlink_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string snapshot_json;
                std::string snapshot_error;
                if (!capture_live_graph_snapshot(graph, snapshot_json, snapshot_error)) {
                    result = json_err(snapshot_error);
                } else if (package_manager->unlink(root["name"].get<std::string>())) {
                    auto rr = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
                    if (!rr.ok)
                        result = json_err("package unlinked but runtime refresh failed: " + rr.message);
                    else
                        result = json_ok_msg("unlinked");
                } else {
                    result = json_err("failed to unlink package");
                }
            }
        }
    } else if (method == "rebuild_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                const std::string pkg_name = root["name"].get<std::string>();
                std::string snapshot_json;
                std::string snapshot_error;
                if (!capture_live_graph_snapshot(graph, snapshot_json, snapshot_error)) {
                    result = json_err(snapshot_error);
                } else {
                    auto ir = package_manager->rebuild(pkg_name);
                    if (ir.success) {
                        std::error_code build_ec;
                        std::string pkg_path = package_manager->resolve_package_path(pkg_name);
                        if (!pkg_path.empty()) {
                            std::string build_root = std::filesystem::canonical(pkg_path, build_ec).string();
                            if (build_ec || build_root.empty())
                                build_root = pkg_path;
                            registry.clear_deferred_probe_handles_for_dir(build_root + "/build");
                        }
                        auto rr = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
                        if (!rr.ok) {
                            result = json_err("package rebuilt but runtime refresh failed: " + rr.message);
                        } else {
                            scheduler.tick(0.0, 0.016, 0);
                            nlohmann::json res = {
                                {"name", ir.info.name},
                                {"operator_count", static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size())},
                                {"linked", ir.info.linked}
                            };
                            result = json_ok(std::move(res));
                        }
                    } else {
                        result = json_err(ir.error);
                    }
                }
            }
        }
    } else if (method == "list_packages") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else {
            auto packages = package_manager->list();
            nlohmann::json res = nlohmann::json::object();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& pkg : packages) {
                nlohmann::json p = nlohmann::json::object();
                p["name"] = pkg.name;
                p["version"] = pkg.version;
                if (!pkg.vivid_core.empty()) p["vivid_core"] = pkg.vivid_core;
                if (!pkg.source_scope.empty()) p["source_scope"] = pkg.source_scope;
                if (!pkg.path.empty()) p["path"] = pkg.path;
                if (!pkg.build_type.empty()) p["build_type"] = pkg.build_type;
                p["description"] = pkg.description;
                p["author"] = pkg.author;
                nlohmann::json ops = nlohmann::json::array();
                for (const auto& op : pkg.operators) ops.push_back(op);
                for (const auto& op : pkg.gpu_operators) ops.push_back(op);
                p["operators"] = std::move(ops);
                p["linked"] = pkg.linked;
                arr.push_back(std::move(p));
            }
            res["packages"] = std::move(arr);
            result = json_ok(std::move(res));
        }
    } else if (method == "read_package_docs") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string name = root["name"].get<std::string>();
                if (!is_safe_package_name(name)) {
                    result = json_err("invalid package name");
                } else if (!package_manager->is_installed(name)) {
                    result = json_err("package not installed: " + name);
                } else {
                    auto readme_path = std::filesystem::path(PackageManager::packages_dir()) / name / "README.md";
                    std::ifstream f(readme_path);
                    if (!f.is_open()) {
                        result = json_ok_msg("No README.md found for package '" + name + "'");
                    } else {
                        std::ostringstream ss;
                        ss << f.rdbuf();
                        result = json_ok(nlohmann::json{{"name", name}, {"content", ss.str()}});
                    }
                }
            }
        }
    } else if (method == "list_package_examples") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string name = root["name"].get<std::string>();
                if (!is_safe_package_name(name)) {
                    result = json_err("invalid package name");
                } else if (!package_manager->is_installed(name)) {
                    result = json_err("package not installed: " + name);
                } else {
                    auto graphs_dir = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs";
                    nlohmann::json res = nlohmann::json::object();
                    res["name"] = name;
                    nlohmann::json arr = nlohmann::json::array();
                    std::error_code ec;
                    if (std::filesystem::is_directory(graphs_dir, ec)) {
                        for (const auto& entry : std::filesystem::directory_iterator(graphs_dir, ec)) {
                            if (!entry.is_regular_file()) continue;
                            if (entry.path().extension() != ".json") continue;
                            nlohmann::json ex = nlohmann::json::object();
                            ex["filename"] = entry.path().filename().string();
                            std::string desc_str;
                            std::ifstream f(entry.path());
                            if (f.is_open()) {
                                std::ostringstream ss;
                                ss << f.rdbuf();
                                auto content = ss.str();
                                try {
                                    auto gdoc = nlohmann::json::parse(content);
                                    if (gdoc.contains("description") && gdoc["description"].is_string())
                                        desc_str = gdoc["description"].get<std::string>();
                                } catch (...) {}
                            }
                            ex["description"] = desc_str;
                            arr.push_back(std::move(ex));
                        }
                    }
                    res["examples"] = std::move(arr);
                    result = json_ok(std::move(res));
                }
            }
        }
    } else if (method == "read_package_example") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string() ||
                !root.contains("filename") || !root["filename"].is_string())
                result = json_err("missing 'name' or 'filename'");
            else {
                std::string name = root["name"].get<std::string>();
                std::string filename = root["filename"].get<std::string>();
                if (!is_safe_package_name(name)) {
                    result = json_err("invalid package name");
                } else if (filename.find('/') != std::string::npos ||
                    filename.find('\\') != std::string::npos ||
                    filename.find("..") != std::string::npos) {
                    result = json_err("invalid filename");
                } else if (!package_manager->is_installed(name)) {
                    result = json_err("package not installed: " + name);
                } else {
                    auto file_path = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs" / filename;
                    std::ifstream f(file_path);
                    if (!f.is_open()) {
                        result = json_err("example not found: " + filename);
                    } else {
                        std::ostringstream ss;
                        ss << f.rdbuf();
                        result = json_ok(nlohmann::json{{"name", name}, {"filename", filename}, {"content", ss.str()}});
                    }
                }
            }
        }
    } else if (method == "package_operator_docs") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string name = root["name"].get<std::string>();
                if (!package_manager->is_installed(name)) {
                    result = json_err("package not installed: " + name);
                } else {
                    nlohmann::json res = nlohmann::json::object();
                    res["package"] = name;
                    nlohmann::json ops_arr = nlohmann::json::array();
                    for (const auto& type_name : registry.type_names()) {
                        const auto* pkg = registry.package_for_type(type_name);
                        if (!pkg || *pkg != name) continue;
                        const auto* desc = registry.probe_descriptor(type_name);
                        if (!desc) continue;

                        nlohmann::json op = nlohmann::json::object();
                        op["name"] = desc->name;
                        op["domain"] = env_str(desc->execution_env);
                        op["time_dependent"] = (desc->time_dependent != 0);

                        nlohmann::json params_arr = nlohmann::json::array();
                        for (uint32_t i = 0; i < desc->param_count; ++i) {
                            const auto& pd = desc->params[i];
                            nlohmann::json p = nlohmann::json::object();
                            p["name"] = pd.name;
                            p["type"] = param_type_str(pd.type);
                            p["default"] = static_cast<double>(pd.default_value);
                            p["min"] = static_cast<double>(pd.min_value);
                            p["max"] = static_cast<double>(pd.max_value);
                            if (pd.semantic_tag) p["semantic_tag"] = pd.semantic_tag;
                            if (pd.semantic_shape) p["semantic_shape"] = pd.semantic_shape;
                            if (pd.semantic_unit) p["semantic_unit"] = pd.semantic_unit;
                            if (pd.semantic_intent) p["semantic_intent"] = pd.semantic_intent;
                            if (pd.default_string) p["default_string"] = pd.default_string;
                            if (pd.group) p["group"] = pd.group;
                            if (pd.choice_count > 0 && pd.choice_labels) {
                                nlohmann::json choices = nlohmann::json::array();
                                for (uint32_t c = 0; c < pd.choice_count; ++c)
                                    choices.push_back(pd.choice_labels[c]);
                                p["choices"] = std::move(choices);
                            }
                            params_arr.push_back(std::move(p));
                        }
                        op["params"] = std::move(params_arr);

                        nlohmann::json inputs_arr = nlohmann::json::array();
                        nlohmann::json outputs_arr = nlohmann::json::array();
                        for (uint32_t i = 0; i < desc->port_count; ++i) {
                            const auto& portd = desc->ports[i];
                            nlohmann::json p = nlohmann::json::object();
                            p["name"] = portd.name;
                            p["type"] = port_type_str(portd.type);
                            p["transport"] = transport_str(portd.transport);
                            if (portd.type_name) p["type_name"] = portd.type_name;
                            if (portd.stable_type_id) p["stable_type_id"] = portd.stable_type_id;
                            if (portd.payload_size > 0) p["payload_size"] = portd.payload_size;
                            if (portd.direction == VIVID_PORT_INPUT)
                                inputs_arr.push_back(std::move(p));
                            else
                                outputs_arr.push_back(std::move(p));
                        }
                        op["inputs"] = std::move(inputs_arr);
                        op["outputs"] = std::move(outputs_arr);

                        ops_arr.push_back(std::move(op));
                    }
                    res["operators"] = std::move(ops_arr);
                    result = json_ok(std::move(res));
                }
            }
        }
    } else if (method == "test_package") {
        if (!package_manager || !package_compiler) {
            result = json_err("package manager/compiler not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string name = root["name"].get<std::string>();
                auto tr = run_package_tests(name, *package_manager,
                                             *package_compiler, registry);
                if (!tr.error.empty()) {
                    result = json_err(tr.error);
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
                    result = json_ok(std::move(res));
                }
            }
        }
    } else if (method == "set_solo") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            std::string node_id_str;
            if (root.contains("node_id") && root["node_id"].is_string())
                node_id_str = root["node_id"].get<std::string>();
            result = command_result_to_json(api.set_solo(node_id_str));
        }
    } else if (method == "get_solo") {
        std::string solo_id = api.solo_node_id();
        result = nlohmann::json{{"ok", true}, {"node_id", solo_id}, {"active", !solo_id.empty()}}.dump();
    } else if (method == "add_sticky_note") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("text") || !root["text"].is_string() ||
                !root.contains("x") || !root["x"].is_number() ||
                !root.contains("y") || !root["y"].is_number())
                result = json_err("missing 'text', 'x', or 'y'");
            else {
                StickyNoteDef note;
                if (root.contains("id") && root["id"].is_string())
                    note.id = root["id"].get<std::string>();
                else
                    note.id = "sticky_" + std::to_string(graph.sticky_notes().size() + 1);
                note.text = root["text"].get<std::string>();
                note.x = root["x"].get<float>();
                note.y = root["y"].get<float>();
                if (root.contains("width") && root["width"].is_number()) note.width = root["width"].get<float>();
                if (root.contains("height") && root["height"].is_number()) note.height = root["height"].get<float>();
                if (root.contains("color") && root["color"].is_number_integer()) note.color = root["color"].get<int>();
                std::string assigned_id = note.id;
                graph.add_sticky_note(std::move(note));
                result = json_ok(nlohmann::json{{"id", assigned_id}});
            }
        }
    } else if (method == "list_sticky_notes") {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& sn : graph.sticky_notes()) {
            arr.push_back({
                {"id", sn.id}, {"text", sn.text},
                {"x", static_cast<double>(sn.x)}, {"y", static_cast<double>(sn.y)},
                {"width", static_cast<double>(sn.width)}, {"height", static_cast<double>(sn.height)},
                {"color", sn.color}
            });
        }
        result = json_ok(std::move(arr));
    } else if (method == "update_sticky_note") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("id") || !root["id"].is_string())
                result = json_err("missing 'id'");
            else {
                auto* sn = graph.find_sticky_note(root["id"].get<std::string>());
                if (!sn)
                    result = json_err("sticky note not found");
                else {
                    if (root.contains("text") && root["text"].is_string()) sn->text = root["text"].get<std::string>();
                    if (root.contains("x") && root["x"].is_number()) sn->x = root["x"].get<float>();
                    if (root.contains("y") && root["y"].is_number()) sn->y = root["y"].get<float>();
                    if (root.contains("width") && root["width"].is_number()) sn->width = root["width"].get<float>();
                    if (root.contains("height") && root["height"].is_number()) sn->height = root["height"].get<float>();
                    if (root.contains("color") && root["color"].is_number_integer()) sn->color = root["color"].get<int>();
                    result = json_ok_msg("updated");
                }
            }
        }
    } else if (method == "remove_sticky_note") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("id") || !root["id"].is_string())
                result = json_err("missing 'id'");
            else {
                bool removed = graph.remove_sticky_note(root["id"].get<std::string>());
                if (removed) result = json_ok_msg("removed");
                else result = json_err("sticky note not found");
            }
        }
    } else {
        result = json_err("unknown method '" + method + "'");
    }

    return result;
}

// ---------------------------------------------------------------------------
// Pimpl
// ---------------------------------------------------------------------------

struct ControlServer::Impl {
    struct PendingRequest {
        std::string method;
        std::string body;
        std::promise<std::string> promise;
    };

    ix::HttpServer server;
    std::mutex queue_mutex;
    std::deque<PendingRequest> queue;
    std::atomic<bool> running{false};
    vivid::UndoManager undo_history{200};

    Impl(int port) : server(port, "127.0.0.1") {}
};

// ---------------------------------------------------------------------------
// ControlServer lifecycle
// ---------------------------------------------------------------------------

ControlServer::ControlServer() = default;
ControlServer::~ControlServer() { stop(); }

void ControlServer::set_src_dir(const std::string& src_dir) { src_dir_ = src_dir; }
void ControlServer::set_hot_reloader(HotReloader* hr) { hot_reloader_ = hr; }
void ControlServer::set_capture_coordinator(CaptureCoordinator* cc) { assert(!impl_); capture_coordinator_ = cc; }
void ControlServer::set_package_manager(PackageManager* pm) { assert(!impl_); package_manager_ = pm; }
void ControlServer::set_package_compiler(PackageCompiler* pc) { assert(!impl_); package_compiler_ = pc; }
void ControlServer::set_package_catalog(PackageCatalog* cat) { assert(!impl_); package_catalog_ = cat; }
void ControlServer::set_app_update_manager(AppUpdateManager* aum) { assert(!impl_); app_update_manager_ = aum; }
void ControlServer::set_settings(const Settings* settings) { assert(!impl_); settings_ = settings; }

uint64_t ControlServer::mcp_last_ping_ms(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mcp_ping_mutex_);
    auto it = mcp_last_ping_ms_.find(name);
    return (it != mcp_last_ping_ms_.end()) ? it->second : 0;
}

bool ControlServer::start(int port) {
    impl_ = std::make_unique<Impl>(port);

    impl_->server.setOnConnectionCallback(
        [this](ix::HttpRequestPtr request,
               std::shared_ptr<ix::ConnectionState>) -> ix::HttpResponsePtr
        {
            // Reject non-POST
            if (request->method != "POST") {
                return std::make_shared<ix::HttpResponse>(
                    405, "Method Not Allowed", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    R"({"ok":false,"error":"use POST"})");
            }

            // Reject if shutting down
            if (!impl_->running) {
                return std::make_shared<ix::HttpResponse>(
                    503, "Shutting Down", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    R"({"ok":false,"error":"server shutting down"})");
            }

            // Method name is the URI path without leading /
            std::string method = request->uri;
            if (!method.empty() && method[0] == '/')
                method = method.substr(1);

            // MCP heartbeat ping — immediate, no main-thread dispatch needed
            if (method == "mcp_ping") {
                std::string server_name;
                try {
                    auto pdoc = nlohmann::json::parse(request->body);
                    if (pdoc.contains("server") && pdoc["server"].is_string())
                        server_name = pdoc["server"].get<std::string>();
                } catch (...) {}
                if (!server_name.empty()) {
                    auto now_ms = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    std::lock_guard<std::mutex> lk(mcp_ping_mutex_);
                    mcp_last_ping_ms_[server_name] = now_ms;
                }
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    R"({"ok":true})");
            }

            // Recording tap start/stop (immediate, no main-thread dispatch needed)
            if (capture_coordinator_ &&
                (method == "start_recording_tap" || method == "stop_recording_tap")) {
                std::string response_body = (method == "start_recording_tap")
                    ? capture_coordinator_->handle_start_recording_tap()
                    : capture_coordinator_->handle_stop_recording_tap();
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Recording start/stop — routed through coordinator's pending queue (needs main thread)
            if (capture_coordinator_ &&
                (method == "start_recording" || method == "stop_recording")) {
                std::future<std::string> future;
                if (method == "start_recording") {
                    std::string path = "/tmp/vivid_recording.mov";
                    double fps = 60.0;
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("path") && doc["path"].is_string()) {
                            std::string candidate = doc["path"].get<std::string>();
                            if (!is_safe_recording_path(candidate)) {
                                return std::make_shared<ix::HttpResponse>(
                                    200, "OK", ix::HttpErrorCode::Ok,
                                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                                    R"({"ok":false,"error":"invalid recording path"})");
                            }
                            path = candidate;
                        }
                        if (doc.contains("fps") && doc["fps"].is_number())
                            fps = doc["fps"].get<double>();
                    } catch (...) {}
                    future = capture_coordinator_->request_start_recording(path, fps);
                } else {
                    future = capture_coordinator_->request_stop_recording();
                }

                auto status = future.wait_for(std::chrono::seconds(kRecordingTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Capture methods bypass normal dispatch — route to CaptureCoordinator
            if (capture_coordinator_ &&
                (method == "capture_frame" || method == "capture_audio" ||
                 method == "capture_av" || method == "capture_interface")) {
                CaptureType ctype = CaptureType::Frame;
                float audio_dur = 1.0f;
                std::string node_id;
                std::string save_path;
                bool ensure_ui_visible = true;
                if (method == "capture_audio") ctype = CaptureType::Audio;
                else if (method == "capture_av") ctype = CaptureType::AV;

                // Parse optional duration from body
                if (ctype == CaptureType::Audio || ctype == CaptureType::AV || method == "capture_interface") {
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("duration") && doc["duration"].is_number())
                            audio_dur = doc["duration"].get<float>();
                        if (method == "capture_interface") {
                            if (doc.contains("node_id") && doc["node_id"].is_string())
                                node_id = doc["node_id"].get<std::string>();
                            if (doc.contains("save_path") && doc["save_path"].is_string()) {
                                std::string candidate = doc["save_path"].get<std::string>();
                                if (!candidate.empty() && !is_safe_capture_image_path(candidate)) {
                                    return std::make_shared<ix::HttpResponse>(
                                        200, "OK", ix::HttpErrorCode::Ok,
                                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                                        R"({"ok":false,"error":"invalid save_path"})");
                                }
                                save_path = candidate;
                            }
                            if (doc.contains("ensure_ui_visible") && doc["ensure_ui_visible"].is_boolean())
                                ensure_ui_visible = doc["ensure_ui_visible"].get<bool>();
                        }
                    } catch (...) {}
                }

                std::future<std::string> future;
                if (method == "capture_interface") {
                    future = capture_coordinator_->request_interface_capture(
                        node_id, save_path, ensure_ui_visible);
                } else {
                    future = capture_coordinator_->request_capture(ctype, audio_dur);
                }
                auto status = future.wait_for(std::chrono::seconds(method == "capture_interface" ? kInterfaceCaptureTimeoutSec : kCaptureTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Analysis endpoints — route through CaptureCoordinator
            if (capture_coordinator_ &&
                (method == "analyze_output" || method == "compare_outputs")) {

                // Parse request JSON
                AnalysisMode amode = AnalysisMode::Frame;
                float window_seconds = 1.0f;
                float window_a = 1.0f, window_b = 1.0f;
                bool include_payload = false;
                std::string node_id;
                bool parse_ok = true;

                try {
                    auto doc = nlohmann::json::parse(request->body);
                    if (doc.contains("mode") && doc["mode"].is_string()) {
                        std::string mode_str = doc["mode"].get<std::string>();
                        if (mode_str == "audio") amode = AnalysisMode::Audio;
                        else if (mode_str == "av") amode = AnalysisMode::AV;
                        else if (mode_str == "frame") amode = AnalysisMode::Frame;
                        else parse_ok = false;
                    }

                    if (doc.contains("window_seconds") && doc["window_seconds"].is_number())
                        window_seconds = doc["window_seconds"].get<float>();

                    if (doc.contains("include_payload") && doc["include_payload"].is_boolean())
                        include_payload = doc["include_payload"].get<bool>();

                    if (doc.contains("node_id") && doc["node_id"].is_string())
                        node_id = doc["node_id"].get<std::string>();

                    // For compare_outputs, parse a/b sub-objects
                    if (method == "compare_outputs") {
                        if (doc.contains("a") && doc["a"].is_object()) {
                            const auto& a_v = doc["a"];
                            if (a_v.contains("window_seconds") && a_v["window_seconds"].is_number())
                                window_a = a_v["window_seconds"].get<float>();
                        }
                        if (doc.contains("b") && doc["b"].is_object()) {
                            const auto& b_v = doc["b"];
                            if (b_v.contains("window_seconds") && b_v["window_seconds"].is_number())
                                window_b = b_v["window_seconds"].get<float>();
                        }
                    }
                } catch (...) {}

                if (!parse_ok) {
                    return std::make_shared<ix::HttpResponse>(
                        200, "OK", ix::HttpErrorCode::Ok,
                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                        R"json({"ok":false,"error":"invalid mode (expected 'frame', 'audio', or 'av')"})json");
                }

                std::future<std::string> future;
                if (method == "analyze_output")
                    future = capture_coordinator_->request_analyze(amode, window_seconds, include_payload, node_id);
                else
                    future = capture_coordinator_->request_compare(amode, window_a, window_b, include_payload, node_id);

                auto status = future.wait_for(std::chrono::seconds(kAnalysisTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Package catalog — thread-safe, no main-thread dispatch needed
            if (method == "package_catalog" && package_catalog_) {
                auto entries = package_catalog_->entries();
                nlohmann::json resp = nlohmann::json::object();
                resp["ok"] = true;
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& e : entries) {
                    nlohmann::json obj = nlohmann::json::object();
                    obj["name"] = e.name;
                    obj["description"] = e.description;
                    obj["version"] = e.version;
                    if (!e.vivid_core.empty()) obj["vivid_core"] = e.vivid_core;
                    obj["author"] = e.author;
                    obj["url"] = e.url;
                    if (!e.category.empty()) obj["category"] = e.category;
                    if (!e.description_short.empty()) obj["description_short"] = e.description_short;
                    if (!e.status.empty()) obj["status"] = e.status;
                    if (!e.status_note.empty()) obj["status_note"] = e.status_note;
                    if (!e.preview_image_url.empty()) obj["preview_image_url"] = e.preview_image_url;
                    if (!e.repo_url.empty()) obj["repo_url"] = e.repo_url;
                    if (!e.homepage_url.empty()) obj["homepage_url"] = e.homepage_url;
                    if (!e.install_url.empty()) obj["install_url"] = e.install_url;
                    obj["installed"] = e.installed;
                    if (e.installed) obj["installed_version"] = e.installed_version;
                    arr.push_back(std::move(obj));
                }
                resp["packages"] = std::move(arr);
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    resp.dump());
            }

            // Check package updates using catalog metadata + installed package versions
            if (method == "check_package_updates" && package_catalog_ && package_manager_) {
                std::string core_version = "0.1.0";
                bool include_all_installed = false;
                if (!request->body.empty()) {
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("core_version") && doc["core_version"].is_string())
                            core_version = doc["core_version"].get<std::string>();
                        if (doc.contains("include_all_installed") && doc["include_all_installed"].is_boolean())
                            include_all_installed = doc["include_all_installed"].get<bool>();
                    } catch (...) {}
                }

                auto entries = package_catalog_->entries();
                nlohmann::json resp = nlohmann::json::object();
                resp["ok"] = true;
                resp["core_version"] = core_version;

                nlohmann::json updates = nlohmann::json::array();
                int64_t update_count = 0;
                int64_t incompatible_count = 0;
                for (const auto& e : entries) {
                    if (!e.installed) continue;

                    PackageInfo installed;
                    installed.name = e.name;
                    installed.version = e.installed_version;
                    auto assessment = PackageManager::assess_update(
                        installed, e.version, e.vivid_core, core_version);

                    if (!include_all_installed && !assessment.update_available) continue;

                    nlohmann::json obj = nlohmann::json::object();
                    obj["name"] = assessment.package_name;
                    obj["installed_version"] = assessment.installed_version;
                    obj["remote_version"] = assessment.remote_version;
                    if (!assessment.remote_vivid_core.empty())
                        obj["vivid_core"] = assessment.remote_vivid_core;
                    obj["update_available"] = assessment.update_available;
                    obj["compatible"] = assessment.compatible;
                    obj["constraint_valid"] = assessment.constraint_valid;
                    obj["classification"] = update_class_str(assessment.classification);
                    obj["message"] = assessment.message;

                    if (assessment.update_available) update_count++;
                    if (assessment.classification == PackageUpdateClass::IncompatibleUpdate)
                        incompatible_count++;

                    updates.push_back(std::move(obj));
                }
                resp["updates_available"] = update_count;
                resp["incompatible_updates"] = incompatible_count;
                resp["packages"] = std::move(updates);

                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    resp.dump());
            }

            // Check core app updates using appcast metadata.
            if (method == "check_core_updates") {
                if (!app_update_manager_) {
                    return std::make_shared<ix::HttpResponse>(
                        200, "OK", ix::HttpErrorCode::Ok,
                        ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                        R"({"ok":false,"error":"core update manager unavailable"})");
                }
                bool force_refresh = false;
                if (!request->body.empty()) {
                    try {
                        auto doc = nlohmann::json::parse(request->body);
                        if (doc.contains("force_refresh") && doc["force_refresh"].is_boolean())
                            force_refresh = doc["force_refresh"].get<bool>();
                    } catch (...) {}
                }
                if (force_refresh) app_update_manager_->refresh();
                if (app_update_manager_->fetch_state() == AppUpdateFetchState::Idle)
                    app_update_manager_->refresh();
                for (int i = 0; i < 200; ++i) {
                    auto st = app_update_manager_->fetch_state();
                    if (st != AppUpdateFetchState::Fetching) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                nlohmann::json resp = nlohmann::json::object();
                resp["ok"] = true;

                const auto st = app_update_manager_->fetch_state();
                switch (st) {
                    case AppUpdateFetchState::Idle:     resp["state"] = "idle"; break;
                    case AppUpdateFetchState::Fetching: resp["state"] = "fetching"; break;
                    case AppUpdateFetchState::Ready:    resp["state"] = "ready"; break;
                    case AppUpdateFetchState::Error:    resp["state"] = "error"; break;
                }

                auto info = app_update_manager_->latest();
                resp["update_available"] = info.update_available;
                resp["current_version"] = info.current_version;
                resp["latest_version"] = info.latest_version;
                resp["download_url"] = info.download_url;
                resp["release_notes_url"] = info.release_notes_url;
                resp["title"] = info.title;
                resp["publication_date"] = info.publication_date;
                resp["minimum_system_version"] = info.minimum_system_version;
                resp["appcast_url"] = AppUpdateManager::appcast_url();
                if (st == AppUpdateFetchState::Error) {
                    resp["error"] = app_update_manager_->fetch_error();
                }

                std::string response_body = resp.dump();
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // test_package needs longer timeout (compiles + runs tests)
            if (method == "test_package") {
                Impl::PendingRequest req;
                req.method = std::move(method);
                req.body = request->body;
                auto future = req.promise.get_future();
                {
                    std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                    impl_->queue.push_back(std::move(req));
                }
                auto status = future.wait_for(std::chrono::seconds(kTestPackageTimeoutSec));
                std::string response_body;
                if (status == std::future_status::ready)
                    response_body = future.get();
                else
                    response_body = R"({"ok":false,"error":"timeout"})";
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Push request to queue, block until main thread processes it
            const bool is_sample_node_outputs = (method == "sample_node_outputs");

            Impl::PendingRequest req;
            req.method = std::move(method);
            req.body = request->body;
            auto future = req.promise.get_future();

            {
                std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                impl_->queue.push_back(std::move(req));
            }

            const int timeout_seconds =
                is_sample_node_outputs ? kSampleNodeOutputsTimeoutSec : kDefaultDispatchTimeoutSec;
            auto status = future.wait_for(std::chrono::seconds(timeout_seconds));
            std::string response_body;
            if (status == std::future_status::ready)
                response_body = future.get();
            else
                response_body = R"({"ok":false,"error":"timeout"})";

            return std::make_shared<ix::HttpResponse>(
                200, "OK", ix::HttpErrorCode::Ok,
                ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                response_body);
        }
    );

    auto res = impl_->server.listen();
    if (!res.first) {
        std::fprintf(stderr, "[vivid] Control server failed to listen: %s\n",
                     res.second.c_str());
        impl_.reset();
        return false;
    }

    impl_->server.start();
    impl_->running = true;
    std::fprintf(stderr,
        "[vivid] Control server listening on http://127.0.0.1:%d\n", port);
    return true;
}

void ControlServer::stop() {
    if (!impl_ || !impl_->running) return;
    impl_->running = false;

    // Drain queue so blocked handler threads can unblock and finish
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        while (!impl_->queue.empty()) {
            impl_->queue.front().promise.set_value(
                R"({"ok":false,"error":"server shutting down"})");
            impl_->queue.pop_front();
        }
    }

    impl_->server.stop();
    std::fprintf(stderr, "[vivid] Control server stopped\n");
}

void ControlServer::process_requests(RuntimeAPI& api, Graph& graph,
                                     Scheduler& scheduler,
                                     OperatorRegistry& registry,
                                     bool& has_gpu_ops, bool& has_audio) {
    if (!impl_) return;

    // Swap the queue out under lock, then process without holding it
    std::deque<Impl::PendingRequest> local;
    {
        std::lock_guard<std::mutex> lock(impl_->queue_mutex);
        if (impl_->queue.empty()) return;
        local.swap(impl_->queue);
    }

    for (auto& req : local) {
        // MCP undo/redo are handled here so they can use control-server history.
        if (req.method == "undo") {
            std::string snapshot_json;
            if (!impl_->undo_history.undo(snapshot_json)) {
                req.promise.set_value(json_err("nothing to undo"));
                continue;
            }
            auto r = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
            if (!r.ok) {
                std::string ignored;
                (void)impl_->undo_history.redo(ignored);
                impl_->undo_history.clear();
                std::string baseline_json;
                if (graph.save_to_string(baseline_json)) {
                    impl_->undo_history.push(std::move(baseline_json));
                }
                req.promise.set_value(json_err("undo failed: " + r.message));
                continue;
            }
            req.promise.set_value(command_result_to_json(r));
            continue;
        }
        if (req.method == "new_graph") {
            auto r = api.new_graph(has_gpu_ops, has_audio);
            impl_->undo_history.clear();
            req.promise.set_value(command_result_to_json(r));
            continue;
        }
        if (req.method == "new_project") {
            nlohmann::json np_root;
            bool np_valid = false;
            try { np_root = nlohmann::json::parse(req.body); np_valid = true; } catch (...) {}
            if (!np_valid || !np_root.contains("path") || !np_root["path"].is_string()) {
                req.promise.set_value(json_err("new_project requires 'path' parameter"));
                continue;
            }
            std::string path = np_root["path"].get<std::string>();
            auto r = api.new_project(path, has_gpu_ops, has_audio);
            impl_->undo_history.clear();
            req.promise.set_value(command_result_to_json(r));
            continue;
        }
        if (req.method == "redo") {
            std::string snapshot_json;
            if (!impl_->undo_history.redo(snapshot_json)) {
                req.promise.set_value(json_err("nothing to redo"));
                continue;
            }
            auto r = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
            if (!r.ok) {
                std::string ignored;
                (void)impl_->undo_history.undo(ignored);
                impl_->undo_history.clear();
                std::string baseline_json;
                if (graph.save_to_string(baseline_json)) {
                    impl_->undo_history.push(std::move(baseline_json));
                }
                req.promise.set_value(json_err("redo failed: " + r.message));
                continue;
            }
            req.promise.set_value(command_result_to_json(r));
            continue;
        }

        bool track_for_undo = is_undo_tracked_method(req.method);
        if (track_for_undo && impl_->undo_history.size() == 0) {
            std::string baseline_json;
            if (graph.save_to_string(baseline_json)) {
                impl_->undo_history.push(std::move(baseline_json));
            }
        }

        std::string response = dispatch(req.method, req.body,
                                        api, graph, scheduler, registry,
                                        has_gpu_ops, has_audio,
                                        src_dir_, hot_reloader_,
                                        package_manager_,
                                        package_compiler_,
                                        settings_);

        if (track_for_undo && response_is_ok(response)) {
            if (req.method == "load_graph") {
                impl_->undo_history.clear();
            }
            std::string current_json;
            if (graph.save_to_string(current_json)) {
                impl_->undo_history.push(std::move(current_json));
            }
        }

        req.promise.set_value(std::move(response));
    }
}

} // namespace vivid
