#include "runtime/control/runtime_command_sink.h"

void RuntimeCommandSink::open_shader(const std::string& type_name) {
    // User C++ operator → open its source .cpp
    if (registry_ && registry_->is_user_operator(type_name)) {
        auto* src = registry_->user_operator_source(type_name);
        if (src) {
            vivid::open_in_editor(*src, settings_ ? *settings_ : vivid::Settings{});
        }
        return;
    }
    // User filter → open its working .wgsl file
    if (registry_ && registry_->is_user_filter(type_name)) {
        std::string path = working_filters_dir_ + "/" + type_name + ".wgsl";
        if (std::filesystem::exists(path)) {
            vivid::open_in_editor(path, settings_ ? *settings_ : vivid::Settings{});
            return;
        }
    }
    // Built-in WGSL preset → tell user to use Clone & Edit
    if (registry_ && registry_->is_wgsl_preset(type_name)) {
        std::fprintf(stderr, "[vivid] Built-in filter '%s': use Clone & Edit to modify\n",
                     type_name.c_str());
        return;
    }
    if (!filters_dir_.empty()) {
        auto preset_path = find_preset_wgsl(type_name);
        if (!preset_path.empty()) {
            std::fprintf(stderr, "[vivid] Built-in filter '%s': use Clone & Edit to modify\n",
                         type_name.c_str());
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

void RuntimeCommandSink::duplicate_as_user_filter(const std::string& type_name) {
    if (!registry_ || !graph_ || !op_cache_) return;

    // Look up source's descriptor for params
    auto* loader = registry_->find(type_name);
    const VividOperatorDescriptor* desc = nullptr;
    std::unique_ptr<vivid::OperatorLoader> temp_loader;

    if (loader) {
        desc = loader->descriptor();
    } else if (auto* cfg = registry_->wgsl_config(type_name)) {
        // WGSL preset not in loaders_ — create temporary loader for descriptor
        temp_loader = std::make_unique<vivid::OperatorLoader>();
        temp_loader->init_data_driven(*cfg);
        loader = temp_loader.get();
        desc = loader->descriptor();
    }
    if (!desc) return;

    // Read the source .wgsl file — try config path, then filters/ scan, then working dir
    std::string wgsl_path;
    if (auto* cfg = registry_->wgsl_config(type_name))
        wgsl_path = (*cfg)->shader_path;
    if (wgsl_path.empty())
        wgsl_path = find_preset_wgsl(type_name);
    if (wgsl_path.empty() && !working_filters_dir_.empty()) {
        std::string try_path = working_filters_dir_ + "/" + type_name + ".wgsl";
        if (std::filesystem::exists(try_path))
            wgsl_path = try_path;
    }
    if (wgsl_path.empty()) {
        std::fprintf(stderr, "[vivid] Cannot find .wgsl source for '%s'\n", type_name.c_str());
        return;
    }

    std::string full_source;
    {
        std::ifstream ifs(wgsl_path);
        if (!ifs) {
            std::fprintf(stderr, "[vivid] Cannot read shader: %s\n", wgsl_path.c_str());
            return;
        }
        std::ostringstream ss;
        ss << ifs.rdbuf();
        full_source = ss.str();
    }

    // Parse the source to get its header (for preserving metadata in the copy)
    std::string parse_error;
    auto header = vivid::parse_wgsl_header(full_source, parse_error);
    // If parsing fails, use full_source as fragment and build from descriptor
    std::string fragment_source = header ? header->fragment_source : full_source;

    // Generate unique name
    std::string base_name = type_name + "_copy";
    std::string unique_name = base_name;
    for (int n = 2; graph_->find_filter(unique_name) || registry_->find(unique_name); ++n) {
        unique_name = base_name + std::to_string(n);
    }

    // Build a self-describing .wgsl file with JSON header for the copy
    // (so the copy is also a self-describing preset)
    std::string new_source = full_source;
    if (header) {
        // Replace the name in the JSON header
        // Simple approach: rebuild from parsed header with new name
        size_t name_pos = new_source.find("\"name\"");
        if (name_pos != std::string::npos) {
            size_t colon = new_source.find(':', name_pos);
            size_t quote1 = new_source.find('"', colon + 1);
            size_t quote2 = new_source.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                new_source.replace(quote1 + 1, quote2 - quote1 - 1, unique_name);
            }
        }
    }

    // Create FilterDef for graph persistence
    vivid::FilterDef fd;
    fd.name = unique_name;
    fd.source = type_name;
    fd.time_dependent = desc->time_dependent != 0;
    fd.shader = fragment_source;
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        vivid::FilterDef::ParamDef pd;
        pd.name = desc->params[i].name;
        pd.default_value = desc->params[i].default_value;
        pd.min_value = desc->params[i].min_value;
        pd.max_value = desc->params[i].max_value;
        fd.params.push_back(std::move(pd));
    }
    graph_->add_filter(fd);
    capture_undo_snapshot();

    // Write self-describing .wgsl to working file
    if (!working_filters_dir_.empty()) {
        std::filesystem::create_directories(working_filters_dir_);
        std::string working_path = working_filters_dir_ + "/" + unique_name + ".wgsl";
        {
            std::ofstream ofs(working_path);
            ofs << new_source;
        }

        // Build config and register
        auto config = std::make_shared<vivid::DataDrivenFilterConfig>();
        config->name = unique_name;
        config->shader_path = working_path;
        config->source_builtin = type_name;
        config->time_dependent = fd.time_dependent;

        // If we parsed a header, use its rich metadata
        if (header) {
            config->inputs_specified = header->inputs_specified;
            for (const auto& inp : header->inputs)
                config->inputs.push_back({inp.name});
            for (const auto& hp : header->params) {
                vivid::DataDrivenFilterConfig::ParamDef cpd;
                cpd.name = hp.name;
                cpd.type = hp.type;
                cpd.default_value = hp.default_value;
                cpd.min_value = hp.min_value;
                cpd.max_value = hp.max_value;
                cpd.label = hp.label;
                cpd.choices = hp.choices;
                cpd.display_hint = hp.display_hint;
                cpd.group = hp.group;
                cpd.layout_columns = hp.layout_columns;
                cpd.layout_column_index = hp.layout_column_index;
                config->params.push_back(std::move(cpd));
            }
        } else {
            for (const auto& pd : fd.params) {
                vivid::DataDrivenFilterConfig::ParamDef cpd;
                cpd.name = pd.name;
                cpd.default_value = pd.default_value;
                cpd.min_value = pd.min_value;
                cpd.max_value = pd.max_value;
                config->params.push_back(std::move(cpd));
            }
        }
        registry_->register_user_filter(unique_name, config);
        op_cache_->invalidate_all();

        // Open in external editor
        vivid::open_in_editor(working_path, settings_ ? *settings_ : vivid::Settings{});
    }

    std::fprintf(stderr, "[vivid] Duplicated '%s' as user filter '%s'\n",
                 type_name.c_str(), unique_name.c_str());
}

void RuntimeCommandSink::clone_and_edit(const std::string& type_name, const std::string& destination) {
    if (!registry_ || !op_cache_) return;

    // WGSL presets (no longer in loaders_) → duplicate as user filter
    if (registry_->is_wgsl_preset(type_name)) {
        duplicate_as_user_filter(type_name);
        return;
    }

    auto* loader = registry_->find(type_name);

    // Data-driven filters (user filters) → duplicate as user filter
    if (loader && loader->is_data_driven()) {
        duplicate_as_user_filter(type_name);
        return;
    }

    // Check if there's a WGSL preset in filters/ for this type
    if (!find_preset_wgsl(type_name).empty()) {
        duplicate_as_user_filter(type_name);
        return;
    }

    if (!loader || operators_dir_.empty()) return;
    clone_cpp_operator(type_name, destination);
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

std::string RuntimeCommandSink::find_preset_wgsl(const std::string& type_name) {
    if (filters_dir_.empty()) return {};
    // Scan the filters directory for a file whose parsed name matches
    for (auto& entry : std::filesystem::directory_iterator(filters_dir_)) {
        if (entry.path().extension() != ".wgsl") continue;
        // Quick check: read and parse the header
        std::ifstream ifs(entry.path());
        if (!ifs) continue;
        std::ostringstream ss;
        ss << ifs.rdbuf();
        std::string error;
        auto header = vivid::parse_wgsl_header(ss.str(), error);
        if (header && header->name == type_name)
            return entry.path().string();
    }
    return {};
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

void RuntimeCommandSink::clone_cpp_operator(const std::string& type_name, const std::string& destination) {
    if (build_dir_.empty()) {
        std::fprintf(stderr, "[vivid] Clone: no build directory configured\n");
        return;
    }

    auto* loader = registry_->find(type_name);
    if (!loader) return;
    const auto* desc = loader->descriptor();
    if (!desc) return;

    // Map operator kind → subdirectory
    const char* kind_dir = "control";
    switch (vivid_operator_kind(desc)) {
        case VIVID_OP_AUDIO: kind_dir = "audio"; break;
        case VIVID_OP_GPU:   kind_dir = "gpu"; break;
        default: break;
    }

    // Derive stem (lowercase type name)
    std::string stem = type_name;
    for (auto& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Read source .cpp
    std::string src_dir = operators_dir_ + "/" + kind_dir + "/" + stem;
    std::string cpp_path = src_dir + "/" + stem + ".cpp";
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

    // Generate unique name
    std::string base_name = type_name + "_copy";
    std::string new_type = base_name;
    for (int n = 2; registry_->find(new_type); ++n) {
        new_type = base_name + std::to_string(n);
    }
    std::string new_stem = new_type;
    for (auto& c : new_stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    const std::string core_src_dir = std::filesystem::path(operators_dir_).parent_path().string();
    const std::vector<vivid::PackageInfo> packages = package_manager_
        ? package_manager_->list()
        : std::vector<vivid::PackageInfo>{};
    vivid::OperatorDestination dest;
    std::string resolve_error;
    std::string request = destination.empty() ? "auto" : destination;
    if (!vivid::resolve_operator_destination(request, core_src_dir, packages, settings_,
                                             dest, resolve_error)) {
        std::fprintf(stderr, "[vivid] Clone destination error: %s\n", resolve_error.c_str());
        return;
    }
    if (!dest.warning.empty()) {
        std::fprintf(stderr, "[vivid] %s\n", dest.warning.c_str());
    }
    if (dest.package_layout && dest.package_name.empty()) {
        std::fprintf(stderr,
                     "[vivid] Clone destination '%s' is not an active package; using core destination\n",
                     dest.root.c_str());
        dest = {};
        dest.root = core_src_dir;
    }
    const bool use_project_package = dest.package_layout && !dest.package_name.empty();

    std::string new_dir;
    if (use_project_package) {
        new_dir = (std::filesystem::path(dest.root) / "src").string();
    } else {
        new_dir = operators_dir_ + "/" + kind_dir + "/" + new_stem;
        std::filesystem::create_directories(new_dir);
    }

    // Transform source: replace old type name → new type name
    std::string transformed = cpp_source;
    {
        size_t pos = 0;
        while ((pos = transformed.find(type_name, pos)) != std::string::npos) {
            transformed.replace(pos, type_name.size(), new_type);
            pos += new_type.size();
        }
    }
    // Replace old stem .wgsl references → new stem
    {
        std::string old_wgsl_ref = stem + ".wgsl";
        std::string new_wgsl_ref = new_stem + ".wgsl";
        size_t pos = 0;
        while ((pos = transformed.find(old_wgsl_ref, pos)) != std::string::npos) {
            transformed.replace(pos, old_wgsl_ref.size(), new_wgsl_ref);
            pos += new_wgsl_ref.size();
        }
    }

    // If a .wgsl file exists alongside the .cpp, copy it with the new name
    std::string wgsl_path = src_dir + "/" + stem + ".wgsl";
    if (std::filesystem::exists(wgsl_path)) {
        std::string new_wgsl = new_dir + "/" + new_stem + ".wgsl";
        std::filesystem::copy_file(wgsl_path, new_wgsl,
                                   std::filesystem::copy_options::overwrite_existing);
    }

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

    if (use_project_package) {
        if (!patch_package_cmake_ops(dest.root, new_stem)) {
            std::fprintf(stderr, "[vivid] Clone: cannot update package CMakeLists.txt for %s\n",
                         dest.package_name.c_str());
            return;
        }
    } else {
        // Append CMake target
        std::string cmake_path = operators_dir_ + "/../CMakeLists.txt";
        std::ofstream ofs(cmake_path, std::ios::app);
        if (!ofs) {
            std::fprintf(stderr, "[vivid] Clone: cannot append to CMakeLists.txt\n");
            return;
        }
        ofs << "\nadd_library(" << new_stem << " MODULE operators/"
            << kind_dir << "/" << new_stem << "/" << new_stem << ".cpp)\n";
        ofs << "set_target_properties(" << new_stem
            << " PROPERTIES PREFIX \"\" SUFFIX \"${VIVID_PLUGIN_SUFFIX}\")\n";
        if (vivid_operator_kind(desc) == VIVID_OP_GPU)
            ofs << "target_link_libraries(" << new_stem << " PRIVATE vivid_operator_api webgpu)\n";
        else
            ofs << "target_link_libraries(" << new_stem << " PRIVATE vivid_operator_api)\n";
    }

    // Validate stem contains only safe characters (alphanumeric + underscore)
    for (char c : new_stem) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            std::fprintf(stderr, "[vivid] Clone: invalid character '%c' in operator name\n", c);
            return;
        }
    }

    if (use_project_package) {
        // Package clones rebuild via package hot-reload flow.
        registry_->register_user_operator(new_type, new_cpp);
        op_cache_->invalidate_all();
        if (hot_reloader_) {
            hot_reloader_->queue_rebuild("pkg:" + dest.package_name + ":" + new_stem);
        }
        vivid::open_in_editor(new_cpp, settings_ ? *settings_ : vivid::Settings{});
        std::fprintf(stderr, "[vivid] Cloned '%s' as '%s' in package '%s'\n",
                     type_name.c_str(), new_type.c_str(), dest.package_name.c_str());
        return;
    }

    // Build synchronously (core destination)
    std::string build_cmd = "cmake --build \"" + build_dir_ + "\" --target \"" + new_stem + "\" 2>&1";
    std::fprintf(stderr, "[vivid] Clone: building %s...\n", new_stem.c_str());
    vivid::BuildTaskId task_id = build_console_
        ? build_console_->begin_task(vivid::BuildTaskKind::PackageBuild, "clone " + new_stem)
        : 0;
    FILE* pipe = popen(build_cmd.c_str(), "r");
    if (!pipe) {
        if (build_console_)
            build_console_->append_system_line(task_id, "Failed to execute cmake build");
        if (build_console_)
            build_console_->finish_task(task_id, vivid::BuildTaskState::Failed, "launch failed");
        std::fprintf(stderr, "[vivid] Clone: failed to start build\n");
        return;
    }
    std::array<char, 256> build_buf;
    while (fgets(build_buf.data(), build_buf.size(), pipe) != nullptr) {
        if (build_console_)
            build_console_->append_line(task_id, vivid::BuildConsoleStreamKind::Stdout, build_buf.data());
    }
    int rc = pclose(pipe);
    if (rc != 0) {
        std::fprintf(stderr, "[vivid] Clone: build failed (exit %d)\n", rc);
        if (build_console_)
            build_console_->finish_task(task_id, vivid::BuildTaskState::Failed,
                                        "failed (exit " + std::to_string(rc) + ")");
        return;
    }
    if (build_console_)
        build_console_->finish_task(task_id, vivid::BuildTaskState::Succeeded, "built");

    // Load the new dylib (core destination)
#if defined(__APPLE__)
    std::string dylib_name = new_stem + ".dylib";
#elif defined(_WIN32)
    std::string dylib_name = new_stem + ".dll";
#else
    std::string dylib_name = new_stem + ".so";
#endif
    std::string dylib_path = build_dir_ + "/" + dylib_name;
    if (!registry_->register_loaded_operator(dylib_path)) {
        std::fprintf(stderr, "[vivid] Clone: failed to load %s\n", dylib_path.c_str());
        return;
    }

    // Register as user operator and invalidate caches
    registry_->register_user_operator(new_type, new_cpp);
    op_cache_->invalidate_all();

    // Open source in editor
    vivid::open_in_editor(new_cpp, settings_ ? *settings_ : vivid::Settings{});

    std::fprintf(stderr, "[vivid] Cloned '%s' as '%s'\n", type_name.c_str(), new_type.c_str());
}

