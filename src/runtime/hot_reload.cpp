#include "runtime/hot_reload.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <array>
#include <filesystem>

namespace vivid {

// Platform-specific shared library suffix
#if defined(__APPLE__)
static constexpr const char* kPluginSuffix = ".dylib";
#elif defined(_WIN32)
static constexpr const char* kPluginSuffix = ".dll";
#else
static constexpr const char* kPluginSuffix = ".so";
#endif

HotReloader::HotReloader() = default;

HotReloader::~HotReloader() {
    stop();
}

void HotReloader::start(const std::string& build_dir) {
    build_dir_ = build_dir;
    staging_dir_ = build_dir + "/.hot_reload";

    // Create staging directory
    std::filesystem::create_directories(staging_dir_);

    running_ = true;
    thread_ = std::thread(&HotReloader::compile_thread, this);
}

void HotReloader::stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        running_ = false;
    }
    queue_cv_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void HotReloader::queue_rebuild(const std::string& target_name) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    // Deduplicate: if this target is already in the queue, skip
    for (const auto& queued : build_queue_) {
        if (queued == target_name) return;
    }

    build_queue_.push_back(target_name);
    queue_cv_.notify_one();
}

std::vector<ReloadResult> HotReloader::poll_ready() {
    std::lock_guard<std::mutex> lock(result_mutex_);
    std::vector<ReloadResult> out;
    out.swap(results_);
    return out;
}

void HotReloader::compile_thread() {
    while (true) {
        std::string target;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [&] { return !build_queue_.empty() || !running_; });
            if (!running_ && build_queue_.empty()) break;
            if (build_queue_.empty()) continue;
            target = build_queue_.front();
            build_queue_.pop_front();
        }

        std::fprintf(stderr, "[vivid] Hot-reload: compiling %s...\n", target.c_str());

        // Run cmake --build and capture output
        std::string cmd = "cmake --build " + build_dir_ + " --target " + target + " 2>&1";
        std::string output;
        bool compile_ok = false;

        FILE* pipe = popen(cmd.c_str(), "r");
        if (pipe) {
            std::array<char, 256> buf;
            while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
                output += buf.data();
            }
            int status = pclose(pipe);
            compile_ok = (status == 0);
        }

        ReloadResult result;
        result.target_name = target;

        if (!compile_ok) {
            result.success = false;
            result.error_output = output;
            std::fprintf(stderr, "[vivid] Hot-reload: compile FAILED for %s:\n%s",
                target.c_str(), output.c_str());
        } else {
            // Get unique counter for staging path
            uint32_t counter;
            {
                std::lock_guard<std::mutex> lock(counter_mutex_);
                counter = reload_counters_[target]++;
            }

            // Clean up previous staging file for this target
            if (counter > 0) {
                std::string prev = staging_dir_ + "/" + target + "_" +
                    std::to_string(counter - 1) + kPluginSuffix;
                std::filesystem::remove(prev);
            }

            // Copy rebuilt plugin to staging directory with unique name
            std::string src_path = build_dir_ + "/" + target + kPluginSuffix;
            std::string staged_path = staging_dir_ + "/" + target + "_" +
                std::to_string(counter) + kPluginSuffix;

            std::error_code ec;
            std::filesystem::copy_file(src_path, staged_path,
                std::filesystem::copy_options::overwrite_existing, ec);

            if (ec) {
                result.success = false;
                result.error_output = "Failed to stage dylib: " + ec.message();
                std::fprintf(stderr, "[vivid] Hot-reload: staging failed for %s: %s\n",
                    target.c_str(), ec.message().c_str());
            } else {
                result.success = true;
                result.staged_dylib_path = staged_path;
                std::fprintf(stderr, "[vivid] Hot-reload: %s compiled successfully\n", target.c_str());
            }
        }

        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            results_.push_back(std::move(result));
        }
    }
}

} // namespace vivid
