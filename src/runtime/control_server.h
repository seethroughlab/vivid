#ifndef VIVID_RUNTIME_CONTROL_SERVER_H
#define VIVID_RUNTIME_CONTROL_SERVER_H

#include <memory>

namespace vivid {

class RuntimeAPI;
class Graph;
class Scheduler;
class OperatorRegistry;

class ControlServer {
public:
    ControlServer();
    ~ControlServer();

    // Non-copyable, non-movable (pimpl with running threads)
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    bool start(int port = 9876);
    void stop();

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
};

} // namespace vivid

#endif // VIVID_RUNTIME_CONTROL_SERVER_H
