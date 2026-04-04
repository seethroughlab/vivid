#include "runtime/graph/graph_compiler.h"
#include "runtime/graph/graph_compiler_internal.h"

#include <cstdio>

namespace vivid {

bool GraphCompiler::reload_operator(CompiledGraph& cg,
                                    const std::string& type_name,
                                    OperatorRegistry& registry,
                                    const std::string& new_dylib_path,
                                    const std::filesystem::path& graph_base_dir) {
    struct SavedParams {
        uint32_t node_idx;
        std::unordered_map<std::string, float> values;
        std::unordered_map<std::string, std::string> string_values;
        std::unordered_map<std::string, uint8_t> lock_flags;
    };
    std::vector<SavedParams> saved;

    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.nodes.size()); ++i) {
        auto& cn = cg.nodes[i];
        if (!cn.loader) continue;
        const auto* desc = cn.loader->descriptor();
        if (!desc || std::string(desc->name) != type_name) continue;

        SavedParams sp;
        sp.node_idx = i;
        for (const auto& [name, idx] : cn.param_indices) {
            sp.values[name] = cn.param_values[idx];
            if (cn.param_lock_flags[idx] != PARAM_LOCK_NONE)
                sp.lock_flags[name] = cn.param_lock_flags[idx];
        }
        for (const auto& [name, idx] : cn.file_param_indices) {
            sp.string_values[name] = cn.file_param_storage[idx];
        }
        saved.push_back(std::move(sp));
    }

    if (saved.empty()) return true;

    for (const auto& sp : saved) {
        auto& cn = cg.nodes[sp.node_idx];
        if (cn.instance) {
            cn.loader->destroy_instance(cn.instance);
            cn.instance = nullptr;
        }
    }

    if (!registry.reload_operator(type_name, new_dylib_path)) {
        std::fprintf(stderr, "[vivid] GraphCompiler: dylib reload failed for '%s'\n", type_name.c_str());
        OperatorLoader* old_loader = registry.find_loaded(type_name);
        if (old_loader && old_loader->is_loaded()) {
            const auto* old_desc = old_loader->descriptor();
            if (old_desc) {
                for (const auto& sp : saved) {
                    auto& cn = cg.nodes[sp.node_idx];
                    cn.instance = old_loader->create_instance();
                    init_frame_state(cn, old_desc, &sp.values,
                                     sp.string_values.empty() ? nullptr : &sp.string_values,
                                     graph_base_dir);
                    graph_compiler_internal::warm_up_instance_assets(cn);
                    for (const auto& [pname, flags] : sp.lock_flags) {
                        auto pi = cn.param_indices.find(pname);
                        if (pi != cn.param_indices.end())
                            cn.param_lock_flags[pi->second] = flags;
                    }
                    cn.dirty = true;
                }
            }
        }
        return false;
    }

    OperatorLoader* new_loader = registry.find_loaded(type_name);
    if (!new_loader) return false;
    const auto* new_desc = new_loader->descriptor();
    if (!new_desc) return false;

    for (const auto& sp : saved) {
        auto& cn = cg.nodes[sp.node_idx];
        cn.loader = new_loader;
        cn.instance = new_loader->create_instance();
        init_frame_state(cn, new_desc, &sp.values,
                         sp.string_values.empty() ? nullptr : &sp.string_values,
                         graph_base_dir);
        graph_compiler_internal::warm_up_instance_assets(cn);

        for (const auto& [pname, flags] : sp.lock_flags) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end())
                cn.param_lock_flags[pi->second] = flags;
        }

        cn.errored = false;
        cn.error_message.clear();
        cn.dirty = true;
    }

    return true;
}

} // namespace vivid
