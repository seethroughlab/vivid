#pragma once

#include "ui/ui_command_sink.h"
#include "runtime/runtime_api.h"
#include "runtime/operator_registry.h"
#include "runtime/operator_creator.h"
#include "runtime/hot_reload.h"
#include "runtime/wgsl_header_parser.h"
#include "runtime/graph.h"
#include "runtime/operator_info_cache.h"
#include "runtime/capture_coordinator.h"
#include "runtime/settings.h"
#include "runtime/editor_detect.h"
#include "operator_api/data_driven_filter.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

class RuntimeCommandSink : public vivid::ui::UICommandSink {
public:
    explicit RuntimeCommandSink(vivid::RuntimeAPI& api) : api_(api) {}
    void set_param(const std::string& node_id, const std::string& param, float value) override {
        api_.set_param(node_id, param, value);
    }
    void add_node(const std::string& type, const std::string& id) override {
        api_.add_node(type, id);
    }
    void remove_node(const std::string& id) override {
        api_.remove_node(id);
    }
    void connect(const std::string& from, const std::string& to) override {
        api_.connect(from, to);
    }
    void disconnect(const std::string& from, const std::string& to) override {
        api_.disconnect(from, to);
    }
    void set_connection_remap(const std::string& from, const std::string& to,
                              float from_min, float from_max,
                              float to_min, float to_max, bool clamp) override {
        api_.set_connection_remap(from, to, from_min, from_max, to_min, to_max, clamp);
    }
    void set_node_layout(const std::string& node_id, float x, float y) override {
        api_.set_node_layout(node_id, x, y);
    }
    void set_resolution(const std::string& node_id, uint32_t w, uint32_t h) override {
        api_.set_resolution(node_id, w, h);
    }
    void add_midi_mapping(const std::string& node_id, const std::string& param,
                          int cc, int channel, float range_min, float range_max) override {
        api_.add_midi_mapping(node_id, param, cc, channel, range_min, range_max);
    }
    void remove_midi_mapping(const std::string& node_id, const std::string& param) override {
        api_.remove_midi_mapping(node_id, param);
    }
    void update_midi_mapping(const std::string& node_id, const std::string& param,
                             float range_min, float range_max) override {
        api_.update_midi_mapping(node_id, param, range_min, range_max);
    }

    void set_string_param(const std::string& node_id, const std::string& param,
                          const std::string& value) override {
        api_.set_string_param(node_id, param, value);
    }

    void save_variation(const std::string& name) override { api_.save_variation(name); }
    void recall_variation(const std::string& name) override { api_.recall_variation(name); }
    void recall_variation_idx(int idx) override { api_.recall_variation_idx(idx); }
    void remove_variation(const std::string& name) override { api_.remove_variation(name); }
    void rename_variation(const std::string& old_name, const std::string& new_name) override {
        api_.rename_variation(old_name, new_name);
    }
    void update_variation(const std::string& name) override { api_.update_variation(name); }
    void queue_variation(const std::string& name, const std::string& quantize) override {
        api_.queue_variation(name, quantize);
    }
    void set_quantize_clock(const std::string& node_id) override { api_.set_quantize_clock(node_id); }

    void set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags) override {
        api_.set_param_lock(node_id, param, flags);
    }

    void recall_preset(const std::string& node_id, const std::string& name) override {
        api_.recall_preset(node_id, name);
    }
    void save_preset(const std::string& node_id, const std::string& name) override {
        api_.save_preset(node_id, name);
    }

    void set_state_preset(const std::string& sm_node, int state_idx,
                          const std::string& target_node,
                          const std::string& preset_name) override {
        api_.set_state_preset(sm_node, state_idx, target_node, preset_name);
    }
    void remove_state_preset(const std::string& sm_node, int state_idx,
                             const std::string& target_node) override {
        api_.remove_state_preset(sm_node, state_idx, target_node);
    }

    void open_shader(const std::string& type_name) override {
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

    void duplicate_as_user_filter(const std::string& type_name) override {
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

    void clone_and_edit(const std::string& type_name) override {
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
        clone_cpp_operator(type_name);
    }

    void set_editor_preference(const std::string& editor_id,
                               const std::string& custom_command) override {
        if (!settings_) return;
        settings_->editor = editor_id;
        settings_->editor_command = custom_command;
        vivid::save_settings(*settings_);
    }

    void set_style_preference(const std::string& style_id) override {
        if (!settings_) return;
        settings_->style_id = style_id;
        vivid::save_settings(*settings_);
    }

    bool can_create_operator() const override {
        return !operators_dir_.empty() && !build_dir_.empty();
    }

    std::string validate_operator_name(const std::string& name) override {
        if (!registry_) return "registry not available";
        return vivid::OperatorCreator::validate_name(name, *registry_);
    }

    bool create_operator(const std::string& name, int domain) override {
        if (operators_dir_.empty() || !registry_) return false;

        VividDomain d;
        switch (domain) {
            case 0: d = VIVID_DOMAIN_CONTROL; break;
            case 1: d = VIVID_DOMAIN_AUDIO;   break;
            case 2: d = VIVID_DOMAIN_GPU;     break;
            default: return false;
        }

        // src_dir is the parent of operators/
        std::string src_dir = std::filesystem::path(operators_dir_).parent_path().string();
        auto cr = vivid::OperatorCreator::create(name, d, src_dir);
        if (!cr.success) return false;

        if (hot_reloader_)
            hot_reloader_->queue_rebuild(cr.target_name);

        vivid::OperatorCreator::open_in_editor(cr.cpp_path);
        return true;
    }

    void capture_snapshot() override {
        if (!capture_coordinator_) return;
        // Fire-and-forget — PNG is saved to disk
        capture_coordinator_->request_snapshot_to_file("");
    }

    void start_recording(const std::string& path, const std::string& codec, double fps) override {
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

    void stop_recording() override {
        if (!capture_coordinator_) return;
        capture_coordinator_->request_stop_recording();
    }

    void set_capture_coordinator(vivid::CaptureCoordinator* cc) { capture_coordinator_ = cc; }
    void set_operators_dir(const std::string& dir) { operators_dir_ = dir; }
    void set_filters_dir(const std::string& dir) { filters_dir_ = dir; }
    void set_registry(vivid::OperatorRegistry* r) { registry_ = r; }
    void set_graph(vivid::Graph* g) { graph_ = g; }
    void set_op_cache(OperatorInfoCache* c) { op_cache_ = c; }
    void set_working_filters_dir(const std::string& dir) { working_filters_dir_ = dir; }
    void set_build_dir(const std::string& dir) { build_dir_ = dir; }
    void set_settings(vivid::Settings* s) { settings_ = s; }
    void set_hot_reloader(vivid::HotReloader* hr) { hot_reloader_ = hr; }

private:
    // Find the .wgsl preset file for a given type name in the filters/ directory
    std::string find_preset_wgsl(const std::string& type_name) {
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

    void clone_cpp_operator(const std::string& type_name) {
        if (build_dir_.empty()) {
            std::fprintf(stderr, "[vivid] Clone: no build directory configured\n");
            return;
        }

        auto* loader = registry_->find(type_name);
        if (!loader) return;
        const auto* desc = loader->descriptor();
        if (!desc) return;

        // Map domain → subdirectory
        const char* domain_dir = "control";
        switch (desc->domain) {
            case VIVID_DOMAIN_AUDIO: domain_dir = "audio"; break;
            case VIVID_DOMAIN_GPU:   domain_dir = "gpu"; break;
            default: break;
        }

        // Derive stem (lowercase type name)
        std::string stem = type_name;
        for (auto& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Read source .cpp
        std::string src_dir = operators_dir_ + "/" + domain_dir + "/" + stem;
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

        // Create directory
        std::string new_dir = operators_dir_ + "/" + domain_dir + "/" + new_stem;
        std::filesystem::create_directories(new_dir);

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
            std::filesystem::copy_file(wgsl_path, new_wgsl);
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

        // Append CMake target
        {
            std::string cmake_path = operators_dir_ + "/../CMakeLists.txt";
            std::ofstream ofs(cmake_path, std::ios::app);
            if (!ofs) {
                std::fprintf(stderr, "[vivid] Clone: cannot append to CMakeLists.txt\n");
                return;
            }
            ofs << "\nadd_library(" << new_stem << " MODULE operators/"
                << domain_dir << "/" << new_stem << "/" << new_stem << ".cpp)\n";
            ofs << "set_target_properties(" << new_stem
                << " PROPERTIES PREFIX \"\" SUFFIX \"${VIVID_PLUGIN_SUFFIX}\")\n";
            if (desc->domain == VIVID_DOMAIN_GPU)
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

        // Build synchronously
        std::string build_cmd = "cmake --build \"" + build_dir_ + "\" --target \"" + new_stem + "\" 2>&1";
        std::fprintf(stderr, "[vivid] Clone: building %s...\n", new_stem.c_str());
        int rc = std::system(build_cmd.c_str());
        if (rc != 0) {
            std::fprintf(stderr, "[vivid] Clone: build failed (exit %d)\n", rc);
            return;
        }

        // Load the new dylib
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

    vivid::RuntimeAPI& api_;
    vivid::CaptureCoordinator* capture_coordinator_ = nullptr;
    std::string operators_dir_;
    std::string filters_dir_;
    std::string working_filters_dir_;
    std::string build_dir_;
    vivid::OperatorRegistry* registry_ = nullptr;
    vivid::Graph* graph_ = nullptr;
    OperatorInfoCache* op_cache_ = nullptr;
    vivid::Settings* settings_ = nullptr;
    vivid::HotReloader* hot_reloader_ = nullptr;
};
