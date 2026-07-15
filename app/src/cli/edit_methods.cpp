#include "cli/edit_methods.h"

#include <unordered_map>

namespace vivid {

const EditMethodInfo* edit_method_info(const std::string& method) {
    // label, coalesces. Structural edits (add/remove/connect) never coalesce — each is its own
    // undo boundary; value edits (set_*_param) coalesce a rapid run into one entry.
    static const std::unordered_map<std::string, EditMethodInfo> kTable = {
        // ---- visuals (G2) ----
        { "add_node",            { "Add Node",           false } },
        { "remove_node",         { "Delete Node",        false } },
        { "connect_nodes",       { "Connect",            false } },
        { "set_active_output",   { "Set Output",         false } },
        { "set_generator",       { "Set Generator",      false } },
        { "add_data_node",       { "Add Data Node",      false } },
        { "set_node_param",      { "Set Param",          true  } },
        { "set_node_file_param", { "Set File",           true  } },
        { "set_node_asset",      { "Set Asset",          true  } },
        // ---- the bridge (G2) ----
        { "connect_mapping",     { "Connect Mapping",    false } },
        { "disconnect_mapping",  { "Disconnect Mapping", false } },
        // ---- layout (G2) ----
        { "layout_graph",        { "Auto-Layout",        false } },
        // ---- audio (G3) appends here ----
    };
    auto it = kTable.find(method);
    return it == kTable.end() ? nullptr : &it->second;
}

}  // namespace vivid
