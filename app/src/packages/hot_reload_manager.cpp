#include "packages/hot_reload_manager.h"

#include "packages/package_manager.h"   // user_operators_dir
#include "gpu/operator_loader.h"
#include "gpu/op_runtime.h"
#include "gpu/visual_graph.h"

#include <filesystem>
#include <cstdio>

namespace vivid {

void HotReloadManager::start(OpRegistry* registry, VisualGraph* vgraph) {
    if (active_) return;
    registry_ = registry;
    vgraph_   = vgraph;
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
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& w : watched_)
            if (w.op_name == op_name) { w = { op_name, package_dir, source_path, op, loader }; goto watched; }
        watched_.push_back({ op_name, package_dir, source_path, op, loader });
    }
watched:
    watcher_.watch(source_path, op_name);
    std::fprintf(stderr, "[vivid] hot-reload watching '%s' (%s)\n",
                 op_name.c_str(), source_path.c_str());
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
            std::fprintf(stderr, "[vivid] hot-reload compile failed for '%s':\n%s\n",
                         r.target.c_str(), r.error.c_str());
            continue;
        }
        OperatorLoader* loader = nullptr;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (const auto& w : watched_) if (w.op_name == r.target) { loader = w.loader; break; }
        }
        if (!loader) continue;

        // Swap on the main thread: destroy old node instances (against the still-loaded
        // old dylib), load the new dylib, recreate instances (params preserved).
        const int released = vgraph_->release_op_instances(r.target);
        const bool ok = loader->load(r.dylib_path.c_str());
        const int rebuilt = vgraph_->rebuild_op_instances(r.target);
        if (ok) {
            registry_->invalidate_descriptor(r.target);
            std::fprintf(stderr, "[vivid] hot-reloaded '%s' (%d node(s) swapped)\n",
                         r.target.c_str(), rebuilt);
        } else {
            // Rejected (incompatible) or load error — instances were rebuilt from the
            // unchanged old dylib, so the graph stays valid.
            std::fprintf(stderr, "[vivid] hot-reload of '%s' rejected: %s (kept previous; "
                         "released %d / rebuilt %d)\n", r.target.c_str(),
                         loader->last_error().message.c_str(), released, rebuilt);
        }
    }
}

}  // namespace vivid
