// Qualitative Output Tests
// ========================
// Dynamically discovers all projects with vivid-assertions.json,
// runs `vivid check` on each, and verifies assertions pass.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ProjectInfo {
    fs::path path;         // absolute path to project directory
    std::string relName;   // relative display name
};

struct CheckResult {
    bool success;
    std::string output;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string getVividPath() {
    return VIVID_BINARY_PATH;
}

static std::string getSourceDir() {
    return VIVID_SOURCE_DIR;
}

// Recursively find all directories containing a vivid-assertions.json file.
static std::vector<ProjectInfo> scanProjects(const fs::path& baseDir, const std::string& prefix) {
    std::vector<ProjectInfo> projects;

    if (!fs::exists(baseDir) || !fs::is_directory(baseDir)) {
        return projects;
    }

    for (auto& entry : fs::recursive_directory_iterator(baseDir, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && entry.path().filename() == "vivid-assertions.json") {
            fs::path projDir = entry.path().parent_path();
            // Verify it's actually a project (has chain.cpp too)
            if (!fs::exists(projDir / "chain.cpp")) continue;

            std::string relName = prefix + "/" + fs::relative(projDir, baseDir).string();
            projects.push_back({projDir, relName});
        }
    }

    std::sort(projects.begin(), projects.end(),
              [](const ProjectInfo& a, const ProjectInfo& b) { return a.relName < b.relName; });

    return projects;
}

// Gather all discoverable projects with assertions.
static std::vector<ProjectInfo> discoverAllProjects() {
    std::string sourceDir = getSourceDir();

    auto projectsDir = scanProjects(fs::path(sourceDir) / "projects", "projects");
    auto fixturesDir = scanProjects(fs::path(sourceDir) / "tests" / "fixtures", "tests/fixtures");
    auto modulesDir  = scanProjects(fs::path(sourceDir) / "modules", "modules");

    std::vector<ProjectInfo> all;
    all.insert(all.end(), projectsDir.begin(), projectsDir.end());
    all.insert(all.end(), fixturesDir.begin(), fixturesDir.end());
    all.insert(all.end(), modulesDir.begin(), modulesDir.end());

    return all;
}

// Run `vivid check <projectPath> --verbose` and capture output + exit code.
static CheckResult runCheck(const fs::path& projectPath) {
    std::string cmd = "\"" + getVividPath() + "\" check \"" + projectPath.string() + "\" --verbose 2>&1";

#ifdef _WIN32
    cmd = "cmd /c \"" + cmd + "\"";
#endif

    CheckResult result;
    result.success = false;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.output = "Failed to execute command";
        return result;
    }

    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result.output += buffer;
    }

    int status = pclose(pipe);

#ifdef _WIN32
    result.success = (status == 0);
#else
    result.success = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

    return result;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("All assertion projects pass", "[qualitative]") {
    auto allProjects = discoverAllProjects();
    REQUIRE_FALSE(allProjects.empty());
    INFO("Found " << allProjects.size() << " projects with vivid-assertions.json");

    for (auto& project : allProjects) {
        DYNAMIC_SECTION("Check: " << project.relName) {
            auto result = runCheck(project.path);

            INFO("Project: " << project.relName);
            INFO("Path: " << project.path.string());
            INFO("Check output:\n" << result.output);

            REQUIRE(result.success);
        }
    }
}
