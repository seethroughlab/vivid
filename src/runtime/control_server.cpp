#include "runtime/control_server.h"
#include "runtime/capture_coordinator.h"
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
#include "yyjson.h"
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
// Enum → string helpers
// ---------------------------------------------------------------------------

static const char* domain_str(VividDomain d) {
    switch (d) {
        case VIVID_DOMAIN_CONTROL: return "control";
        case VIVID_DOMAIN_AUDIO:   return "audio";
        case VIVID_DOMAIN_GPU:     return "gpu";
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
        case VIVID_PORT_CONTROL_FLOAT:  return "control_float";
        case VIVID_PORT_CONTROL_INT:    return "control_int";
        case VIVID_PORT_CONTROL_BOOL:   return "control_bool";
        case VIVID_PORT_AUDIO_FLOAT:    return "audio_float";
        case VIVID_PORT_CONTROL_SPREAD: return "control_spread";
        case VIVID_PORT_GPU_TEXTURE:    return "gpu_texture";
        case VIVID_PORT_DATA:          return "data";
        case VIVID_PORT_CONTROL_STRING: return "control_string";
        case VIVID_PORT_CONTROL_STRING_SPREAD: return "control_string_spread";
        default: return "unknown";
    }
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

static std::string json_serialize(yyjson_mut_doc* doc) {
    size_t len = 0;
    char* json = yyjson_mut_write(doc, 0, &len);
    std::string result(json ? json : "{}", json ? len : 2);
    std::free(json);
    return result;
}

// Wrap a pre-built result value in {"ok": true, "result": ...}
// Takes ownership of doc (frees it).
static std::string json_ok(yyjson_mut_doc* doc, yyjson_mut_val* result_val) {
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_val(doc, root, "result", result_val);
    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
}

static std::string json_ok_msg(const std::string& msg) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_strcpy(doc, root, "message", msg.c_str());
    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
}

static std::string json_err(const std::string& msg) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", false);
    yyjson_mut_obj_add_strcpy(doc, root, "error", msg.c_str());
    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
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

static bool response_is_ok(const std::string& response_json) {
    yyjson_doc* doc = yyjson_read(response_json.c_str(), response_json.size(), 0);
    if (!doc) return false;
    yyjson_val* root = yyjson_doc_get_root(doc);
    yyjson_val* ok_val = root ? yyjson_obj_get(root, "ok") : nullptr;
    bool ok = ok_val && yyjson_is_bool(ok_val) && yyjson_get_bool(ok_val);
    yyjson_doc_free(doc);
    return ok;
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
           method == "set_node_layout" ||
           method == "add_midi_mapping" ||
           method == "remove_midi_mapping" ||
           method == "update_midi_mapping" ||
           method == "save_variation" ||
           method == "recall_variation" ||
           method == "remove_variation" ||
           method == "rename_variation" ||
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
           method == "load_graph";
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static std::string handle_inspect_graph(Graph& graph, Scheduler& scheduler) {
    std::unordered_map<std::string, const NodeState*> state_map;
    for (const auto& ns : scheduler.nodes())
        state_map[ns.node_id] = &ns;

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* result = yyjson_mut_obj(doc);

    // -- Nodes --
    yyjson_mut_val* nodes_arr = yyjson_mut_arr(doc);
    for (const auto& ndef : graph.nodes()) {
        yyjson_mut_val* node = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, node, "id", ndef.id.c_str());
        yyjson_mut_obj_add_strcpy(doc, node, "type", ndef.type.c_str());

        auto sit = state_map.find(ndef.id);
        const NodeState* ns = (sit != state_map.end()) ? sit->second : nullptr;
        const VividOperatorDescriptor* desc = ns ? ns->loader->descriptor() : nullptr;

        // Params (with live values from scheduler)
        yyjson_mut_val* params_arr = yyjson_mut_arr(doc);
        if (desc) {
            for (uint32_t i = 0; i < desc->param_count; ++i) {
                const auto& pd = desc->params[i];
                yyjson_mut_val* p = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, p, "name", pd.name);
                yyjson_mut_obj_add_str(doc, p, "type", param_type_str(pd.type));
                float value = pd.default_value;
                if (ns) {
                    auto pi = ns->param_indices.find(pd.name);
                    if (pi != ns->param_indices.end())
                        value = ns->param_values[pi->second];
                }
                yyjson_mut_obj_add_real(doc, p, "value", static_cast<double>(value));
                yyjson_mut_obj_add_real(doc, p, "min", static_cast<double>(pd.min_value));
                yyjson_mut_obj_add_real(doc, p, "max", static_cast<double>(pd.max_value));
                yyjson_mut_obj_add_real(doc, p, "default", static_cast<double>(pd.default_value));
                if (pd.semantic_tag)
                    yyjson_mut_obj_add_strcpy(doc, p, "semantic_tag", pd.semantic_tag);
                if (pd.semantic_shape)
                    yyjson_mut_obj_add_strcpy(doc, p, "semantic_shape", pd.semantic_shape);
                if (pd.semantic_unit)
                    yyjson_mut_obj_add_strcpy(doc, p, "semantic_unit", pd.semantic_unit);
                if (pd.semantic_intent)
                    yyjson_mut_obj_add_strcpy(doc, p, "semantic_intent", pd.semantic_intent);
                if (pd.choice_count > 0 && pd.choice_labels) {
                    yyjson_mut_val* choices = yyjson_mut_arr(doc);
                    for (uint32_t c = 0; c < pd.choice_count; ++c)
                        yyjson_mut_arr_add_strcpy(doc, choices, pd.choice_labels[c]);
                    yyjson_mut_obj_add_val(doc, p, "choices", choices);
                }
                if ((pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT) && ns) {
                    auto fi = ns->file_param_indices.find(pd.name);
                    if (fi != ns->file_param_indices.end()) {
                        yyjson_mut_obj_add_strcpy(doc, p, "string_value",
                            ns->file_param_storage[fi->second].c_str());
                    }
                }
                yyjson_mut_arr_add_val(params_arr, p);
            }
        }
        yyjson_mut_obj_add_val(doc, node, "params", params_arr);

        // Ports split into inputs / outputs
        yyjson_mut_val* inputs_arr = yyjson_mut_arr(doc);
        yyjson_mut_val* outputs_arr = yyjson_mut_arr(doc);
        if (desc) {
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                const auto& pd = desc->ports[i];
                yyjson_mut_val* p = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, p, "name", pd.name);
                yyjson_mut_obj_add_str(doc, p, "type", port_type_str(pd.type));
                if (pd.type == VIVID_PORT_DATA && pd.data_type)
                    yyjson_mut_obj_add_strcpy(doc, p, "data_type", pd.data_type);

                if (pd.direction == VIVID_PORT_OUTPUT && ns) {
                    auto oi = ns->output_port_indices.find(pd.name);
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_values.size()) {
                        yyjson_mut_obj_add_real(doc, p, "current_value",
                            static_cast<double>(ns->output_values[oi->second]));
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_string_values.size() &&
                        !ns->output_string_values[oi->second].empty()) {
                        yyjson_mut_obj_add_strcpy(doc, p, "current_string",
                            ns->output_string_values[oi->second].c_str());
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_spreads.size() &&
                        !ns->output_spreads[oi->second].empty()) {
                        yyjson_mut_val* spread_arr = yyjson_mut_arr(doc);
                        for (float sv : ns->output_spreads[oi->second])
                            yyjson_mut_arr_add_real(doc, spread_arr, static_cast<double>(sv));
                        yyjson_mut_obj_add_val(doc, p, "spread", spread_arr);
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_string_spreads.size() &&
                        !ns->output_string_spreads[oi->second].empty()) {
                        yyjson_mut_val* spread_arr = yyjson_mut_arr(doc);
                        for (const auto& sv : ns->output_string_spreads[oi->second])
                            yyjson_mut_arr_add_strcpy(doc, spread_arr, sv.c_str());
                        yyjson_mut_obj_add_val(doc, p, "string_spread", spread_arr);
                    }
                }

                if (pd.direction == VIVID_PORT_INPUT && ns) {
                    auto ii = ns->input_port_indices.find(pd.name);
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_values.size()) {
                        yyjson_mut_obj_add_real(doc, p, "current_value",
                            static_cast<double>(ns->input_values[ii->second]));
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_string_values.size() &&
                        !ns->input_string_values[ii->second].empty()) {
                        yyjson_mut_obj_add_strcpy(doc, p, "current_string",
                            ns->input_string_values[ii->second].c_str());
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_spreads.size() &&
                        !ns->input_spreads[ii->second].empty()) {
                        yyjson_mut_val* spread_arr = yyjson_mut_arr(doc);
                        for (float sv : ns->input_spreads[ii->second])
                            yyjson_mut_arr_add_real(doc, spread_arr, static_cast<double>(sv));
                        yyjson_mut_obj_add_val(doc, p, "spread", spread_arr);
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_string_spreads.size() &&
                        !ns->input_string_spreads[ii->second].empty()) {
                        yyjson_mut_val* spread_arr = yyjson_mut_arr(doc);
                        for (const auto& sv : ns->input_string_spreads[ii->second])
                            yyjson_mut_arr_add_strcpy(doc, spread_arr, sv.c_str());
                        yyjson_mut_obj_add_val(doc, p, "string_spread", spread_arr);
                    }
                }

                if (pd.direction == VIVID_PORT_INPUT)
                    yyjson_mut_arr_add_val(inputs_arr, p);
                else
                    yyjson_mut_arr_add_val(outputs_arr, p);
            }
        }
        yyjson_mut_obj_add_val(doc, node, "inputs", inputs_arr);
        yyjson_mut_obj_add_val(doc, node, "outputs", outputs_arr);

        yyjson_mut_arr_add_val(nodes_arr, node);
    }
    yyjson_mut_obj_add_val(doc, result, "nodes", nodes_arr);

    // -- Connections --
    yyjson_mut_val* conns_arr = yyjson_mut_arr(doc);
    for (const auto& conn : graph.connections()) {
        yyjson_mut_val* c = yyjson_mut_obj(doc);
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        yyjson_mut_obj_add_strcpy(doc, c, "from", from_addr.c_str());
        yyjson_mut_obj_add_strcpy(doc, c, "to", to_addr.c_str());
        if (conn.has_remap()) {
            yyjson_mut_obj_add_real(doc, c, "from_min", conn.from_min);
            yyjson_mut_obj_add_real(doc, c, "from_max", conn.from_max);
            yyjson_mut_obj_add_real(doc, c, "to_min",   conn.to_min);
            yyjson_mut_obj_add_real(doc, c, "to_max",   conn.to_max);
            if (conn.clamp)
                yyjson_mut_obj_add_bool(doc, c, "clamp", true);
        }
        yyjson_mut_arr_add_val(conns_arr, c);
    }
    yyjson_mut_obj_add_val(doc, result, "connections", conns_arr);

    return json_ok(doc, result);
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

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_int(doc, root, "schema_version", 1);

    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_val* nodes_arr = yyjson_mut_arr(doc);

    const auto& nodes = scheduler.nodes();
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        const auto& ns = nodes[ni];
        yyjson_mut_val* node = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, node, "node_id", ns.node_id.c_str());
        yyjson_mut_obj_add_int(doc, node, "node_index", static_cast<int64_t>(ni));

        std::string type_name = ns.type_name;
        if (type_name.empty()) {
            auto dit = def_map.find(ns.node_id);
            if (dit != def_map.end() && dit->second)
                type_name = dit->second->type;
        }
        yyjson_mut_obj_add_strcpy(doc, node, "type", type_name.c_str());
        yyjson_mut_obj_add_str(doc, node, "domain",
                               ns.is_gpu ? "gpu" : (ns.is_audio ? "audio" : "control"));
        yyjson_mut_obj_add_int(doc, node, "incoming_wires",
                               static_cast<int64_t>(incoming_wires[ns.node_id]));
        yyjson_mut_obj_add_int(doc, node, "outgoing_wires",
                               static_cast<int64_t>(outgoing_wires[ns.node_id]));

        // Health
        yyjson_mut_val* health = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_bool(doc, health, "errored", ns.errored || ns.missing_operator);
        yyjson_mut_obj_add_strcpy(doc, health, "message", ns.error_message.c_str());
        yyjson_mut_obj_add_bool(doc, health, "missing_operator", ns.missing_operator);
        yyjson_mut_obj_add_val(doc, node, "health", health);

        const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;

        // Current params
        yyjson_mut_val* params_obj = yyjson_mut_obj(doc);
        if (desc) {
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                if (pi < ns.param_values.size())
                    yyjson_mut_obj_add_real(doc, params_obj, pd.name,
                                            static_cast<double>(ns.param_values[pi]));
            }
            for (const auto& [name, idx] : ns.file_param_indices) {
                if (idx < ns.file_param_storage.size())
                    yyjson_mut_obj_add_strcpy(doc, params_obj, name.c_str(),
                                              ns.file_param_storage[idx].c_str());
            }
        } else {
            auto dit = def_map.find(ns.node_id);
            if (dit != def_map.end() && dit->second) {
                for (const auto& [k, v] : dit->second->params)
                    yyjson_mut_obj_add_real(doc, params_obj, k.c_str(), static_cast<double>(v));
                for (const auto& [k, v] : dit->second->string_params)
                    yyjson_mut_obj_add_strcpy(doc, params_obj, k.c_str(), v.c_str());
            }
        }
        yyjson_mut_obj_add_val(doc, node, "params", params_obj);

        // Param metadata
        yyjson_mut_val* param_meta_arr = yyjson_mut_arr(doc);
        if (desc) {
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                yyjson_mut_val* pm = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, pm, "name", pd.name);
                yyjson_mut_obj_add_str(doc, pm, "kind", param_type_str(pd.type));
                yyjson_mut_obj_add_real(doc, pm, "default", static_cast<double>(pd.default_value));
                yyjson_mut_obj_add_real(doc, pm, "min", static_cast<double>(pd.min_value));
                yyjson_mut_obj_add_real(doc, pm, "max", static_cast<double>(pd.max_value));
                if (pd.semantic_tag)
                    yyjson_mut_obj_add_strcpy(doc, pm, "semantic_tag", pd.semantic_tag);
                if (pd.semantic_shape)
                    yyjson_mut_obj_add_strcpy(doc, pm, "semantic_shape", pd.semantic_shape);
                if (pd.semantic_unit)
                    yyjson_mut_obj_add_strcpy(doc, pm, "semantic_unit", pd.semantic_unit);
                if (pd.semantic_intent)
                    yyjson_mut_obj_add_strcpy(doc, pm, "semantic_intent", pd.semantic_intent);
                yyjson_mut_arr_add_val(param_meta_arr, pm);
            }
        }
        yyjson_mut_obj_add_val(doc, node, "param_meta", param_meta_arr);

        // Input summary
        yyjson_mut_val* inputs_arr = yyjson_mut_arr(doc);
        if (desc) {
            for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                const auto& pd = desc->ports[pi];
                if (pd.direction != VIVID_PORT_INPUT) continue;

                yyjson_mut_val* in = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, in, "name", pd.name);
                yyjson_mut_obj_add_str(doc, in, "kind", port_type_str(pd.type));
                yyjson_mut_obj_add_int(doc, in, "connected_wires",
                    static_cast<int64_t>(incoming_port_wires[ns.node_id][pd.name]));

                auto iit = ns.input_port_indices.find(pd.name);
                if (iit != ns.input_port_indices.end()) {
                    uint32_t ii = iit->second;
                    if (ii < ns.input_values.size()) {
                        yyjson_mut_obj_add_real(doc, in, "scalar",
                                                static_cast<double>(ns.input_values[ii]));
                    }
                    if (ii < ns.input_string_values.size() &&
                        !ns.input_string_values[ii].empty()) {
                        yyjson_mut_obj_add_strcpy(doc, in, "string",
                                                  ns.input_string_values[ii].c_str());
                    }
                    if (ii < ns.input_spreads.size()) {
                        yyjson_mut_val* spread = yyjson_mut_obj(doc);
                        yyjson_mut_obj_add_int(doc, spread, "length",
                                               static_cast<int64_t>(ns.input_spreads[ii].size()));
                        yyjson_mut_obj_add_val(doc, in, "spread", spread);
                    }
                    if (ii < ns.input_string_spreads.size()) {
                        yyjson_mut_val* sspread = yyjson_mut_obj(doc);
                        yyjson_mut_obj_add_int(doc, sspread, "length",
                                               static_cast<int64_t>(ns.input_string_spreads[ii].size()));
                        yyjson_mut_obj_add_val(doc, in, "string_spread", sspread);
                    }
                }
                yyjson_mut_arr_add_val(inputs_arr, in);
            }
        }
        yyjson_mut_obj_add_val(doc, node, "inputs", inputs_arr);

        // Output summary
        yyjson_mut_val* outputs_arr = yyjson_mut_arr(doc);
        if (desc) {
            for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                const auto& pd = desc->ports[pi];
                if (pd.direction != VIVID_PORT_OUTPUT) continue;

                yyjson_mut_val* out = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, out, "name", pd.name);
                yyjson_mut_obj_add_str(doc, out, "kind", port_type_str(pd.type));
                yyjson_mut_obj_add_int(doc, out, "connected_wires",
                    static_cast<int64_t>(outgoing_port_wires[ns.node_id][pd.name]));

                auto oit = ns.output_port_indices.find(pd.name);
                if (oit != ns.output_port_indices.end()) {
                    uint32_t oi = oit->second;
                    if (oi < ns.output_values.size())
                        yyjson_mut_obj_add_real(doc, out, "scalar",
                                                static_cast<double>(ns.output_values[oi]));
                    if (oi < ns.output_string_values.size() &&
                        !ns.output_string_values[oi].empty()) {
                        yyjson_mut_obj_add_strcpy(doc, out, "string",
                                                  ns.output_string_values[oi].c_str());
                    }
                    if (oi < ns.output_spreads.size()) {
                        yyjson_mut_val* spread = yyjson_mut_obj(doc);
                        yyjson_mut_obj_add_int(doc, spread, "length",
                                               static_cast<int64_t>(ns.output_spreads[oi].size()));
                        yyjson_mut_obj_add_val(doc, out, "spread", spread);
                    }
                    if (oi < ns.output_string_spreads.size()) {
                        yyjson_mut_val* sspread = yyjson_mut_obj(doc);
                        yyjson_mut_obj_add_int(doc, sspread, "length",
                                               static_cast<int64_t>(ns.output_string_spreads[oi].size()));
                        yyjson_mut_obj_add_val(doc, out, "string_spread", sspread);
                    }
                }

                if (pd.type == VIVID_PORT_GPU_TEXTURE && ns.gpu_tex_width > 0 && ns.gpu_tex_height > 0) {
                    yyjson_mut_obj_add_int(doc, out, "width", ns.gpu_tex_width);
                    yyjson_mut_obj_add_int(doc, out, "height", ns.gpu_tex_height);
                }
                yyjson_mut_arr_add_val(outputs_arr, out);
            }
        }
        yyjson_mut_obj_add_val(doc, node, "outputs", outputs_arr);

        // Domain metrics (lightweight first pass)
        yyjson_mut_val* domain_metrics = yyjson_mut_obj(doc);
        if (ns.is_gpu) {
            yyjson_mut_val* gpu = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, gpu, "width", ns.gpu_tex_width);
            yyjson_mut_obj_add_int(doc, gpu, "height", ns.gpu_tex_height);
            yyjson_mut_obj_add_bool(doc, gpu, "has_texture", ns.gpu_texture != nullptr);
            yyjson_mut_obj_add_int(doc, gpu, "aux_texture_count",
                static_cast<int64_t>(ns.aux_gpu_texture_views.size()));
            yyjson_mut_obj_add_val(doc, domain_metrics, "gpu", gpu);
        } else if (ns.is_audio) {
            yyjson_mut_val* audio = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, audio, "output_port_count", ns.output_port_count);
            yyjson_mut_obj_add_int(doc, audio, "input_port_count", ns.input_port_count);
            auto rms_it = ns.output_port_indices.find("rms");
            if (rms_it != ns.output_port_indices.end() &&
                rms_it->second < ns.output_values.size()) {
                yyjson_mut_obj_add_real(doc, audio, "rms",
                    static_cast<double>(ns.output_values[rms_it->second]));
            }
            auto peak_it = ns.output_port_indices.find("peak");
            if (peak_it != ns.output_port_indices.end() &&
                peak_it->second < ns.output_values.size()) {
                yyjson_mut_obj_add_real(doc, audio, "peak",
                    static_cast<double>(ns.output_values[peak_it->second]));
            }
            auto wave_it = ns.output_port_indices.find("waveform");
            if (wave_it != ns.output_port_indices.end() &&
                wave_it->second < ns.output_spreads.size()) {
                const auto& wave = ns.output_spreads[wave_it->second];
                yyjson_mut_obj_add_int(doc, audio, "waveform_length",
                    static_cast<int64_t>(wave.size()));
                yyjson_mut_val* preview = yyjson_mut_arr(doc);
                size_t preview_count = wave.size();
                if (preview_count > 32) preview_count = 32;
                for (size_t wi = 0; wi < preview_count; ++wi) {
                    yyjson_mut_arr_add_real(doc, preview, static_cast<double>(wave[wi]));
                }
                yyjson_mut_obj_add_val(doc, audio, "waveform_preview", preview);
            }
            yyjson_mut_obj_add_val(doc, domain_metrics, "audio", audio);
        } else {
            yyjson_mut_val* control = yyjson_mut_obj(doc);
            int64_t spread_out_nonempty = 0;
            int64_t scalar_out_nonzero = 0;
            for (const auto& sp : ns.output_spreads)
                if (!sp.empty()) spread_out_nonempty++;
            for (float v : ns.output_values)
                if (v != 0.0f) scalar_out_nonzero++;
            yyjson_mut_obj_add_int(doc, control, "non_empty_spread_outputs", spread_out_nonempty);
            yyjson_mut_obj_add_int(doc, control, "non_zero_scalar_outputs", scalar_out_nonzero);
            yyjson_mut_obj_add_val(doc, domain_metrics, "control", control);
        }
        yyjson_mut_obj_add_val(doc, node, "domain_metrics", domain_metrics);

        yyjson_mut_arr_add_val(nodes_arr, node);
    }

    yyjson_mut_obj_add_val(doc, result, "nodes", nodes_arr);
    yyjson_mut_obj_add_val(doc, root, "result", result);

    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
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

    const auto& nodes = scheduler.nodes();
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

        if (ns.is_audio && type_name == "audio_out" && incoming_wires[ns.node_id] == 0) {
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

        if (ns.is_audio) {
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
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_bool(doc, root, "ok", true);
    yyjson_mut_obj_add_int(doc, root, "schema_version", 1);

    std::vector<DiagnosticFinding> findings = collect_diagnostics(graph, scheduler, registry);

    yyjson_mut_val* summary = yyjson_mut_obj(doc);
    int64_t critical_count = 0;
    int64_t warning_count = 0;
    int64_t info_count = 0;
    for (const auto& f : findings) {
        if (f.severity == "critical") critical_count++;
        else if (f.severity == "warning") warning_count++;
        else info_count++;
    }
    yyjson_mut_obj_add_int(doc, summary, "critical", critical_count);
    yyjson_mut_obj_add_int(doc, summary, "warning", warning_count);
    yyjson_mut_obj_add_int(doc, summary, "info", info_count);

    yyjson_mut_val* findings_arr = yyjson_mut_arr(doc);
    for (const auto& f : findings) {
        yyjson_mut_val* fv = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, fv, "id", f.id.c_str());
        yyjson_mut_obj_add_strcpy(doc, fv, "severity", f.severity.c_str());
        yyjson_mut_obj_add_strcpy(doc, fv, "node_id", f.node_id.c_str());
        yyjson_mut_obj_add_strcpy(doc, fv, "message", f.message.c_str());
        yyjson_mut_obj_add_strcpy(doc, fv, "suggestion", f.suggestion.c_str());
        yyjson_mut_arr_add_val(findings_arr, fv);
    }

    // Hint list: dedupe by finding id, keep highest-priority instance.
    yyjson_mut_val* hints_arr = yyjson_mut_arr(doc);
    std::unordered_set<std::string> seen_hint_ids;
    for (const auto& f : findings) {
        if (seen_hint_ids.find(f.id) != seen_hint_ids.end()) continue;
        seen_hint_ids.insert(f.id);
        yyjson_mut_val* hint = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, hint, "id", f.id.c_str());
        yyjson_mut_obj_add_strcpy(doc, hint, "severity", f.severity.c_str());
        yyjson_mut_obj_add_strcpy(doc, hint, "suggestion", f.suggestion.c_str());
        yyjson_mut_arr_add_val(hints_arr, hint);
    }

    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_val(doc, result, "summary", summary);
    yyjson_mut_obj_add_val(doc, result, "findings", findings_arr);
    yyjson_mut_obj_add_val(doc, result, "hints", hints_arr);
    yyjson_mut_obj_add_val(doc, root, "result", result);

    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
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

static bool parse_check_value(yyjson_val* v, CheckValue& out) {
    if (!v) return false;
    if (yyjson_is_num(v)) {
        out = cv_number(yyjson_get_num(v));
        return true;
    }
    if (yyjson_is_bool(v)) {
        out = cv_bool(yyjson_get_bool(v));
        return true;
    }
    if (yyjson_is_str(v)) {
        out = cv_string(yyjson_get_str(v));
        return true;
    }
    return false;
}

static void add_json_check_value(yyjson_mut_doc* doc, yyjson_mut_val* obj,
                                 const char* key, const CheckValue& v) {
    if (v.kind == CheckValue::Kind::Number)
        yyjson_mut_obj_add_real(doc, obj, key, v.number);
    else if (v.kind == CheckValue::Kind::Bool)
        yyjson_mut_obj_add_bool(doc, obj, key, v.boolean);
    else if (v.kind == CheckValue::Kind::String)
        yyjson_mut_obj_add_strcpy(doc, obj, key, v.string.c_str());
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

    const NodeState* node = nullptr;
    for (const auto& ns : scheduler.nodes()) {
        if (ns.node_id == node_id) { node = &ns; break; }
    }
    if (!node) return false;

    if (rest == "domain") {
        out = cv_string(node->is_gpu ? "gpu" : (node->is_audio ? "audio" : "control"));
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
        if (!node->is_audio || it == node->output_port_indices.end() || it->second >= node->output_values.size())
            return false;
        out = cv_number(node->output_values[it->second]);
        return true;
    }
    if (rest == "domain_metrics.audio.peak") {
        auto it = node->output_port_indices.find("peak");
        if (!node->is_audio || it == node->output_port_indices.end() || it->second >= node->output_values.size())
            return false;
        out = cv_number(node->output_values[it->second]);
        return true;
    }
    if (rest == "domain_metrics.audio.waveform_length") {
        auto it = node->output_port_indices.find("waveform");
        if (!node->is_audio || it == node->output_port_indices.end() || it->second >= node->output_spreads.size())
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

static bool parse_check_def(yyjson_val* obj, ParsedCheck& out, std::string& err) {
    if (!obj || !yyjson_is_obj(obj)) { err = "check must be an object"; return false; }
    yyjson_val* idv = yyjson_obj_get(obj, "id");
    yyjson_val* tv = yyjson_obj_get(obj, "type");
    yyjson_val* opv = yyjson_obj_get(obj, "op");
    if (!idv || !yyjson_is_str(idv)) { err = "check missing 'id'"; return false; }
    if (!tv || !yyjson_is_str(tv)) { err = "check missing 'type'"; return false; }
    if (!opv || !yyjson_is_str(opv)) { err = "check missing 'op'"; return false; }
    out.id = yyjson_get_str(idv);
    out.type = yyjson_get_str(tv);
    out.op = yyjson_get_str(opv);
    yyjson_val* sev = yyjson_obj_get(obj, "severity");
    if (sev && yyjson_is_str(sev)) out.severity = yyjson_get_str(sev);
    yyjson_val* msg = yyjson_obj_get(obj, "message");
    if (msg && yyjson_is_str(msg)) out.message = yyjson_get_str(msg);
    yyjson_val* tol = yyjson_obj_get(obj, "tolerance");
    if (tol && yyjson_is_num(tol)) out.tolerance = yyjson_get_num(tol);
    yyjson_val* ff = yyjson_obj_get(obj, "for_frames");
    if (ff && yyjson_is_int(ff)) out.for_frames = yyjson_get_sint(ff);
    yyjson_val* af = yyjson_obj_get(obj, "after_frame");
    if (af && yyjson_is_int(af)) out.after_frame = yyjson_get_sint(af);

    if (out.type == "state_check") {
        yyjson_val* path = yyjson_obj_get(obj, "path");
        if (!path || !yyjson_is_str(path)) { err = "state_check missing 'path'"; return false; }
        out.path = yyjson_get_str(path);
        if (out.op != "exists" && out.op != "not_exists") {
            if (out.op == "between") {
                yyjson_val* vv = yyjson_obj_get(obj, "value");
                if (vv && yyjson_is_arr(vv) && yyjson_arr_size(vv) == 2) {
                    yyjson_val* v0 = yyjson_arr_get(vv, 0);
                    yyjson_val* v1 = yyjson_arr_get(vv, 1);
                    out.has_value = parse_check_value(v0, out.value);
                    out.has_between_max = parse_check_value(v1, out.between_max);
                } else {
                    yyjson_val* vmin = yyjson_obj_get(obj, "min");
                    yyjson_val* vmax = yyjson_obj_get(obj, "max");
                    out.has_value = parse_check_value(vmin, out.value);
                    out.has_between_max = parse_check_value(vmax, out.between_max);
                }
                if (!out.has_value || !out.has_between_max) {
                    err = "state_check 'between' requires numeric min/max (or value[2])";
                    return false;
                }
            } else {
                yyjson_val* vv = yyjson_obj_get(obj, "value");
                out.has_value = parse_check_value(vv, out.value);
                if (!out.has_value) { err = "state_check missing scalar 'value'"; return false; }
            }
        }
    } else if (out.type == "diagnostic_check") {
        yyjson_val* sev2 = yyjson_obj_get(obj, "check_severity");
        if (!sev2) sev2 = yyjson_obj_get(obj, "severity");
        if (sev2 && yyjson_is_str(sev2)) out.check_diag_severity = yyjson_get_str(sev2);
        yyjson_val* fid = yyjson_obj_get(obj, "finding_id");
        if (fid && yyjson_is_str(fid)) out.finding_id = yyjson_get_str(fid);
        yyjson_val* fids = yyjson_obj_get(obj, "check_diagnostics_ids");
        if (out.finding_id.empty() && fids && yyjson_is_arr(fids) && yyjson_arr_size(fids) > 0) {
            yyjson_val* first = yyjson_arr_get_first(fids);
            if (first && yyjson_is_str(first)) out.finding_id = yyjson_get_str(first);
        }
        if (out.op == "count_by_severity_eq" ||
            out.op == "count_by_severity_lte" ||
            out.op == "count_by_severity_gte") {
            yyjson_val* vv = yyjson_obj_get(obj, "value");
            out.has_value = parse_check_value(vv, out.value);
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

    yyjson_val* when = yyjson_obj_get(obj, "when");
    if (when) {
        if (!yyjson_is_obj(when)) { err = "'when' must be object"; return false; }
        yyjson_val* wp = yyjson_obj_get(when, "path");
        yyjson_val* wo = yyjson_obj_get(when, "op");
        if (!wp || !wo || !yyjson_is_str(wp) || !yyjson_is_str(wo)) {
            err = "'when' requires 'path' and 'op'";
            return false;
        }
        out.has_when = true;
        out.when_path = yyjson_get_str(wp);
        out.when_op = yyjson_get_str(wo);
        yyjson_val* wv = yyjson_obj_get(when, "value");
        out.has_when_value = parse_check_value(wv, out.when_value);
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

static std::string handle_validate_checks(yyjson_val* root) {
    if (!root) return json_err("invalid JSON body");
    yyjson_val* checks = yyjson_obj_get(root, "checks");
    if (!checks || !yyjson_is_arr(checks)) return json_err("missing 'checks' array");

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* r = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_bool(doc, r, "ok", true);
    yyjson_mut_obj_add_int(doc, r, "schema_version", 1);
    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_val* errs = yyjson_mut_arr(doc);
    int64_t error_count = 0;

    std::unordered_set<std::string> seen_ids;
    size_t idx, max;
    yyjson_val* cv;
    yyjson_arr_foreach(checks, idx, max, cv) {
        ParsedCheck pc;
        std::string err;
        if (!parse_check_def(cv, pc, err)) {
            yyjson_mut_val* e = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, e, "index", static_cast<int64_t>(idx));
            yyjson_mut_obj_add_strcpy(doc, e, "message", err.c_str());
            yyjson_mut_arr_add_val(errs, e);
            error_count++;
            continue;
        }
        if (!seen_ids.insert(pc.id).second) {
            yyjson_mut_val* e = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_int(doc, e, "index", static_cast<int64_t>(idx));
            yyjson_mut_obj_add_strcpy(doc, e, "id", pc.id.c_str());
            yyjson_mut_obj_add_strcpy(doc, e, "message", "duplicate check id");
            yyjson_mut_arr_add_val(errs, e);
            error_count++;
        }
    }

    yyjson_mut_obj_add_bool(doc, result, "valid", error_count == 0);
    yyjson_mut_obj_add_int(doc, result, "error_count", error_count);
    yyjson_mut_obj_add_val(doc, result, "errors", errs);
    yyjson_mut_obj_add_val(doc, r, "result", result);
    yyjson_mut_doc_set_root(doc, r);
    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
}

static std::string handle_run_checks(Graph& graph, Scheduler& scheduler, OperatorRegistry& registry, yyjson_val* root) {
    if (!root) return json_err("invalid JSON body");
    yyjson_val* checks = yyjson_obj_get(root, "checks");
    if (!checks || !yyjson_is_arr(checks)) return json_err("missing 'checks' array");

    std::vector<ParsedCheck> parsed;
    parsed.reserve(yyjson_arr_size(checks));
    std::unordered_set<std::string> seen_ids;
    size_t idx, max;
    yyjson_val* cv;
    yyjson_arr_foreach(checks, idx, max, cv) {
        ParsedCheck pc;
        std::string err;
        if (!parse_check_def(cv, pc, err))
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

    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root_out = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root_out);
    yyjson_mut_obj_add_bool(doc, root_out, "ok", true);
    yyjson_mut_obj_add_int(doc, root_out, "schema_version", 1);

    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_val* results = yyjson_mut_arr(doc);

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

        yyjson_mut_val* row = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, row, "id", c.id.c_str());
        yyjson_mut_obj_add_strcpy(doc, row, "type", c.type.c_str());
        yyjson_mut_obj_add_strcpy(doc, row, "severity", c.severity.c_str());
        yyjson_mut_obj_add_bool(doc, row, "passed", r_skipped ? false : r_passed);
        yyjson_mut_obj_add_bool(doc, row, "skipped", r_skipped);
        yyjson_mut_obj_add_strcpy(doc, row, "op", c.op.c_str());
        if (!c.path.empty()) yyjson_mut_obj_add_strcpy(doc, row, "path", c.path.c_str());
        if (!message.empty()) yyjson_mut_obj_add_strcpy(doc, row, "message", message.c_str());
        if (c.has_value) add_json_check_value(doc, row, "expected", expected);
        if (c.op == "between" && c.has_between_max) add_json_check_value(doc, row, "expected_max", c.between_max);
        add_json_check_value(doc, row, "actual", actual);
        yyjson_mut_arr_add_val(results, row);

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

    yyjson_mut_val* summary = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, summary, "passed", passed);
    yyjson_mut_obj_add_int(doc, summary, "failed", failed);
    yyjson_mut_obj_add_int(doc, summary, "skipped", skipped);
    yyjson_mut_obj_add_int(doc, summary, "critical_failed", critical_failed);
    yyjson_mut_obj_add_int(doc, summary, "warning_failed", warning_failed);
    yyjson_mut_obj_add_int(doc, summary, "info_failed", info_failed);
    yyjson_mut_obj_add_bool(doc, result, "all_passed", all_passed);
    yyjson_mut_obj_add_bool(doc, result, "all_critical_passed", all_critical_passed);
    yyjson_mut_obj_add_val(doc, result, "summary", summary);
    yyjson_mut_obj_add_val(doc, result, "results", results);
    yyjson_mut_obj_add_val(doc, root_out, "result", result);

    std::string s = json_serialize(doc);
    yyjson_mut_doc_free(doc);
    return s;
}

static std::string handle_list_types(OperatorRegistry& registry) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* result = yyjson_mut_obj(doc);
    yyjson_mut_val* types_arr = yyjson_mut_arr(doc);

    for (const auto& name : registry.type_names()) {
        auto* loader = registry.find(name);
        if (!loader) continue;
        const auto* desc = loader->descriptor();
        if (!desc) continue;

        yyjson_mut_val* t = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_strcpy(doc, t, "name", desc->name);
        yyjson_mut_obj_add_str(doc, t, "domain", domain_str(desc->domain));

        // Params
        yyjson_mut_val* params_arr = yyjson_mut_arr(doc);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            const auto& pd = desc->params[i];
            yyjson_mut_val* p = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, p, "name", pd.name);
            yyjson_mut_obj_add_str(doc, p, "type", param_type_str(pd.type));
            yyjson_mut_obj_add_real(doc, p, "default", static_cast<double>(pd.default_value));
            yyjson_mut_obj_add_real(doc, p, "min", static_cast<double>(pd.min_value));
            yyjson_mut_obj_add_real(doc, p, "max", static_cast<double>(pd.max_value));
            if (pd.semantic_tag)
                yyjson_mut_obj_add_strcpy(doc, p, "semantic_tag", pd.semantic_tag);
            if (pd.semantic_shape)
                yyjson_mut_obj_add_strcpy(doc, p, "semantic_shape", pd.semantic_shape);
            if (pd.semantic_unit)
                yyjson_mut_obj_add_strcpy(doc, p, "semantic_unit", pd.semantic_unit);
            if (pd.semantic_intent)
                yyjson_mut_obj_add_strcpy(doc, p, "semantic_intent", pd.semantic_intent);
            yyjson_mut_arr_add_val(params_arr, p);
        }
        yyjson_mut_obj_add_val(doc, t, "params", params_arr);

        // Ports split into inputs / outputs
        yyjson_mut_val* inputs_arr = yyjson_mut_arr(doc);
        yyjson_mut_val* outputs_arr = yyjson_mut_arr(doc);
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            const auto& pd = desc->ports[i];
            yyjson_mut_val* p = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, p, "name", pd.name);
            yyjson_mut_obj_add_str(doc, p, "type", port_type_str(pd.type));
            if (pd.type == VIVID_PORT_DATA && pd.data_type)
                yyjson_mut_obj_add_strcpy(doc, p, "data_type", pd.data_type);
            if (pd.direction == VIVID_PORT_INPUT)
                yyjson_mut_arr_add_val(inputs_arr, p);
            else
                yyjson_mut_arr_add_val(outputs_arr, p);
        }
        yyjson_mut_obj_add_val(doc, t, "inputs", inputs_arr);
        yyjson_mut_obj_add_val(doc, t, "outputs", outputs_arr);

        yyjson_mut_arr_add_val(types_arr, t);
    }

    yyjson_mut_obj_add_val(doc, result, "types", types_arr);
    return json_ok(doc, result);
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

    // Parse body JSON (may be empty for some commands)
    yyjson_doc* doc = yyjson_read(body.c_str(), body.size(), 0);
    yyjson_val* root = doc ? yyjson_doc_get_root(doc) : nullptr;

    std::string result;

    if (method == "validate_checks") {
        result = handle_validate_checks(root);
    } else if (method == "run_checks") {
        result = handle_run_checks(graph, scheduler, registry, root);
    } else if (method == "add_node") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* type_v = yyjson_obj_get(root, "type");
            yyjson_val* id_v   = yyjson_obj_get(root, "node_id");
            if (!type_v || !id_v || !yyjson_is_str(type_v) || !yyjson_is_str(id_v))
                result = json_err("missing 'type' or 'node_id'");
            else
                result = command_result_to_json(
                    api.add_node(yyjson_get_str(type_v), yyjson_get_str(id_v)));
        }
    } else if (method == "remove_node") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            if (!nid || !yyjson_is_str(nid))
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.remove_node(yyjson_get_str(nid)));
        }
    } else if (method == "connect") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* from = yyjson_obj_get(root, "from_addr");
            yyjson_val* to   = yyjson_obj_get(root, "to_addr");
            yyjson_val* sem  = yyjson_obj_get(root, "semantic_defaults");
            if (!from || !to || !yyjson_is_str(from) || !yyjson_is_str(to))
                result = json_err("missing 'from_addr' or 'to_addr'");
            else {
                const std::string from_addr = yyjson_get_str(from);
                const std::string to_addr = yyjson_get_str(to);
                const bool semantic_defaults = sem && yyjson_is_bool(sem) && yyjson_get_bool(sem);
                CommandResult cr = api.connect(from_addr, to_addr, semantic_defaults);
                if (!cr.ok) {
                    result = json_err(cr.message);
                } else {
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* rroot = yyjson_mut_obj(rdoc);
                    yyjson_mut_doc_set_root(rdoc, rroot);
                    yyjson_mut_obj_add_bool(rdoc, rroot, "ok", true);
                    yyjson_mut_obj_add_strcpy(rdoc, rroot, "message", cr.message.c_str());

                    bool inferred_applied = false;
                    if (semantic_defaults) {
                        const ConnectionDef* conn = find_connection_by_addr(graph, from_addr, to_addr);
                        if (conn && conn->has_remap()) {
                            inferred_applied = true;
                            yyjson_mut_val* remap = yyjson_mut_obj(rdoc);
                            yyjson_mut_obj_add_real(rdoc, remap, "from_min", conn->from_min);
                            yyjson_mut_obj_add_real(rdoc, remap, "from_max", conn->from_max);
                            yyjson_mut_obj_add_real(rdoc, remap, "to_min", conn->to_min);
                            yyjson_mut_obj_add_real(rdoc, remap, "to_max", conn->to_max);
                            yyjson_mut_obj_add_bool(rdoc, remap, "clamp", conn->clamp);
                            yyjson_mut_obj_add_val(rdoc, rroot, "inferred_remap", remap);
                        }
                    }
                    yyjson_mut_obj_add_bool(rdoc, rroot, "inferred_remap_applied", inferred_applied);
                    result = json_serialize(rdoc);
                    yyjson_mut_doc_free(rdoc);
                }
            }
        }
    } else if (method == "disconnect") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* from = yyjson_obj_get(root, "from_addr");
            yyjson_val* to   = yyjson_obj_get(root, "to_addr");
            if (!from || !to || !yyjson_is_str(from) || !yyjson_is_str(to))
                result = json_err("missing 'from_addr' or 'to_addr'");
            else
                result = command_result_to_json(
                    api.disconnect(yyjson_get_str(from), yyjson_get_str(to)));
        }
    } else if (method == "set_connection_remap") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* from      = yyjson_obj_get(root, "from_addr");
            yyjson_val* to        = yyjson_obj_get(root, "to_addr");
            yyjson_val* fmin_val  = yyjson_obj_get(root, "from_min");
            yyjson_val* fmax_val  = yyjson_obj_get(root, "from_max");
            yyjson_val* tmin_val  = yyjson_obj_get(root, "to_min");
            yyjson_val* tmax_val  = yyjson_obj_get(root, "to_max");
            yyjson_val* clamp_val = yyjson_obj_get(root, "clamp");
            if (!from || !to || !yyjson_is_str(from) || !yyjson_is_str(to))
                result = json_err("missing 'from_addr' or 'to_addr'");
            else {
                float fmin = fmin_val && yyjson_is_num(fmin_val) ? static_cast<float>(yyjson_get_num(fmin_val)) : 0.0f;
                float fmax = fmax_val && yyjson_is_num(fmax_val) ? static_cast<float>(yyjson_get_num(fmax_val)) : 1.0f;
                float tmin = tmin_val && yyjson_is_num(tmin_val) ? static_cast<float>(yyjson_get_num(tmin_val)) : 0.0f;
                float tmax = tmax_val && yyjson_is_num(tmax_val) ? static_cast<float>(yyjson_get_num(tmax_val)) : 1.0f;
                bool  cval = clamp_val && yyjson_is_bool(clamp_val) ? yyjson_get_bool(clamp_val) : false;
                result = command_result_to_json(
                    api.set_connection_remap(yyjson_get_str(from), yyjson_get_str(to),
                                              fmin, fmax, tmin, tmax, cval));
            }
        }
    } else if (method == "set_param") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            yyjson_val* value = yyjson_obj_get(root, "value");
            if (!nid || !param || !value ||
                !yyjson_is_str(nid) || !yyjson_is_str(param) || !yyjson_is_num(value))
                result = json_err("missing 'node_id', 'param', or 'value'");
            else
                result = command_result_to_json(
                    api.set_param(yyjson_get_str(nid), yyjson_get_str(param),
                                  static_cast<float>(yyjson_get_num(value))));
        }
    } else if (method == "set_string_param") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            yyjson_val* value = yyjson_obj_get(root, "value");
            if (!nid || !param || !value ||
                !yyjson_is_str(nid) || !yyjson_is_str(param) || !yyjson_is_str(value))
                result = json_err("missing 'node_id', 'param', or 'value' (string)");
            else
                result = command_result_to_json(
                    api.set_string_param(yyjson_get_str(nid), yyjson_get_str(param),
                                         yyjson_get_str(value)));
        }
    } else if (method == "get_param") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            if (!nid || !param || !yyjson_is_str(nid) || !yyjson_is_str(param))
                result = json_err("missing 'node_id' or 'param'");
            else {
                auto r = api.get_param(yyjson_get_str(nid), yyjson_get_str(param));
                if (r.ok) {
                    // Return the value as a number
                    float v = 0;
                    try { v = std::stof(r.message); } catch (...) {}
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* rroot = yyjson_mut_obj(rdoc);
                    yyjson_mut_doc_set_root(rdoc, rroot);
                    yyjson_mut_obj_add_bool(rdoc, rroot, "ok", true);
                    yyjson_mut_obj_add_real(rdoc, rroot, "value",
                                            static_cast<double>(v));
                    result = json_serialize(rdoc);
                    yyjson_mut_doc_free(rdoc);
                } else {
                    result = json_err(r.message);
                }
            }
        }
    } else if (method == "save_graph") {
        if (root) {
            yyjson_val* path = yyjson_obj_get(root, "path");
            if (path && yyjson_is_str(path))
                result = command_result_to_json(api.save_as(yyjson_get_str(path)));
            else
                result = command_result_to_json(api.save());
        } else {
            result = command_result_to_json(api.save());
        }
    } else if (method == "load_graph") {
        // reload updates has_gpu_ops/has_audio via out-params;
        // main loop's needs_gpu_realloc() check handles GPU textures.
        result = command_result_to_json(api.reload(has_gpu_ops, has_audio));
    } else if (method == "set_resolution") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            yyjson_val* w   = yyjson_obj_get(root, "width");
            yyjson_val* h   = yyjson_obj_get(root, "height");
            if (!nid || !w || !h || !yyjson_is_str(nid) || !yyjson_is_num(w) || !yyjson_is_num(h))
                result = json_err("missing 'node_id', 'width', or 'height'");
            else
                result = command_result_to_json(
                    api.set_resolution(yyjson_get_str(nid),
                                       static_cast<uint32_t>(yyjson_get_num(w)),
                                       static_cast<uint32_t>(yyjson_get_num(h))));
        }
    } else if (method == "set_node_layout") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            yyjson_val* x   = yyjson_obj_get(root, "x");
            yyjson_val* y   = yyjson_obj_get(root, "y");
            if (!nid || !x || !y || !yyjson_is_str(nid) || !yyjson_is_num(x) || !yyjson_is_num(y))
                result = json_err("missing 'node_id', 'x', or 'y'");
            else
                result = command_result_to_json(
                    api.set_node_layout(yyjson_get_str(nid),
                                        static_cast<float>(yyjson_get_num(x)),
                                        static_cast<float>(yyjson_get_num(y))));
        }
    } else if (method == "inspect") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            if (!nid || !yyjson_is_str(nid))
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.inspect(yyjson_get_str(nid)));
        }
    } else if (method == "list_nodes") {
        result = command_result_to_json(api.list_nodes());
    } else if (method == "add_midi_mapping") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            yyjson_val* cc    = yyjson_obj_get(root, "cc");
            yyjson_val* ch    = yyjson_obj_get(root, "channel");
            yyjson_val* rmin  = yyjson_obj_get(root, "range_min");
            yyjson_val* rmax  = yyjson_obj_get(root, "range_max");
            if (!nid || !param || !cc || !ch || !rmin || !rmax ||
                !yyjson_is_str(nid) || !yyjson_is_str(param) ||
                !yyjson_is_num(cc) || !yyjson_is_num(ch) ||
                !yyjson_is_num(rmin) || !yyjson_is_num(rmax))
                result = json_err("missing or invalid params for add_midi_mapping");
            else
                result = command_result_to_json(
                    api.add_midi_mapping(yyjson_get_str(nid), yyjson_get_str(param),
                                         static_cast<int>(yyjson_get_int(cc)),
                                         static_cast<int>(yyjson_get_int(ch)),
                                         static_cast<float>(yyjson_get_num(rmin)),
                                         static_cast<float>(yyjson_get_num(rmax))));
        }
    } else if (method == "remove_midi_mapping") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            if (!nid || !param || !yyjson_is_str(nid) || !yyjson_is_str(param))
                result = json_err("missing 'node_id' or 'param'");
            else
                result = command_result_to_json(
                    api.remove_midi_mapping(yyjson_get_str(nid), yyjson_get_str(param)));
        }
    } else if (method == "update_midi_mapping") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            yyjson_val* rmin  = yyjson_obj_get(root, "range_min");
            yyjson_val* rmax  = yyjson_obj_get(root, "range_max");
            if (!nid || !param || !rmin || !rmax ||
                !yyjson_is_str(nid) || !yyjson_is_str(param) ||
                !yyjson_is_num(rmin) || !yyjson_is_num(rmax))
                result = json_err("missing or invalid params for update_midi_mapping");
            else
                result = command_result_to_json(
                    api.update_midi_mapping(yyjson_get_str(nid), yyjson_get_str(param),
                                            static_cast<float>(yyjson_get_num(rmin)),
                                            static_cast<float>(yyjson_get_num(rmax))));
        }
    } else if (method == "get_graph_errors") {
        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
        yyjson_mut_val* res = yyjson_mut_obj(rdoc);
        yyjson_mut_val* errs = yyjson_mut_arr(rdoc);
        for (const auto& ns : scheduler.nodes()) {
            if (!ns.errored) continue;
            yyjson_mut_val* e = yyjson_mut_obj(rdoc);
            yyjson_mut_obj_add_strcpy(rdoc, e, "node_id", ns.node_id.c_str());
            yyjson_mut_obj_add_strcpy(rdoc, e, "error", ns.error_message.c_str());
            yyjson_mut_arr_add_val(errs, e);
        }
        yyjson_mut_obj_add_val(rdoc, res, "errors", errs);
        result = json_ok(rdoc, res);
    } else if (method == "save_variation") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!name || !yyjson_is_str(name))
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.save_variation(yyjson_get_str(name)));
        }
    } else if (method == "recall_variation") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!name || !yyjson_is_str(name))
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.recall_variation(yyjson_get_str(name)));
        }
    } else if (method == "remove_variation") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!name || !yyjson_is_str(name))
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.remove_variation(yyjson_get_str(name)));
        }
    } else if (method == "rename_variation") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* old_name = yyjson_obj_get(root, "old_name");
            yyjson_val* new_name = yyjson_obj_get(root, "new_name");
            if (!old_name || !new_name || !yyjson_is_str(old_name) || !yyjson_is_str(new_name))
                result = json_err("missing 'old_name' or 'new_name'");
            else
                result = command_result_to_json(
                    api.rename_variation(yyjson_get_str(old_name), yyjson_get_str(new_name)));
        }
    } else if (method == "update_variation") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!name || !yyjson_is_str(name))
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.update_variation(yyjson_get_str(name)));
        }
    } else if (method == "list_variations") {
        result = command_result_to_json(api.list_variations());
    } else if (method == "queue_variation") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* name = yyjson_obj_get(root, "name");
            yyjson_val* quantize = yyjson_obj_get(root, "quantize");
            if (!name || !yyjson_is_str(name))
                result = json_err("missing 'name'");
            else {
                std::string q = (quantize && yyjson_is_str(quantize))
                    ? yyjson_get_str(quantize) : "instant";
                result = command_result_to_json(api.queue_variation(yyjson_get_str(name), q));
            }
        }
    } else if (method == "set_quantize_clock") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            if (!nid || !yyjson_is_str(nid))
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.set_quantize_clock(yyjson_get_str(nid)));
        }
    } else if (method == "save_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid  = yyjson_obj_get(root, "node_id");
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!nid || !name || !yyjson_is_str(nid) || !yyjson_is_str(name))
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.save_preset(yyjson_get_str(nid), yyjson_get_str(name)));
        }
    } else if (method == "recall_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid  = yyjson_obj_get(root, "node_id");
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!nid || !name || !yyjson_is_str(nid) || !yyjson_is_str(name))
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.recall_preset(yyjson_get_str(nid), yyjson_get_str(name)));
        }
    } else if (method == "update_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid  = yyjson_obj_get(root, "node_id");
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!nid || !name || !yyjson_is_str(nid) || !yyjson_is_str(name))
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.update_preset(yyjson_get_str(nid), yyjson_get_str(name)));
        }
    } else if (method == "remove_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid  = yyjson_obj_get(root, "node_id");
            yyjson_val* name = yyjson_obj_get(root, "name");
            if (!nid || !name || !yyjson_is_str(nid) || !yyjson_is_str(name))
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.remove_preset(yyjson_get_str(nid), yyjson_get_str(name)));
        }
    } else if (method == "rename_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid      = yyjson_obj_get(root, "node_id");
            yyjson_val* old_name = yyjson_obj_get(root, "old_name");
            yyjson_val* new_name = yyjson_obj_get(root, "new_name");
            if (!nid || !old_name || !new_name ||
                !yyjson_is_str(nid) || !yyjson_is_str(old_name) || !yyjson_is_str(new_name))
                result = json_err("missing 'node_id', 'old_name', or 'new_name'");
            else
                result = command_result_to_json(
                    api.rename_preset(yyjson_get_str(nid), yyjson_get_str(old_name),
                                      yyjson_get_str(new_name)));
        }
    } else if (method == "list_presets") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            if (!nid || !yyjson_is_str(nid))
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.list_presets(yyjson_get_str(nid)));
        }
    } else if (method == "list_factory_presets") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid = yyjson_obj_get(root, "node_id");
            if (!nid || !yyjson_is_str(nid))
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.list_factory_presets(yyjson_get_str(nid)));
        }
    } else if (method == "set_param_lock") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            yyjson_val* flags = yyjson_obj_get(root, "flags");
            if (!nid || !param || !flags ||
                !yyjson_is_str(nid) || !yyjson_is_str(param) || !yyjson_is_num(flags))
                result = json_err("missing 'node_id', 'param', or 'flags'");
            else
                result = command_result_to_json(
                    api.set_param_lock(yyjson_get_str(nid), yyjson_get_str(param),
                                       static_cast<uint8_t>(yyjson_get_int(flags))));
        }
    } else if (method == "get_param_lock") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* nid   = yyjson_obj_get(root, "node_id");
            yyjson_val* param = yyjson_obj_get(root, "param");
            if (!nid || !param || !yyjson_is_str(nid) || !yyjson_is_str(param))
                result = json_err("missing 'node_id' or 'param'");
            else
                result = command_result_to_json(
                    api.get_param_lock(yyjson_get_str(nid), yyjson_get_str(param)));
        }
    } else if (method == "set_state_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* sm    = yyjson_obj_get(root, "sm_node");
            yyjson_val* sidx  = yyjson_obj_get(root, "state_idx");
            yyjson_val* tgt   = yyjson_obj_get(root, "target_node");
            yyjson_val* pname = yyjson_obj_get(root, "name");
            if (!sm || !sidx || !tgt || !pname ||
                !yyjson_is_str(sm) || !yyjson_is_num(sidx) ||
                !yyjson_is_str(tgt) || !yyjson_is_str(pname))
                result = json_err("missing 'sm_node', 'state_idx', 'target_node', or 'name'");
            else
                result = command_result_to_json(
                    api.set_state_preset(yyjson_get_str(sm),
                                         static_cast<int>(yyjson_get_int(sidx)),
                                         yyjson_get_str(tgt), yyjson_get_str(pname)));
        }
    } else if (method == "remove_state_preset") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* sm   = yyjson_obj_get(root, "sm_node");
            yyjson_val* sidx = yyjson_obj_get(root, "state_idx");
            yyjson_val* tgt  = yyjson_obj_get(root, "target_node");
            if (!sm || !sidx || !tgt ||
                !yyjson_is_str(sm) || !yyjson_is_num(sidx) || !yyjson_is_str(tgt))
                result = json_err("missing 'sm_node', 'state_idx', or 'target_node'");
            else
                result = command_result_to_json(
                    api.remove_state_preset(yyjson_get_str(sm),
                                            static_cast<int>(yyjson_get_int(sidx)),
                                            yyjson_get_str(tgt)));
        }
    } else if (method == "clear_state_presets") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* sm = yyjson_obj_get(root, "sm_node");
            if (!sm || !yyjson_is_str(sm))
                result = json_err("missing 'sm_node'");
            else
                result = command_result_to_json(
                    api.clear_state_presets(yyjson_get_str(sm)));
        }
    } else if (method == "inspect_state_presets") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* sm = yyjson_obj_get(root, "sm_node");
            if (!sm || !yyjson_is_str(sm))
                result = json_err("missing 'sm_node'");
            else
                result = command_result_to_json(
                    api.inspect_state_presets(yyjson_get_str(sm)));
        }
    } else if (method == "scaffold_operator") {
        result = [&]() -> std::string {
            if (src_dir.empty())
                return json_err("scaffold_operator requires --src-dir");
            if (!root)
                return json_err("invalid JSON body");

            yyjson_val* name_v   = yyjson_obj_get(root, "name");
            yyjson_val* domain_v = yyjson_obj_get(root, "domain");
            if (!name_v || !domain_v || !yyjson_is_str(name_v) || !yyjson_is_str(domain_v))
                return json_err("missing 'name' or 'domain'");

            std::string name = yyjson_get_str(name_v);
            std::string domain_str_val = yyjson_get_str(domain_v);

            VividDomain domain;
            if (domain_str_val == "control")      domain = VIVID_DOMAIN_CONTROL;
            else if (domain_str_val == "audio")    domain = VIVID_DOMAIN_AUDIO;
            else if (domain_str_val == "gpu")      domain = VIVID_DOMAIN_GPU;
            else return json_err("domain must be 'control', 'audio', or 'gpu'");

            // Optional variant (e.g. "composite")
            std::string variant;
            yyjson_val* variant_v = yyjson_obj_get(root, "variant");
            if (variant_v && yyjson_is_str(variant_v))
                variant = yyjson_get_str(variant_v);

            // Optional destination:
            //   "auto" (policy-driven default)
            //   "project"
            //   "core"
            //   "package:<name>"
            //   absolute path to a project/package root
            std::string destination = "auto";
            yyjson_val* dest_v = yyjson_obj_get(root, "destination");
            if (dest_v && yyjson_is_str(dest_v))
                destination = yyjson_get_str(dest_v);

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

            auto cr = OperatorCreator::create(name, domain, resolved.root, variant,
                                              resolved.package_layout);
            if (!cr.success) return json_err(cr.error);

            if (hot_reloader) {
                if (resolved.package_layout && !resolved.package_name.empty())
                    hot_reloader->queue_rebuild("pkg:" + resolved.package_name + ":" + cr.target_name);
                else
                    hot_reloader->queue_rebuild(cr.target_name);
            }

            OperatorCreator::open_in_editor(cr.cpp_path);

            yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* res = yyjson_mut_obj(rdoc);
            yyjson_mut_obj_add_strcpy(rdoc, res, "cpp_path", cr.cpp_path.c_str());
            yyjson_mut_obj_add_strcpy(rdoc, res, "target_name", cr.target_name.c_str());
            yyjson_mut_obj_add_strcpy(rdoc, res, "destination_root", resolved.root.c_str());
            yyjson_mut_obj_add_bool(rdoc, res, "destination_is_package", resolved.package_layout);
            if (!resolved.package_name.empty())
                yyjson_mut_obj_add_strcpy(rdoc, res, "destination_package", resolved.package_name.c_str());
            if (!resolved.warning.empty())
                yyjson_mut_obj_add_strcpy(rdoc, res, "destination_warning", resolved.warning.c_str());
            return json_ok(rdoc, res);
        }();
    } else if (method == "install_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* url_v = yyjson_obj_get(root, "url");
            if (!url_v || !yyjson_is_str(url_v))
                result = json_err("missing 'url'");
            else {
                auto ir = package_manager->install(yyjson_get_str(url_v));
                if (ir.success) {
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, res, "name", ir.info.name.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, res, "version", ir.info.version.c_str());
                    if (!ir.info.vivid_core.empty())
                        yyjson_mut_obj_add_strcpy(rdoc, res, "vivid_core", ir.info.vivid_core.c_str());
                    yyjson_mut_obj_add_int(rdoc, res, "operator_count",
                        static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size()));
                    result = json_ok(rdoc, res);
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "uninstall_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                if (package_manager->uninstall(yyjson_get_str(name_v)))
                    result = json_ok_msg("uninstalled");
                else
                    result = json_err("failed to uninstall package");
            }
        }
    } else if (method == "link_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* path_v = yyjson_obj_get(root, "path");
            if (!path_v || !yyjson_is_str(path_v))
                result = json_err("missing 'path'");
            else {
                auto ir = package_manager->link(yyjson_get_str(path_v));
                if (ir.success) {
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, res, "name", ir.info.name.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, res, "version", ir.info.version.c_str());
                    if (!ir.info.vivid_core.empty())
                        yyjson_mut_obj_add_strcpy(rdoc, res, "vivid_core", ir.info.vivid_core.c_str());
                    yyjson_mut_obj_add_int(rdoc, res, "operator_count",
                        static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size()));
                    yyjson_mut_obj_add_bool(rdoc, res, "linked", true);
                    result = json_ok(rdoc, res);
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "unlink_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                if (package_manager->unlink(yyjson_get_str(name_v)))
                    result = json_ok_msg("unlinked");
                else
                    result = json_err("failed to unlink package");
            }
        }
    } else if (method == "rebuild_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                auto ir = package_manager->rebuild(yyjson_get_str(name_v));
                if (ir.success) {
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, res, "name", ir.info.name.c_str());
                    yyjson_mut_obj_add_int(rdoc, res, "operator_count",
                        static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size()));
                    yyjson_mut_obj_add_bool(rdoc, res, "linked", ir.info.linked);
                    result = json_ok(rdoc, res);
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "list_packages") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else {
            auto packages = package_manager->list();
            yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* res = yyjson_mut_obj(rdoc);
            yyjson_mut_val* arr = yyjson_mut_arr(rdoc);
            for (const auto& pkg : packages) {
                yyjson_mut_val* p = yyjson_mut_obj(rdoc);
                yyjson_mut_obj_add_strcpy(rdoc, p, "name", pkg.name.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, p, "version", pkg.version.c_str());
                if (!pkg.vivid_core.empty())
                    yyjson_mut_obj_add_strcpy(rdoc, p, "vivid_core", pkg.vivid_core.c_str());
                if (!pkg.source_scope.empty())
                    yyjson_mut_obj_add_strcpy(rdoc, p, "source_scope", pkg.source_scope.c_str());
                if (!pkg.path.empty())
                    yyjson_mut_obj_add_strcpy(rdoc, p, "path", pkg.path.c_str());
                if (!pkg.build_type.empty())
                    yyjson_mut_obj_add_strcpy(rdoc, p, "build_type", pkg.build_type.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, p, "description", pkg.description.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, p, "author", pkg.author.c_str());
                yyjson_mut_val* ops = yyjson_mut_arr(rdoc);
                for (const auto& op : pkg.operators)
                    yyjson_mut_arr_add_strcpy(rdoc, ops, op.c_str());
                for (const auto& op : pkg.gpu_operators)
                    yyjson_mut_arr_add_strcpy(rdoc, ops, op.c_str());
                yyjson_mut_obj_add_val(rdoc, p, "operators", ops);
                yyjson_mut_obj_add_bool(rdoc, p, "linked", pkg.linked);
                yyjson_mut_arr_add_val(arr, p);
            }
            yyjson_mut_obj_add_val(rdoc, res, "packages", arr);
            result = json_ok(rdoc, res);
        }
    } else if (method == "read_package_docs") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                std::string name = yyjson_get_str(name_v);
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
                        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                        yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                        yyjson_mut_obj_add_strcpy(rdoc, res, "name", name.c_str());
                        yyjson_mut_obj_add_strcpy(rdoc, res, "content", ss.str().c_str());
                        result = json_ok(rdoc, res);
                    }
                }
            }
        }
    } else if (method == "list_package_examples") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                std::string name = yyjson_get_str(name_v);
                if (!is_safe_package_name(name)) {
                    result = json_err("invalid package name");
                } else if (!package_manager->is_installed(name)) {
                    result = json_err("package not installed: " + name);
                } else {
                    auto graphs_dir = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs";
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, res, "name", name.c_str());
                    yyjson_mut_val* arr = yyjson_mut_arr(rdoc);
                    std::error_code ec;
                    if (std::filesystem::is_directory(graphs_dir, ec)) {
                        for (const auto& entry : std::filesystem::directory_iterator(graphs_dir, ec)) {
                            if (!entry.is_regular_file()) continue;
                            if (entry.path().extension() != ".json") continue;
                            yyjson_mut_val* ex = yyjson_mut_obj(rdoc);
                            yyjson_mut_obj_add_strcpy(rdoc, ex, "filename", entry.path().filename().c_str());
                            // Try to extract a top-level "description" from the graph JSON
                            std::string desc_str;
                            std::ifstream f(entry.path());
                            if (f.is_open()) {
                                std::ostringstream ss;
                                ss << f.rdbuf();
                                auto content = ss.str();
                                yyjson_doc* gdoc = yyjson_read(content.c_str(), content.size(), 0);
                                if (gdoc) {
                                    yyjson_val* groot = yyjson_doc_get_root(gdoc);
                                    yyjson_val* dval = groot ? yyjson_obj_get(groot, "description") : nullptr;
                                    if (dval && yyjson_is_str(dval))
                                        desc_str = yyjson_get_str(dval);
                                    yyjson_doc_free(gdoc);
                                }
                            }
                            yyjson_mut_obj_add_strcpy(rdoc, ex, "description", desc_str.c_str());
                            yyjson_mut_arr_add_val(arr, ex);
                        }
                    }
                    yyjson_mut_obj_add_val(rdoc, res, "examples", arr);
                    result = json_ok(rdoc, res);
                }
            }
        }
    } else if (method == "read_package_example") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            yyjson_val* file_v = yyjson_obj_get(root, "filename");
            if (!name_v || !yyjson_is_str(name_v) || !file_v || !yyjson_is_str(file_v))
                result = json_err("missing 'name' or 'filename'");
            else {
                std::string name = yyjson_get_str(name_v);
                std::string filename = yyjson_get_str(file_v);
                // Path traversal prevention
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
                        yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                        yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                        yyjson_mut_obj_add_strcpy(rdoc, res, "name", name.c_str());
                        yyjson_mut_obj_add_strcpy(rdoc, res, "filename", filename.c_str());
                        yyjson_mut_obj_add_strcpy(rdoc, res, "content", ss.str().c_str());
                        result = json_ok(rdoc, res);
                    }
                }
            }
        }
    } else if (method == "package_operator_docs") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                std::string name = yyjson_get_str(name_v);
                if (!package_manager->is_installed(name)) {
                    result = json_err("package not installed: " + name);
                } else {
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, res, "package", name.c_str());
                    yyjson_mut_val* ops_arr = yyjson_mut_arr(rdoc);
                    for (const auto& type_name : registry.type_names()) {
                        const auto* pkg = registry.package_for_type(type_name);
                        if (!pkg || *pkg != name) continue;
                        const auto* desc = registry.probe_descriptor(type_name);
                        if (!desc) continue;

                        yyjson_mut_val* op = yyjson_mut_obj(rdoc);
                        yyjson_mut_obj_add_strcpy(rdoc, op, "name", desc->name);
                        yyjson_mut_obj_add_str(rdoc, op, "domain", domain_str(desc->domain));
                        yyjson_mut_obj_add_bool(rdoc, op, "time_dependent", desc->time_dependent != 0);

                        // Params — richer than list_types
                        yyjson_mut_val* params_arr = yyjson_mut_arr(rdoc);
                        for (uint32_t i = 0; i < desc->param_count; ++i) {
                            const auto& pd = desc->params[i];
                            yyjson_mut_val* p = yyjson_mut_obj(rdoc);
                            yyjson_mut_obj_add_strcpy(rdoc, p, "name", pd.name);
                            yyjson_mut_obj_add_str(rdoc, p, "type", param_type_str(pd.type));
                            yyjson_mut_obj_add_real(rdoc, p, "default", static_cast<double>(pd.default_value));
                            yyjson_mut_obj_add_real(rdoc, p, "min", static_cast<double>(pd.min_value));
                            yyjson_mut_obj_add_real(rdoc, p, "max", static_cast<double>(pd.max_value));
                            if (pd.semantic_tag)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "semantic_tag", pd.semantic_tag);
                            if (pd.semantic_shape)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "semantic_shape", pd.semantic_shape);
                            if (pd.semantic_unit)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "semantic_unit", pd.semantic_unit);
                            if (pd.semantic_intent)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "semantic_intent", pd.semantic_intent);
                            if (pd.default_string)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "default_string", pd.default_string);
                            if (pd.group)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "group", pd.group);
                            if (pd.choice_count > 0 && pd.choice_labels) {
                                yyjson_mut_val* choices = yyjson_mut_arr(rdoc);
                                for (uint32_t c = 0; c < pd.choice_count; ++c)
                                    yyjson_mut_arr_add_strcpy(rdoc, choices, pd.choice_labels[c]);
                                yyjson_mut_obj_add_val(rdoc, p, "choices", choices);
                            }
                            yyjson_mut_arr_add_val(params_arr, p);
                        }
                        yyjson_mut_obj_add_val(rdoc, op, "params", params_arr);

                        // Ports — same split as list_types
                        yyjson_mut_val* inputs_arr = yyjson_mut_arr(rdoc);
                        yyjson_mut_val* outputs_arr = yyjson_mut_arr(rdoc);
                        for (uint32_t i = 0; i < desc->port_count; ++i) {
                            const auto& portd = desc->ports[i];
                            yyjson_mut_val* p = yyjson_mut_obj(rdoc);
                            yyjson_mut_obj_add_strcpy(rdoc, p, "name", portd.name);
                            yyjson_mut_obj_add_str(rdoc, p, "type", port_type_str(portd.type));
                            if (portd.type == VIVID_PORT_DATA && portd.data_type)
                                yyjson_mut_obj_add_strcpy(rdoc, p, "data_type", portd.data_type);
                            if (portd.direction == VIVID_PORT_INPUT)
                                yyjson_mut_arr_add_val(inputs_arr, p);
                            else
                                yyjson_mut_arr_add_val(outputs_arr, p);
                        }
                        yyjson_mut_obj_add_val(rdoc, op, "inputs", inputs_arr);
                        yyjson_mut_obj_add_val(rdoc, op, "outputs", outputs_arr);

                        yyjson_mut_arr_add_val(ops_arr, op);
                    }
                    yyjson_mut_obj_add_val(rdoc, res, "operators", ops_arr);
                    result = json_ok(rdoc, res);
                }
            }
        }
    } else if (method == "test_package") {
        if (!package_manager || !package_compiler) {
            result = json_err("package manager/compiler not available");
        } else if (!root) {
            result = json_err("invalid JSON body");
        } else {
            yyjson_val* name_v = yyjson_obj_get(root, "name");
            if (!name_v || !yyjson_is_str(name_v))
                result = json_err("missing 'name'");
            else {
                std::string name = yyjson_get_str(name_v);
                auto tr = run_package_tests(name, *package_manager,
                                             *package_compiler, registry);
                if (!tr.error.empty()) {
                    result = json_err(tr.error);
                } else {
                    yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                    yyjson_mut_val* res = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, res, "package", tr.package_name.c_str());

                    yyjson_mut_val* summary = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_int(rdoc, summary, "total", tr.total);
                    yyjson_mut_obj_add_int(rdoc, summary, "passed", tr.passed);
                    yyjson_mut_obj_add_int(rdoc, summary, "failed", tr.failed);
                    yyjson_mut_obj_add_int(rdoc, summary, "skipped", tr.skipped);
                    yyjson_mut_obj_add_val(rdoc, res, "summary", summary);

                    yyjson_mut_val* tests_arr = yyjson_mut_arr(rdoc);
                    for (const auto& t : tr.tests) {
                        yyjson_mut_val* obj = yyjson_mut_obj(rdoc);
                        yyjson_mut_obj_add_strcpy(rdoc, obj, "name", t.name.c_str());
                        yyjson_mut_obj_add_strcpy(rdoc, obj, "type", t.type.c_str());
                        yyjson_mut_obj_add_strcpy(rdoc, obj, "status", t.status.c_str());
                        if (!t.reason.empty())
                            yyjson_mut_obj_add_strcpy(rdoc, obj, "reason", t.reason.c_str());
                        if (!t.output.empty())
                            yyjson_mut_obj_add_strcpy(rdoc, obj, "output", t.output.c_str());
                        yyjson_mut_arr_add_val(tests_arr, obj);
                    }
                    yyjson_mut_obj_add_val(rdoc, res, "tests", tests_arr);
                    result = json_ok(rdoc, res);
                }
            }
        }
    } else {
        result = json_err("unknown method '" + method + "'");
    }

    if (doc) yyjson_doc_free(doc);
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

void ControlServer::set_src_dir(const std::string& src_dir) { assert(!impl_); src_dir_ = src_dir; }
void ControlServer::set_hot_reloader(HotReloader* hr) { assert(!impl_); hot_reloader_ = hr; }
void ControlServer::set_capture_coordinator(CaptureCoordinator* cc) { assert(!impl_); capture_coordinator_ = cc; }
void ControlServer::set_package_manager(PackageManager* pm) { assert(!impl_); package_manager_ = pm; }
void ControlServer::set_package_compiler(PackageCompiler* pc) { assert(!impl_); package_compiler_ = pc; }
void ControlServer::set_package_catalog(PackageCatalog* cat) { assert(!impl_); package_catalog_ = cat; }
void ControlServer::set_app_update_manager(AppUpdateManager* aum) { assert(!impl_); app_update_manager_ = aum; }
void ControlServer::set_settings(const Settings* settings) { assert(!impl_); settings_ = settings; }

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
                    yyjson_doc* doc = yyjson_read(request->body.c_str(), request->body.size(), 0);
                    if (doc) {
                        yyjson_val* root = yyjson_doc_get_root(doc);
                        yyjson_val* path_v = root ? yyjson_obj_get(root, "path") : nullptr;
                        yyjson_val* fps_v  = root ? yyjson_obj_get(root, "fps")  : nullptr;
                        if (path_v && yyjson_is_str(path_v)) {
                            std::string candidate = yyjson_get_str(path_v);
                            if (!is_safe_recording_path(candidate)) {
                                yyjson_doc_free(doc);
                                return std::make_shared<ix::HttpResponse>(
                                    200, "OK", ix::HttpErrorCode::Ok,
                                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                                    R"({"ok":false,"error":"invalid recording path"})");
                            }
                            path = candidate;
                        }
                        if (fps_v && yyjson_is_real(fps_v))
                            fps = yyjson_get_real(fps_v);
                        else if (fps_v && yyjson_is_int(fps_v))
                            fps = static_cast<double>(yyjson_get_int(fps_v));
                        yyjson_doc_free(doc);
                    }
                    future = capture_coordinator_->request_start_recording(path, fps);
                } else {
                    future = capture_coordinator_->request_stop_recording();
                }

                auto status = future.wait_for(std::chrono::seconds(10));
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
                (method == "capture_frame" || method == "capture_audio" || method == "capture_av")) {
                CaptureType ctype = CaptureType::Frame;
                float audio_dur = 1.0f;
                if (method == "capture_audio") ctype = CaptureType::Audio;
                else if (method == "capture_av") ctype = CaptureType::AV;

                // Parse optional duration from body
                if (ctype == CaptureType::Audio || ctype == CaptureType::AV) {
                    yyjson_doc* doc = yyjson_read(request->body.c_str(), request->body.size(), 0);
                    if (doc) {
                        yyjson_val* root = yyjson_doc_get_root(doc);
                        yyjson_val* dur_v = root ? yyjson_obj_get(root, "duration") : nullptr;
                        if (dur_v && yyjson_is_real(dur_v))
                            audio_dur = static_cast<float>(yyjson_get_real(dur_v));
                        else if (dur_v && yyjson_is_int(dur_v))
                            audio_dur = static_cast<float>(yyjson_get_int(dur_v));
                        yyjson_doc_free(doc);
                    }
                }

                auto future = capture_coordinator_->request_capture(ctype, audio_dur);
                auto status = future.wait_for(std::chrono::seconds(5));
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
                yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                yyjson_mut_val* rroot = yyjson_mut_obj(rdoc);
                yyjson_mut_doc_set_root(rdoc, rroot);
                yyjson_mut_obj_add_true(rdoc, rroot, "ok");
                yyjson_mut_val* arr = yyjson_mut_arr(rdoc);
                for (const auto& e : entries) {
                    yyjson_mut_val* obj = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "name", e.name.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "description", e.description.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "version", e.version.c_str());
                    if (!e.vivid_core.empty())
                        yyjson_mut_obj_add_strcpy(rdoc, obj, "vivid_core", e.vivid_core.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "author", e.author.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "url", e.url.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "category", e.category.c_str());
                    yyjson_mut_obj_add_bool(rdoc, obj, "installed", e.installed);
                    if (e.installed)
                        yyjson_mut_obj_add_strcpy(rdoc, obj, "installed_version", e.installed_version.c_str());
                    yyjson_mut_val* tags = yyjson_mut_arr(rdoc);
                    for (const auto& tag : e.tags)
                        yyjson_mut_arr_add_strcpy(rdoc, tags, tag.c_str());
                    yyjson_mut_obj_add_val(rdoc, obj, "tags", tags);
                    yyjson_mut_arr_add_val(arr, obj);
                }
                yyjson_mut_obj_add_val(rdoc, rroot, "packages", arr);
                char* json_str = yyjson_mut_write(rdoc, 0, nullptr);
                std::string response_body = json_str ? json_str : R"({"ok":false,"error":"json write failed"})";
                if (json_str) free(json_str);
                yyjson_mut_doc_free(rdoc);
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
            }

            // Check package updates using catalog metadata + installed package versions
            if (method == "check_package_updates" && package_catalog_ && package_manager_) {
                std::string core_version = "0.1.0";
                bool include_all_installed = false;
                if (!request->body.empty()) {
                    yyjson_doc* doc = yyjson_read(request->body.c_str(), request->body.size(), 0);
                    if (doc) {
                        yyjson_val* root = yyjson_doc_get_root(doc);
                        yyjson_val* cv = root ? yyjson_obj_get(root, "core_version") : nullptr;
                        if (cv && yyjson_is_str(cv))
                            core_version = yyjson_get_str(cv);
                        yyjson_val* ia = root ? yyjson_obj_get(root, "include_all_installed") : nullptr;
                        if (ia && yyjson_is_bool(ia))
                            include_all_installed = yyjson_get_bool(ia);
                        yyjson_doc_free(doc);
                    }
                }

                auto entries = package_catalog_->entries();
                yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                yyjson_mut_val* rroot = yyjson_mut_obj(rdoc);
                yyjson_mut_doc_set_root(rdoc, rroot);
                yyjson_mut_obj_add_true(rdoc, rroot, "ok");
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "core_version", core_version.c_str());

                yyjson_mut_val* updates = yyjson_mut_arr(rdoc);
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

                    yyjson_mut_val* obj = yyjson_mut_obj(rdoc);
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "name", assessment.package_name.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "installed_version", assessment.installed_version.c_str());
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "remote_version", assessment.remote_version.c_str());
                    if (!assessment.remote_vivid_core.empty())
                        yyjson_mut_obj_add_strcpy(rdoc, obj, "vivid_core", assessment.remote_vivid_core.c_str());
                    yyjson_mut_obj_add_bool(rdoc, obj, "update_available", assessment.update_available);
                    yyjson_mut_obj_add_bool(rdoc, obj, "compatible", assessment.compatible);
                    yyjson_mut_obj_add_bool(rdoc, obj, "constraint_valid", assessment.constraint_valid);
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "classification", update_class_str(assessment.classification));
                    yyjson_mut_obj_add_strcpy(rdoc, obj, "message", assessment.message.c_str());

                    if (assessment.update_available) update_count++;
                    if (assessment.classification == PackageUpdateClass::IncompatibleUpdate)
                        incompatible_count++;

                    yyjson_mut_arr_add_val(updates, obj);
                }
                yyjson_mut_obj_add_int(rdoc, rroot, "updates_available", update_count);
                yyjson_mut_obj_add_int(rdoc, rroot, "incompatible_updates", incompatible_count);
                yyjson_mut_obj_add_val(rdoc, rroot, "packages", updates);

                char* json_str = yyjson_mut_write(rdoc, 0, nullptr);
                std::string response_body = json_str ? json_str : R"({"ok":false,"error":"json write failed"})";
                if (json_str) free(json_str);
                yyjson_mut_doc_free(rdoc);
                return std::make_shared<ix::HttpResponse>(
                    200, "OK", ix::HttpErrorCode::Ok,
                    ix::WebSocketHttpHeaders{{"Content-Type", "application/json"}},
                    response_body);
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
                    yyjson_doc* doc = yyjson_read(request->body.c_str(), request->body.size(), 0);
                    if (doc) {
                        yyjson_val* root = yyjson_doc_get_root(doc);
                        yyjson_val* fr = root ? yyjson_obj_get(root, "force_refresh") : nullptr;
                        if (fr && yyjson_is_bool(fr))
                            force_refresh = yyjson_get_bool(fr);
                        yyjson_doc_free(doc);
                    }
                }
                if (force_refresh) app_update_manager_->refresh();
                if (app_update_manager_->fetch_state() == AppUpdateFetchState::Idle)
                    app_update_manager_->refresh();
                for (int i = 0; i < 200; ++i) {
                    auto st = app_update_manager_->fetch_state();
                    if (st != AppUpdateFetchState::Fetching) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
                yyjson_mut_val* rroot = yyjson_mut_obj(rdoc);
                yyjson_mut_doc_set_root(rdoc, rroot);
                yyjson_mut_obj_add_true(rdoc, rroot, "ok");

                const auto st = app_update_manager_->fetch_state();
                switch (st) {
                    case AppUpdateFetchState::Idle:
                        yyjson_mut_obj_add_strcpy(rdoc, rroot, "state", "idle");
                        break;
                    case AppUpdateFetchState::Fetching:
                        yyjson_mut_obj_add_strcpy(rdoc, rroot, "state", "fetching");
                        break;
                    case AppUpdateFetchState::Ready:
                        yyjson_mut_obj_add_strcpy(rdoc, rroot, "state", "ready");
                        break;
                    case AppUpdateFetchState::Error:
                        yyjson_mut_obj_add_strcpy(rdoc, rroot, "state", "error");
                        break;
                }

                auto info = app_update_manager_->latest();
                yyjson_mut_obj_add_bool(rdoc, rroot, "update_available", info.update_available);
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "current_version", info.current_version.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "latest_version", info.latest_version.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "download_url", info.download_url.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "release_notes_url", info.release_notes_url.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "title", info.title.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "publication_date", info.publication_date.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "minimum_system_version", info.minimum_system_version.c_str());
                yyjson_mut_obj_add_strcpy(rdoc, rroot, "appcast_url",
                                          AppUpdateManager::appcast_url().c_str());
                if (st == AppUpdateFetchState::Error) {
                    yyjson_mut_obj_add_strcpy(rdoc, rroot, "error",
                                              app_update_manager_->fetch_error().c_str());
                }

                char* json_str = yyjson_mut_write(rdoc, 0, nullptr);
                std::string response_body = json_str ? json_str : R"({"ok":false,"error":"json write failed"})";
                if (json_str) free(json_str);
                yyjson_mut_doc_free(rdoc);
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
                auto status = future.wait_for(std::chrono::seconds(60));
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
            Impl::PendingRequest req;
            req.method = std::move(method);
            req.body = request->body;
            auto future = req.promise.get_future();

            {
                std::lock_guard<std::mutex> lock(impl_->queue_mutex);
                impl_->queue.push_back(std::move(req));
            }

            auto status = future.wait_for(std::chrono::seconds(5));
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
