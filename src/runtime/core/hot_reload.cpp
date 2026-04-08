#include "runtime/core/hot_reload.h"
#include "runtime/core/build_console.h"
#include "runtime/core/tool_discovery.h"
#include "runtime/platform/platform.h"
#include "runtime/platform/process_runner.h"
#include <cstdio>
#include <sys/stat.h>
#include <unistd.h>
#include <filesystem>
#include <stdexcept>

namespace vivid {

HotReloader::HotReloader() = default;

HotReloader::~HotReloader() {
    stop();
}

bool HotReloader::start(const std::string& build_dir) {
    if (thread_.joinable()) return false;

    build_dir_ = build_dir;
    staging_dir_ = build_dir + "/.hot_reload";

    // Create staging directory
    std::error_code ec;
    std::filesystem::create_directories(staging_dir_, ec);
    if (ec) {
        std::fprintf(stderr, "[vivid] HotReloader: cannot create staging dir %s: %s\n",
                     staging_dir_.c_str(), ec.message().c_str());
        return false;
    }

    running_ = true;
    thread_ = std::thread(&HotReloader::compile_thread, this);
    return true;
}

void HotReloader::stop() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        running_ = false;
        build_queue_.clear();
        queued_targets_.clear();
        deferred_targets_.clear();
    }
    queue_cv_.notify_one();
    if (thread_.joinable())
        thread_.join();
}

void HotReloader::queue_rebuild(const std::string& target_name) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (!running_) return;

    // If currently compiling, remember one deferred compile pass.
    if (in_flight_targets_.find(target_name) != in_flight_targets_.end()) {
        deferred_targets_.insert(target_name);
        return;
    }

    // Deduplicate queued work for this target.
    if (queued_targets_.find(target_name) != queued_targets_.end()) return;

    build_queue_.push_back(target_name);
    queued_targets_.insert(target_name);
    queue_cv_.notify_one();
}

void HotReloader::set_package_compiler(PackageCompileFn fn) {
    package_compile_fn_ = std::move(fn);
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
            queued_targets_.erase(target);
            in_flight_targets_.insert(target);
        }

        std::fprintf(stderr, "[vivid] Hot-reload: compiling %s...\n", target.c_str());
        BuildTaskId task_id = 0;
        if (target.rfind("pkg:", 0) != 0 && build_console_) {
            task_id = build_console_->begin_task(BuildTaskKind::HotReload, target);
        }

        ReloadResult result;
        try {
        // Package targets use format "pkg:<package>:<operator>" — route to PackageCompiler.
        if (target.substr(0, 4) == "pkg:" && package_compile_fn_) {
            result = package_compile_fn_(target);
            result.target_name = target;
        } else {
            // Resolve cmake path before running build.
            std::string cmake_exe = find_tool("cmake");
            if (cmake_exe.empty()) {
                result.target_name = target;
                result.success = false;
                result.error_output = missing_tool_error("cmake");
                std::fprintf(stderr, "[vivid] Hot-reload: %s\n", result.error_output.c_str());
                if (build_console_) {
                    build_console_->append_system_line(task_id, result.error_output);
                    build_console_->finish_task(task_id, BuildTaskState::Failed, "missing cmake");
                }
            } else {
            // Run cmake --build and capture output.
            ProcessRunOptions build_opts;
            build_opts.argv = {cmake_exe, "--build", build_dir_, "--target", target};

            ProcessRunResult build_result;
            if (build_console_) {
                build_result = run_build_process(build_opts, *build_console_, task_id,
                                                 BuildConsoleStreamKind::Stdout);
            } else {
                build_result = run_process(build_opts);
            }

            bool compile_ok = build_result.launched && build_result.exit_code == 0;
            if (!compile_ok && build_console_) {
                if (!build_result.launched)
                    build_console_->finish_task(task_id, BuildTaskState::Failed, "launch failed");
                else
                    build_console_->finish_task(task_id, BuildTaskState::Failed,
                                                "failed (exit " + std::to_string(build_result.exit_code) + ")");
            }

            result.target_name = target;
            if (!compile_ok) {
                result.success = false;
                result.error_output = build_result.launched ? build_result.output : build_result.error;
                std::fprintf(stderr, "[vivid] Hot-reload: compile FAILED for %s:\n%s",
                    target.c_str(), result.error_output.c_str());
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
                    if (build_console_) {
                        build_console_->append_system_line(task_id, result.error_output);
                        build_console_->finish_task(task_id, BuildTaskState::Failed, "staging failed");
                    }
                } else {
                    result.success = true;
                    result.staged_dylib_path = staged_path;
                    std::fprintf(stderr, "[vivid] Hot-reload: %s compiled successfully\n", target.c_str());
                    if (build_console_)
                        build_console_->finish_task(task_id, BuildTaskState::Succeeded, "succeeded");
                }
            }
            } // cmake found
        }
        } catch (const std::exception& e) {
            result.success = false;
            result.error_output = std::string("Internal error: ") + e.what();
            result.target_name = target;
            std::fprintf(stderr, "[vivid] Hot-reload: exception in compile_thread for %s: %s\n",
                         target.c_str(), e.what());
            if (build_console_) {
                build_console_->append_system_line(task_id, result.error_output);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "internal error");
            }
        } catch (...) {
            result.success = false;
            result.error_output = "Internal error: unknown exception";
            result.target_name = target;
            std::fprintf(stderr, "[vivid] Hot-reload: unknown exception in compile_thread for %s\n",
                         target.c_str());
            if (build_console_) {
                build_console_->append_system_line(task_id, result.error_output);
                build_console_->finish_task(task_id, BuildTaskState::Failed, "internal error");
            }
        }

        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            results_.push_back(std::move(result));
        }

        bool queue_deferred = false;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            in_flight_targets_.erase(target);
            if (deferred_targets_.erase(target) > 0 && running_ &&
                queued_targets_.find(target) == queued_targets_.end()) {
                build_queue_.push_back(target);
                queued_targets_.insert(target);
                queue_deferred = true;
            }
        }
        if (queue_deferred) queue_cv_.notify_one();
    }
}

} // namespace vivid
