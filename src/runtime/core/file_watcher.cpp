#include "runtime/core/file_watcher.h"
#include <efsw/efsw.hpp>
#include <filesystem>
#include <cstdio>
#include <chrono>
#include <optional>

namespace vivid {

static constexpr int64_t kDebounceMs = 100;

namespace {

std::string normalize_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto normalized = std::filesystem::absolute(path, ec);
    if (ec)
        normalized = path;
    return normalized.lexically_normal().string();
}

bool is_under_root(const std::string& path, const std::string& root, std::filesystem::path& rel_out) {
    if (root.empty()) return false;
    std::error_code ec;
    rel_out = std::filesystem::relative(path, root, ec);
    if (ec || rel_out.empty()) return false;
    for (const auto& part : rel_out) {
        if (part == "..") return false;
    }
    return true;
}

std::optional<std::string> path_part(const std::filesystem::path& path, size_t index) {
    size_t i = 0;
    for (const auto& part : path) {
        if (i++ == index)
            return part.string();
    }
    return std::nullopt;
}

// Process-lifetime graveyard for retired Listener + SharedState objects.
//
// efsw's macOS FSEvents backend (WatcherFSEvents) delivers callbacks on a serial
// dispatch queue and does NOT drain that queue when the watcher is destroyed, so
// a callback block already in flight can run after FileWatcher::stop() tears the
// watcher down. Rather than free the Listener (which efsw still references) and
// the state it touches out from under that late callback, we retire them here so
// they outlive any in-flight callback; the callback finds active == false and
// no-ops. The retained objects are tiny (the maps are cleared on retire) and
// bounded by the number of start()/stop() cycles — negligible in practice.
std::mutex g_retired_mutex;
std::vector<std::shared_ptr<void>> g_retired;

void retire(std::shared_ptr<void> a, std::shared_ptr<void> b) {
    std::lock_guard<std::mutex> lock(g_retired_mutex);
    if (a) g_retired.push_back(std::move(a));
    if (b) g_retired.push_back(std::move(b));
}

} // namespace

std::string FileWatcher::SharedState::resolve_target_locked(const std::string& path) const {
    auto explicit_it = path_to_target.find(path);
    if (explicit_it != path_to_target.end())
        return explicit_it->second;

    const std::filesystem::path p(path);
    const auto ext = p.extension().string();
    std::filesystem::path rel;
    for (const auto& root : watch_roots) {
        if (!is_under_root(path, root.root, rel))
            continue;

        switch (root.kind) {
            case WatchRootKind::SeedOperators: {
                if (ext != ".cpp") break;
                auto op_name = path_part(rel, 1);
                if (op_name && !op_name->empty())
                    return *op_name;
                break;
            }
            case WatchRootKind::PackageOperators: {
                if (ext != ".cpp") break;
                auto op_name = path_part(rel, 1);
                if (op_name && !op_name->empty())
                    return "pkg:" + root.package_name + ":" + *op_name;
                break;
            }
            case WatchRootKind::ShaderDirectory:
                if (ext == ".wgsl")
                    return "shader:" + path;
                break;
        }
    }
    return {};
}

// --- Listener (efsw callback → pending queue) ---

class FileWatcher::Listener : public efsw::FileWatchListener {
public:
    explicit Listener(std::shared_ptr<SharedState> state) : state_(std::move(state)) {}

    void handleFileAction(efsw::WatchID, const std::string& dir,
                          const std::string& filename, efsw::Action action,
                          std::string) override {
        // The shared state outlives this object; bail out fast if the watcher has
        // been stopped (a late FSEvents callback after teardown — see retire()).
        std::shared_ptr<SharedState> st = state_;
        if (!st || !st->active.load(std::memory_order_acquire))
            return;

        if (action != efsw::Actions::Modified &&
            action != efsw::Actions::Add &&
            action != efsw::Actions::Moved &&
            action != efsw::Actions::Delete)
            return;

        std::string path = dir;
        if (!path.empty() && path.back() != '/')
            path += '/';
        path += filename;
        path = normalize_path(path);

        std::string target;
        {
            std::lock_guard<std::mutex> lock(st->watch_mutex);
            target = st->resolve_target_locked(path);
            if (target.empty()) return;

            // Debounce: skip if within 100ms of last event for same target
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            auto& last = st->last_event_time[target];
            if (now - last < kDebounceMs) return;
            last = now;
        }

        std::fprintf(stderr, "[vivid] File changed: %s (operator: %s)\n",
                     path.c_str(), target.c_str());

        {
            std::lock_guard<std::mutex> lock(st->queue_mutex);
            st->pending.push_back({path, target});
        }
    }

private:
    std::shared_ptr<SharedState> state_;
};

// --- FileWatcher ---

FileWatcher::FileWatcher() = default;

FileWatcher::~FileWatcher() {
    stop();
}

void FileWatcher::ensure_dir_watched(const std::string& dir, bool recursive) {
    if (!watcher_) return;
    std::string normalized = normalize_path(dir);
    auto it = watched_dirs_.find(normalized);
    if (it != watched_dirs_.end() && (it->second || !recursive))
        return;
    watched_dirs_[normalized] = watched_dirs_[normalized] || recursive;
    watcher_->addWatch(normalized, listener_.get(), recursive);
}

bool FileWatcher::start(const std::string& operators_dir) {
    if (watcher_) return false;

    operators_dir_ = normalize_path(operators_dir);
    state_ = std::make_shared<SharedState>();
    listener_ = std::make_shared<Listener>(state_);
    watcher_ = std::make_unique<efsw::FileWatcher>();
    watcher_->followSymlinks(true);

    namespace fs = std::filesystem;
    if (!fs::exists(operators_dir_)) {
        std::fprintf(stderr, "[vivid] FileWatcher: cannot open %s\n", operators_dir.c_str());
        watcher_.reset();
        listener_.reset();
        state_.reset();
        return false;
    }

    int count = 0;
    std::error_code ec;
    for (auto& domain_entry : fs::directory_iterator(operators_dir_, ec)) {
        if (ec || !domain_entry.is_directory()) { ec.clear(); continue; }

        for (auto& op_entry : fs::directory_iterator(domain_entry.path(), ec)) {
            if (ec || !op_entry.is_directory()) { ec.clear(); continue; }

            std::string target_name = op_entry.path().filename().string();
            std::string op_dir = normalize_path(op_entry.path());
            for (auto& file_entry : fs::directory_iterator(op_entry.path(), ec)) {
                if (ec) { ec.clear(); continue; }
                if (!file_entry.is_regular_file()) continue;
                if (file_entry.path().extension() != ".cpp") continue;

                std::lock_guard<std::mutex> lock(state_->watch_mutex);
                state_->path_to_target[normalize_path(file_entry.path())] = target_name;
                ensure_dir_watched(op_dir, false);
                count++;
            }
        }
    }

    if (count == 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: no .cpp files found under %s\n",
                     operators_dir.c_str());
        watcher_.reset();
        listener_.reset();
        state_.reset();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(state_->watch_mutex);
        state_->watch_roots.push_back({WatchRootKind::SeedOperators, operators_dir_, {}});
    }
    watcher_->watch();
    std::fprintf(stderr, "[vivid] FileWatcher: watching %d files\n", count);
    return true;
}

void FileWatcher::stop() {
    // Deactivate first so a late callback no-ops, then tear efsw down (which may
    // still fire one in-flight callback on its undrained dispatch queue).
    if (state_)
        state_->active.store(false, std::memory_order_release);
    watcher_.reset();

    // Free the heavy maps now that the watcher is gone (a late callback already
    // saw active == false; one mid-acquire serializes on watch_mutex and then
    // resolves nothing).
    if (state_) {
        std::lock_guard<std::mutex> lock(state_->watch_mutex);
        state_->path_to_target.clear();
        state_->watch_roots.clear();
        state_->last_event_time.clear();
    }

    // Retire the listener + state so a post-teardown callback (efsw doesn't drain
    // its FSEvents dispatch queue) hits live, inert memory instead of a UAF.
    retire(std::move(listener_), std::move(state_));
    listener_.reset();  // moved-from; make the null explicit
    state_.reset();

    watched_dirs_.clear();
    operators_dir_.clear();
}

std::vector<FileChangeEvent> FileWatcher::poll_changes() {
    if (!state_) return {};
    std::lock_guard<std::mutex> lock(state_->queue_mutex);
    std::vector<FileChangeEvent> result;
    result.swap(state_->pending);
    return result;
}

bool FileWatcher::add_watch(const std::string& path, const std::string& target_name) {
    namespace fs = std::filesystem;
    if (!fs::exists(path)) return false;
    if (!state_) return false;

    std::string abs = normalize_path(path);
    std::string dir = fs::path(abs).parent_path().string();
    std::string stored_target = target_name;
    if (stored_target.rfind("shader:", 0) == 0)
        stored_target = "shader:" + abs;

    std::lock_guard<std::mutex> lock(state_->watch_mutex);
    state_->path_to_target[abs] = stored_target;
    if (watcher_) ensure_dir_watched(dir, false);
    return true;
}

int FileWatcher::add_package_watches(const std::string& packages_dir) {
    namespace fs = std::filesystem;
    if (!fs::exists(packages_dir) || !state_) return 0;

    int count = 0;
    std::error_code ec;
    for (auto& pkg_entry : fs::directory_iterator(packages_dir, ec)) {
        if (ec) { ec.clear(); continue; }
        if (!pkg_entry.is_directory()) continue;

        fs::path ops_dir = pkg_entry.path() / "operators";
        if (!fs::exists(ops_dir)) continue;

        std::string pkg_name = pkg_entry.path().filename().string();
        std::string normalized_ops_dir = normalize_path(ops_dir);
        bool added_package_root = false;

        std::error_code ec2;
        for (auto& category_entry : fs::directory_iterator(ops_dir, ec2)) {
            if (ec2) { ec2.clear(); continue; }
            if (!category_entry.is_directory()) continue;

            std::error_code ec3;
            for (auto& op_entry : fs::directory_iterator(category_entry.path(), ec3)) {
                if (ec3) { ec3.clear(); continue; }
                if (!op_entry.is_directory()) continue;

                std::string op_name = op_entry.path().filename().string();
                std::string target = "pkg:" + pkg_name + ":" + op_name;
                std::string op_dir_str = normalize_path(op_entry.path());

                std::error_code ec4;
                for (auto& file_entry : fs::directory_iterator(op_entry.path(), ec4)) {
                    if (ec4) { ec4.clear(); continue; }
                    if (!file_entry.is_regular_file()) continue;
                    std::string fname = file_entry.path().filename().string();
                    if (fname.size() < 5 || fname.substr(fname.size() - 4) != ".cpp") continue;

                    std::lock_guard<std::mutex> lock(state_->watch_mutex);
                    state_->path_to_target[normalize_path(file_entry.path())] = target;
                    if (!added_package_root) {
                        state_->watch_roots.push_back({WatchRootKind::PackageOperators, normalized_ops_dir, pkg_name});
                        added_package_root = true;
                    }
                    ensure_dir_watched(op_dir_str, false);
                    count++;
                }
            }
        }
    }

    if (count > 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: watching %d package files\n", count);
    }
    return count;
}

int FileWatcher::add_shader_operator_watches(const std::string& directory) {
    namespace fs = std::filesystem;
    if (directory.empty() || !fs::exists(directory) || !state_) return 0;

    std::string normalized_dir = normalize_path(directory);
    int count = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(normalized_dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".wgsl") continue;

        std::string abs = normalize_path(entry.path());
        std::string target = "shader:" + abs;

        std::lock_guard<std::mutex> lock(state_->watch_mutex);
        state_->path_to_target[abs] = target;
        count++;
    }

    if (count > 0) {
        std::lock_guard<std::mutex> lock(state_->watch_mutex);
        state_->watch_roots.push_back({WatchRootKind::ShaderDirectory, normalized_dir, {}});
        ensure_dir_watched(normalized_dir, false);
    }
    if (count > 0) {
        std::fprintf(stderr, "[vivid] FileWatcher: watching %d shader files in %s\n",
                     count, directory.c_str());
    }
    return count;
}

} // namespace vivid
