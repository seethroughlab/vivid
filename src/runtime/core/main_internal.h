#pragma once

#include "common/dialog_types.h"
#include "runtime/control/runtime_api.h"
#include "runtime/core/runtime_core.h"
#include "runtime/core/settings.h"
#include "runtime/core/file_drop_registry.h"
#include "runtime/core/runtime_bootstrap.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/operators/operator_preparation_service.h"
#include "ui/graph/node_graph.h"
#include <GLFW/glfw3.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class RuntimeCommandSink;

namespace vivid {
class AudioEngine;
class AppUpdateManager;
class BuildConsole;
class CaptureCoordinator;
class ControlServer;
class FileDropRegistry;
class GpuContext;
class Graph;
class OperatorRegistry;
class PackageCatalog;
class PackageManager;
class RuntimeAPI;
class RuntimeCore;
struct ExampleEntry;
class SubgraphModuleRegistry;
class SystemMidiListener;

namespace ui { class ThumbnailCache; }

namespace main_internal {

struct DisplayState {
    bool fullscreen = false;
    GLFWmonitor* fullscreen_monitor = nullptr;
    int windowed_x = 100;
    int windowed_y = 100;
    int windowed_w = 1280;
    int windowed_h = 800;
    uint64_t seen_monitor_serial = 0;
    int sink_target = -1;
    bool surface_reconfigure_pending = false;
    int surface_settle_frames = 0;
};

struct AsyncAddPreparedResult {
    bool success = false;
    std::string user_message;
    std::string node_id;
    vivid::Graph graph;
    vivid::RuntimeCore::PreparedBuild prepared;
};

struct AsyncGraphLoadRequest {
    enum class Kind {
        StartupInitial,
        Open,
        OpenRecent,
        OpenExample,
        DropGraph,
        Reload,
    };

    Kind kind = Kind::Open;
    std::string requested_path;
    std::string resolved_path;
    std::string display_name;
    bool clear_source_path = false;
    bool update_recent_files = false;
};

struct AsyncGraphLoadPreparedResult {
    bool success = false;
    std::string user_message;
    AsyncGraphLoadRequest request;
    vivid::RuntimeAPI::PreservedRuntimeState preserved_state;
    vivid::Graph graph;
    vivid::RuntimeCore::PreparedBuild prepared;
};

class AsyncAddCoordinator {
public:
    enum class Stage {
        Idle,
        Preparing,
        Compiling,
    };

    ~AsyncAddCoordinator();

    bool begin(const vivid::ui::NodeGraphUI::AsyncAddOperatorRequest& request,
               const vivid::Graph& live_graph,
               const vivid::RuntimeCore& runtime,
               vivid::OperatorRegistry& registry,
               std::string& error);

    bool active() const { return active_; }
    // True if the worker finished but take_completed() hasn't been called yet.
    // The window where active_=false but completed_=true is a race that the
    // drop-settle screenshot guard must also block on.
    bool has_result_pending() const { return completed_; }
    Stage stage() const { return stage_; }
    bool take_completed(AsyncAddPreparedResult& out);

private:
    std::thread worker_;
    std::atomic<bool> active_{false};
    std::atomic<Stage> stage_{Stage::Idle};
    std::atomic<vivid::OperatorPreparationService::TaskId> prep_task_id_{0};
    std::mutex result_mutex_;
    std::atomic<bool> completed_{false};
    AsyncAddPreparedResult result_;
};

class AsyncGraphLoadCoordinator {
public:
    enum class Stage {
        Idle,
        Loading,
        PreparingOperators,
        Compiling,
    };

    ~AsyncGraphLoadCoordinator();

    bool begin(const AsyncGraphLoadRequest& request,
               const std::vector<vivid::PackageInfo>& packages,
               const vivid::RuntimeAPI::PreservedRuntimeState& preserved_state,
               const vivid::RuntimeCore& runtime,
               vivid::OperatorRegistry& registry,
               std::string& error);

    bool active() const { return active_; }
    Stage stage() const { return stage_; }
    bool startup_active() const;
    const char* stage_text() const;
    bool take_completed(AsyncGraphLoadPreparedResult& out);

private:
    std::thread worker_;
    std::atomic<bool> active_{false};
    std::atomic<Stage> stage_{Stage::Idle};
    std::atomic<vivid::OperatorPreparationService::TaskId> prep_task_id_{0};
    std::mutex result_mutex_;
    std::atomic<bool> completed_{false};
    AsyncGraphLoadPreparedResult result_;
    AsyncGraphLoadRequest request_{};
};

struct MainAppContext {
    vivid::Graph& graph;
    vivid::RuntimeCore& runtime;
    vivid::AudioEngine& audio_engine;
    vivid::OperatorRegistry& registry;
    vivid::RuntimeAPI& runtime_api;
    ::RuntimeCommandSink& command_sink;
    vivid::CaptureCoordinator& capture_coordinator;
    vivid::Settings& settings;
    vivid::ui::NodeGraphUI& graph_ui;
    vivid::ui::ThumbnailCache& thumb_cache;
    vivid::GpuContext& gpu;
    vivid::PackageManager& pkg_manager;
    vivid::PackageCatalog& pkg_catalog;
    vivid::AppUpdateManager& app_updates;
    vivid::ControlServer& control_server;
    vivid::SystemMidiListener& system_midi;
    vivid::SubgraphModuleRegistry& subgraph_modules;
    std::shared_ptr<vivid::BuildConsole>& build_console;
    const std::filesystem::path& resources_dir;
    const std::filesystem::path& exe_dir;
    const std::filesystem::path& graphs_root;
    std::vector<vivid::ExampleEntry>& discovered_examples;
    bool& graph_loaded;
    bool& has_gpu_ops;
    bool& has_audio;
    int& video_out_idx;
};

struct PackageBrowserState {
    enum class ActionState {
        Idle,
        Running,
        Done,
        Error,
    };

    ~PackageBrowserState();

    std::mutex action_mutex;
    ActionState action_state{ActionState::Idle};
    std::string action_error_msg;
    bool action_needs_refresh{false};
    std::thread action_thread;
    std::vector<vivid::PackageBrowserEntry> entries_cache;
};

std::string derive_project_shader_dir(const vivid::Graph& graph);
bool prepare_graph_shader_operators(const vivid::Graph& graph,
                                    vivid::OperatorRegistry& registry,
                                    std::string& error);
void populate_graph_package_diagnostics(vivid::Graph& graph,
                                        const std::vector<vivid::PackageInfo>& packages);
void annotate_graph_packages(vivid::Graph& graph,
                             vivid::PackageManager& pkg_manager,
                             vivid::OperatorRegistry& registry);
bool rebuild_live_runtime_from_graph(MainAppContext& ctx);
bool adopt_prepared_graph(MainAppContext& ctx,
                          vivid::Graph&& next_graph,
                          vivid::RuntimeCore::PreparedBuild&& prepared_build,
                          bool reset_live_metronome);
bool adopt_prepared_runtime_build(MainAppContext& ctx,
                                  AsyncAddPreparedResult prepared);
bool adopt_prepared_graph_load(MainAppContext& ctx,
                               AsyncGraphLoadPreparedResult prepared);
bool make_initial_graph_load_request(const std::string& graph_file,
                                     const std::filesystem::path& resources_dir,
                                     AsyncGraphLoadRequest& out);
bool create_file_drop_node(vivid::ui::NodeGraphUI& graph_ui,
                           const vivid::FileDropMatch& match,
                           const std::string& dropped_path,
                           float graph_x,
                           float graph_y);

void refresh_discovered_examples(MainAppContext& ctx);
void refresh_package_browser_entries_cache(MainAppContext& ctx,
                                           PackageBrowserState& state);
void configure_package_browser(MainAppContext& ctx,
                               PackageBrowserState& state);
void poll_package_browser_actions(MainAppContext& ctx,
                                  PackageBrowserState& state,
                                  bool graph_transaction_active);

bool do_save_graph(MainAppContext& ctx);
bool do_save_as_dialog(MainAppContext& ctx);
void execute_pending_action(MainAppContext& ctx,
                            vivid::ui::NodeGraphUI::SaveConfirmAction action);
void setup_save_confirm_callbacks(MainAppContext& ctx);
#ifdef __APPLE__
void setup_macos_menu(MainAppContext& ctx,
                      const vivid::RuntimeBootstrapPaths& runtime_paths,
                      DisplayState& display_state,
                      const std::function<void()>& toggle_fullscreen,
                      const std::function<bool(AsyncGraphLoadRequest, const char*)>& request_graph_load,
                      const std::function<bool()>& new_graph_runtime);
#endif

} // namespace main_internal
} // namespace vivid
