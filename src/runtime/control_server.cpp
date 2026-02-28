#include "runtime/control_server.h"
#include "runtime/capture_coordinator.h"
#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/operator_registry.h"
#include "runtime/operator_loader.h"
#include "runtime/operator_creator.h"
#include "runtime/hot_reload.h"
#include "operator_api/types.h"
#include "yyjson.h"
#include <ixwebsocket/IXHttpServer.h>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>

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
        if (conn.scale != 1.0f)
            yyjson_mut_obj_add_real(doc, c, "scale", conn.scale);
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
                            const std::string& src_dir, HotReloader* hot_reloader) {
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
    } else if (method == "set_connection_scale") {
        if (!root) { result = json_err("invalid JSON body"); }
        else {
            yyjson_val* from  = yyjson_obj_get(root, "from_addr");
            yyjson_val* to    = yyjson_obj_get(root, "to_addr");
            yyjson_val* scale = yyjson_obj_get(root, "scale");
            if (!from || !to || !scale ||
                !yyjson_is_str(from) || !yyjson_is_str(to) || !yyjson_is_num(scale))
                result = json_err("missing 'from_addr', 'to_addr', or 'scale'");
            else
                result = command_result_to_json(
                    api.set_connection_scale(yyjson_get_str(from), yyjson_get_str(to),
                                             static_cast<float>(yyjson_get_num(scale))));
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
    bool running = false;

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
        std::string response = dispatch(req.method, req.body,
                                        api, graph, scheduler, registry,
                                        has_gpu_ops, has_audio,
                                        src_dir_, hot_reloader_);
        req.promise.set_value(std::move(response));
    }
}

} // namespace vivid
