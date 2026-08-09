#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace vivid::session { struct Session; }
namespace vivid { namespace ui { class NodeGraph; } class VisualGraph; struct App; struct Window; }
struct Transport;

namespace vivid {

// Pointers to the live app state the handlers act on. Window/split/dock are
// passed by pointer so get_session can read them and load_session can update them.
struct ControlCtx {
    vivid::session::Session*   session   = nullptr;
    vivid::ui::NodeGraph* graph     = nullptr;
    vivid::VisualGraph*   vgraph    = nullptr;
    Transport*            transport = nullptr;
    vivid::App*           app       = nullptr;   // for op registry + loaders (package install)
    vivid::Window*        window    = nullptr;   // ADR-0032 Phase C2: live Window for offline AV reactivity
    int*   win_w   = nullptr;
    int*   win_h   = nullptr;
    float* split_x = nullptr;
    float* dock_h  = nullptr;
};

// A tiny loopback HTTP control server (cpp-httplib) on a background thread. Each
// `POST /<method>` (JSON body) is queued; the main thread drains the queue once
// per frame via process_pending() and dispatches to a handler that mutates the
// live app — so all mutations happen on the UI thread (thread-safe). The MCP
// bridge (mcp/vivid_mcp.py) proxies tool calls to these endpoints.
class ControlServer {
public:
    ControlServer();
    ~ControlServer();
    bool start(int port);                          // bg thread on 127.0.0.1:port
    void stop();
    void process_pending(const ControlCtx& ctx);   // call once per frame (main thread)
    int  port() const { return port_; }
    bool running() const { return running_.load(); }   // bound + listening (health signal)

    // Called (from the HTTP thread) right after a request is queued, so the host can wake a
    // sleeping main loop to drain it promptly. Without it, a backgrounded app whose CFRunLoop
    // is App-Nap-throttled drains only on its next (coalesced, seconds-late) tick and requests
    // time out. Wire to glfwPostEmptyEvent in main. Must be thread-safe.
    void set_wake(std::function<void()> w) { wake_ = std::move(w); }

    // A method handler: (context, request-json) -> response-json. Public so the per-family
    // register_<family>_handlers() free functions (audit #7, in control_handlers*.cpp) can name it.
    using Handler = std::function<nlohmann::json(const ControlCtx&, const nlohmann::json&)>;

private:
    struct Pending { std::string method; nlohmann::json body; std::promise<nlohmann::json> reply; };
    void register_handlers();

    std::unordered_map<std::string, Handler> handlers_;
    std::function<void()> wake_;   // host hook: wake the main loop after enqueue (main-thread-safe)
    std::mutex          mtx_;
    std::deque<Pending> queue_;
    std::thread         thread_;
    void*               server_ = nullptr;   // httplib::Server* (opaque here)
    int                 port_ = 0;
    std::atomic<bool>   running_{false};
};

}  // namespace vivid
