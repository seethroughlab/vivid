#include "runtime/tool_discovery.h"
#include <cstdio>
#include <cstdlib>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main() {
    std::fprintf(stderr, "--- test_tool_discovery ---\n");

    // 1. find_tool returns non-empty for clang++ (must exist on dev/CI machines)
    {
        std::string path = vivid::find_tool("clang++");
        check(!path.empty(), "find_tool(clang++) returns non-empty path");
    }

    // 2. VIVID_MOCK_MISSING_TOOL masks a tool
    {
        setenv("VIVID_MOCK_MISSING_TOOL", "clang++", 1);
        std::string path = vivid::find_tool("clang++");
        check(path.empty(), "VIVID_MOCK_MISSING_TOOL masks clang++");
        unsetenv("VIVID_MOCK_MISSING_TOOL");
    }

    // 3. VIVID_MOCK_MISSING_TOOL with comma-separated list
    {
        setenv("VIVID_MOCK_MISSING_TOOL", "cmake, clang++", 1);
        check(vivid::find_tool("clang++").empty(), "comma-separated mock masks clang++");
        check(vivid::find_tool("cmake").empty(), "comma-separated mock masks cmake");
        // git should still be findable
        std::string git = vivid::find_tool("git");
        check(!git.empty(), "comma-separated mock does not mask git");
        unsetenv("VIVID_MOCK_MISSING_TOOL");
    }

    // 4. VIVID_CXX env var overrides find_tool for clang++
    {
        setenv("VIVID_CXX", "/custom/path/clang++", 1);
        std::string path = vivid::find_tool("clang++");
        check(path == "/custom/path/clang++", "VIVID_CXX overrides clang++ path");
        unsetenv("VIVID_CXX");
    }

    // 5. VIVID_CMAKE env var overrides cmake
    {
        setenv("VIVID_CMAKE", "/opt/cmake/bin/cmake", 1);
        std::string path = vivid::find_tool("cmake");
        check(path == "/opt/cmake/bin/cmake", "VIVID_CMAKE overrides cmake path");
        unsetenv("VIVID_CMAKE");
    }

    // 6. VIVID_GIT env var overrides git
    {
        setenv("VIVID_GIT", "/usr/local/bin/git", 1);
        std::string path = vivid::find_tool("git");
        check(path == "/usr/local/bin/git", "VIVID_GIT overrides git path");
        unsetenv("VIVID_GIT");
    }

    // 7. missing_tool_error messages
    {
        std::string msg = vivid::missing_tool_error("clang++");
        check(msg.find("xcode-select") != std::string::npos,
              "clang++ error mentions xcode-select");
        check(msg.find("VIVID_CXX") != std::string::npos,
              "clang++ error mentions VIVID_CXX");
    }
    {
        std::string msg = vivid::missing_tool_error("cmake");
        check(msg.find("brew install cmake") != std::string::npos,
              "cmake error mentions brew install cmake");
        check(msg.find("VIVID_CMAKE") != std::string::npos,
              "cmake error mentions VIVID_CMAKE");
    }
    {
        std::string msg = vivid::missing_tool_error("git");
        check(msg.find("VIVID_GIT") != std::string::npos,
              "git error mentions VIVID_GIT");
    }

    // 8. Unknown tool returns generic message without env var hint
    {
        std::string msg = vivid::missing_tool_error("ninja");
        check(msg.find("ninja") != std::string::npos,
              "unknown tool error contains tool name");
        check(msg.find("VIVID_") == std::string::npos,
              "unknown tool error has no VIVID_ env hint");
    }

    // 9. Mock missing takes precedence over env var override
    {
        setenv("VIVID_CXX", "/custom/clang++", 1);
        setenv("VIVID_MOCK_MISSING_TOOL", "clang++", 1);
        std::string path = vivid::find_tool("clang++");
        check(path.empty(), "mock missing takes precedence over env override");
        unsetenv("VIVID_CXX");
        unsetenv("VIVID_MOCK_MISSING_TOOL");
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
