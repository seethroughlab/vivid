#include "runtime/graph.h"
#include "yyjson.h"
#include <cstdio>

namespace vivid {

static bool split_address(const char* addr, std::string& node, std::string& port) {
    const char* slash = std::strchr(addr, '/');
    if (!slash) return false;
    node.assign(addr, slash);
    port.assign(slash + 1);
    return !node.empty() && !port.empty();
}

bool Graph::load(const char* path) {
    nodes_.clear();
    connections_.clear();

    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(path, 0, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[vivid] Graph: failed to read %s: %s\n", path, err.msg);
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);

    // Parse nodes
    yyjson_val* nodes_obj = yyjson_obj_get(root, "nodes");
    if (nodes_obj && yyjson_is_obj(nodes_obj)) {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(nodes_obj, &iter);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* val = yyjson_obj_iter_get_val(key);

            NodeDef node;
            node.id = yyjson_get_str(key);

            yyjson_val* type_val = yyjson_obj_get(val, "type");
            if (!type_val || !yyjson_is_str(type_val)) {
                std::fprintf(stderr, "[vivid] Graph: node '%s' missing type\n", node.id.c_str());
                yyjson_doc_free(doc);
                return false;
            }
            node.type = yyjson_get_str(type_val);

            yyjson_val* params_obj = yyjson_obj_get(val, "params");
            if (params_obj && yyjson_is_obj(params_obj)) {
                yyjson_obj_iter piter;
                yyjson_obj_iter_init(params_obj, &piter);
                yyjson_val* pkey;
                while ((pkey = yyjson_obj_iter_next(&piter)) != nullptr) {
                    yyjson_val* pval = yyjson_obj_iter_get_val(pkey);
                    if (yyjson_is_num(pval)) {
                        node.params[yyjson_get_str(pkey)] = static_cast<float>(yyjson_get_num(pval));
                    }
                }
            }

            nodes_.push_back(std::move(node));
        }
    }

    // Parse connections
    yyjson_val* conns_arr = yyjson_obj_get(root, "connections");
    if (conns_arr && yyjson_is_arr(conns_arr)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(conns_arr, idx, max, val) {
            yyjson_val* from_val = yyjson_obj_get(val, "from");
            yyjson_val* to_val   = yyjson_obj_get(val, "to");
            if (!from_val || !to_val) continue;

            ConnectionDef conn;
            if (!split_address(yyjson_get_str(from_val), conn.from_node, conn.from_port) ||
                !split_address(yyjson_get_str(to_val),   conn.to_node,   conn.to_port)) {
                std::fprintf(stderr, "[vivid] Graph: invalid connection address\n");
                continue;
            }
            connections_.push_back(std::move(conn));
        }
    }

    yyjson_doc_free(doc);

    std::fprintf(stderr, "[vivid] Loaded graph: %s (%zu nodes, %zu connections)\n",
        path, nodes_.size(), connections_.size());
    return true;
}

} // namespace vivid
