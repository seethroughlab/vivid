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
#include "operator_api/types.h"
#include "yyjson.h"
#include <ixwebsocket/IXHttpServer.h>
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

static bool is_safe_package_name(const std::string& name) {
    return name.find('/') == std::string::npos &&
           name.find('\\') == std::string::npos &&
           name.find("..") == std::string::npos;
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
                if (pd.type == VIVID_PARAM_FILE && ns) {
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
                        oi->second < ns->output_spreads.size() &&
                        !ns->output_spreads[oi->second].empty()) {
                        yyjson_mut_val* spread_arr = yyjson_mut_arr(doc);
                        for (float sv : ns->output_spreads[oi->second])
                            yyjson_mut_arr_add_real(doc, spread_arr, static_cast<double>(sv));
                        yyjson_mut_obj_add_val(doc, p, "spread", spread_arr);
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
                        ii->second < ns->input_spreads.size() &&
                        !ns->input_spreads[ii->second].empty()) {
                        yyjson_mut_val* spread_arr = yyjson_mut_arr(doc);
                        for (float sv : ns->input_spreads[ii->second])
                            yyjson_mut_arr_add_real(doc, spread_arr, static_cast<double>(sv));
                        yyjson_mut_obj_add_val(doc, p, "spread", spread_arr);
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
                            PackageCompiler* package_compiler) {
    // Read-only queries (no body needed)
    if (method == "inspect_graph") return handle_inspect_graph(graph, scheduler);
    if (method == "list_types")    return handle_list_types(registry);

    // Parse body JSON (may be empty for some commands)
    yyjson_doc* doc = yyjson_read(body.c_str(), body.size(), 0);
    yyjson_val* root = doc ? yyjson_doc_get_root(doc) : nullptr;

    std::string result;

    if (method == "add_node") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* type_v = yyjson_obj_get(root, "type");
            yyjson_val* id_v   = yyjson_obj_get(root, "id");
            if (!type_v || !id_v || !yyjson_is_str(type_v) || !yyjson_is_str(id_v))
                result = json_err("missing 'type' or 'id'");
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
            if (!from || !to || !yyjson_is_str(from) || !yyjson_is_str(to))
                result = json_err("missing 'from_addr' or 'to_addr'");
            else
                result = command_result_to_json(
                    api.connect(yyjson_get_str(from), yyjson_get_str(to)));
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
                                       static_cast<uint32_t>(yyjson_get_int(w)),
                                       static_cast<uint32_t>(yyjson_get_int(h))));
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
            yyjson_val* pname = yyjson_obj_get(root, "preset_name");
            if (!sm || !sidx || !tgt || !pname ||
                !yyjson_is_str(sm) || !yyjson_is_num(sidx) ||
                !yyjson_is_str(tgt) || !yyjson_is_str(pname))
                result = json_err("missing 'sm_node', 'state_idx', 'target_node', or 'preset_name'");
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

            std::string err = OperatorCreator::validate_name(name, registry);
            if (!err.empty()) return json_err(err);

            auto cr = OperatorCreator::create(name, domain, src_dir, variant);
            if (!cr.success) return json_err(cr.error);

            if (hot_reloader)
                hot_reloader->queue_rebuild(cr.target_name);

            OperatorCreator::open_in_editor(cr.cpp_path);

            yyjson_mut_doc* rdoc = yyjson_mut_doc_new(nullptr);
            yyjson_mut_val* res = yyjson_mut_obj(rdoc);
            yyjson_mut_obj_add_strcpy(rdoc, res, "cpp_path", cr.cpp_path.c_str());
            yyjson_mut_obj_add_strcpy(rdoc, res, "target_name", cr.target_name.c_str());
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

void ControlServer::set_src_dir(const std::string& src_dir) { src_dir_ = src_dir; }
void ControlServer::set_hot_reloader(HotReloader* hr) { hot_reloader_ = hr; }
void ControlServer::set_capture_coordinator(CaptureCoordinator* cc) { capture_coordinator_ = cc; }
void ControlServer::set_package_manager(PackageManager* pm) { package_manager_ = pm; }
void ControlServer::set_package_compiler(PackageCompiler* pc) { package_compiler_ = pc; }
void ControlServer::set_package_catalog(PackageCatalog* cat) { package_catalog_ = cat; }

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
                        if (path_v && yyjson_is_str(path_v))
                            path = yyjson_get_str(path_v);
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
                                        package_compiler_);

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
