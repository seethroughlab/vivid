#include "runtime/core/main_internal.h"

#include "export/export_pipeline.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/graph_file_io.h"
#include "runtime/control/runtime_command_sink.h"
#include "runtime/core/main_helpers.h"
#include "runtime/core/settings.h"
#include "runtime/platform/app_update_manager.h"
#include "runtime/platform/platform.h"
#include "ui/dialogs/file_dialog.h"
#include "ui/graph/node_graph.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>

#ifdef __APPLE__
#include "runtime/platform/macos_menu.h"
#include "runtime/platform/sparkle_bridge.h"
#endif

namespace vivid::main_internal {

bool do_save_graph(MainAppContext& ctx) {
    if (ctx.graph_ui.visible()) {
        ctx.graph.set_viewport(ctx.graph_ui.pan_x(), ctx.graph_ui.pan_y(), ctx.graph_ui.zoom());
    }
    annotate_graph_packages(ctx.graph, ctx.pkg_manager, ctx.registry);
    auto result = ctx.runtime_api.save();
    std::fprintf(stderr, "[vivid] Save: %s\n", result.message.c_str());
    return result.ok;
}

bool do_save_as_dialog(MainAppContext& ctx) {
    std::string path = vivid::ui::save_file_dialog();
    if (path.empty()) return false;
    if (ctx.graph_ui.visible()) {
        ctx.graph.set_viewport(ctx.graph_ui.pan_x(), ctx.graph_ui.pan_y(), ctx.graph_ui.zoom());
    }
    annotate_graph_packages(ctx.graph, ctx.pkg_manager, ctx.registry);
    auto result = ctx.runtime_api.save_as(path);
    std::fprintf(stderr, "[vivid] Save As: %s\n", result.message.c_str());
    return result.ok;
}

void execute_pending_action(MainAppContext& ctx,
                            vivid::ui::NodeGraphUI::SaveConfirmAction action) {
    if (action == vivid::ui::NodeGraphUI::SaveConfirmAction::kNewGraph) {
        auto result = ctx.runtime_api.new_graph(ctx.has_gpu_ops, ctx.has_audio);
        if (result.ok) {
            ctx.graph_loaded = true;
            ctx.command_sink.reset_undo_history();
        }
        std::fprintf(stderr, "[vivid] New Graph: %s\n", result.message.c_str());
        return;
    }

    std::string dir = vivid::ui::save_directory_dialog("MyProject");
    if (dir.empty()) return;
    auto result = ctx.runtime_api.new_project(dir, ctx.has_gpu_ops, ctx.has_audio);
    if (result.ok) {
        ctx.graph_loaded = true;
        ctx.command_sink.reset_undo_history();
    }
    std::fprintf(stderr, "[vivid] New Project: %s\n", result.message.c_str());
}

void setup_save_confirm_callbacks(MainAppContext& ctx) {
    ctx.graph_ui.on_save_confirm_cancel = [&ctx]() {
        (void)ctx;
    };
    ctx.graph_ui.on_save_confirm_dont_save = [&ctx]() {
        execute_pending_action(ctx, ctx.graph_ui.save_confirm_action());
    };
    ctx.graph_ui.on_save_confirm_save = [&ctx]() {
        auto action = ctx.graph_ui.save_confirm_action();
        bool saved = ctx.graph.source_path().empty() ? do_save_as_dialog(ctx) : do_save_graph(ctx);
        if (saved) execute_pending_action(ctx, action);
    };
}

#ifdef __APPLE__
void setup_macos_menu(MainAppContext& ctx,
                      const vivid::RuntimeBootstrapPaths& runtime_paths,
                      DisplayState& display_state,
                      const std::function<void()>& toggle_fullscreen,
                      const std::function<bool(AsyncGraphLoadRequest, const char*)>& request_graph_load,
                      const std::function<bool()>& new_graph_runtime) {
    vivid::MenuCallbacks menu_cbs;

    menu_cbs.on_about = [&ctx]() { ctx.graph_ui.open_about(); };
    menu_cbs.on_new = [&ctx, &new_graph_runtime]() {
        if (ctx.runtime_api.graph_dirty()) {
            ctx.graph_ui.open_save_confirm_dialog(vivid::ui::NodeGraphUI::SaveConfirmAction::kNewGraph);
        } else {
            new_graph_runtime();
        }
    };
    menu_cbs.on_new_project = [&ctx]() {
        if (ctx.runtime_api.graph_dirty()) {
            ctx.graph_ui.open_save_confirm_dialog(vivid::ui::NodeGraphUI::SaveConfirmAction::kNewProject);
        } else {
            execute_pending_action(ctx, vivid::ui::NodeGraphUI::SaveConfirmAction::kNewProject);
        }
    };
    menu_cbs.on_preferences = [&ctx]() {
        ctx.graph_ui.toggle_preferences();
    };
    menu_cbs.on_save = [&ctx]() {
        if (ctx.graph.source_path().empty()) {
            do_save_as_dialog(ctx);
        } else {
            do_save_graph(ctx);
        }
    };
    menu_cbs.on_save_as = [&ctx]() {
        do_save_as_dialog(ctx);
    };
    menu_cbs.on_open = [&request_graph_load]() {
        std::string path = vivid::ui::open_file_dialog();
        if (path.empty()) return;
        AsyncGraphLoadRequest request;
        request.kind = AsyncGraphLoadRequest::Kind::Open;
        request.requested_path = path;
        request.display_name = std::filesystem::path(path).filename().string();
        request.update_recent_files = true;
        request_graph_load(std::move(request), "Open");
    };
    menu_cbs.on_open_example = [&ctx]() {
        ctx.graph_ui.toggle_example_browser();
    };
    menu_cbs.on_open_graph_folder = [&ctx]() {
        auto folder = std::filesystem::path(ctx.graph.source_path()).parent_path().string();
        if (!folder.empty()) vivid::open_url(folder);
    };
    menu_cbs.has_graph_path = [&ctx]() {
        return !ctx.graph.source_path().empty();
    };
    menu_cbs.on_export = [&ctx, &runtime_paths]() {
        if (ctx.graph.source_path().empty()) {
            std::fprintf(stderr, "[vivid] Export: no graph loaded\n");
            return;
        }
        std::string output_path = vivid::ui::save_file_dialog("my_app");
        if (output_path.empty()) return;
        auto out = std::filesystem::path(output_path);
        std::string output_name = out.stem().string();
        std::string output_dir = (out.parent_path() / (output_name + "_export")).string();
        if (runtime_paths.source_dir.empty()) {
            std::fprintf(stderr, "[vivid] Export: cannot determine source directory\n");
            return;
        }
        vivid::ExportOptions opts;
        opts.graph_path = ctx.graph.source_path();
        opts.output_name = output_name;
        opts.output_path = output_path;
        opts.output_dir = output_dir;
        vivid::ExportPipeline pipeline(runtime_paths.source_dir, runtime_paths.build_dir);
        if (pipeline.run(opts, ctx.registry)) {
            std::fprintf(stderr, "[vivid] Export succeeded: %s\n", output_name.c_str());
        } else {
            std::fprintf(stderr, "[vivid] Export failed\n");
        }
    };
    menu_cbs.on_browse_packages = [&ctx]() {
        ctx.graph_ui.toggle_package_browser();
    };
    menu_cbs.on_open_package_catalog_website = [&ctx]() {
        const char* env_url = std::getenv("VIVID_PACKAGE_DISCOVERY_URL");
        const std::string url =
            (env_url && env_url[0] != '\0') ? std::string(env_url)
                                             : std::string("https://vivid.seethroughlab.com");
        std::string err;
        if (!vivid::open_url(url, &err)) {
            std::fprintf(stderr, "[vivid] Failed to open package catalog URL '%s': %s\n",
                         url.c_str(), err.c_str());
        } else {
            std::fprintf(stderr, "[vivid] Opened package catalog website: %s\n", url.c_str());
        }
    };
    menu_cbs.on_check_for_updates = [&ctx]() {
        std::string err;
        if (vivid::SparkleBridge::available() && vivid::SparkleBridge::check_for_updates(&err)) {
            ctx.settings.core_update_last_checked_at = now_epoch_seconds_str();
            vivid::save_settings(ctx.settings);
            return;
        }
        ctx.app_updates.refresh();
        ctx.settings.core_update_last_checked_at = now_epoch_seconds_str();
        vivid::save_settings(ctx.settings);
        std::fprintf(stderr, "[vivid] Checking for core updates via appcast...\n");
    };
    menu_cbs.on_toggle_auto_check_updates = [&ctx]() {
        ctx.settings.core_update_auto_check = !ctx.settings.core_update_auto_check;
        vivid::save_settings(ctx.settings);
        std::fprintf(stderr, "[vivid] Core auto-update checks: %s\n",
                     ctx.settings.core_update_auto_check ? "enabled" : "disabled");
    };
    menu_cbs.on_check_system_requirements = [&ctx]() {
        ctx.graph_ui.open_system_requirements(/*auto_opened=*/false);
    };
    menu_cbs.on_report_issue = [&ctx]() {
        const auto packages = ctx.pkg_manager.list();
        const auto operators = ctx.registry.type_names();
        const char* graph_path = ctx.graph.source_path().empty() ? "<unsaved>" : ctx.graph.source_path().c_str();
#ifdef NDEBUG
        const char* build_mode = "Release";
#else
        const char* build_mode = "Debug";
#endif
        std::ostringstream body;
        body << "## What happened?\n";
        body << "<!-- Describe expected vs actual behavior -->\n\n";
        body << "## Steps to reproduce\n";
        body << "1. \n2. \n3. \n\n";
        body << "## Runtime diagnostics\n";
        body << "- Core version: " << VIVID_CORE_VERSION << "\n";
        body << "- Platform: " << platform_label() << "\n";
        body << "- Build mode: " << build_mode << "\n";
        body << "- Graph: " << graph_path << "\n";
        body << "- Registered operator types: " << operators.size() << "\n";
        body << "- Installed packages: " << packages.size() << "\n";
        body << "- Audio enabled: " << (ctx.has_audio ? "yes" : "no") << "\n";
        body << "- GPU operators enabled: " << (ctx.has_gpu_ops ? "yes" : "no") << "\n";
        const std::string issue_url =
            "https://github.com/seethroughlab/vivid/issues/new"
            "?title=" + url_encode("[Bug] ") +
            "&body=" + url_encode(body.str());
        std::string err;
        if (!vivid::open_url(issue_url, &err)) {
            std::fprintf(stderr, "[vivid] Failed to open issue URL: %s\n", err.c_str());
        } else {
            std::fprintf(stderr, "[vivid] Opened issue reporter URL\n");
        }
    };

    menu_cbs.on_delete_selected = [&ctx]() { ctx.graph_ui.delete_selected(); };
    menu_cbs.on_edit_meta = [&ctx]() {
        if (ctx.graph.source_path().empty()) return;
        vivid::GraphMetaEditData data;
        std::string error;
        if (!load_graph_meta_edit_data(ctx.graph.source_path(), data, error)) {
            std::fprintf(stderr, "[vivid] Edit Meta: %s\n", error.c_str());
            return;
        }
        ctx.graph_ui.open_graph_meta_editor(data);
    };

    menu_cbs.on_toggle_ui = [&ctx]() { ctx.graph_ui.toggle_visible(); };
    menu_cbs.on_toggle_fullscreen = [toggle_fullscreen]() { toggle_fullscreen(); };
    menu_cbs.on_toggle_bezier_wires = [&ctx]() { ctx.graph_ui.set_bezier_wires(!ctx.graph_ui.bezier_wires()); };
    menu_cbs.on_toggle_show_param_wires = [&ctx]() { ctx.graph_ui.set_show_param_wires(!ctx.graph_ui.show_param_wires()); };
    menu_cbs.on_toggle_analysis = [&ctx]() {
        bool next = !ctx.runtime.frame_executor().analysis_enabled();
        ctx.runtime.frame_executor().set_analysis_enabled(next);
        ctx.audio_engine.set_analysis_enabled(next);
        ctx.settings.show_analysis = next;
    };
    menu_cbs.on_toggle_session_grid = [&ctx]() { ctx.graph_ui.toggle_session_grid(); };
    menu_cbs.on_toggle_build_console = [&ctx]() { ctx.graph_ui.toggle_build_console(); };
    menu_cbs.on_toggle_midi_map = [&ctx]() { ctx.graph_ui.toggle_midi_map_mode(); };
    menu_cbs.on_add_node = [&ctx]() { ctx.graph_ui.open_chooser(); };

    menu_cbs.is_ui_visible = [&ctx]() { return ctx.graph_ui.visible(); };
    menu_cbs.is_fullscreen = [&display_state]() { return display_state.fullscreen; };
    menu_cbs.is_bezier_wires = [&ctx]() { return ctx.graph_ui.bezier_wires(); };
    menu_cbs.is_show_param_wires = [&ctx]() { return ctx.graph_ui.show_param_wires(); };
    menu_cbs.is_analysis_enabled = [&ctx]() { return ctx.runtime.frame_executor().analysis_enabled(); };
    menu_cbs.is_session_grid_open = [&ctx]() { return ctx.graph_ui.session_grid_open(); };
    menu_cbs.is_build_console_open = [&ctx]() { return ctx.graph_ui.build_console_open(); };
    menu_cbs.is_midi_map_mode = [&ctx]() { return ctx.graph_ui.midi_map_mode(); };
    menu_cbs.has_selection = [&ctx]() { return ctx.graph_ui.has_selection(); };
    menu_cbs.can_edit_meta = [&ctx]() { return !ctx.graph.source_path().empty(); };
    menu_cbs.is_auto_check_updates = [&ctx]() { return ctx.settings.core_update_auto_check; };

    menu_cbs.on_open_recent = [&request_graph_load](const std::string& path) {
        AsyncGraphLoadRequest request;
        request.kind = AsyncGraphLoadRequest::Kind::OpenRecent;
        request.requested_path = path;
        request.display_name = std::filesystem::path(path).filename().string();
        request.update_recent_files = true;
        request_graph_load(std::move(request), "Open Recent");
    };
    menu_cbs.on_clear_recent = [&ctx]() {
        ctx.settings.recent_files.clear();
        vivid::save_settings(ctx.settings);
        vivid::macos_update_recent_files_menu(ctx.settings.recent_files);
    };

    vivid::macos_setup_menu(menu_cbs);
    vivid::macos_update_recent_files_menu(ctx.settings.recent_files);
}
#endif

} // namespace vivid::main_internal
