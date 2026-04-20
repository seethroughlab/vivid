#include "runtime/control/runtime_command_sink.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_creator.h"
#include "runtime/core/hot_reload.h"
#include "runtime/gpu/wgsl_header_parser.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_info_cache.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/core/settings.h"
#include "runtime/core/editor_detect.h"
#include "runtime/packages/package_manager.h"
#include "runtime/core/build_console.h"
#include "runtime/operators/operator_destination_policy.h"
#include "runtime/operators/project_package.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <array>

void RuntimeCommandSink::open_shader(const std::string& type_name) {
    // User C++ operator → open its source .cpp
    if (registry_ && registry_->is_user_operator(type_name)) {
        auto* src = registry_->user_operator_source(type_name);
        if (src) {
            vivid::open_in_editor(*src, settings_ ? *settings_ : vivid::Settings{});
        }
        return;
    }
    if (registry_) {
        auto* shader_src = registry_->shader_operator_source(type_name);
        if (shader_src && std::filesystem::exists(*shader_src)) {
            vivid::open_in_editor(*shader_src, settings_ ? *settings_ : vivid::Settings{});
            return;
        }
    }
    // Non-filter operator → open .cpp if it exists
    if (!operators_dir_.empty()) {
        std::string name = type_name;
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        std::string cpp_path = operators_dir_ + "/gpu/" + name + "/" + name + ".cpp";
        if (std::filesystem::exists(cpp_path)) {
            vivid::open_in_editor(cpp_path, settings_ ? *settings_ : vivid::Settings{});
        }
    }
}

void RuntimeCommandSink::open_module_source(const std::string& type_name) {
    if (!subgraph_modules_) return;
    const auto* mod = subgraph_modules_->find(type_name);
    if (mod && !mod->source_path.empty() && std::filesystem::exists(mod->source_path)) {
        vivid::open_in_editor(mod->source_path, settings_ ? *settings_ : vivid::Settings{});
    }
}

std::string RuntimeCommandSink::project_shader_dir() const {
    if (!graph_ || graph_->source_path().empty()) return {};
    return (std::filesystem::path(graph_->source_path()).parent_path() / "filters").string();
}

std::string RuntimeCommandSink::make_unique_shader_operator_name(const std::string& base_name) const {
    std::string unique_name = base_name;
    for (int n = 2;
         (registry_ && (registry_->probe_descriptor(unique_name) || registry_->find_loaded(unique_name))) ||
             (graph_ && graph_->find_node(unique_name));
         ++n) {
        unique_name = base_name + std::to_string(n);
    }
    return unique_name;
}

bool RuntimeCommandSink::clone_shader_operator(const std::string& type_name,
                                               const std::string& node_id,
                                               std::string* error) {
    if (!registry_ || !graph_ || !op_cache_) {
        if (error) *error = "shader operator clone unavailable";
        return false;
    }
    if (graph_->source_path().empty()) {
        if (error) *error = "save the graph before cloning a shader operator";
        return false;
    }

    const std::string* shader_src = registry_->shader_operator_source(type_name);
    if (!shader_src || shader_src->empty()) {
        if (error) *error = "unknown shader operator '" + type_name + "'";
        return false;
    }

    std::ifstream ifs(*shader_src);
    if (!ifs) {
        if (error) *error = "cannot read shader source '" + *shader_src + "'";
        return false;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string full_source = ss.str();

    std::string parse_error;
    auto header = vivid::parse_wgsl_header(full_source, parse_error);
    if (!header) {
        if (error) *error = "cannot parse shader header for '" + type_name + "': " + parse_error;
        return false;
    }

    const std::string unique_name = make_unique_shader_operator_name(type_name + "_copy");
    std::string new_source = full_source;
    size_t name_pos = new_source.find("\"name\"");
    if (name_pos == std::string::npos) {
        if (error) *error = "shader header for '" + type_name + "' is missing a name field";
        return false;
    }
    size_t colon = new_source.find(':', name_pos);
    size_t quote1 = new_source.find('"', colon + 1);
    size_t quote2 = new_source.find('"', quote1 + 1);
    if (colon == std::string::npos || quote1 == std::string::npos || quote2 == std::string::npos) {
        if (error) *error = "shader header for '" + type_name + "' has an invalid name field";
        return false;
    }
    new_source.replace(quote1 + 1, quote2 - quote1 - 1, unique_name);

    const std::string shader_dir = project_shader_dir();
    std::error_code ec;
    std::filesystem::create_directories(shader_dir, ec);
    if (ec) {
        if (error) *error = "failed to create project shader directory: " + ec.message();
        return false;
    }

    const std::string output_path =
        (std::filesystem::path(shader_dir) / (unique_name + ".wgsl")).string();
    std::ofstream ofs(output_path);
    if (!ofs) {
        if (error) *error = "failed to write shader clone '" + output_path + "'";
        return false;
    }
    ofs << new_source;
    ofs.close();

    if (!registry_->scan_shader_operators(shader_dir, true)) {
        if (error) *error = "failed to register cloned shader operator '" + unique_name + "'";
        return false;
    }

    if (shader_watch_callback_)
        shader_watch_callback_(output_path);

    if (!node_id.empty()) {
        if (!has_gpu_ops_ || !has_audio_) {
            if (error) *error = "runtime flags unavailable for shader retarget";
            return false;
        }

        std::string snapshot_json;
        if (!graph_->save_to_string(snapshot_json)) {
            if (error) *error = "failed to serialize graph before retargeting shader clone";
            return false;
        }

        nlohmann::json root;
        try {
            root = nlohmann::json::parse(snapshot_json);
        } catch (const std::exception& e) {
            if (error) *error = std::string("failed to parse graph snapshot JSON: ") + e.what();
            return false;
        }

        auto nodes_it = root.find("nodes");
        if (nodes_it == root.end() || !nodes_it->is_object() || !nodes_it->contains(node_id)) {
            if (error) *error = "node '" + node_id + "' not found while retargeting shader clone";
            return false;
        }
        (*nodes_it)[node_id]["type"] = unique_name;

        vivid::CommandResult apply_result =
            api_.apply_snapshot_json(root.dump(), *has_gpu_ops_, *has_audio_);
        if (!apply_result.ok) {
            if (error) {
                *error = "failed to retarget node '" + node_id + "' to shader operator '" +
                         unique_name + "': " + apply_result.message;
            }
            return false;
        }
        capture_undo_snapshot();
    }

    op_cache_->invalidate_all();
    vivid::open_in_editor(output_path, settings_ ? *settings_ : vivid::Settings{});
    std::fprintf(stderr, "[vivid] Cloned shader operator '%s' to '%s'\n",
                 type_name.c_str(), unique_name.c_str());
    if (error) error->clear();
    return true;
}

void RuntimeCommandSink::clone_and_edit(const std::string& type_name,
                                        const std::string& custom_name,
                                        const std::string& node_id) {
    if (!registry_ || !op_cache_) return;

    if (registry_->is_shader_operator(type_name)) {
        std::string error;
        if (!clone_shader_operator(type_name, node_id, &error) && !error.empty())
            std::fprintf(stderr, "[vivid] %s\n", error.c_str());
        return;
    }

    auto* loader = registry_->find(type_name);
    if (!loader || operators_dir_.empty()) return;
    clone_cpp_operator(type_name, custom_name, node_id);
}

bool RuntimeCommandSink::has_project_clone_destination() {
    const std::string core_src_dir = operators_dir_.empty()
        ? std::string{}
        : std::filesystem::path(operators_dir_).parent_path().string();
    const std::vector<vivid::PackageInfo> packages = package_manager_
        ? package_manager_->list()
        : std::vector<vivid::PackageInfo>{};
    vivid::OperatorDestination dest;
    std::string err;
    if (!vivid::resolve_operator_destination("project", core_src_dir, packages, settings_, dest, err))
        return false;
    return !dest.used_core_fallback;
}

bool RuntimeCommandSink::create_operator(const VividCreateOperatorRequest& request,
                     std::string* error) {
    if (operators_dir_.empty() || !registry_) {
        if (error) *error = "operator creation not available";
        return false;
    }

    const std::string core_src_dir = std::filesystem::path(operators_dir_).parent_path().string();
    const std::vector<vivid::PackageInfo> packages = package_manager_
        ? package_manager_->list()
        : std::vector<vivid::PackageInfo>{};

    std::string dest_str = request.destination.empty() ? "auto" : request.destination;
    vivid::OperatorDestination dest;
    std::string resolve_error;
    if (!vivid::resolve_operator_destination(dest_str, core_src_dir, packages, settings_,
                                             dest, resolve_error)) {
        std::fprintf(stderr, "[vivid] Create operator destination error: %s\n",
                     resolve_error.c_str());
        if (error) *error = resolve_error;
        return false;
    }
    if (!dest.warning.empty()) {
        std::fprintf(stderr, "[vivid] %s\n", dest.warning.c_str());
    }
    if (dest.package_layout && dest.package_name.empty()) {
        std::fprintf(stderr,
                     "[vivid] Project destination '%s' is not an active package; using core destination\n",
                     dest.root.c_str());
        dest = {};
        dest.root = core_src_dir;
    }

    auto cr = vivid::OperatorCreator::create(request, dest.root, dest.package_layout);
    if (!cr.success) {
        if (error) *error = cr.error;
        return false;
    }

    if (hot_reloader_) {
        if (dest.package_layout && !dest.package_name.empty()) {
            hot_reloader_->queue_rebuild("pkg:" + dest.package_name + ":" + cr.target_name);
        } else {
            hot_reloader_->queue_rebuild(cr.target_name);
        }
    }

    vivid::OperatorCreator::open_in_editor(cr.cpp_path);
    return true;
}

void RuntimeCommandSink::start_recording(const std::string& path, const std::string& codec, double fps) {
    if (!capture_coordinator_) return;
    std::string out_path = path;
    if (out_path.empty()) {
        const char* home = std::getenv("HOME");
        std::string desktop = home ? std::string(home) + "/Desktop" : ".";
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_r(&t, &tm);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);
        out_path = desktop + "/vivid_recording_" + ts + ".mov";
    }
    // TODO: pass codec selection to AVExporter when multiple codecs are supported
    (void)codec;
    capture_coordinator_->request_start_recording(out_path, fps);
}

bool RuntimeCommandSink::undo() {
    std::string snapshot_json;
    if (!undo_manager_.undo(snapshot_json)) return false;
    if (!has_gpu_ops_ || !has_audio_) return false;
    auto r = api_.apply_snapshot_json(snapshot_json, *has_gpu_ops_, *has_audio_);
    if (r.ok) {
        last_coalesce_key_.clear();
        return true;
    }
    // Restore undo cursor when apply fails, then reset history to current state.
    std::string ignored;
    (void)undo_manager_.redo(ignored);
    std::fprintf(stderr, "[vivid] Undo: snapshot apply failed (%s); resetting undo history\n",
                 r.message.c_str());
    reset_undo_history();
    return false;
}

bool RuntimeCommandSink::redo() {
    std::string snapshot_json;
    if (!undo_manager_.redo(snapshot_json)) return false;
    if (!has_gpu_ops_ || !has_audio_) return false;
    auto r = api_.apply_snapshot_json(snapshot_json, *has_gpu_ops_, *has_audio_);
    if (r.ok) {
        last_coalesce_key_.clear();
        return true;
    }
    // Restore redo cursor when apply fails, then reset history to current state.
    std::string ignored;
    (void)undo_manager_.undo(ignored);
    std::fprintf(stderr, "[vivid] Redo: snapshot apply failed (%s); resetting undo history\n",
                 r.message.c_str());
    reset_undo_history();
    return false;
}

void RuntimeCommandSink::capture_undo_snapshot(const std::string& coalesce_key) {
    if (!graph_) return;
    auto now = std::chrono::steady_clock::now();
    bool replace_top = false;
    if (!coalesce_key.empty() && !last_coalesce_key_.empty() && coalesce_key == last_coalesce_key_) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_coalesce_time_).count();
        replace_top = (ms <= 300);
    }
    std::string json;
    if (!graph_->save_to_string(json)) return;
    undo_manager_.push(std::move(json), replace_top);
    if (coalesce_key.empty()) {
        last_coalesce_key_.clear();
    } else {
        last_coalesce_key_ = coalesce_key;
        last_coalesce_time_ = now;
    }
}

bool RuntimeCommandSink::patch_package_cmake_ops(const std::string& pkg_dir, const std::string& op_name) {
    std::string cmake_path = (std::filesystem::path(pkg_dir) / "CMakeLists.txt").string();
    std::ifstream ifs(cmake_path);
    if (!ifs) return false;
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    if (content.find("\n  " + op_name + "\n") != std::string::npos ||
        content.find("\n  " + op_name + ")") != std::string::npos ||
        content.find(" " + op_name + "\n") != std::string::npos) {
        return true;
    }

    size_t scan = 0;
    while (true) {
        size_t set_pos = content.find("set(", scan);
        if (set_pos == std::string::npos) break;
        size_t close = content.find(')', set_pos);
        if (close == std::string::npos) break;
        std::string block = content.substr(set_pos, close - set_pos + 1);
        if (block.find("_OPS") != std::string::npos) {
            content.insert(close, "\n  " + op_name);
            std::ofstream ofs(cmake_path);
            if (!ofs) return false;
            ofs << content;
            return true;
        }
        scan = close + 1;
    }
    return false;
}

std::pair<std::string, std::string> RuntimeCommandSink::ensure_project_package() {
    if (!package_manager_) {
        std::fprintf(stderr, "[vivid] Clone: no package manager available\n");
        return {};
    }
    if (!graph_) {
        std::fprintf(stderr, "[vivid] Clone: no graph available\n");
        return {};
    }
    return vivid::ensure_project_package(*package_manager_, *graph_);
}

void RuntimeCommandSink::swap_node_type(const std::string& old_id, const std::string& new_id,
                                         const std::string& new_type) {
    if (!graph_) return;

    // Capture connections involving this node, remapping to new ID
    std::vector<vivid::ConnectionDef> saved_connections;
    for (const auto& conn : graph_->connections()) {
        if (conn.from_node == old_id || conn.to_node == old_id) {
            vivid::ConnectionDef remapped = conn;
            if (remapped.from_node == old_id) remapped.from_node = new_id;
            if (remapped.to_node == old_id) remapped.to_node = new_id;
            saved_connections.push_back(remapped);
        }
    }

    // Capture layout position
    float nx = NAN, ny = NAN;
    if (auto* node_def = graph_->find_node(old_id)) {
        nx = node_def->layout_x;
        ny = node_def->layout_y;
    }

    // Remove old node (auto-removes connections, sets pending_topology_change)
    api_.remove_node(old_id);

    // Add new node directly to graph (bypasses registry check — type may not be loaded yet)
    graph_->add_node(new_id, new_type);

    // Restore layout position
    if (auto* node_def = graph_->find_node(new_id)) {
        node_def->layout_x = nx;
        node_def->layout_y = ny;
    }

    // Reconnect with remapped IDs
    for (const auto& conn : saved_connections)
        graph_->add_connection(conn.from_node, conn.from_port, conn.to_node, conn.to_port);

    std::fprintf(stderr, "[vivid] Swapped node '%s' → '%s' (type '%s')\n",
                 old_id.c_str(), new_id.c_str(), new_type.c_str());
}

void RuntimeCommandSink::clone_cpp_operator(const std::string& type_name,
                                             const std::string& custom_name,
                                             const std::string& node_id) {
    auto* loader = registry_->find(type_name);
    if (!loader) return;
    const auto* desc = loader->descriptor();
    if (!desc) return;

    // Map operator kind → source subdirectory
    const char* kind_dir = "control";
    switch (vivid_operator_kind(desc)) {
        case VIVID_OP_AUDIO: kind_dir = "audio"; break;
        case VIVID_OP_GPU:   kind_dir = "gpu"; break;
        default: break;
    }

    // Resolve source path: user-registered explicit path, then dylib-stem
    // convention, then lowercase(type_name) convention. Dylib stem and source
    // dir don't always match (e.g. dual-cadence ops build lfo_au.dylib from
    // operators/control/lfo/).
    std::string stem;
    std::string src_dir;
    std::string cpp_path;
    auto try_candidate = [&](const std::string& candidate_stem,
                             const std::string& candidate_dir) -> bool {
        std::string candidate_cpp = candidate_dir + "/" + candidate_stem + ".cpp";
        if (!std::filesystem::exists(candidate_cpp)) return false;
        stem = candidate_stem;
        src_dir = candidate_dir;
        cpp_path = std::move(candidate_cpp);
        return true;
    };

    if (const std::string* user_src = registry_->user_operator_source(type_name)) {
        std::filesystem::path p(*user_src);
        try_candidate(p.stem().string(), p.parent_path().string());
    }
    if (cpp_path.empty()) {
        std::string target = registry_->type_to_target(type_name);
        if (!target.empty())
            try_candidate(target, operators_dir_ + "/" + kind_dir + "/" + target);
    }
    if (cpp_path.empty()) {
        std::string lower = type_name;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (!lower.empty())
            try_candidate(lower, operators_dir_ + "/" + kind_dir + "/" + lower);
    }
    if (cpp_path.empty()) {
        std::fprintf(stderr, "[vivid] Clone: cannot locate source for type '%s' under %s/%s\n",
                     type_name.c_str(), operators_dir_.c_str(), kind_dir);
        return;
    }

    std::string cpp_source;
    {
        std::ifstream ifs(cpp_path);
        if (!ifs) {
            std::fprintf(stderr, "[vivid] Clone: cannot read source %s\n", cpp_path.c_str());
            return;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        cpp_source = ss.str();
    }

    // Generate unique operator type name
    std::string new_type = custom_name.empty() ? (type_name + "_copy") : custom_name;
    if (registry_->find(new_type)) {
        std::string base = new_type;
        for (int n = 2; registry_->find(new_type); ++n)
            new_type = base + std::to_string(n);
    }
    std::string new_stem = new_type;
    for (auto& c : new_stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Validate stem contains only safe characters
    for (char c : new_stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            std::fprintf(stderr, "[vivid] Clone: invalid character '%c' in operator name\n", c);
            return;
        }
    }

    // Resolve destination package (scaffold + link if needed)
    auto [pkg_root, pkg_name] = ensure_project_package();
    if (pkg_root.empty()) return;

    std::string new_dir = (std::filesystem::path(pkg_root) / "src").string();
    std::filesystem::create_directories(new_dir);

    // Transform source: replace type name and .wgsl references
    std::string transformed = cpp_source;
    {
        size_t pos = 0;
        while ((pos = transformed.find(type_name, pos)) != std::string::npos) {
            transformed.replace(pos, type_name.size(), new_type);
            pos += new_type.size();
        }
    }
    {
        std::string old_wgsl = stem + ".wgsl", new_wgsl = new_stem + ".wgsl";
        size_t pos = 0;
        while ((pos = transformed.find(old_wgsl, pos)) != std::string::npos) {
            transformed.replace(pos, old_wgsl.size(), new_wgsl);
            pos += new_wgsl.size();
        }
    }

    // Copy .wgsl shader if present
    std::string wgsl_path = src_dir + "/" + stem + ".wgsl";
    if (std::filesystem::exists(wgsl_path))
        std::filesystem::copy_file(wgsl_path, new_dir + "/" + new_stem + ".wgsl",
                                   std::filesystem::copy_options::overwrite_existing);

    // Write transformed .cpp
    std::string new_cpp = new_dir + "/" + new_stem + ".cpp";
    {
        std::ofstream ofs(new_cpp);
        if (!ofs) {
            std::fprintf(stderr, "[vivid] Clone: cannot write %s\n", new_cpp.c_str());
            return;
        }
        ofs << transformed;
    }

    // Register the new operator target in the package CMakeLists.txt
    if (!patch_package_cmake_ops(pkg_root, new_stem)) {
        std::string cmake_path = (std::filesystem::path(pkg_root) / "CMakeLists.txt").string();
        std::ofstream ofs(cmake_path, std::ios::app);
        if (!ofs) {
            std::fprintf(stderr, "[vivid] Clone: cannot update CMakeLists.txt in %s\n", pkg_root.c_str());
            return;
        }
        ofs << "\nadd_vivid_pkg_operator(" << new_stem << " src/" << new_stem << ".cpp)\n";
    }

    // Register and queue build
    registry_->register_user_operator(new_type, new_cpp);
    op_cache_->invalidate_all();
    if (hot_reloader_)
        hot_reloader_->queue_rebuild("pkg:" + pkg_name + ":" + new_stem);

    // Replace the originating node in the graph (placeholder until build completes)
    if (!node_id.empty()) {
        std::string new_node_id = new_stem;
        if (graph_ && graph_->find_node(new_node_id)) {
            for (int n = 2; ; ++n) {
                std::string candidate = new_stem + std::to_string(n);
                if (!graph_->find_node(candidate)) { new_node_id = candidate; break; }
            }
        }
        swap_node_type(node_id, new_node_id, new_type);
    }

    vivid::open_in_editor(new_cpp, settings_ ? *settings_ : vivid::Settings{});
    std::fprintf(stderr, "[vivid] Cloned '%s' as '%s' in package '%s'\n",
                 type_name.c_str(), new_type.c_str(), pkg_name.c_str());
}

void RuntimeCommandSink::set_editor_preference(const std::string& editor_id,
                                               const std::string& custom_command) {
    if (!settings_) return;
    settings_->editor = editor_id;
    settings_->editor_command = custom_command;
    vivid::save_settings(*settings_);
}

void RuntimeCommandSink::set_style_preference(const std::string& style_id) {
    if (!settings_) return;
    settings_->style_id = style_id;
    vivid::save_settings(*settings_);
}

void RuntimeCommandSink::set_pan_gesture_preference(const std::string& gesture) {
    if (!settings_) return;
    settings_->pan_gesture = gesture;
    vivid::save_settings(*settings_);
}

bool RuntimeCommandSink::try_set_audio_buffer_preference(uint32_t buffer_size,
                                                         std::string* error) {
    if (!settings_) {
        if (error) *error = "settings unavailable";
        return false;
    }

    const uint32_t sanitized = vivid::sanitize_audio_buffer_size(buffer_size);
    if (sanitized != buffer_size) {
        if (error) *error = "unsupported buffer size";
        return false;
    }

    const uint32_t previous = vivid::sanitize_audio_buffer_size(settings_->audio_buffer_size);
    if (sanitized == previous) {
        if (error) error->clear();
        return true;
    }

    if (audio_buffer_preference_callback_) {
        std::string callback_error;
        if (!audio_buffer_preference_callback_(previous, sanitized, callback_error)) {
            if (error) *error = callback_error.empty()
                ? "failed to apply audio buffer size"
                : callback_error;
            return false;
        }
    }

    settings_->audio_buffer_size = sanitized;
    vivid::save_settings(*settings_);
    if (error) error->clear();
    return true;
}

std::string RuntimeCommandSink::validate_operator_name(const std::string& name) {
    if (!registry_) return "registry not available";
    return vivid::OperatorCreator::validate_name(name, *registry_);
}

void RuntimeCommandSink::add_sticky_note(const std::string& id, const std::string& text,
                                         float x, float y, float w, float h, int color) {
    if (!graph_) return;
    vivid::StickyNoteDef note;
    note.id = id; note.text = text;
    note.x = x; note.y = y; note.width = w; note.height = h; note.color = color;
    graph_->add_sticky_note(std::move(note));
    capture_undo_snapshot();
}

void RuntimeCommandSink::remove_sticky_note(const std::string& id) {
    if (!graph_) return;
    if (graph_->remove_sticky_note(id))
        capture_undo_snapshot();
}

void RuntimeCommandSink::update_sticky_note(const std::string& id, const std::string& text,
                                            float x, float y, float w, float h, int color) {
    if (!graph_) return;
    auto* sn = graph_->find_sticky_note(id);
    if (!sn) return;
    sn->text = text; sn->x = x; sn->y = y;
    sn->width = w; sn->height = h; sn->color = color;
    capture_undo_snapshot("sticky:" + id);
}

void RuntimeCommandSink::capture_snapshot() {
    if (!capture_coordinator_) return;
    capture_coordinator_->request_snapshot_to_file("");
}

void RuntimeCommandSink::stop_recording() {
    if (!capture_coordinator_) return;
    capture_coordinator_->request_stop_recording();
}
