#pragma once

#include <memory>
#include <string>

namespace vivid {

class RuntimeAPI;
class Graph;
class Scheduler;
class OperatorRegistry;
class HotReloader;
class CaptureCoordinator;
class PackageManager;

class ControlServer {
public:
    ControlServer();
    ~ControlServer();

    // Non-copyable, non-movable (pimpl with running threads)
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(int port = 9876);
    void stop();

    // Set context needed by scaffold_operator (call before main loop)
    void set_src_dir(const std::string& src_dir);
    void set_hot_reloader(HotReloader* hr);
    void set_capture_coordinator(CaptureCoordinator* cc);
    void set_package_manager(PackageManager* pm);

    // Call from main loop each frame. Drains pending HTTP requests,
    // dispatches commands against the runtime, and signals responses.
    // has_gpu_ops/has_audio are updated by reload commands.
    void process_requests(RuntimeAPI& api, Graph& graph,
                          Scheduler& scheduler,
                          OperatorRegistry& registry,
                          bool& has_gpu_ops, bool& has_audio);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string src_dir_;
    HotReloader* hot_reloader_ = nullptr;
    CaptureCoordinator* capture_coordinator_ = nullptr;
    PackageManager* package_manager_ = nullptr;
};

} // namespace vivid
