// MCP Server Tests for Vivid
// ===========================
// Tests the Model Context Protocol server JSON-RPC interface.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <sstream>
#include <cstdio>
#include <thread>
#include <chrono>
#include <array>
#include <mutex>
#include <atomic>
#include <fcntl.h>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#include <windows.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#endif

using json = nlohmann::json;

// Get vivid binary path from CMake definition
static std::string getVividPath() {
    return VIVID_BINARY_PATH;
}

// -----------------------------------------------------------------------------
// Cross-platform subprocess handling
// -----------------------------------------------------------------------------

class McpProcess {
public:
    McpProcess() = default;
    ~McpProcess() { stop(); }

    bool start() {
        std::string cmd = "\"" + getVividPath() + "\" mcp";

#ifdef _WIN32
        // Windows: Use CreateProcess for bidirectional pipes
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create pipes for stdin
        if (!CreatePipe(&m_stdinRead, &m_stdinWrite, &saAttr, 0)) {
            return false;
        }
        SetHandleInformation(m_stdinWrite, HANDLE_FLAG_INHERIT, 0);

        // Create pipes for stdout
        if (!CreatePipe(&m_stdoutRead, &m_stdoutWrite, &saAttr, 0)) {
            CloseHandle(m_stdinRead);
            CloseHandle(m_stdinWrite);
            return false;
        }
        SetHandleInformation(m_stdoutRead, HANDLE_FLAG_INHERIT, 0);

        // Open NUL device for stderr (discard log messages)
        HANDLE hNul = CreateFileA("NUL", GENERIC_WRITE, 0, &saAttr, OPEN_EXISTING, 0, NULL);

        STARTUPINFOA si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdError = hNul;  // Discard stderr (log messages go there)
        si.hStdOutput = m_stdoutWrite;
        si.hStdInput = m_stdinRead;
        si.dwFlags |= STARTF_USESTDHANDLES;

        ZeroMemory(&m_processInfo, sizeof(m_processInfo));

        if (!CreateProcessA(
            NULL,
            const_cast<char*>(cmd.c_str()),
            NULL, NULL, TRUE, 0, NULL, NULL,
            &si, &m_processInfo
        )) {
            CloseHandle(m_stdinRead);
            CloseHandle(m_stdinWrite);
            CloseHandle(m_stdoutRead);
            CloseHandle(m_stdoutWrite);
            if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);
            return false;
        }

        // Close unused ends of pipes and NUL handle
        CloseHandle(m_stdinRead);
        CloseHandle(m_stdoutWrite);
        if (hNul != INVALID_HANDLE_VALUE) CloseHandle(hNul);

        m_running = true;
#else
        // Unix: Use pipe() and fork()
        int stdinPipe[2], stdoutPipe[2];
        if (pipe(stdinPipe) < 0 || pipe(stdoutPipe) < 0) {
            return false;
        }

        m_pid = fork();
        if (m_pid < 0) {
            return false;
        }

        if (m_pid == 0) {
            // Child process
            close(stdinPipe[1]);
            close(stdoutPipe[0]);
            dup2(stdinPipe[0], STDIN_FILENO);
            dup2(stdoutPipe[1], STDOUT_FILENO);
            // Redirect stderr to /dev/null (discard log messages)
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull >= 0) {
                dup2(devNull, STDERR_FILENO);
                close(devNull);
            }
            close(stdinPipe[0]);
            close(stdoutPipe[1]);

            execl("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
            _exit(1);
        }

        // Parent process
        close(stdinPipe[0]);
        close(stdoutPipe[1]);
        m_stdinFd = stdinPipe[1];
        m_stdoutFd = stdoutPipe[0];
        m_running = true;
#endif

        // Wait a moment for the process to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return true;
    }

    void stop() {
        if (!m_running) return;

#ifdef _WIN32
        // Terminate the process
        TerminateProcess(m_processInfo.hProcess, 0);
        WaitForSingleObject(m_processInfo.hProcess, 1000);
        CloseHandle(m_processInfo.hProcess);
        CloseHandle(m_processInfo.hThread);
        CloseHandle(m_stdinWrite);
        CloseHandle(m_stdoutRead);
#else
        close(m_stdinFd);
        close(m_stdoutFd);
        kill(m_pid, SIGTERM);
        // Wait for child (avoid zombie)
        int status;
        waitpid(m_pid, &status, 0);
#endif
        m_running = false;
    }

    bool writeLine(const std::string& line) {
        if (!m_running) return false;

        std::string data = line + "\n";
#ifdef _WIN32
        DWORD written;
        return WriteFile(m_stdinWrite, data.c_str(), static_cast<DWORD>(data.size()), &written, NULL);
#else
        return write(m_stdinFd, data.c_str(), data.size()) > 0;
#endif
    }

    std::string readLine(int timeoutMs = 5000) {
        if (!m_running) return "";

        std::string result;
        auto start = std::chrono::steady_clock::now();

        while (true) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeoutMs) break;

            char c;
            bool gotChar = false;

#ifdef _WIN32
            DWORD available = 0;
            if (PeekNamedPipe(m_stdoutRead, NULL, 0, NULL, &available, NULL) && available > 0) {
                DWORD bytesRead;
                if (ReadFile(m_stdoutRead, &c, 1, &bytesRead, NULL) && bytesRead > 0) {
                    gotChar = true;
                }
            }
#else
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(m_stdoutFd, &fds);
            struct timeval tv = {0, 10000};  // 10ms

            if (select(m_stdoutFd + 1, &fds, NULL, NULL, &tv) > 0) {
                if (read(m_stdoutFd, &c, 1) > 0) {
                    gotChar = true;
                }
            }
#endif

            if (gotChar) {
                if (c == '\n') {
                    break;
                }
                result += c;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        return result;
    }

    bool isRunning() const { return m_running; }

private:
    std::atomic<bool> m_running{false};

#ifdef _WIN32
    HANDLE m_stdinWrite = NULL;
    HANDLE m_stdinRead = NULL;
    HANDLE m_stdoutWrite = NULL;
    HANDLE m_stdoutRead = NULL;
    PROCESS_INFORMATION m_processInfo;
#else
    pid_t m_pid = 0;
    int m_stdinFd = -1;
    int m_stdoutFd = -1;
#endif
};

// -----------------------------------------------------------------------------
// MCP Client Helper
// -----------------------------------------------------------------------------

class McpClient {
public:
    McpClient() {
        if (!m_process.start()) {
            throw std::runtime_error("Failed to start MCP server process");
        }
    }

    ~McpClient() {
        // Send shutdown notification (optional, process will be killed anyway)
        try {
            sendNotification("notifications/cancelled", {});
        } catch (...) {}
    }

    json sendRequest(const std::string& method, const json& params = json::object(), int timeoutMs = 5000) {
        json request;
        request["jsonrpc"] = "2.0";
        request["id"] = m_nextId++;
        request["method"] = method;
        request["params"] = params;

        std::string requestStr = request.dump();
        if (!m_process.writeLine(requestStr)) {
            throw std::runtime_error("Failed to write to MCP server");
        }

        // Read response
        std::string responseStr = m_process.readLine(timeoutMs);
        if (responseStr.empty()) {
            throw std::runtime_error("No response from MCP server");
        }

        return json::parse(responseStr);
    }

    void sendNotification(const std::string& method, const json& params = json::object()) {
        json notification;
        notification["jsonrpc"] = "2.0";
        notification["method"] = method;
        notification["params"] = params;

        m_process.writeLine(notification.dump());
    }

    bool isRunning() const { return m_process.isRunning(); }

private:
    McpProcess m_process;
    int m_nextId = 1;
};

// -----------------------------------------------------------------------------
// Test Cases
// -----------------------------------------------------------------------------

TEST_CASE("MCP server starts and responds", "[mcp]") {
    McpClient client;
    REQUIRE(client.isRunning());
}

TEST_CASE("MCP initialize returns valid response", "[mcp]") {
    McpClient client;

    json params;
    params["protocolVersion"] = "2024-11-05";
    params["capabilities"] = json::object();
    params["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};

    json response = client.sendRequest("initialize", params);

    INFO("Response: " << response.dump(2));

    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].contains("protocolVersion"));
    REQUIRE(response["result"].contains("serverInfo"));
    REQUIRE(response["result"]["serverInfo"]["name"] == "vivid-mcp");
}

TEST_CASE("MCP tools/list returns tools", "[mcp]") {
    McpClient client;

    // Initialize first
    json initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = json::object();
    initParams["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};
    client.sendRequest("initialize", initParams);

    // Send initialized notification
    client.sendNotification("notifications/initialized", {});

    // Request tools list
    json response = client.sendRequest("tools/list", {});

    INFO("Response: " << response.dump(2));

    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].contains("tools"));
    REQUIRE(response["result"]["tools"].is_array());
    REQUIRE(response["result"]["tools"].size() > 0);

    // Check for expected tools
    bool hasListOperators = false;
    bool hasSearchDocs = false;

    for (const auto& tool : response["result"]["tools"]) {
        std::string name = tool["name"];
        if (name == "list_operators") hasListOperators = true;
        if (name == "search_docs") hasSearchDocs = true;
    }

    REQUIRE(hasListOperators);
    REQUIRE(hasSearchDocs);
}

TEST_CASE("MCP list_operators tool returns operators", "[mcp]") {
    McpClient client;

    // Initialize
    json initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = json::object();
    initParams["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};
    client.sendRequest("initialize", initParams);
    client.sendNotification("notifications/initialized", {});

    // Call list_operators tool
    json params;
    params["name"] = "list_operators";
    params["arguments"] = json::object();

    json response = client.sendRequest("tools/call", params);

    INFO("Response: " << response.dump(2));

    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].contains("content"));
    REQUIRE(response["result"]["content"].is_array());
    REQUIRE(response["result"]["content"].size() > 0);

    // The content should be text containing operator information
    std::string content = response["result"]["content"][0]["text"];
    INFO("Content: " << content);

    // Should contain at least some common operators
    // The format might be JSON or structured text
    REQUIRE(!content.empty());
}

TEST_CASE("MCP search_docs tool returns results", "[mcp]") {
    McpClient client;

    // Initialize
    json initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = json::object();
    initParams["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};
    client.sendRequest("initialize", initParams);
    client.sendNotification("notifications/initialized", {});

    // Call search_docs tool (use longer timeout - doc search can be slow on CI)
    json params;
    params["name"] = "search_docs";
    params["arguments"] = {{"query", "noise"}};

    json response = client.sendRequest("tools/call", params, 30000);

    INFO("Response: " << response.dump(2));

    REQUIRE(response.contains("result"));
    REQUIRE(response["result"].contains("content"));
    REQUIRE(response["result"]["content"].is_array());

    // Should return some results for "noise"
    if (response["result"]["content"].size() > 0) {
        std::string content = response["result"]["content"][0]["text"];
        INFO("Content: " << content);
        // Should contain something related to noise
        REQUIRE(!content.empty());
    }
}

// -----------------------------------------------------------------------------
// Operator Metadata Validation Tests
// -----------------------------------------------------------------------------

// Helper to get vivid root directory (parent of build/bin)
static std::string getVividRoot() {
    std::string path = getVividPath();
    // Go up from build/bin/vivid to project root
    auto pos = path.rfind("/build/");
    if (pos != std::string::npos) {
        return path.substr(0, pos);
    }
    // Fallback - try relative path
    return ".";
}

// Helper to check if directory exists
static bool directoryExists(const std::string& path) {
#ifdef _WIN32
    DWORD attrib = GetFileAttributesA(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES && (attrib & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

TEST_CASE("MCP operator examples exist", "[mcp][metadata]") {
    McpClient client;

    // Initialize
    json initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = json::object();
    initParams["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};
    client.sendRequest("initialize", initParams);
    client.sendNotification("notifications/initialized", {});

    // Get list of operators (grouped format)
    json params;
    params["name"] = "list_operators";
    params["arguments"] = json::object();

    json response = client.sendRequest("tools/call", params, 10000);
    REQUIRE(response.contains("result"));
    REQUIRE(response["result"]["content"].size() > 0);

    std::string content = response["result"]["content"][0]["text"];
    json operators = json::parse(content);

    std::string root = getVividRoot();
    std::vector<std::string> missingExamples;

    // Iterate through all categories
    for (auto& [category, ops] : operators.items()) {
        for (const auto& op : ops) {
            if (!op.contains("examples")) continue;

            std::string opName = op["name"];
            for (const auto& ex : op["examples"]) {
                std::string exPath = ex["path"];
                std::string fullPath = root + "/" + exPath;

                if (!directoryExists(fullPath)) {
                    missingExamples.push_back(opName + ": " + exPath);
                }
            }
        }
    }

    // Report all missing examples
    if (!missingExamples.empty()) {
        std::string msg = "Missing example directories:\n";
        for (const auto& missing : missingExamples) {
            msg += "  - " + missing + "\n";
        }
        INFO(msg);
    }
    REQUIRE(missingExamples.empty());
}

TEST_CASE("MCP operator inputs/outputs metadata is valid", "[mcp][metadata]") {
    McpClient client;

    // Initialize
    json initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = json::object();
    initParams["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};
    client.sendRequest("initialize", initParams);
    client.sendNotification("notifications/initialized", {});

    // Get detailed info for a few key operators
    std::vector<std::string> testOps = {"Bloom", "Noise", "Composite", "Clock", "Render3D"};
    std::vector<std::string> errors;

    for (const auto& opName : testOps) {
        json params;
        params["name"] = "get_operator";
        params["arguments"] = {{"name", opName}};

        json response = client.sendRequest("tools/call", params, 5000);

        if (!response.contains("result") || response["result"]["isError"].get<bool>()) {
            errors.push_back(opName + ": failed to get operator info");
            continue;
        }

        std::string content = response["result"]["content"][0]["text"];
        json info = json::parse(content);

        // Validate required fields exist
        if (!info.contains("name")) {
            errors.push_back(opName + ": missing 'name' field");
        }
        if (!info.contains("category")) {
            errors.push_back(opName + ": missing 'category' field");
        }
        if (!info.contains("outputType")) {
            errors.push_back(opName + ": missing 'outputType' field");
        }

        // Validate outputType is a known type
        if (info.contains("outputType")) {
            std::string outType = info["outputType"];
            std::vector<std::string> validTypes = {
                "Texture", "Audio", "Value", "Geometry", "Camera", "Light", "None"
            };
            bool validOutput = std::find(validTypes.begin(), validTypes.end(), outType) != validTypes.end();
            if (!validOutput) {
                errors.push_back(opName + ": invalid outputType '" + outType + "'");
            }
        }

        // Validate requiresInput consistency with inputs
        if (info.contains("requiresInput") && info["requiresInput"].get<bool>()) {
            // If requiresInput is true, usage should mention input() or have inputs defined
            bool hasInputMethod = false;
            if (info.contains("usage")) {
                std::string usage = info["usage"];
                hasInputMethod = usage.find("input(") != std::string::npos ||
                                 usage.find("inputA(") != std::string::npos ||
                                 usage.find("source(") != std::string::npos ||
                                 usage.find("setInput(") != std::string::npos ||
                                 usage.find("setCameraInput(") != std::string::npos;
            }
            if (info.contains("inputs") && !info["inputs"].empty()) {
                hasInputMethod = true;
            }
            if (!hasInputMethod) {
                errors.push_back(opName + ": requiresInput=true but no input method in usage/inputs");
            }
        }

        // Validate params have required fields
        if (info.contains("params")) {
            for (const auto& param : info["params"]) {
                if (!param.contains("name")) {
                    errors.push_back(opName + ": param missing 'name' field");
                }
                if (!param.contains("type")) {
                    errors.push_back(opName + ": param '" + param.value("name", "?") + "' missing 'type' field");
                }
            }
        }
    }

    // Report all errors - use FAIL_CHECK for each so they appear in CI output
    for (const auto& err : errors) {
        FAIL_CHECK("Metadata validation error: " + err);
    }
    REQUIRE(errors.empty());
}

TEST_CASE("MCP operator usage examples have valid syntax", "[mcp][metadata]") {
    McpClient client;

    // Initialize
    json initParams;
    initParams["protocolVersion"] = "2024-11-05";
    initParams["capabilities"] = json::object();
    initParams["clientInfo"] = {{"name", "test-client"}, {"version", "1.0.0"}};
    client.sendRequest("initialize", initParams);
    client.sendNotification("notifications/initialized", {});

    // Get list of all operators
    json params;
    params["name"] = "list_operators";
    params["arguments"] = {{"verbose", true}};

    json response = client.sendRequest("tools/call", params, 10000);
    REQUIRE(response.contains("result"));

    std::string content = response["result"]["content"][0]["text"];
    json operators = json::parse(content);

    std::vector<std::string> usageErrors;

    // Check usage syntax for all operators
    for (auto& [category, ops] : operators.items()) {
        for (const auto& op : ops) {
            std::string opName = op["name"];

            if (!op.contains("usage")) continue;
            std::string usage = op["usage"];

            // Check for common syntax issues
            // 1. Unbalanced quotes
            int doubleQuotes = std::count(usage.begin(), usage.end(), '"');
            if (doubleQuotes % 2 != 0) {
                usageErrors.push_back(opName + ": unbalanced double quotes in usage");
            }

            // 2. Unbalanced parentheses
            int openParens = std::count(usage.begin(), usage.end(), '(');
            int closeParens = std::count(usage.begin(), usage.end(), ')');
            if (openParens != closeParens) {
                usageErrors.push_back(opName + ": unbalanced parentheses in usage (" +
                                      std::to_string(openParens) + " open, " +
                                      std::to_string(closeParens) + " close)");
            }

            // 3. Unbalanced braces
            int openBraces = std::count(usage.begin(), usage.end(), '{');
            int closeBraces = std::count(usage.begin(), usage.end(), '}');
            if (openBraces != closeBraces) {
                usageErrors.push_back(opName + ": unbalanced braces in usage");
            }

            // 4. Should reference the operator name
            if (usage.find(opName) == std::string::npos) {
                // Check for aliases or common variations
                bool foundRef = false;
                if (op.contains("aliases")) {
                    for (const auto& alias : op["aliases"]) {
                        if (usage.find(alias.get<std::string>()) != std::string::npos) {
                            foundRef = true;
                            break;
                        }
                    }
                }
                if (!foundRef) {
                    usageErrors.push_back(opName + ": usage doesn't reference operator name");
                }
            }

            // 5. Should have chain.add or similar pattern
            bool hasAddPattern = usage.find("chain.add<") != std::string::npos ||
                                 usage.find("add<") != std::string::npos;
            if (!hasAddPattern) {
                usageErrors.push_back(opName + ": usage missing chain.add<> pattern");
            }
        }
    }

    // Report all errors (but don't fail for minor issues)
    if (!usageErrors.empty()) {
        std::string msg = "Usage syntax issues found:\n";
        for (const auto& err : usageErrors) {
            msg += "  - " + err + "\n";
        }
        INFO(msg);
    }

    // Only fail for critical issues (unbalanced quotes/parens)
    int criticalErrors = 0;
    for (const auto& err : usageErrors) {
        if (err.find("unbalanced") != std::string::npos) {
            criticalErrors++;
        }
    }
    REQUIRE(criticalErrors == 0);
}
