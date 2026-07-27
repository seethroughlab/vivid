#include "packages/hot_reload_manager.h"

#include "packages/package_manager.h"   // user_operators_dir
#include "app/log.h"                     // ADR-0020: route compile output to the logger
#include "gpu/operator_loader.h"
#include "gpu/op_runtime.h"
#include "gpu/visual_graph.h"

#include <filesystem>
#include <cstdio>
#include <string>

namespace vivid {

namespace {
// The first line of a (possibly multi-line) compiler blob — the actionable bit for a toast/summary.
std::string first_line(const std::string& s) {
    const auto n = s.find('\n');
    return n == std::string::npos ? s : s.substr(0, n);
}
}  // namespace

void HotReloadManager::start(OpRegistry* registry, VisualGraph* vgraph, Logger* log) {
    if (active_) return;
    registry_ = registry;
    vgraph_   = vgraph;
    log_      = log;
    reloader_.start([this](const std::string& target) { return compile(target); });
    active_ = true;
}

void HotReloadManager::stop() {
    if (!active_) return;
    reloader_.stop();
    active_ = false;
}

void HotReloadManager::watch_op(const std::string& op_name, const std::string& package_dir,
                                const PackageOperator& op, const std::string& source_path,
                                OperatorLoader* loader) {
    // Never hot-swap a compiled AUDIO operator from the watcher: tick() would release + dlclose its
    // dylib while the RT audio thread may be mid-process_audio through that same loader (a
    // use-after-unload). The swap path only coordinates with the VISUAL graph, not the audio graph, so
    // audio ops are excluded until an audio-thread-safe swap exists (edit → reload the project instead).
    if (loader && loader->descriptor() && loader->descriptor()->has_process_audio) {
        if (log_) log_->log(LogLevel::Info, "not hot-watching audio operator '%s' (reload to apply)", op_name.c_str());
        else std::fprintf(stderr, "[vivid] hot-reload: skipping audio operator '%s' (reload to apply)\n", op_name.c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& w : watched_)
            if (w.op_name == op_name) { w = { op_name, package_dir, source_path, op, loader }; goto watched; }
        watched_.push_back({ op_name, package_dir, source_path, op, loader });
    }
watched:
    watcher_.watch(source_path, op_name);
    if (log_) log_->log(LogLevel::Info, "watching '%s' for edits (%s)", op_name.c_str(), source_path.c_str());
    else std::fprintf(stderr, "[vivid] hot-reload watching '%s' (%s)\n", op_name.c_str(), source_path.c_str());
}

std::string HotReloadManager::source_for(const std::string& op_name) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto& w : watched_) if (w.op_name == op_name) return w.source_path;
    return {};
}

void HotReloadManager::watch_manifest(const std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                                      const PackageManifest& mf) {
    if (!mf.ok) return;
    namespace fs = std::filesystem;
    for (const auto& op : mf.operators) {
        OperatorLoader* loader = nullptr;
        for (const auto& l : loaders)
            if (l->descriptor() && op.name == l->descriptor()->name) { loader = l.get(); break; }
        if (!loader) continue;   // not loaded (never compiled) — nothing to hot-swap
        watch_op(op.name, mf.dir, op, (fs::path(mf.dir) / op.source).string(), loader);
    }
}

ReloadResult HotReloadManager::compile(const std::string& op_name) {
    Watched w;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        bool found = false;
        for (const auto& e : watched_) if (e.op_name == op_name) { w = e; found = true; break; }
        if (!found) return ReloadResult{ op_name, "", "not watched", false };
    }
    // Compile into a unique staged dir so dlopen loads a fresh image (dyld caches by
    // path — reusing the same path would return the OLD dylib).
    const uint32_t c = ++counter_;
    const std::string staged = (std::filesystem::path(user_operators_dir()) / ".hot" /
                                std::to_string(c)).string();
    PackageCompileResult cr = compiler_.compile_operator(w.package_dir, w.op, staged);
    return ReloadResult{ op_name, cr.dylib_path, cr.error_output, cr.success };
}

void HotReloadManager::tick() {
    if (!active_) return;

    for (const auto& target : watcher_.poll_changes())
        reloader_.queue_rebuild(target);

    for (const auto& r : reloader_.poll_ready()) {
        if (!r.success) {
            // ADR-0020: the compile error is now VISIBLE — an Error summary rides a toast + the log
            // view, and the full compiler output goes to the log (and stderr) for the detail.
            if (log_) {
                log_->log(LogLevel::Error, "compile failed: '%s' — %s", r.target.c_str(), first_line(r.error).c_str());
                if (r.error.find('\n') != std::string::npos) log_->log(LogLevel::Debug, "%s", r.error.c_str());
            } else {
                std::fprintf(stderr, "[vivid] hot-reload compile failed for '%s':\n%s\n", r.target.c_str(), r.error.c_str());
            }
            registry_->set_reload_error(r.target, first_line(r.error));   // ADR-0020 V2: badge the node
            continue;
        }
        OperatorLoader* loader = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const auto& w : watched_) if (w.op_name == r.target) { loader = w.loader; break; }
        }
        if (!loader) continue;

        // ADR-0020 rollback-first: VALIDATE the candidate before touching the running graph. Only
        // once we know the new dylib loads (ABI/symbols/descriptor/compat all pass) do we destroy
        // the old instances (against the still-loaded old dylib), commit the swap, and rebuild
        // (params preserved). A rejected candidate leaves the running graph EXACTLY as it was — no
        // destroy/rebuild churn — and the last-good operator keeps running.
        if (!loader->validate(r.dylib_path.c_str())) {
            const std::string msg = "reload rejected: '" + r.target + "' — " + loader->last_error().message
                                  + " (kept previous, unchanged)";
            if (log_) log_->log(LogLevel::Error, "%s", msg.c_str());
            else std::fprintf(stderr, "[vivid] hot-reload %s\n", msg.c_str());
            registry_->set_reload_error(r.target, loader->last_error().message);   // ADR-0020 V2: badge the node
            continue;
        }
        vgraph_->release_op_instances(r.target);
        const bool ok = loader->load(r.dylib_path.c_str());   // just validated — expected to succeed
        // Drop the cached descriptor BEFORE rebuilding. It was built from the old dylib, which load()
        // has now unloaded, so its char* fields (param names, choice labels) dangle — rebuild ->
        // create -> validate_descriptor would read freed memory (a use-after-unload SIGSEGV). The
        // next descriptor_for() rebuilds it from the new dylib.
        if (ok) registry_->invalidate_descriptor(r.target);
        const int rebuilt = vgraph_->rebuild_op_instances(r.target);
        if (ok) {
            registry_->set_reload_error(r.target, "");   // ADR-0020 V2: clear the node badge on success
            if (log_) log_->log(LogLevel::Info, "reloaded '%s' (%d node(s) swapped)", r.target.c_str(), rebuilt);
            else std::fprintf(stderr, "[vivid] hot-reloaded '%s' (%d node(s) swapped)\n", r.target.c_str(), rebuilt);
        } else {
            // Should be unreachable (validate passed); instances rebuilt from the unchanged old dylib.
            if (log_) log_->log(LogLevel::Error, "reload of '%s' failed post-validate: %s",
                                r.target.c_str(), loader->last_error().message.c_str());
            else std::fprintf(stderr, "[vivid] hot-reload of '%s' load failed post-validate: %s (rebuilt %d)\n",
                              r.target.c_str(), loader->last_error().message.c_str(), rebuilt);
        }
    }
}

}  // namespace vivid
