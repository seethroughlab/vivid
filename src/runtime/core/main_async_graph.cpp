#include "runtime/core/main_internal.h"

#include "runtime/audio/audio_engine.h"
#include "runtime/control/graph_file_io.h"
#include "runtime/control/runtime_api.h"
#include "runtime/core/file_drop_registry.h"
#include "runtime/core/settings.h"
#include "runtime/core/main_helpers.h"
#include "runtime/debug/capture_coordinator.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/core/runtime_core.h"
#include "runtime/gpu/gpu_context.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_preparation_service.h"
#include "runtime/packages/package_manager.h"
#include "runtime/platform/platform.h"
#include "ui/graph/node_graph.h"
#include "ui/rendering/thumbnail_cache.h"

#ifdef __APPLE__
#include "runtime/platform/macos_menu.h"
#endif

#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace vivid::main_internal {
namespace {

bool split_async_add_addr(const std::string& addr,
                          std::string& node,
                          std::string& port) {
    auto slash = addr.find('/');
    if (slash == std::string::npos || slash == 0 || slash == addr.size() - 1)
        return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

bool apply_async_add_request_to_graph(
    vivid::Graph& graph,
    const vivid::ui::NodeGraphUI::AsyncAddOperatorRequest& request,
    std::string& error) {
    if (!graph.add_node(request.node_id, request.type_name, {}, request.string_params)) {
        error = "node '" + request.node_id + "' already exists";
        return false;
    }

    auto* ndef = graph.find_node(request.node_id);
    if (!ndef) {
        error = "failed to find newly added node";
        return false;
    }
    ndef->layout_x = request.graph_x;
    ndef->layout_y = request.graph_y;

    for (const auto& mut : request.connection_mutations) {
        std::string from_node, from_port, to_node, to_port;
        if (!split_async_add_addr(mut.from_addr, from_node, from_port) ||
            !split_async_add_addr(mut.to_addr, to_node, to_port)) {
            error = "invalid connection address in async add request";
            return false;
        }
        bool ok = false;
        if (mut.kind == vivid::ui::NodeGraphUI::AsyncAddConnectionMutation::Kind::Connect) {
            ok = graph.add_connection(from_node, from_port, to_node, to_port);
            if (!ok) error = "failed to connect " + mut.from_addr + " -> " + mut.to_addr;
        } else {
            ok = graph.remove_connection(from_node, from_port, to_node, to_port);
            if (!ok) error = "failed to disconnect " + mut.from_addr + " -> " + mut.to_addr;
        }
        if (!ok) return false;
    }

    return true;
}

} // namespace

AsyncAddCoordinator::~AsyncAddCoordinator() {
    if (worker_.joinable()) worker_.join();
}

bool AsyncAddCoordinator::begin(const vivid::ui::NodeGraphUI::AsyncAddOperatorRequest& request,
                                const vivid::Graph& live_graph,
                                const vivid::RuntimeCore& runtime,
                                vivid::OperatorRegistry& registry,
                                std::string& error) {
    if (active_) {
        error = "another operator is already being added";
        return false;
    }
    if (worker_.joinable()) worker_.join();

    active_ = true;
    stage_ = Stage::Preparing;
    completed_ = false;
    result_ = {};

    worker_ = std::thread([this, request, live_graph, &runtime, &registry]() mutable {
        AsyncAddPreparedResult result;
        result.node_id = request.node_id;

        auto finish = [&](bool success, std::string message) {
            result.success = success;
            result.user_message = std::move(message);
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                result_ = std::move(result);
                completed_ = true;
            }
            active_ = false;
            if (!success) stage_ = Stage::Idle;
        };

        const bool is_non_registry_type =
            (runtime.subgraph_modules() && runtime.subgraph_modules()->find(request.type_name));
        if (!is_non_registry_type) {
            auto prep_task_id = operator_preparation_service().submit(
                make_prepare_operator_type_request(registry, request.type_name, true));
            prep_task_id_ = prep_task_id;
            auto prepared = operator_preparation_service().wait(prep_task_id);
            prep_task_id_ = 0;
            if (!prepared.success) {
                finish(false, prepared.user_message.empty()
                                  ? "unknown operator type '" + request.type_name + "'"
                                  : prepared.user_message);
                return;
            }
        }

        vivid::Graph candidate = live_graph;
        std::string apply_error;
        if (!apply_async_add_request_to_graph(candidate, request, apply_error)) {
            finish(false, apply_error);
            return;
        }

        result.graph = std::move(candidate);
        stage_ = Stage::Compiling;
        if (!runtime.prepare_build(result.graph, registry, result.prepared, &apply_error)) {
            if (apply_error.empty())
                apply_error = "failed to compile graph after adding " + request.type_name;
            finish(false, apply_error);
            return;
        }

        finish(true, {});
    });

    return true;
}

bool AsyncAddCoordinator::take_completed(AsyncAddPreparedResult& out) {
    if (!completed_) return false;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!completed_) return false;
    out = std::move(result_);
    completed_ = false;
    prep_task_id_ = 0;
    stage_ = Stage::Idle;
    return true;
}

AsyncGraphLoadCoordinator::~AsyncGraphLoadCoordinator() {
    if (worker_.joinable()) worker_.join();
}

bool AsyncGraphLoadCoordinator::begin(const AsyncGraphLoadRequest& request,
                                      const std::vector<vivid::PackageInfo>& packages,
                                      const vivid::RuntimeAPI::PreservedRuntimeState& preserved_state,
                                      const vivid::RuntimeCore& runtime,
                                      vivid::OperatorRegistry& registry,
                                      std::string& error) {
    if (active_) {
        error = "another graph load is already in progress";
        return false;
    }
    if (worker_.joinable()) worker_.join();

    active_ = true;
    completed_ = false;
    result_ = {};
    stage_ = Stage::Loading;
    request_ = request;

    worker_ = std::thread([this, request, packages, preserved_state, &runtime, &registry]() mutable {
        AsyncGraphLoadPreparedResult result;
        result.request = request;
        result.preserved_state = preserved_state;

        auto finish = [&](bool success, std::string message) {
            result.success = success;
            result.user_message = std::move(message);
            {
                std::lock_guard<std::mutex> lock(result_mutex_);
                result_ = std::move(result);
                completed_ = true;
            }
            active_ = false;
            if (!success) stage_ = Stage::Idle;
        };

        vivid::Graph candidate;
        if (!candidate.load(request.resolved_path.c_str())) {
            finish(false, "failed to load " + request.resolved_path);
            return;
        }
        if (request.clear_source_path)
            candidate.set_source_path("");

        populate_graph_package_diagnostics(candidate, packages);
        std::string prep_error;
        if (!prepare_graph_shader_operators(candidate, registry, prep_error)) {
            finish(false, prep_error);
            return;
        }

        stage_ = Stage::PreparingOperators;
        auto prep_task_id = operator_preparation_service().submit(
            make_prepare_graph_request(registry, candidate, true));
        prep_task_id_ = prep_task_id;
        auto prepared = operator_preparation_service().wait(prep_task_id);
        prep_task_id_ = 0;
        if (!prepared.success) {
            finish(false, prepared.user_message.empty()
                              ? "failed to prepare operators for " + request.resolved_path
                              : prepared.user_message);
            return;
        }

        result.graph = std::move(candidate);
        stage_ = Stage::Compiling;
        if (!runtime.prepare_build(result.graph, registry, result.prepared, &prep_error)) {
            if (prep_error.empty())
                prep_error = "failed to compile " + request.resolved_path;
            finish(false, prep_error);
            return;
        }

        finish(true, {});
    });

    return true;
}

bool AsyncGraphLoadCoordinator::startup_active() const {
    return active_ && request_.kind == AsyncGraphLoadRequest::Kind::StartupInitial;
}

const char* AsyncGraphLoadCoordinator::stage_text() const {
    switch (stage_) {
        case Stage::Loading:
            return "Loading graph...";
        case Stage::PreparingOperators: {
            auto prep_task_id = prep_task_id_.load();
            if (prep_task_id != 0) {
                return operator_prepare_stage_text(
                    operator_preparation_service().task_stage(prep_task_id));
            }
            return "Preparing operators...";
        }
        case Stage::Compiling:
            return "Compiling graph...";
        case Stage::Idle:
            break;
    }
    return "Loading graph...";
}

bool AsyncGraphLoadCoordinator::take_completed(AsyncGraphLoadPreparedResult& out) {
    if (!completed_) return false;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!completed_) return false;
    out = std::move(result_);
    completed_ = false;
    prep_task_id_ = 0;
    stage_ = Stage::Idle;
    request_ = {};
    return true;
}

std::string derive_project_shader_dir(const vivid::Graph& graph) {
    if (graph.source_path().empty()) return {};
    return (std::filesystem::path(graph.source_path()).parent_path() / "filters").string();
}

bool prepare_graph_shader_operators(const vivid::Graph& graph,
                                    vivid::OperatorRegistry& registry,
                                    std::string& error) {
    const std::string shader_dir = derive_project_shader_dir(graph);
    if (shader_dir.empty() || !std::filesystem::exists(shader_dir))
        return true;
    if (!registry.scan_shader_operators(shader_dir, true)) {
        error = "failed to register project shader operators from " + shader_dir;
        return false;
    }
    return true;
}

void populate_graph_package_diagnostics(vivid::Graph& graph,
                                        const std::vector<vivid::PackageInfo>& packages) {
    graph.load_diagnostics.clear();
    std::unordered_map<std::string, std::string> installed_map;
    for (const auto& pkg : packages) installed_map[pkg.name] = pkg.version;
    for (const auto& node : graph.nodes()) {
        if (node.pkg_name.empty()) continue;
        auto it = installed_map.find(node.pkg_name);
        if (it == installed_map.end() || it->second.empty()) continue;
        const std::string& installed_ver = it->second;
        auto cls = vivid::PackageManager::classify_version_delta(node.pkg_version, installed_ver);
        if (cls == vivid::PackageUpdateClass::CompatibleUpdate ||
            cls == vivid::PackageUpdateClass::IncompatibleUpdate) {
            vivid::Graph::LoadDiagnostic diag;
            diag.node_id = node.id;
            diag.pkg_name = node.pkg_name;
            diag.saved_version = node.pkg_version;
            diag.installed_version = installed_ver;
            diag.classification = (cls == vivid::PackageUpdateClass::IncompatibleUpdate)
                ? "incompatible_update"
                : "compatible_update";
            graph.load_diagnostics.push_back(std::move(diag));
            if (cls == vivid::PackageUpdateClass::IncompatibleUpdate) {
                std::fprintf(stderr,
                             "[graph] Package version mismatch (incompatible): node '%s' saved with %s@%s, installed %s\n",
                             node.id.c_str(), node.pkg_name.c_str(),
                             node.pkg_version.c_str(), installed_ver.c_str());
            } else {
                std::fprintf(stderr,
                             "[graph] Package update: node '%s' %s saved=%s installed=%s\n",
                             node.id.c_str(), node.pkg_name.c_str(),
                             node.pkg_version.c_str(), installed_ver.c_str());
            }
        }
    }
}

void annotate_graph_packages(vivid::Graph& graph,
                             vivid::PackageManager& pkg_manager,
                             vivid::OperatorRegistry& registry) {
    auto packages = pkg_manager.list();
    std::unordered_map<std::string, std::string> pkg_ver_map;
    for (const auto& p : packages) pkg_ver_map[p.name] = p.version;
    for (auto& node : graph.nodes_mut()) {
        const auto* pkg = registry.package_for_type(node.type);
        if (pkg) {
            node.pkg_name = *pkg;
            node.pkg_version = pkg_ver_map.count(*pkg) ? pkg_ver_map[*pkg] : "";
        }
    }
}

// Shared teardown before rebuilding/adopting a live runtime graph (audit 03-R2-F2):
// stop the audio engine, tear down the runtime, and drop cached thumbnails.
static void teardown_live_runtime(MainAppContext& ctx) {
    if (ctx.has_audio) {
        ctx.audio_engine.shutdown();
        ctx.has_audio = false;
    }
    ctx.runtime.shutdown();
    ctx.thumb_cache.clear();
}

// Shared post-build initialization after a successful build()/adopt_prepared_build()
// (audit 03-R2-F2): publish graph-loaded state, (re)allocate GPU textures and resolve
// the video-out sink, then rebuild+start the audio engine when the graph has audio
// operators, and point the capture coordinator at the live engine.
static void finalize_live_runtime(MainAppContext& ctx) {
    ctx.graph_loaded = ctx.runtime.compiled_graph() && !ctx.runtime.compiled_graph()->nodes.empty();
    ctx.has_gpu_ops = ctx.runtime.has_gpu_operators();
    if (ctx.has_gpu_ops) {
        ctx.runtime.allocate_gpu_textures(ctx.gpu.device(), kDefaultTexW, kDefaultTexH,
                                          WGPUTextureFormat_RGBA16Float);
        ctx.video_out_idx = ctx.runtime.find_effective_gpu_sink();
    } else {
        ctx.video_out_idx = -1;
    }

    if (ctx.runtime.has_audio_operators()) {
        if (ctx.audio_engine.build(ctx.runtime) && ctx.audio_engine.start())
            ctx.has_audio = true;
    }

    ctx.capture_coordinator.set_audio_engine(ctx.has_audio ? &ctx.audio_engine : nullptr);
}

bool rebuild_live_runtime_from_graph(MainAppContext& ctx) {
    teardown_live_runtime(ctx);

    if (!ctx.runtime.build(ctx.graph, ctx.registry)) {
        std::fprintf(stderr, "[vivid] Runtime rebuild failed after registry refresh\n");
        ctx.graph_loaded = false;
        ctx.has_gpu_ops = false;
        ctx.video_out_idx = -1;
        ctx.capture_coordinator.set_audio_engine(nullptr);
        return false;
    }

    finalize_live_runtime(ctx);
    return true;
}

bool adopt_prepared_graph(MainAppContext& ctx,
                          vivid::Graph&& next_graph,
                          vivid::RuntimeCore::PreparedBuild&& prepared_build,
                          bool reset_live_metronome) {
    teardown_live_runtime(ctx);

    ctx.graph = std::move(next_graph);
    ctx.runtime.adopt_prepared_build(std::move(prepared_build));
    if (reset_live_metronome) {
        ctx.runtime.reset_live_metronome(ctx.graph.metronome(), ctx.runtime.last_tick_time());
    }

    finalize_live_runtime(ctx);
    return true;
}

bool adopt_prepared_runtime_build(MainAppContext& ctx,
                                  AsyncAddPreparedResult prepared) {
    auto state = ctx.runtime_api.capture_current_runtime_state();
    if (!adopt_prepared_graph(ctx, std::move(prepared.graph), std::move(prepared.prepared), false))
        return false;
    ctx.runtime_api.apply_preserved_runtime_state(state);
    ctx.runtime_api.notify_external_graph_mutation();
    return true;
}

bool adopt_prepared_graph_load(MainAppContext& ctx,
                               AsyncGraphLoadPreparedResult prepared) {
    const std::string previous_shader_dir = derive_project_shader_dir(ctx.graph);
    const std::string next_shader_dir = derive_project_shader_dir(prepared.graph);

    if (!adopt_prepared_graph(ctx, std::move(prepared.graph), std::move(prepared.prepared), true))
        return false;

    if (prepared.preserved_state.active)
        ctx.runtime_api.apply_preserved_runtime_state(prepared.preserved_state);

    if (!previous_shader_dir.empty() && previous_shader_dir != next_shader_dir)
        ctx.registry.clear_shader_operators_in_dir(previous_shader_dir);
    ctx.runtime_api.finalize_external_graph_load();

    if (prepared.request.update_recent_files && !prepared.request.resolved_path.empty()) {
        vivid::add_recent_file(ctx.settings, prepared.request.resolved_path);
        vivid::save_settings(ctx.settings);
#ifdef __APPLE__
        vivid::macos_update_recent_files_menu(ctx.settings.recent_files);
#endif
    }
    return true;
}

bool make_initial_graph_load_request(const std::string& graph_file,
                                     const std::filesystem::path& resources_dir,
                                     AsyncGraphLoadRequest& out) {
    if (graph_file.empty()) {
        auto user_template = std::filesystem::path(vivid::get_config_dir()) / "default_graph.json";
        auto bundled_template = resources_dir / "default_graph.json";
        std::filesystem::path template_path;
        if (std::filesystem::exists(user_template)) {
            template_path = user_template;
        } else if (std::filesystem::exists(bundled_template)) {
            template_path = bundled_template;
        } else {
            std::fprintf(stderr, "[vivid] Error: could not load default graph template\n");
            return false;
        }
        out.kind = AsyncGraphLoadRequest::Kind::StartupInitial;
        out.requested_path = template_path.string();
        out.resolved_path = template_path.string();
        out.display_name = "the default graph";
        out.clear_source_path = true;
        return true;
    }

    out.kind = AsyncGraphLoadRequest::Kind::StartupInitial;
    out.requested_path = graph_file;
    out.resolved_path = graph_file;
    out.display_name = std::filesystem::path(graph_file).filename().string();
    return true;
}

bool create_file_drop_node(vivid::ui::NodeGraphUI& graph_ui,
                           const vivid::FileDropMatch& match,
                           const std::string& dropped_path,
                           float graph_x,
                           float graph_y) {
    vivid::ui::FileDropChooserAction action;
    action.label = match.label.empty() ? match.type_name : match.label;
    action.subtitle = match.package_name.empty()
        ? match.type_name
        : (match.type_name + "  [" + match.package_name + "]");
    action.type_name = match.type_name;
    action.file_param = match.file_param;
    action.dropped_path = dropped_path;
    graph_ui.open_file_drop_chooser({action}, graph_x, graph_y);
    graph_ui.confirm_chooser_selection(action.type_name);
    return true;
}

void refresh_discovered_examples(MainAppContext& ctx) {
    ctx.discovered_examples = discover_examples_with_packages(ctx.graphs_root, &ctx.pkg_manager);
    ctx.graph_ui.set_examples(ctx.discovered_examples);
}

} // namespace vivid::main_internal
