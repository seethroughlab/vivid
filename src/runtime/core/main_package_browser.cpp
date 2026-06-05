#include "runtime/core/main_internal.h"

#include "runtime/control/runtime_command_sink.h"
#include "runtime/operators/operator_preparation_service.h"
#include "runtime/packages/package_catalog.h"
#include "runtime/packages/package_manager.h"
#include "ui/graph/node_graph.h"
#include "ui/rendering/thumbnail_cache.h"

#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace vivid::main_internal {

PackageBrowserState::~PackageBrowserState() {
    if (action_thread.joinable()) action_thread.join();
}

void refresh_package_browser_entries_cache(MainAppContext& ctx,
                                           PackageBrowserState& state) {
    {
        std::lock_guard<std::mutex> lk(state.action_mutex);
        if (state.action_state == PackageBrowserState::ActionState::Running)
            return;
    }

    std::vector<vivid::PackageBrowserEntry> out;
    std::unordered_map<std::string, vivid::PackageInfo> installed_map;
    for (const auto& p : ctx.pkg_manager.list()) {
        installed_map[p.name] = p;
    }

    std::unordered_set<std::string> packages_needing_rebuild;
    for (const auto& d : ctx.registry.abi_mismatch_diagnostics()) {
        if (!d.package_name.empty()) packages_needing_rebuild.insert(d.package_name);
    }
    for (const auto& d : ctx.registry.loader_failure_diagnostics()) {
        if (!d.package_name.empty()) packages_needing_rebuild.insert(d.package_name);
    }

    auto entries = ctx.pkg_catalog.entries();
    out.reserve(entries.size() + installed_map.size());
    for (const auto& e : entries) {
        vivid::PackageBrowserEntry ui_e;
        ui_e.name = e.name;
        ui_e.description = e.description;
        ui_e.version = e.version;
        ui_e.author = e.author;
        auto it = installed_map.find(e.name);
        if (it != installed_map.end()) {
            ui_e.installed = true;
            ui_e.linked = it->second.linked;
            ui_e.category = it->second.category;
            ui_e.tags = it->second.tags;
            if (packages_needing_rebuild.count(ui_e.name)) {
                ui_e.needs_rebuild = true;
                ui_e.health_detail = "ABI mismatch -- try rebuild";
            }
            installed_map.erase(it);
        }
        out.push_back(std::move(ui_e));
    }

    for (const auto& [name, info] : installed_map) {
        vivid::PackageBrowserEntry ui_e;
        ui_e.name = info.name;
        ui_e.description = info.description;
        ui_e.version = info.version;
        ui_e.author = info.author;
        ui_e.category = info.category;
        ui_e.tags = info.tags;
        ui_e.installed = true;
        ui_e.linked = info.linked;
        if (packages_needing_rebuild.count(ui_e.name)) {
            ui_e.needs_rebuild = true;
            ui_e.health_detail = "ABI mismatch -- try rebuild";
        }
        out.push_back(std::move(ui_e));
    }

    state.entries_cache = std::move(out);
}

void configure_package_browser(MainAppContext& ctx,
                               PackageBrowserState& state) {
    refresh_package_browser_entries_cache(ctx, state);

    vivid::PackageBrowserCallbacks callbacks;
    callbacks.refresh = [&ctx]() {
        ctx.pkg_catalog.refresh();
    };
    callbacks.list_entries = [&state]() {
        return state.entries_cache;
    };
    callbacks.fetch_state = [&ctx]() {
        switch (ctx.pkg_catalog.fetch_state()) {
            case vivid::CatalogFetchState::Idle:
                return vivid::PackageBrowserFetchState::Idle;
            case vivid::CatalogFetchState::Fetching:
                return vivid::PackageBrowserFetchState::Fetching;
            case vivid::CatalogFetchState::Ready:
                return vivid::PackageBrowserFetchState::Ready;
            case vivid::CatalogFetchState::Error:
                return vivid::PackageBrowserFetchState::Error;
        }
        return vivid::PackageBrowserFetchState::Error;
    };
    callbacks.fetch_error = [&ctx]() {
        return ctx.pkg_catalog.fetch_error();
    };
    callbacks.update_summary = [&ctx]() {
        auto s = ctx.pkg_catalog.summarize_updates(VIVID_CORE_VERSION);
        vivid::PackageBrowserUpdateSummary out;
        out.installed_packages = s.installed_packages;
        out.updates_available = s.updates_available;
        out.incompatible_updates = s.incompatible_updates;
        return out;
    };

    auto begin_action = [&state]() -> bool {
        std::lock_guard<std::mutex> lk(state.action_mutex);
        if (state.action_state == PackageBrowserState::ActionState::Running) return false;
        state.action_state = PackageBrowserState::ActionState::Running;
        state.action_error_msg.clear();
        state.action_needs_refresh = false;
        return true;
    };

    callbacks.install = [&ctx, &state, begin_action](const std::string& name, std::string&) -> bool {
        if (!begin_action()) return false;
        if (state.action_thread.joinable()) state.action_thread.join();
        state.action_thread = std::thread([&ctx, &state, name]() {
            auto r = ctx.pkg_catalog.install(name);
            std::lock_guard<std::mutex> lk(state.action_mutex);
            state.action_error_msg = r.success ? "" : r.error;
            state.action_needs_refresh = r.success;
            state.action_state = r.success ? PackageBrowserState::ActionState::Done
                                           : PackageBrowserState::ActionState::Error;
        });
        return true;
    };
    callbacks.uninstall = [&ctx, &state, begin_action](const std::string& name, std::string&) -> bool {
        if (!begin_action()) return false;
        if (state.action_thread.joinable()) state.action_thread.join();
        state.action_thread = std::thread([&ctx, &state, name]() {
            auto rm = ctx.pkg_catalog.uninstall(name);
            bool ok = rm.success;
            std::lock_guard<std::mutex> lk(state.action_mutex);
            state.action_error_msg = ok ? "" : (rm.error.empty() ? "Failed to uninstall " + name : rm.error);
            state.action_needs_refresh = ok;
            state.action_state = ok ? PackageBrowserState::ActionState::Done
                                    : PackageBrowserState::ActionState::Error;
        });
        return true;
    };
    callbacks.unlink = [&ctx, &state, begin_action](const std::string& name, std::string&) -> bool {
        if (!begin_action()) return false;
        if (state.action_thread.joinable()) state.action_thread.join();
        state.action_thread = std::thread([&ctx, &state, name]() {
            auto rm = ctx.pkg_manager.unlink(name);
            bool ok = rm.success;
            std::lock_guard<std::mutex> lk(state.action_mutex);
            state.action_error_msg = ok ? "" : (rm.error.empty() ? "Failed to unlink " + name : rm.error);
            state.action_needs_refresh = ok;
            state.action_state = ok ? PackageBrowserState::ActionState::Done
                                    : PackageBrowserState::ActionState::Error;
        });
        return true;
    };
    callbacks.link = [&ctx, &state, begin_action](const std::string& path, std::string&) -> bool {
        if (!begin_action()) return false;
        if (state.action_thread.joinable()) state.action_thread.join();
        state.action_thread = std::thread([&ctx, &state, path]() {
            auto r = ctx.pkg_manager.link(path);
            std::lock_guard<std::mutex> lk(state.action_mutex);
            state.action_error_msg = r.success ? "" : r.error;
            state.action_needs_refresh = r.success || !r.info.path.empty();
            state.action_state = r.success ? PackageBrowserState::ActionState::Done
                                           : PackageBrowserState::ActionState::Error;
        });
        return true;
    };
    callbacks.rebuild = [&ctx, &state, begin_action](const std::string& name, std::string&) -> bool {
        if (!begin_action()) return false;
        if (state.action_thread.joinable()) state.action_thread.join();
        state.action_thread = std::thread([&ctx, &state, name]() {
            auto r = ctx.pkg_manager.rebuild(name);
            std::lock_guard<std::mutex> lk(state.action_mutex);
            state.action_error_msg = r.success ? "" : r.error;
            state.action_needs_refresh = true;
            state.action_state = r.success ? PackageBrowserState::ActionState::Done
                                           : PackageBrowserState::ActionState::Error;
        });
        return true;
    };
    callbacks.open_build_console = [&ctx]() {
        if (!ctx.graph_ui.build_console_open()) ctx.graph_ui.toggle_build_console();
    };

    ctx.graph_ui.set_package_browser_callbacks(std::move(callbacks));

    if (ctx.registry.has_abi_mismatch_diagnostics()) {
        auto mismatches = ctx.registry.abi_mismatch_diagnostics();
        std::string msg = "Plugin ABI mismatch detected. Rebuild vivid and rerun package rebuild.";
        if (!mismatches.empty()) {
            msg += " First mismatch: ";
            msg += mismatches.front().plugin_name.empty()
                       ? mismatches.front().plugin_path
                       : mismatches.front().plugin_name;
            msg += " (plugin ABI " + std::to_string(mismatches.front().plugin_abi) +
                   ", runtime ABI " + std::to_string(mismatches.front().runtime_abi) + ")";
        }
        ctx.graph_ui.notify_pkg_action_complete(false, msg);
    }
}

void poll_package_browser_actions(MainAppContext& ctx,
                                  PackageBrowserState& state,
                                  bool graph_transaction_active) {
    bool done = false;
    bool needs_refresh = false;
    std::string err;
    {
        std::lock_guard<std::mutex> lk(state.action_mutex);
        if (!graph_transaction_active &&
            (state.action_state == PackageBrowserState::ActionState::Done ||
             state.action_state == PackageBrowserState::ActionState::Error)) {
            done = true;
            needs_refresh = state.action_needs_refresh;
            err = state.action_error_msg;
            state.action_state = PackageBrowserState::ActionState::Idle;
            state.action_needs_refresh = false;
        }
    }
    if (!done) return;

    if (needs_refresh) {
        refresh_discovered_examples(ctx);
        auto prepared = prepare_graph_operators_sync(ctx.registry, ctx.graph, true);
        if (!prepared.success) {
            err = prepared.user_message.empty()
                ? "Failed to prepare operators after package refresh"
                : prepared.user_message;
        } else if (rebuild_live_runtime_from_graph(ctx)) {
            if (auto* compiled = ctx.runtime.compiled_graph()) {
                std::unordered_set<std::string> active_ids;
                for (const auto& cn : compiled->nodes) {
                    active_ids.insert(cn.node_id);
                }
                ctx.thumb_cache.retain_only(active_ids);
            }
        }
    }

    refresh_package_browser_entries_cache(ctx, state);
    ctx.graph_ui.notify_pkg_action_complete(err.empty(), err);

    // If the failure was a missing-tool error, auto-open the system-
    // requirements dialog so the user has a clear next step. The package
    // manager always prefixes these with the same literal.
    if (!err.empty() &&
        err.find("Missing required build tool") != std::string::npos &&
        !ctx.graph_ui.system_requirements_open()) {
        ctx.graph_ui.open_system_requirements(
            /*auto_opened=*/true,
            "A required build tool is missing, so the package action could not complete. "
            "Install the missing tool(s) below, then try again.");
    }
}

} // namespace vivid::main_internal
