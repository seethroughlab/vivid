#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/operator_registry.h"
#include "common/path_util.h"
#include "runtime/platform/platform.h"
#include <cstdio>
#include <filesystem>

namespace vivid {

namespace {
std::string normalized_graph_identity_path(const std::string& path_str) {
    if (path_str.empty()) return {};
    return std::filesystem::path(path_str).lexically_normal().string();
}
} // namespace

CommandResult RuntimeAPI::save() {
    const auto& path = graph_.source_path();
    if (path.empty()) return {false, "no source path (use save <path>)"};
    return save_as(path);
}

CommandResult RuntimeAPI::save_as(const std::string& path) {
    if (graph_.save(path.c_str())) {
        const std::string normalized = normalized_graph_identity_path(path);
        graph_.set_source_path(path);
        active_graph_source_path_ = normalized;
        capture_saved_snapshot();
        return {true, "saved to " + path};
    }
    return {false, "failed to save to " + path};
}

CommandResult RuntimeAPI::reload(bool& has_gpu_ops, bool& has_audio) {
    const auto& path = graph_.source_path();
    if (path.empty()) return {false, "no source path to reload from"};
    return load_graph(path, has_gpu_ops, has_audio);
}

CommandResult RuntimeAPI::load_graph(const std::string& path,
                                     bool& has_gpu_ops,
                                     bool& has_audio) {
    if (path.empty()) return {false, "missing graph path"};
    const PreservedRuntimeState preserved_state =
        capture_preserved_runtime_state_for_path(path);
    const bool preserve_runtime_state = preserved_state.active;
    std::string previous_graph_json;
    if (!graph_.save_to_string(previous_graph_json)) {
        return {false, "failed to serialize current graph before reload"};
    }
    const std::string previous_source_path = graph_.source_path();
    const std::string previous_active_graph_source_path = active_graph_source_path_;

    auto restore_previous_state = [&](const std::string& reason) -> CommandResult {
        if (!graph_.load_from_string(previous_graph_json.c_str(), previous_graph_json.size(), true)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to restore previous graph)"};
        }
        graph_.set_source_path(previous_source_path);

        if (!core_.build(graph_, registry_)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to rebuild previous graph)"};
        }

        has_gpu_ops = core_.has_gpu_operators();
        if (has_gpu_ops) needs_gpu_realloc_ = true;

        has_audio = false;
        if (core_.has_audio_operators()) {
            if (audio_engine_.build(core_)) {
                if (audio_engine_.start()) {
                    has_audio = true;
                }
            }
        }

        active_graph_source_path_ = previous_active_graph_source_path;
        pending_topology_change_ = false;
        active_crossfades_.clear();
        return {false, reason};
    };

    bool had_audio = has_audio;
    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }
    core_.shutdown();
    registry_.clear_retired_package_loaders();

    if (!graph_.load(path.c_str())) {
        return restore_previous_state("failed to reload " + path);
    }

    if (!core_.build(graph_, registry_)) {
        return restore_previous_state("rebuild failed after reload");
    }

    if (preserve_runtime_state) {
        apply_preserved_runtime_state(preserved_state);
    }

    has_gpu_ops = core_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (core_.has_audio_operators()) {
        if (audio_engine_.build(core_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    active_graph_source_path_ = normalized_graph_identity_path(path);
    preserve_undo_history_on_reload_ = false;
    reload_serial_++;
    capture_saved_snapshot();
    return {true, "reloaded from " + path};
}

CommandResult RuntimeAPI::new_graph(bool& has_gpu_ops, bool& has_audio) {
    if (has_audio) { audio_engine_.shutdown(); has_audio = false; }
    core_.shutdown();
    registry_.clear_retired_package_loaders();

    auto read_file = [](const std::string& path, std::string& out) -> bool {
        auto f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        auto sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.resize(sz);
        std::fread(out.data(), 1, sz, f);
        std::fclose(f);
        return true;
    };

    bool loaded = false;
    std::string buf;

    std::string user_path = get_config_dir() + "/default_graph.json";
    if (read_file(user_path, buf)) {
        loaded = graph_.load_from_string(buf.c_str(), buf.size(), false);
        if (!loaded)
            std::fprintf(stderr, "[vivid] Warning: custom default_graph.json is malformed, falling back to bundled default\n");
    }

    if (!loaded && !resources_dir_.empty()) {
        std::string bundled_path = resources_dir_ + "/default_graph.json";
        if (read_file(bundled_path, buf)) {
            loaded = graph_.load_from_string(buf.c_str(), buf.size(), false);
            if (!loaded)
                std::fprintf(stderr, "[vivid] Warning: bundled default_graph.json is malformed\n");
        } else {
            std::fprintf(stderr, "[vivid] Warning: bundled default_graph.json not found at %s\n", bundled_path.c_str());
        }
    }

    if (!loaded) {
        has_gpu_ops = false;
        return {false, "failed to load default graph template"};
    }
    if (!core_.build(graph_, registry_)) {
        has_gpu_ops = false;
        return {false, "runtime build failed for new graph"};
    }

    has_gpu_ops = core_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (core_.has_audio_operators()) {
        if (audio_engine_.build(core_) && audio_engine_.start())
            has_audio = true;
    }

    active_graph_source_path_.clear();
    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = false;
    reload_serial_++;
    capture_saved_snapshot();
    return {true, "new graph"};
}

CommandResult RuntimeAPI::new_project(const std::string& dir_path,
                                       bool& has_gpu_ops, bool& has_audio) {
    namespace fs = std::filesystem;
    std::error_code ec;

    if (fs::exists(dir_path, ec)) {
        if (!fs::is_directory(dir_path, ec))
            return {false, "path exists and is not a directory: " + dir_path};
        if (!fs::is_empty(dir_path, ec))
            return {false, "directory already exists and is non-empty: " + dir_path};
    } else {
        if (!fs::create_directories(dir_path, ec))
            return {false, "failed to create directory: " + dir_path + " (" + ec.message() + ")"};
    }

    std::string graph_path = (fs::path(dir_path) / "graph.json").string();

    auto result = new_graph(has_gpu_ops, has_audio);
    if (!result.ok) return result;

    auto save_result = save_as(graph_path);
    if (!save_result.ok) return save_result;
    return {true, "new project at " + dir_path};
}

CommandResult RuntimeAPI::apply_snapshot_json(const std::string& graph_json,
                                              bool& has_gpu_ops, bool& has_audio) {
    std::string previous_graph_json;
    if (!graph_.save_to_string(previous_graph_json)) {
        return {false, "failed to serialize current graph before applying snapshot"};
    }

    bool had_audio = has_audio;
    auto restore_previous_state = [&](const std::string& reason) -> CommandResult {
        if (!graph_.load_from_string(previous_graph_json.c_str(), previous_graph_json.size(), true)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to restore previous graph)"};
        }

        if (!core_.build(graph_, registry_)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to rebuild previous graph)"};
        }

        has_gpu_ops = core_.has_gpu_operators();
        if (has_gpu_ops) needs_gpu_realloc_ = true;

        has_audio = false;
        if (core_.has_audio_operators()) {
            if (audio_engine_.build(core_)) {
                if (audio_engine_.start()) {
                    has_audio = true;
                }
            }
        }

        pending_topology_change_ = false;
        active_crossfades_.clear();
        preserve_undo_history_on_reload_ = false;
        return {false, reason};
    };

    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }
    core_.shutdown();
    registry_.clear_retired_package_loaders();

    if (!graph_.load_from_string(graph_json.c_str(), graph_json.size(), true)) {
        return restore_previous_state("failed to load graph snapshot JSON");
    }

    if (!core_.build(graph_, registry_)) {
        return restore_previous_state("rebuild failed after snapshot load");
    }

    has_gpu_ops = core_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (core_.has_audio_operators()) {
        if (audio_engine_.build(core_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = true;
    reload_serial_++;
    refresh_graph_dirty_from_saved_snapshot();
    return {true, "applied graph snapshot"};
}

CommandResult RuntimeAPI::rebuild_current_graph(bool& has_gpu_ops, bool& has_audio) {
    const PreservedRuntimeState preserved_state =
        capture_preserved_runtime_state_for_path(graph_.source_path());
    const bool preserve_runtime_state = preserved_state.active;

    bool had_audio = has_audio;
    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }
    core_.shutdown();
    registry_.clear_retired_package_loaders();

    if (!core_.build(graph_, registry_)) {
        has_gpu_ops = false;
        has_audio = false;
        preserve_undo_history_on_reload_ = false;
        reload_serial_++;
        return {false, "rebuild failed"};
    }

    if (preserve_runtime_state) {
        apply_preserved_runtime_state(preserved_state);
    }

    has_gpu_ops = core_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (core_.has_audio_operators()) {
        if (audio_engine_.build(core_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = true;
    reload_serial_++;
    refresh_graph_dirty_from_saved_snapshot();
    return {true, "rebuilt graph"};
}

std::filesystem::path RuntimeAPI::graph_base_dir() const {
    const auto& sp = graph_.source_path();
    if (sp.empty()) return {};
    return std::filesystem::path(sp).parent_path();
}

bool RuntimeAPI::is_path_string_param(const CompiledNode& cn, const std::string& param) const {
    auto fi = cn.file_param_indices.find(param);
    if (fi == cn.file_param_indices.end()) return false;
    return fi->second < cn.file_param_is_path.size() && cn.file_param_is_path[fi->second] != 0;
}

std::string RuntimeAPI::to_runtime_string_value(const CompiledNode& cn, const std::string& param,
                                                const std::string& value) const {
    if (!is_path_string_param(cn, param)) return value;
    return resolve_file_path(value, graph_base_dir());
}

std::string RuntimeAPI::to_persisted_string_value(const CompiledNode& cn, const std::string& param,
                                                  const std::string& value) const {
    if (!is_path_string_param(cn, param)) return value;
    return make_relative_path(value, graph_base_dir());
}

void RuntimeAPI::set_file_param_internal(CompiledNode& cn, const std::string& param,
                                          const std::string& value) {
    auto fi = cn.file_param_indices.find(param);
    if (fi == cn.file_param_indices.end()) return;

    cn.file_param_storage[fi->second] = to_runtime_string_value(cn, param, value);
    cn.file_param_ptrs[fi->second] = cn.file_param_storage[fi->second].c_str();
    cn.dirty = true;

    NodeDef* ndef = graph_.find_node(cn.node_id);
    if (ndef) ndef->string_params[param] = to_persisted_string_value(cn, param, value);
}

void RuntimeAPI::mark_graph_dirty() {
    graph_dirty_ = true;
}

void RuntimeAPI::notify_external_graph_mutation() {
    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = true;
    reload_serial_++;
    needs_gpu_realloc_ = false;
    refresh_graph_dirty_from_saved_snapshot();
}

void RuntimeAPI::finalize_external_graph_load() {
    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = false;
    reload_serial_++;
    needs_gpu_realloc_ = false;
    active_graph_source_path_ = normalized_graph_identity_path(graph_.source_path());
    capture_saved_snapshot();
}

RuntimeAPI::PreservedRuntimeState
RuntimeAPI::capture_preserved_runtime_state_for_path(const std::string& path) const {
    PreservedRuntimeState state;
    if (normalized_graph_identity_path(path) != active_graph_source_path_) return state;
    if (!core_.compiled_graph()) return state;

    state.active = true;
    for (const auto& cn : core_.compiled_graph()->nodes) {
        auto& saved_params = state.params[cn.node_id];
        for (const auto& [name, idx] : cn.param_indices) {
            saved_params[name] = cn.param_values[idx];
            if (cn.param_lock_flags[idx] != PARAM_LOCK_NONE) {
                state.lock_flags[cn.node_id][name] = cn.param_lock_flags[idx];
            }
        }
        auto& saved_strings = state.string_params[cn.node_id];
        for (const auto& [name, idx] : cn.file_param_indices) {
            saved_strings[name] = cn.file_param_storage[idx];
        }
    }
    return state;
}

void RuntimeAPI::apply_preserved_runtime_state(const PreservedRuntimeState& state) {
    if (!state.active || !core_.compiled_graph()) return;
    for (auto& cn : core_.compiled_graph()->nodes) {
        auto sit = state.params.find(cn.node_id);
        if (sit != state.params.end()) {
            for (const auto& [pname, pval] : sit->second) {
                auto pi = cn.param_indices.find(pname);
                if (pi != cn.param_indices.end())
                    cn.param_values[pi->second] = pval;
            }
        }

        auto ssit = state.string_params.find(cn.node_id);
        if (ssit != state.string_params.end()) {
            for (const auto& [pname, pval] : ssit->second) {
                auto fi = cn.file_param_indices.find(pname);
                if (fi != cn.file_param_indices.end()) {
                    cn.file_param_storage[fi->second] = pval;
                    cn.file_param_ptrs[fi->second] = cn.file_param_storage[fi->second].c_str();
                }
            }
        }

        auto lit = state.lock_flags.find(cn.node_id);
        if (lit != state.lock_flags.end()) {
            for (const auto& [pname, flags] : lit->second) {
                auto pi = cn.param_indices.find(pname);
                if (pi != cn.param_indices.end())
                    cn.param_lock_flags[pi->second] = flags;
            }
        }
    }
}

void RuntimeAPI::capture_saved_snapshot() {
    std::string current;
    if (graph_.save_to_string(current)) {
        last_saved_graph_json_ = std::move(current);
        graph_dirty_ = false;
        return;
    }
    last_saved_graph_json_.clear();
    graph_dirty_ = false;
}

void RuntimeAPI::refresh_graph_dirty_from_saved_snapshot() {
    if (last_saved_graph_json_.empty()) {
        graph_dirty_ = true;
        return;
    }
    std::string current;
    if (!graph_.save_to_string(current)) {
        graph_dirty_ = true;
        return;
    }
    graph_dirty_ = (current != last_saved_graph_json_);
}

} // namespace vivid
