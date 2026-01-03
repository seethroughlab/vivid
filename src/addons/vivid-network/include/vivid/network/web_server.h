#pragma once

/**
 * @file web_server.h
 * @brief HTTP and WebSocket server for remote control
 *
 * Provides REST API for parameter control and WebSocket for real-time updates.
 * Can also serve static files (HTML/CSS/JS) for web-based control interfaces.
 */

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

/**
 * @brief HTTP and WebSocket server for remote control
 *
 * Provides a REST API for parameter control, WebSocket for real-time updates,
 * and static file serving for web-based control interfaces.
 *
 * @par Built-in API Endpoints
 * - `GET /api/operators` - List all operators
 * - `GET /api/operator/:id` - Get operator parameters
 * - `POST /api/operator/:id` - Set operator parameters
 * - `ws://localhost:PORT/ws` - WebSocket for real-time updates
 *
 * @par Example
 * @code
 * void setup(Context& ctx) {
 *     auto& chain = ctx.chain();
 *     chain.add<WebServer>("web");
 *     auto& web = chain.get<WebServer>("web");
 *     web.port(8080);
 *     web.staticDir("web/");  // Serve files from web/ directory
 * }
 * // Access at http://localhost:8080
 * @endcode
 *
 * @par Inputs
 * None (network server)
 *
 * @par Output
 * None (serves HTTP/WebSocket requests)
 */
class WebServer : public Operator {
public:
    WebServer();
    ~WebServer() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /// @brief Set listening port
    void port(int port);

    /// @brief Set bind address (default: 0.0.0.0)
    void host(const std::string& host);

    /// @brief Set directory for serving static files
    void staticDir(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Custom Routes
    /// @{

    /// @brief Handler function for custom routes
    using RouteHandler = std::function<std::string(const std::string& method,
                                                   const std::string& path,
                                                   const std::string& body)>;

    /// @brief Register a custom route handler
    void route(const std::string& path, RouteHandler handler);

    /// @}
    // -------------------------------------------------------------------------
    /// @name WebSocket
    /// @{

    /// @brief Broadcast message to all WebSocket clients
    void broadcast(const std::string& message);

    /// @brief Broadcast JSON message to all WebSocket clients
    void broadcastJson(const std::string& type, const std::string& data);

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Query
    /// @{

    /// @brief Check if server is running
    bool isRunning() const { return m_running; }

    /// @brief Get configured port
    int getPort() const { return m_port; }

    /// @brief Get number of connected WebSocket clients
    size_t connectionCount() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "WebServer"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    bool drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) override;

    /// @}

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
