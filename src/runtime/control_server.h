#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace vivid {

class RuntimeAPI;
class Graph;
class RuntimeCore;
class OperatorRegistry;
class HotReloader;
class CaptureCoordinator;
class PackageManager;
class PackageCompiler;
class PackageCatalog;
class AudioEngine;
class AppUpdateManager;
struct Settings;

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
    void set_package_compiler(PackageCompiler* pc);
    void set_package_catalog(PackageCatalog* cat);
    void set_app_update_manager(AppUpdateManager* aum);
    void set_settings(Settings* settings);
    void set_audio_engine(AudioEngine* ae);

    // Returns the wall-clock ms timestamp of the last /mcp_ping from a given
    // server name ("vivid" or "opdev").  Returns 0 if never pinged.
    uint64_t mcp_last_ping_ms(const std::string& name) const;

    // Call from main loop each frame. Drains pending HTTP requests,
    // dispatches commands against the runtime, and signals responses.
    // has_gpu_ops/has_audio are updated by reload commands.
    void process_requests(RuntimeAPI& api, Graph& graph,
                          RuntimeCore& core,
                          OperatorRegistry& registry,
                          bool& has_gpu_ops, bool& has_audio);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // MCP ping timestamps — updated from the HTTP thread, read from the main thread.
    mutable std::mutex mcp_ping_mutex_;
    std::unordered_map<std::string, uint64_t> mcp_last_ping_ms_;

    std::string src_dir_;
    HotReloader* hot_reloader_ = nullptr;
    CaptureCoordinator* capture_coordinator_ = nullptr;
    PackageManager* package_manager_ = nullptr;
    PackageCompiler* package_compiler_ = nullptr;
    PackageCatalog* package_catalog_ = nullptr;
    AppUpdateManager* app_update_manager_ = nullptr;
    Settings* settings_ = nullptr;
    AudioEngine* audio_engine_ = nullptr;
};

} // namespace vivid
