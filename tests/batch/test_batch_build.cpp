// Batch Build Verification & Operator Coverage Index
// ===================================================
// Dynamically discovers all projects with chain.cpp, verifies they compile
// with `vivid build`, and writes an operator→project coverage index to JSON.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ProjectInfo {
    fs::path path;         // absolute path
    std::string relName;   // relative display name (e.g. "getting-started/02-hello-noise")
};

struct BuildResult {
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

// Recursively find all directories containing a chain.cpp file.
static std::vector<ProjectInfo> scanProjects(const fs::path& baseDir, const std::string& prefix) {
    std::vector<ProjectInfo> projects;

    if (!fs::exists(baseDir) || !fs::is_directory(baseDir)) {
        return projects;
    }

    for (auto& entry : fs::recursive_directory_iterator(baseDir, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && entry.path().filename() == "chain.cpp") {
            fs::path projDir = entry.path().parent_path();
            // Build a relative name for display
            std::string relName = prefix + "/" + fs::relative(projDir, baseDir).string();
            projects.push_back({projDir, relName});
        }
    }

    // Sort for deterministic ordering
    std::sort(projects.begin(), projects.end(),
              [](const ProjectInfo& a, const ProjectInfo& b) { return a.relName < b.relName; });

    return projects;
}

// Paths to exclude from batch build verification.
// - broken-chain: intentionally broken test fixture
// - vivid-core/src: contains chain.cpp (runtime entry point) but is not a project
// - vivid-texshare: Syphon framework linking fails at runtime on macOS (not a compile error)
static bool shouldSkipProject(const std::string& relName) {
    static const std::vector<std::string> skipPatterns = {
        "tests/fixtures/broken-chain",
        "modules/vivid-core/src",
        "modules/vivid-texshare/",
    };
    for (auto& pat : skipPatterns) {
        if (relName.find(pat) != std::string::npos) return true;
    }
    return false;
}

// Gather all discoverable projects from standard locations.
static std::vector<ProjectInfo> discoverAllProjects() {
    std::string sourceDir = getSourceDir();

    auto projectsDir = scanProjects(fs::path(sourceDir) / "projects", "projects");
    auto fixturesDir = scanProjects(fs::path(sourceDir) / "tests" / "fixtures", "tests/fixtures");
    auto moduleExamples = scanProjects(fs::path(sourceDir) / "modules", "modules");

    std::vector<ProjectInfo> all;
    all.insert(all.end(), projectsDir.begin(), projectsDir.end());
    all.insert(all.end(), fixturesDir.begin(), fixturesDir.end());
    all.insert(all.end(), moduleExamples.begin(), moduleExamples.end());

    // Remove known non-project paths
    all.erase(std::remove_if(all.begin(), all.end(),
        [](const ProjectInfo& p) { return shouldSkipProject(p.relName); }),
        all.end());

    return all;
}

// Extract operator types from chain.add<Type>(...) patterns in a chain.cpp file.
static std::set<std::string> extractOperators(const fs::path& chainCppPath) {
    std::set<std::string> ops;

    std::ifstream file(chainCppPath);
    if (!file.is_open()) return ops;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    std::regex pattern(R"(\.add<(\w+)>\()");
    auto begin = std::sregex_iterator(content.begin(), content.end(), pattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        ops.insert((*it)[1].str());
    }

    return ops;
}

// Run `vivid build <projectPath>` and capture output + exit code.
// Determines compilation success from output content, not just exit code,
// because `vivid build` may crash after successful compilation (GPU errors).
static BuildResult runBuild(const fs::path& projectPath) {
    std::string cmd = "\"" + getVividPath() + "\" build \"" + projectPath.string() + "\" 2>&1";

#ifdef _WIN32
    cmd = "cmd /c \"" + cmd + "\"";
#endif

    BuildResult result;
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

    // Check exit code first
#ifdef _WIN32
    bool cleanExit = (status == 0);
#else
    bool cleanExit = (WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

    if (cleanExit) {
        result.success = true;
    } else {
        // Exit code was non-zero. Check if compilation itself succeeded
        // but the process crashed later (e.g. GPU validation errors).
        // vivid build prints "Compilation successful" on success and
        // "Compilation failed" / "COMPILE FAILED" on failure.
        bool compiledOk = result.output.find("Compilation successful") != std::string::npos;
        bool compileFailed = result.output.find("Compilation failed") != std::string::npos
                          || result.output.find("COMPILE FAILED") != std::string::npos;

        result.success = compiledOk && !compileFailed;
    }

    return result;
}

// Write operator→projects coverage index as JSON.
static void writeCoverageIndex(const std::map<std::string, std::vector<std::string>>& index,
                               const fs::path& outputPath) {
    fs::create_directories(outputPath.parent_path());

    std::ofstream out(outputPath);
    if (!out.is_open()) return;

    out << "{\n";
    bool firstOp = true;
    for (auto& [op, projects] : index) {
        if (!firstOp) out << ",\n";
        firstOp = false;

        out << "  \"" << op << "\": [";
        for (size_t i = 0; i < projects.size(); ++i) {
            if (i > 0) out << ", ";
            out << "\"" << projects[i] << "\"";
        }
        out << "]";
    }
    out << "\n}\n";
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("All projects compile", "[build-all]") {
    auto allProjects = discoverAllProjects();
    REQUIRE_FALSE(allProjects.empty());
    INFO("Found " << allProjects.size() << " projects with chain.cpp");

    for (auto& project : allProjects) {
        DYNAMIC_SECTION("Build: " << project.relName) {
            auto result = runBuild(project.path);

            INFO("Project: " << project.relName);
            INFO("Path: " << project.path.string());
            INFO("Build output:\n" << result.output);

            REQUIRE(result.success);
        }
    }
}

TEST_CASE("Operator coverage index", "[build-all]") {
    auto allProjects = discoverAllProjects();
    REQUIRE_FALSE(allProjects.empty());

    std::map<std::string, std::vector<std::string>> coverageIndex;

    for (auto& project : allProjects) {
        auto ops = extractOperators(project.path / "chain.cpp");
        for (auto& op : ops) {
            coverageIndex[op].push_back(project.relName);
        }
    }

    REQUIRE_FALSE(coverageIndex.empty());
    INFO("Found " << coverageIndex.size() << " unique operator types across " << allProjects.size() << " projects");

    fs::path coveragePath = fs::path(getSourceDir()) / "build" / "operator-coverage.json";
    writeCoverageIndex(coverageIndex, coveragePath);

    // Verify the file was written
    REQUIRE(fs::exists(coveragePath));
    REQUIRE(fs::file_size(coveragePath) > 2);  // more than "{}"
}
