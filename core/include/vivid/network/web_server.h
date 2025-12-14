// WebServer - HTTP and WebSocket server for remote control
//
// Provides REST API for parameter control and WebSocket for real-time updates.
// Can also serve static files (HTML/CSS/JS) for web-based control interfaces.
//
// Usage:
//   chain.add<WebServer>("web").port(8080).staticDir("web/");
//
//   // Access at http://localhost:8080
//   // REST API:
//   //   GET  /api/operators     - List all operators
//   //   GET  /api/operator/:id  - Get operator params
//   //   POST /api/operator/:id  - Set operator params
//   // WebSocket:
//   //   ws://localhost:8080/ws  - Real-time updates

#pragma once

#include <vivid/operator.h>
#include <string>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <map>

// Forward declarations
namespace ix {
    class HttpServer;
    class WebSocket;
    class ConnectionState;
    struct HttpRequest;
    struct HttpResponse;
}

namespace vivid::network {

class WebServer : public Operator {
public:
    WebServer();
    ~WebServer() override;

    // Configuration
    WebServer& port(int port);
    WebServer& host(const std::string& host);
    WebServer& staticDir(const std::string& path);

    // Custom route handlers
    using RouteHandler = std::function<std::string(const std::string& method,
                                                   const std::string& path,
                                                   const std::string& body)>;
    WebServer& route(const std::string& path, RouteHandler handler);

    // WebSocket broadcast
    void broadcast(const std::string& message);
    void broadcastJson(const std::string& type, const std::string& data);

    // Query state
    bool isRunning() const { return m_running; }
    int getPort() const { return m_port; }
    size_t connectionCount() const;

    // Operator interface
    std::string name() const override { return "WebServer"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

private:
    void startServer();
    void stopServer();

    std::string handleRequest(const std::string& method,
                             const std::string& uri,
                             const std::string& body);
    std::string serveStaticFile(const std::string& path);
    std::string getMimeType(const std::string& path);

    // Built-in API handlers
    std::string handleApiOperators();
    std::string handleApiOperator(const std::string& id, const std::string& method, const std::string& body);

    int m_port = 8080;
    std::string m_host = "0.0.0.0";
    std::string m_staticDir;

    std::unique_ptr<ix::HttpServer> m_server;
    bool m_running = false;

    // WebSocket clients
    std::set<ix::WebSocket*> m_wsClients;
    std::mutex m_wsMutex;

    // Custom routes
    std::map<std::string, RouteHandler> m_routes;

    // Pointer to context for API access
    Context* m_ctx = nullptr;
};

} // namespace vivid::network
