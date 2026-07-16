#pragma once

#include "packages/file_watcher.h"
#include "packages/hot_reload.h"
#include "packages/package_manifest.h"
#include "packages/package_compiler.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Ties the file watcher + background compiler to the live operator swap. Watches an
// installed operator's source file; on edit, recompiles it off the main thread; when
// ready, hot-swaps the dylib on the main thread (release node instances → loader.load
// → rebuild), preserving node params. Opt-in / dev-only.
namespace vivid {

class OpRegistry;
class OperatorLoader;
class VisualGraph;
class Logger;

class HotReloadManager {
public:
    void start(OpRegistry* registry, VisualGraph* vgraph, Logger* log = nullptr);
    void stop();
    bool active() const { return active_; }

    // Watch one installed operator for source edits. `loader` is the live loader for
    // this op (owned by App). source_path/package_dir/op describe how to recompile.
    void watch_op(const std::string& op_name, const std::string& package_dir,
                  const PackageOperator& op, const std::string& source_path,
                  OperatorLoader* loader);

    // ADR-0020: watch every operator in a package manifest — find each op's live loader (by
    // descriptor name) among `loaders` and watch its source. Used at startup (auto-discovery of the
    // clones / installed dirs) and right after a clone, so a fresh operator reloads without a restart.
    // An op with no matching loaded loader is skipped (nothing to hot-swap yet).
    void watch_manifest(const std::vector<std::unique_ptr<OperatorLoader>>& loaders,
                        const PackageManifest& mf);

    // Once per frame (main thread): poll the watcher, then apply any ready swaps.
    void tick();

private:
    struct Watched {
        std::string     op_name, package_dir, source_path;
        PackageOperator op;
        OperatorLoader* loader = nullptr;
    };
    ReloadResult compile(const std::string& op_name);   // runs on the compile thread

    OpRegistry*          registry_ = nullptr;
    VisualGraph*         vgraph_   = nullptr;
    Logger*              log_      = nullptr;   // ADR-0020: compile output → log view + toasts
    FileWatcher          watcher_;
    HotReloader          reloader_;
    PackageCompiler      compiler_;
    std::mutex           mtx_;            // guards watched_ (main + compile thread)
    std::vector<Watched> watched_;
    std::atomic<uint32_t> counter_{0};    // unique staged-dylib dirs per reload
    bool                 active_ = false;
};

}  // namespace vivid
