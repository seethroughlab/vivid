// Vivid CLI Commands
// Handles: vivid new, vivid --help, vivid --version, vivid bundle, vivid operators, vivid libs, vivid mcp

#include <vivid/cli.h>
#include <vivid/app.h>
#ifdef VIVID_ENABLE_MCP
#include <vivid/mcp_server.h>
#endif
#include <vivid/operator_registry.h>
#include <vivid/module_loader.h>
#include <vivid/module_manager.h>
#include <vivid/module_registry.h>
#include <vivid/docs_search.h>
#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <set>
#include <regex>
#include <cctype>

using json = nlohmann::json;

#ifdef __APPLE__
#include <mach-o/dyld.h>  // For _NSGetExecutablePath
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace fs = std::filesystem;

namespace vivid::cli {

// Get executable directory for finding templates
static fs::path getExecutableDir() {
#ifdef __APPLE__
    char pathBuf[4096];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        return fs::canonical(pathBuf).parent_path();
    }
#elif defined(_WIN32)
    char pathBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, pathBuf, MAX_PATH);
    return fs::path(pathBuf).parent_path();
#elif defined(__linux__)
    char pathBuf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len != -1) {
        pathBuf[len] = '\0';
        return fs::path(pathBuf).parent_path();
    }
#endif
    return fs::current_path();
}

// Read template from file, returns empty string if not found
static std::string readTemplateFile(const std::string& templateName) {
    fs::path exeDir = getExecutableDir();
    // Templates are in parent dir (build root), not in bin/
    fs::path templatePath = exeDir.parent_path() / "templates" / templateName / "chain.cpp";

    if (fs::exists(templatePath)) {
        std::ifstream file(templatePath);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }
    return "";
}

// Fallback embedded templates (used when external files not found)
// These are minimal versions to keep cli.cpp lean

static const char* FALLBACK_BLANK_TEMPLATE = R"(// %PROJECT_NAME% - Vivid Project
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    chain.output("noise");
}

void update(Context& ctx) {
    float time = static_cast<float>(ctx.time());
    ctx.chain().get<Noise>("noise").offset.set(time * 0.2f, time * 0.1f, 0.0f);
}

VIVID_CHAIN(setup, update)
)";

static const char* FALLBACK_MINIMAL_TEMPLATE = R"(#include <vivid/vivid.h>

using namespace vivid;

void setup(Context& ctx) {
    // Add operators here
}

void update(Context& ctx) {
    // Update parameters here
}

VIVID_CHAIN(setup, update)
)";

// AGENTS.md template - operational context for AI assistants (industry standard)
static const char* AGENTS_MD_TEMPLATE = R"(# %PROJECT_NAME%

## Commands
- **VS Code**: `Vivid: Run Project` (Cmd/Ctrl+Shift+P) - recommended
- **Terminal**: `vivid .` or `vivid . --show-ui`
- **CI/Agent**: `vivid . --exit-on-error` (exits on compile error instead of waiting)

## Modules
%MODULES_LIST%

## Agent Workflow

Use two nested loops: a fast **inner loop** for autonomous iteration, and an **outer loop** for human review.

### Inner Loop (autonomous)

```
Edit chain.cpp
  -> vivid build .          # MUST pass (exit 0) before anything else
  -> vivid inspect .        # Read structured metrics JSON
  -> vivid check .          # Run assertions (exit 0 = all pass)
  -> iterate or done
```

**Critical:** `vivid build` is the only command that exits non-zero on compile failure. All other commands (`vivid inspect`, `vivid check`, `vivid export`) hang indefinitely on errors. Always use `vivid build` as a gate.

### Outer Loop (human review)

When inner loop metrics are satisfied, export for subjective review:

```
vivid export . -o /tmp/preview.mp4 --duration 15
```

Only export when you've iterated to a point worth showing. Exporting is slow.

## CLI vs MCP Tools

Use **CLI via Bash** for spawn-and-exit tasks (faster, no indirection):

```bash
vivid build .                              # Compile gate (must pass first)
vivid inspect .                            # Single-frame inspection JSON
vivid inspect . --per-operator             # Per-operator texture analysis
vivid inspect . --duration 2 --samples 5   # Multi-sample over 2s
vivid check .                              # Run assertions from vivid-assertions.json
vivid check . --duration 2                 # Allow warmup before checking
vivid export . -o out.mp4 --duration 10    # Export video
vivid export . -o out.wav --audio-only --duration 10  # Export audio only
vivid params .                             # List tweakable parameters as JSON
```

Use **MCP tools** for:
- **Live instance** (requires `run_project`): `set_param`, `capture_frame`, `capture_audio`, `sweep_param`, `solo_operator`, `inspect_chain`, `get_performance_stats`, etc.
- **Slider workflow**: `get_pending_changes` / `clear_pending_changes` / `get_runtime_status`
- **Reference**: `get_operator`, `get_example`, `get_recipe`, `search_docs`

Some MCP tools (`validate_chain`, `export_video`, `export_audio`, `capture_snapshot`) just shell out to CLI commands — prefer Bash directly for efficiency.

## Assertions

Create `vivid-assertions.json` to define health checks. Run with `vivid check .`

```json
{
  "assertions": [
    {"name": "brightness-ok", "path": "output.meanBrightness", "op": "between", "value": [0.1, 0.9]},
    {"name": "has-contrast", "path": "output.contrast", "op": ">", "value": 0.1, "after_frame": 30}
  ]
}
```

Supported paths: `output.*` (visual), `audio.*` (audio), `operators.<name>.metrics.*`, `operators.<name>.textureAnalysis.*`.
Operators: `>`, `>=`, `<`, `<=`, `==`, `!=`, `between`, `exists`, `not_exists`.
Guards: `after_frame` (skip before frame N), `when_path`/`when_check`/`when_value` (conditional).

## MCP Workflow
After editing chain.cpp, Vivid hot-reloads automatically. Always verify:
1. `get_pending_changes` - Check if user adjusted sliders
2. Edit chain.cpp with new values
3. `clear_pending_changes` - Confirm edit applied
4. `get_runtime_status` - **Critical**: Verify compilation succeeded

Key MCP tools: `inspect_chain` (with `per_operator_analysis: true`), `capture_frame`, `capture_audio`, `compare_frames`, `set_param`, `solo_operator`, `sweep_param`.

## Conventions
- Use setter pattern: `noise.scale = 4.0f;`
- Explicit casts: `std::max(0.0f, static_cast<float>(m_param))`
- Keep chains simple - fewer operators is better

## Boundaries
- Don't modify assets/ without asking
- chain.cpp is the single entry point
- Check `get_runtime_status` after every edit

## Resources
- Recipes: https://github.com/seethroughlab/vivid/blob/main/docs/RECIPES.md
- Examples: https://github.com/seethroughlab/vivid/tree/main/modules/vivid-core/examples
- Use `search_docs` MCP tool for operator details
)";

// BRIEF.md template - creative vision (user-owned)
static const char* BRIEF_MD_TEMPLATE = R"(# Vision

[Describe what you want to create - be specific about visuals, motion, inputs, mood]

## Aesthetic Goals
- [Visual style, mood, references]

## Constraints
- [Resolution, performance, target platforms]

## Notes
- [Any other context]
)";

void printUsage() {
    std::cout << "Vivid - Creative coding framework with hot-reload\n\n";
    std::cout << "Usage:\n";
    std::cout << "  vivid <project-path>              Run a project\n";
    std::cout << "  vivid new <name> [options]        Create a new project\n";
    std::cout << "  vivid bundle <project> [options]  Bundle project as standalone app\n";
    std::cout << "  vivid check <project> [options]   Run assertions against a project\n";
    std::cout << "  vivid inspect <project> [options]  Dump inspection data as JSON\n";
    std::cout << "  vivid export <project> [options]  Export video with playback script\n";
    std::cout << "  vivid build <project>             Compile and report structured errors\n";
    std::cout << "  vivid params <project>            List all tweakable parameters as JSON\n";
    std::cout << "  vivid graph <project>             Dump chain topology as JSON\n";
    std::cout << "  vivid docs search <query>         Search documentation\n";
    std::cout << "  vivid docs recipe [name]          List or show recipes\n";
    std::cout << "  vivid docs example <operator>     Show code examples for an operator\n";
    std::cout << "  vivid --help                      Show this help\n";
    std::cout << "  vivid --version                   Show version\n";
}

void printVersion() {
    std::cout << "Vivid " << VERSION << "\n";
}

std::string replaceAll(std::string str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
    return str;
}

// Available modules with descriptions
struct ModuleInfo {
    std::string name;
    std::string description;
    fs::path path;  // Full path to module directory
};

// Dynamically discover available modules by scanning the modules directory
static std::vector<ModuleInfo> discoverModules() {
    std::vector<ModuleInfo> modules;
    std::set<std::string> seenNames;  // Avoid duplicates

    fs::path exeDir = getExecutableDir();

    // Try multiple paths to find modules (scan all, don't stop at first)
    std::vector<fs::path> searchPaths = {
        fs::current_path() / "modules",                    // Dev: running from repo root
        exeDir.parent_path().parent_path() / "modules",    // Dev: exe in build/bin/, modules at ../../modules/
        exeDir.parent_path() / "modules"                   // Release: exe in bin/, modules at ../modules/
    };

    for (const auto& modulesDir : searchPaths) {
        if (!fs::exists(modulesDir) || !fs::is_directory(modulesDir)) {
            continue;
        }

        for (const auto& entry : fs::directory_iterator(modulesDir)) {
            if (!entry.is_directory()) continue;

            fs::path moduleJson = entry.path() / "module.json";
            if (!fs::exists(moduleJson)) continue;

            try {
                std::ifstream f(moduleJson);
                if (!f) continue;

                json j = json::parse(f);
                ModuleInfo info;
                info.name = j.value("name", entry.path().filename().string());

                // Skip if we already found this module
                if (seenNames.count(info.name)) continue;
                seenNames.insert(info.name);

                info.description = j.value("description", "");
                info.path = entry.path();
                modules.push_back(info);
            } catch (...) {
                // Skip modules with invalid JSON
            }
        }
    }

    // Sort by name for consistent ordering
    std::sort(modules.begin(), modules.end(),
              [](const ModuleInfo& a, const ModuleInfo& b) { return a.name < b.name; });

    return modules;
}

// Files that don't count as "real content" when checking if directory is empty
static const std::set<std::string> IGNORABLE_FILES = {
    ".git", ".gitignore", ".gitattributes", ".DS_Store",
    ".vscode", ".idea", "README.md", "BRIEF.md", "AGENTS.md"
};

// Files created by vivid new (potential conflicts when creating in-place)
static const std::vector<std::string> PROJECT_FILES = {
    "chain.cpp", "AGENTS.md", ".gitignore", "assets", "shaders", ".claude"
};

// Check if a directory is "effectively empty" (only has ignorable files)
static bool isEffectivelyEmpty(const fs::path& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return false;
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        std::string name = entry.path().filename().string();
        // Skip all hidden files/folders
        if (!name.empty() && name[0] == '.') continue;
        // Skip known ignorable files
        if (IGNORABLE_FILES.count(name)) continue;
        // Found a real file - not empty
        return false;
    }
    return true;
}

// Check if directory has project setup (BRIEF.md exists but no chain.cpp)
static bool hasProjectSetup(const fs::path& dir) {
    return fs::exists(dir / "BRIEF.md") && !fs::exists(dir / "chain.cpp");
}

// Get list of project files that already exist (conflicts for in-place creation)
static std::vector<std::string> getConflictingFiles(const fs::path& dir) {
    std::vector<std::string> conflicts;
    for (const auto& file : PROJECT_FILES) {
        // Special case: AGENTS.md is regenerated, so skip conflict check if BRIEF.md exists
        if (file == "AGENTS.md" && fs::exists(dir / "BRIEF.md")) continue;
        if (fs::exists(dir / file)) {
            conflicts.push_back(file);
        }
    }
    return conflicts;
}

// Case-insensitive string comparison
static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(a[i]) != std::tolower(b[i])) return false;
    }
    return true;
}

// Decide whether to create project in-place based on heuristics
// Returns: true = create in place, false = create subdirectory
// explicitInPlace: -1 = not specified, 0 = explicit false, 1 = explicit true
static bool shouldCreateInPlace(const fs::path& dir, const std::string& projectName, int explicitInPlace) {
    // 1. Explicit override takes precedence
    if (explicitInPlace == 1) return true;
    if (explicitInPlace == 0) return false;

    // 2. Has BRIEF.md but no chain.cpp - user set up a project folder
    if (hasProjectSetup(dir)) return true;

    // 3. Directory is effectively empty
    if (isEffectivelyEmpty(dir)) return true;

    // 4. Directory name matches project name (case-insensitive)
    if (iequals(dir.filename().string(), projectName)) return true;

    // 5. Default: create subdirectory
    return false;
}

int createProject(const std::string& name, const std::string& templateName,
                  bool minimal, bool skipPrompts, const std::vector<std::string>& modules,
                  int explicitInPlace = -1) {
    fs::path cwd = fs::current_path();
    bool inPlace = shouldCreateInPlace(cwd, name, explicitInPlace);
    fs::path projectPath = inPlace ? cwd : (cwd / name);

    if (inPlace) {
        // Check for conflicting files that would be overwritten
        auto conflicts = getConflictingFiles(projectPath);
        if (!conflicts.empty()) {
            std::cerr << "Error: Cannot create project in-place. The following files already exist:\n";
            for (const auto& conflict : conflicts) {
                std::cerr << "  " << conflict << "\n";
            }
            std::cerr << "\nEither remove these files or create the project in a different directory.\n";
            return 1;
        }
    } else {
        // Check if subdirectory already exists
        if (fs::exists(projectPath)) {
            std::cerr << "Error: Directory '" << name << "' already exists.\n";
            return 1;
        }
    }

    // Validate module names
    auto availableModules = discoverModules();
    for (const auto& mod : modules) {
        bool valid = false;
        for (const auto& available : availableModules) {
            if (mod == available.name) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            std::cerr << "Error: Unknown module '" << mod << "'\n";
            std::cerr << "Available modules:\n";
            for (const auto& m : availableModules) {
                std::cerr << "  " << m.name << " - " << m.description << "\n";
            }
            return 1;
        }
    }

    // Confirm creation (unless --yes flag)
    if (!skipPrompts && !minimal) {
        if (inPlace) {
            std::cout << "Creating project '" << name << "' in current directory";
        } else {
            std::cout << "Creating project '" << name << "' with template '" << templateName << "'";
        }
        if (!modules.empty()) {
            std::cout << " and modules: ";
            for (size_t i = 0; i < modules.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << modules[i];
            }
        }
        std::cout << "...\n";
        std::cout << "Continue? [Y/n] ";
        std::string response;
        std::getline(std::cin, response);
        if (!response.empty() && response[0] != 'y' && response[0] != 'Y') {
            std::cout << "Cancelled.\n";
            return 0;
        }
    }

    // Try to load template from external file first
    std::string templateContent = readTemplateFile(templateName);

    // Fall back to embedded templates if external file not found
    if (templateContent.empty()) {
        if (templateName == "minimal") {
            templateContent = FALLBACK_MINIMAL_TEMPLATE;
        } else {
            // Use blank as fallback for any unknown template
            templateContent = FALLBACK_BLANK_TEMPLATE;
        }
    }

    // Create directory structure
    try {
        fs::create_directories(projectPath);
        fs::create_directories(projectPath / "assets");
        fs::create_directories(projectPath / "shaders");

        // Write chain.cpp with project name substituted
        std::string content = replaceAll(templateContent, "%PROJECT_NAME%", name);
        std::ofstream chainFile(projectPath / "chain.cpp");
        if (!chainFile) {
            std::cerr << "Error: Could not create chain.cpp\n";
            return 1;
        }
        chainFile << content;
        chainFile.close();

        // Write .gitignore
        std::ofstream gitignore(projectPath / ".gitignore");
        gitignore << "# Build artifacts\n";
        gitignore << "*.dylib\n";
        gitignore << "*.so\n";
        gitignore << "*.dll\n";
        gitignore << "build/\n";
        gitignore << "\n# IDE\n";
        gitignore << ".vscode/\n";
        gitignore << ".idea/\n";
        gitignore << "*.swp\n";
        gitignore << "\n# Claude Code local settings\n";
        gitignore << ".claude/\n";
        gitignore << "\n# ImGui state\n";
        gitignore << "imgui.ini\n";
        gitignore.close();

        // Create .claude/settings.json to pre-allow Vivid MCP tools
        // Uses settings.json (not settings.local.json) so it won't be overwritten
        // when Claude Code adds per-tool permissions to settings.local.json
        fs::create_directories(projectPath / ".claude");
        json claudeSettingsJson;
        claudeSettingsJson["permissions"]["allow"] = {"mcp__vivid"};
        std::ofstream claudeSettings(projectPath / ".claude" / "settings.json");
        claudeSettings << claudeSettingsJson.dump(2) << "\n";
        claudeSettings.close();

        // Build modules list for AGENTS.md
        std::string modulesList;
        modulesList += "- **Core** (always included): noise, blur, composite, feedback\n";
        if (!modules.empty()) {
            for (const auto& mod : modules) {
                for (const auto& info : availableModules) {
                    if (mod == info.name) {
                        modulesList += "- **" + info.name + "**: " + info.description + "\n";
                        break;
                    }
                }
            }
        }

        // Write AGENTS.md (operational context for AI assistants)
        std::string agentsMd = replaceAll(AGENTS_MD_TEMPLATE, "%PROJECT_NAME%", name);
        agentsMd = replaceAll(agentsMd, "%MODULES_LIST%", modulesList);
        std::ofstream agentsFile(projectPath / "AGENTS.md");
        if (agentsFile) {
            agentsFile << agentsMd;
            agentsFile.close();
        }

        // Write BRIEF.md (creative vision - user-owned) - skip if it already exists
        bool briefExists = fs::exists(projectPath / "BRIEF.md");
        if (!briefExists) {
            std::ofstream briefFile(projectPath / "BRIEF.md");
            if (briefFile) {
                briefFile << BRIEF_MD_TEMPLATE;
                briefFile.close();
            }
        }

        // Output what was created
        std::cout << "\n";
        std::string prefix = inPlace ? "  Created " : ("  Created " + name + "/");
        if (!inPlace) {
            std::cout << "  Created " << name << "/\n";
        }
        std::cout << prefix << "chain.cpp\n";
        std::cout << prefix << "AGENTS.md\n";
        if (!briefExists) {
            std::cout << prefix << "BRIEF.md\n";
        } else {
            std::cout << "  Kept existing BRIEF.md\n";
        }
        std::cout << prefix << "assets/\n";
        std::cout << prefix << "shaders/\n";
        std::cout << prefix << ".gitignore\n";
        std::cout << prefix << ".claude/settings.json\n";
        std::cout << "\n";
        std::cout << "Project created successfully!\n\n";
        std::cout << "Next steps:\n";
        if (!inPlace) {
            std::cout << "  cd " << name << "\n";
        }
        std::cout << "  vivid .\n";
        std::cout << "\n";
        if (!briefExists) {
            std::cout << "Edit BRIEF.md to describe what you want to create!\n";
        }
        std::cout << "Edit chain.cpp to start coding!\n";

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating project: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

// Convert project name to CamelCase for app name
std::string toCamelCase(const std::string& input) {
    std::string result;
    bool capitalizeNext = true;
    for (char c : input) {
        if (c == '-' || c == '_' || c == ' ') {
            capitalizeNext = true;
        } else if (capitalizeNext) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            capitalizeNext = false;
        } else {
            result += c;
        }
    }
    return result;
}

// Get current platform identifier
std::string getCurrentPlatform() {
#ifdef __APPLE__
    return "mac";
#elif defined(_WIN32)
    return "windows";
#else
    return "linux";
#endif
}

// List of valid platforms for current OS
std::vector<std::string> getValidPlatforms() {
#ifdef __APPLE__
    return {"mac", "ios"};  // Android requires NDK, add later
#elif defined(_WIN32)
    return {"windows"};  // Android requires NDK, add later
#else
    return {"linux"};
#endif
}

// Forward declarations for platform-specific bundlers
// buildDir is the production build directory containing bin/, lib/, etc.
int bundleForMac(const fs::path& srcProject, const fs::path& chainPath,
                 const std::string& appName, const fs::path& outputPath,
                 const fs::path& buildDir);
int bundleForWindows(const fs::path& srcProject, const fs::path& chainPath,
                     const std::string& appName, const fs::path& outputPath,
                     const fs::path& buildDir);
int bundleForLinux(const fs::path& srcProject, const fs::path& chainPath,
                   const std::string& appName, const fs::path& outputPath,
                   const fs::path& buildDir);
int bundleForIOS(const fs::path& srcProject, const fs::path& chainPath,
                 const std::string& appName, const fs::path& outputPath,
                 const fs::path& buildDir);

// Forward declaration for chain analysis
std::vector<std::string> getRequiredLibraries(const fs::path& chainPath, const fs::path& exeDir);

// Build a production version of vivid for bundling
// chainPath: Path to chain.cpp for static linking (makes bundle self-contained)
// Returns the build directory path on success, empty path on failure
fs::path buildProductionVersion(const fs::path& chainPath) {
    fs::path exeDir = getExecutableDir();
    // Find the vivid source root (parent of build directory)
    // exeDir is typically build/bin, so root is build/bin/../..
    fs::path rootDir = exeDir.parent_path().parent_path();

    // Check if we're in a development environment (has CMakeLists.txt)
    if (!fs::exists(rootDir / "CMakeLists.txt")) {
        // Try one level up (might be in installed location)
        rootDir = rootDir.parent_path();
        if (!fs::exists(rootDir / "CMakeLists.txt")) {
            std::cerr << "Error: Cannot find vivid source directory for production build.\n";
            std::cerr << "Bundle command requires building from source.\n";
            return {};
        }
    }

    // Analyze chain.cpp to determine required modules
    std::cout << "Scanning chain.cpp for dependencies...\n";
    auto requiredLibs = getRequiredLibraries(chainPath, exeDir);

    // Build cmake module list (semicolon-separated for cmake)
    std::string moduleList;
    std::cout << "  Required modules: ";
    for (size_t i = 0; i < requiredLibs.size(); i++) {
        if (i > 0) {
            std::cout << ", ";
            moduleList += ";";
        }
        std::cout << requiredLibs[i];
        moduleList += requiredLibs[i];
    }
    std::cout << "\n\n";

    fs::path buildDir = rootDir / "build-bundle";

    std::cout << "Building production version with static chain linking...\n";

    // Configure with production flags, static chain, and only required modules
    std::string configureCmd = "cmake -B \"" + buildDir.string() + "\" "
        "-DVIVID_PRODUCTION=ON "
        "-DVIVID_CHAIN_SOURCE=\"" + chainPath.string() + "\" "
        "-DVIVID_CHAIN_MODULES=\"" + moduleList + "\" "
        "-DCMAKE_BUILD_TYPE=Release "
        "\"" + rootDir.string() + "\" 2>&1";

    std::cout << "  Configuring...\n";
    int configResult = std::system(configureCmd.c_str());
    if (configResult != 0) {
        std::cerr << "Error: CMake configure failed.\n";
        return {};
    }

    // Build the production executable (vivid-production, not vivid)
    // vivid-production uses main_production.cpp and the Runtime class
    // NO HotReload, NO MCP, NO Visualizer - just the core rendering loop
    std::string buildCmd = "cmake --build \"" + buildDir.string() + "\" "
        "--target vivid-production --config Release -j 2>&1";

    std::cout << "  Compiling chain and linking...\n";
    int buildResult = std::system(buildCmd.c_str());
    if (buildResult != 0) {
        std::cerr << "Error: Build failed.\n";
        return {};
    }

    std::cout << "  Production build complete.\n\n";
    return buildDir;
}

int bundleProject(const std::string& projectPath, const std::string& outputPath,
                  const std::string& appName, const std::string& platform) {
    // Validate platform
    std::string targetPlatform = platform.empty() ? getCurrentPlatform() : platform;
    auto validPlatforms = getValidPlatforms();

    bool isValid = false;
    for (const auto& p : validPlatforms) {
        if (p == targetPlatform) {
            isValid = true;
            break;
        }
    }

    if (!isValid) {
        std::cerr << "Error: Cannot build for '" << targetPlatform << "' on this platform.\n";
        std::cerr << "Valid targets: ";
        for (size_t i = 0; i < validPlatforms.size(); i++) {
            if (i > 0) std::cerr << ", ";
            std::cerr << validPlatforms[i];
        }
        std::cerr << "\n";
        return 1;
    }

    // Validate project
    fs::path srcProject = fs::absolute(projectPath);
    fs::path chainPath = srcProject / "chain.cpp";

    if (!fs::exists(chainPath)) {
        if (fs::is_regular_file(srcProject) && srcProject.filename() == "chain.cpp") {
            chainPath = srcProject;
            srcProject = srcProject.parent_path();
        } else {
            std::cerr << "Error: No chain.cpp found in " << projectPath << "\n";
            return 1;
        }
    }

    std::string finalAppName = appName.empty() ? toCamelCase(srcProject.filename().string()) : appName;
    fs::path finalOutput = outputPath.empty() ? fs::current_path() : fs::path(outputPath);

    // Build production version with static chain linking
    fs::path buildDir = buildProductionVersion(chainPath);
    if (buildDir.empty()) {
        return 1;
    }

    // Dispatch to platform-specific bundler
    if (targetPlatform == "mac") {
        return bundleForMac(srcProject, chainPath, finalAppName, finalOutput, buildDir);
    } else if (targetPlatform == "windows") {
        return bundleForWindows(srcProject, chainPath, finalAppName, finalOutput, buildDir);
    } else if (targetPlatform == "linux") {
        return bundleForLinux(srcProject, chainPath, finalAppName, finalOutput, buildDir);
    } else if (targetPlatform == "ios") {
        return bundleForIOS(srcProject, chainPath, finalAppName, finalOutput, buildDir);
    }

    std::cerr << "Error: Platform '" << targetPlatform << "' not yet implemented.\n";
    return 1;
}

// Copy common resources (headers, shaders, etc.) to bundle
void copyCommonResources(const fs::path& exeDir, const fs::path& destDir, const fs::path& includeDir) {
    // Assets are in parent dir (build root), not in bin/
    fs::path buildRoot = exeDir.parent_path();

    // Copy shaders
    fs::path shadersDir = buildRoot / "shaders";
    if (fs::exists(shadersDir)) {
        fs::copy(shadersDir, destDir / "shaders", fs::copy_options::recursive);
    }

    // Copy templates
    fs::path templatesDir = buildRoot / "templates";
    if (fs::exists(templatesDir)) {
        fs::copy(templatesDir, destDir / "templates", fs::copy_options::recursive);
    }

    // Copy headers for hot-reload
    fs::path srcInclude = exeDir.parent_path().parent_path() / "core" / "include";
    if (!fs::exists(srcInclude)) {
        srcInclude = exeDir.parent_path().parent_path() / "include";
    }

    if (fs::exists(srcInclude)) {
        fs::copy(srcInclude, includeDir, fs::copy_options::recursive);
    }

    // Copy module headers
    for (const auto& mod : discoverModules()) {
        fs::path moduleInclude = mod.path / "include";
        if (fs::exists(moduleInclude)) {
            fs::copy(moduleInclude, includeDir,
                     fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
    }

    // Copy GLM headers
    fs::path glmInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "glm-src";
    if (fs::exists(glmInclude / "glm")) {
        fs::copy(glmInclude / "glm", includeDir / "glm", fs::copy_options::recursive);
    }

    // Copy webgpu headers
    fs::path wgpuInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "wgpu" / "include";
    if (fs::exists(wgpuInclude / "webgpu")) {
        fs::copy(wgpuInclude / "webgpu", includeDir / "webgpu", fs::copy_options::recursive);
    }
    if (fs::exists(wgpuInclude / "wgpu.h")) {
        fs::copy_file(wgpuInclude / "wgpu.h", includeDir / "wgpu.h",
                      fs::copy_options::overwrite_existing);
    }

    // Copy GLFW headers
    fs::path glfwInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "glfw-src" / "include";
    if (fs::exists(glfwInclude / "GLFW")) {
        fs::copy(glfwInclude / "GLFW", includeDir / "GLFW", fs::copy_options::recursive);
    }

    // Copy glfw3webgpu header
    fs::path glfw3wgpuDir = exeDir.parent_path().parent_path() / "deps" / "glfw3webgpu";
    if (fs::exists(glfw3wgpuDir / "glfw3webgpu.h")) {
        fs::copy_file(glfw3wgpuDir / "glfw3webgpu.h", includeDir / "glfw3webgpu.h",
                      fs::copy_options::overwrite_existing);
    }

    // Copy magic_enum headers
    fs::path magicEnumInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "magic_enum-src" / "include";
    if (fs::exists(magicEnumInclude / "magic_enum")) {
        fs::copy(magicEnumInclude / "magic_enum", includeDir / "magic_enum", fs::copy_options::recursive);
    }

    // Copy nlohmann/json headers
    fs::path jsonInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "nlohmann_json-src" / "include";
    if (fs::exists(jsonInclude / "nlohmann")) {
        fs::copy(jsonInclude / "nlohmann", includeDir / "nlohmann", fs::copy_options::recursive);
    }

    // Note: Core assets (fonts, icons) are now merged into project/assets/ at bundle time
    // by the platform-specific bundlers. This keeps all assets in one place so the
    // walk-up-hierarchy asset resolution finds them automatically.
}

// Scan chain.cpp for asset paths (e.g., "assets/images/foo.jpg")
std::set<std::string> scanChainForAssets(const fs::path& chainPath) {
    std::set<std::string> assets;

    std::ifstream file(chainPath);
    if (!file.is_open()) return assets;

    std::string line;
    // Match patterns like: "assets/..." or 'assets/...'
    std::regex assetPattern(R"([\"']((assets/[^\"']+))[\"'])");

    while (std::getline(file, line)) {
        std::sregex_iterator it(line.begin(), line.end(), assetPattern);
        std::sregex_iterator end;
        for (; it != end; ++it) {
            assets.insert((*it)[1].str());
        }
    }

    return assets;
}

// Copy project files to bundle
void copyProjectFiles(const fs::path& srcProject, const fs::path& chainPath,
                      const fs::path& destDir, const fs::path& rootDir) {
    fs::create_directories(destDir);
    fs::copy_file(chainPath, destDir / "chain.cpp", fs::copy_options::overwrite_existing);

    // Copy project-local assets
    fs::path assetsDir = srcProject / "assets";
    if (fs::exists(assetsDir) && fs::is_directory(assetsDir)) {
        fs::copy(assetsDir, destDir / "assets", fs::copy_options::recursive);
        std::cout << "Bundled: project assets folder\n";
    }

    // Copy project shaders if any
    fs::path projectShaders = srcProject / "shaders";
    if (fs::exists(projectShaders) && fs::is_directory(projectShaders)) {
        fs::copy(projectShaders, destDir / "shaders", fs::copy_options::recursive);
    }
}

// Determine which module libraries are required by analyzing the chain
std::vector<std::string> getRequiredLibraries(const fs::path& chainPath, const fs::path& exeDir) {
    std::vector<std::string> libs;

    // Core is always required
    libs.push_back("vivid-core");

    // Find the vivid source root (parent of build directory)
    // exeDir is typically build/bin, so root is build/bin/../../
    fs::path rootDir = exeDir.parent_path().parent_path();

    // Scan chain for library dependencies
    ModuleRegistry registry;
    registry.setRootDir(rootDir);
    auto libraries = registry.discoverFromChain(chainPath);

    for (const auto& lib : libraries) {
        libs.push_back(lib.name);
    }

    return libs;
}

// Get platform-specific library filename
std::string getLibraryFilename(const std::string& libName) {
#ifdef __APPLE__
    return "lib" + libName + ".dylib";
#elif defined(_WIN32)
    return libName + ".dll";
#else
    return "lib" + libName + ".so";
#endif
}

// macOS .app bundle
int bundleForMac(const fs::path& srcProject, const fs::path& chainPath,
                 const std::string& appName, const fs::path& outputDir,
                 const fs::path& buildDir) {
#ifdef __APPLE__
    fs::path appPath = outputDir / (appName + ".app");

    if (fs::exists(appPath)) {
        std::cerr << "Error: Output path already exists: " << appPath << "\n";
        return 1;
    }

    std::cout << "Bundling " << srcProject.filename().string() << " -> " << appPath.filename().string() << "\n";

    // Use the production build directory
    // Look for vivid-production (clean executable with Runtime class)
    fs::path exeDir = buildDir / "bin";
    fs::path exePath = exeDir / "vivid-production";

    if (!fs::exists(exePath)) {
        std::cerr << "Error: Production executable not found: " << exePath << "\n";
        std::cerr << "Make sure the build completed successfully.\n";
        return 1;
    }

    try {
        // Create .app bundle structure
        fs::path contentsPath = appPath / "Contents";
        fs::path macosPath = contentsPath / "MacOS";
        fs::path resourcesPath = contentsPath / "Resources";
        fs::path frameworksPath = contentsPath / "Frameworks";
        fs::path bundleInclude = contentsPath / "include";
        fs::path bundleLibPath = contentsPath / "lib";

        fs::create_directories(macosPath);
        fs::create_directories(resourcesPath);
        fs::create_directories(frameworksPath);
        fs::create_directories(bundleInclude);
        fs::create_directories(bundleLibPath);

        // Copy production executable (renamed to appName for cleaner bundle)
        fs::copy_file(exePath, macosPath / appName);

        // Copy only required dylibs (based on chain analysis)
        auto requiredLibs = getRequiredLibraries(chainPath, exeDir);
        std::cout << "Bundled libraries: ";
        for (size_t i = 0; i < requiredLibs.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << requiredLibs[i];
        }
        std::cout << "\n";

        // Libraries are in lib/ directory (sibling to bin/)
        // Copy to Contents/lib/ to match RPATH (@executable_path/../lib)
        fs::path libDir = exeDir.parent_path() / "lib";
        for (const auto& libName : requiredLibs) {
            std::string libFile = getLibraryFilename(libName);
            fs::path libPath = libDir / libFile;
            if (fs::exists(libPath)) {
                fs::copy_file(libPath, bundleLibPath / libFile);
            } else {
                std::cerr << "Warning: Library not found: " << libFile << "\n";
            }
        }

        // Copy common resources
        copyCommonResources(exeDir, macosPath, bundleInclude);

        // Copy project files (including shared assets from root)
        fs::path projectDest = resourcesPath / "project";
        fs::path rootDir = buildDir.parent_path();  // buildDir is rootDir/build-bundle
        copyProjectFiles(srcProject, chainPath, projectDest, rootDir);

        // Merge core assets (fonts) into project/assets/
        // This keeps all assets in one place - walk-up-hierarchy finds them automatically
        fs::path coreAssetsPath = buildDir / "assets";
        if (fs::exists(coreAssetsPath)) {
            fs::path projectAssets = projectDest / "assets";
            fs::create_directories(projectAssets);
            fs::copy(coreAssetsPath, projectAssets,
                     fs::copy_options::recursive | fs::copy_options::skip_existing);
        }

        // Copy app icon (project icon overrides default)
        fs::path projectIcon = srcProject / "icon.icns";
        fs::path defaultIcon = buildDir / "assets" / "icons" / "vivid.icns";
        fs::path destIcon = resourcesPath / "AppIcon.icns";

        if (fs::exists(projectIcon)) {
            fs::copy_file(projectIcon, destIcon);
            std::cout << "Using project icon: " << projectIcon.filename().string() << "\n";
        } else if (fs::exists(defaultIcon)) {
            fs::copy_file(defaultIcon, destIcon);
            std::cout << "Using default Vivid icon\n";
        } else {
            std::cout << "Warning: No icon found (looked for " << projectIcon << " and " << defaultIcon << ")\n";
        }

        // Production executable is self-contained - no launcher script needed
        // The executable finds assets relative to itself (../Resources/project)

        // Create Info.plist
        std::ofstream plist(contentsPath / "Info.plist");
        plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        plist << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
              << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
        plist << "<plist version=\"1.0\">\n<dict>\n";
        plist << "    <key>CFBundleName</key><string>" << appName << "</string>\n";
        plist << "    <key>CFBundleDisplayName</key><string>" << appName << "</string>\n";
        plist << "    <key>CFBundleIdentifier</key><string>com.vivid."
              << srcProject.filename().string() << "</string>\n";
        plist << "    <key>CFBundleVersion</key><string>" << VERSION << "</string>\n";
        plist << "    <key>CFBundleShortVersionString</key><string>" << VERSION << "</string>\n";
        plist << "    <key>CFBundleExecutable</key><string>" << appName << "</string>\n";
        plist << "    <key>CFBundlePackageType</key><string>APPL</string>\n";
        plist << "    <key>CFBundleIconFile</key><string>AppIcon</string>\n";
        plist << "    <key>NSHighResolutionCapable</key><true/>\n";
        plist << "    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>\n";
        plist << "</dict>\n</plist>\n";
        plist.close();

        std::ofstream pkginfo(contentsPath / "PkgInfo");
        pkginfo << "APPL????";
        pkginfo.close();

        std::cout << "\nBundle created: " << appPath << "\n\n";
        std::cout << "Contents:\n";
        std::cout << "  " << appPath.filename().string() << "/Contents/MacOS/" << appName << " (executable)\n";
        std::cout << "  " << appPath.filename().string() << "/Contents/Resources/project/ (assets)\n";
        std::cout << "  " << appPath.filename().string() << "/Contents/lib/ (libraries)\n";
        std::cout << "\nRun with:\n  open " << appPath.filename().string() << "\n";
        std::cout << "\nNote: Production bundle - NO hot-reload, NO MCP server, NO visualizer.\n";

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating bundle: " << e.what() << "\n";
        return 1;
    }

    return 0;
#else
    (void)srcProject; (void)chainPath; (void)appName; (void)outputDir; (void)buildDir;
    std::cerr << "Error: Mac bundling only available on macOS.\n";
    return 1;
#endif
}

// Windows bundling - creates a folder with exe + dlls
int bundleForWindows(const fs::path& srcProject, const fs::path& chainPath,
                     const std::string& appName, const fs::path& outputDir,
                     const fs::path& buildDir) {
#ifdef _WIN32
    fs::path bundlePath = outputDir / appName;

    if (fs::exists(bundlePath)) {
        std::cerr << "Error: Output path already exists: " << bundlePath << "\n";
        return 1;
    }

    std::cout << "Bundling " << srcProject.filename().string() << " -> " << bundlePath.filename().string() << "\n";

    // Use the production build directory
    // Look for vivid-production.exe (clean executable with Runtime class)
    fs::path exeDir = buildDir / "bin";
    fs::path exePath = exeDir / "vivid-production.exe";

    if (!fs::exists(exePath)) {
        std::cerr << "Error: Production executable not found: " << exePath << "\n";
        std::cerr << "Make sure the build completed successfully.\n";
        return 1;
    }

    try {
        fs::path binPath = bundlePath / "bin";
        fs::path includePath = bundlePath / "include";
        fs::path projectPath = bundlePath / "project";

        fs::create_directories(binPath);
        fs::create_directories(includePath);

        // Copy production executable (renamed to appName.exe)
        fs::copy_file(exePath, binPath / (appName + ".exe"));

        // Copy only required DLLs (based on chain analysis)
        auto requiredLibs = getRequiredLibraries(chainPath, exeDir);
        std::cout << "Bundled libraries: ";
        for (size_t i = 0; i < requiredLibs.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << requiredLibs[i];
        }
        std::cout << "\n";

        // DLLs are in lib/ directory on Windows
        fs::path libDir = buildDir / "lib";
        for (const auto& libName : requiredLibs) {
            std::string dllFile = getLibraryFilename(libName);
            fs::path dllPath = libDir / dllFile;
            if (fs::exists(dllPath)) {
                fs::copy_file(dllPath, binPath / dllFile);
            } else {
                std::cerr << "Warning: Library not found: " << dllFile << "\n";
            }
        }

        // Copy glfw3.dll (always required for windowing)
        fs::path glfwPath = libDir / "glfw3.dll";
        if (fs::exists(glfwPath)) {
            fs::copy_file(glfwPath, binPath / "glfw3.dll");
        }

        // Copy common resources
        copyCommonResources(exeDir, binPath, includePath);

        // Copy project files (including shared assets from root)
        fs::path rootDir = buildDir.parent_path();  // buildDir is rootDir/build-bundle
        copyProjectFiles(srcProject, chainPath, projectPath, rootDir);

        // Merge core assets (fonts) into project/assets/
        // This keeps all assets in one place - walk-up-hierarchy finds them automatically
        fs::path coreAssetsPath = buildDir / "assets";
        if (fs::exists(coreAssetsPath)) {
            fs::path projectAssets = projectPath / "assets";
            fs::create_directories(projectAssets);
            fs::copy(coreAssetsPath, projectAssets,
                     fs::copy_options::recursive | fs::copy_options::skip_existing);
        }

        // Copy app icon (project icon overrides default)
        // Note: .ico can be used for creating shortcuts with custom icons
        fs::path projectIcon = srcProject / "icon.ico";
        fs::path defaultIcon = buildDir / "assets" / "icons" / "vivid.ico";
        fs::path destIcon = bundlePath / (appName + ".ico");

        if (fs::exists(projectIcon)) {
            fs::copy_file(projectIcon, destIcon);
            std::cout << "Using project icon: " << projectIcon.filename().string() << "\n";
        } else if (fs::exists(defaultIcon)) {
            fs::copy_file(defaultIcon, destIcon);
            std::cout << "Using default Vivid icon\n";
        }

        // Production executable is self-contained, but a batch file makes it easier to run
        fs::path launcherPath = bundlePath / (appName + ".bat");
        std::ofstream launcher(launcherPath);
        launcher << "@echo off\r\n";
        launcher << "cd /d \"%~dp0bin\"\r\n";
        launcher << "start " << appName << ".exe %*\r\n";
        launcher.close();

        std::cout << "\nBundle created: " << bundlePath << "\n\n";
        std::cout << "Contents:\n";
        std::cout << "  " << appName << "/" << appName << ".bat (launcher)\n";
        std::cout << "  " << appName << "/bin/" << appName << ".exe (executable)\n";
        std::cout << "  " << appName << "/project/ (assets)\n";
        std::cout << "\nRun with:\n  " << appName << ".bat  or  bin\\" << appName << ".exe\n";
        std::cout << "\nNote: Production bundle - NO hot-reload, NO MCP server, NO visualizer.\n";

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating bundle: " << e.what() << "\n";
        return 1;
    }

    return 0;
#else
    (void)srcProject; (void)chainPath; (void)appName; (void)outputDir; (void)buildDir;
    std::cerr << "Error: Windows bundling only available on Windows.\n";
    return 1;
#endif
}

// Linux bundling - creates a folder structure (works for Raspberry Pi too)
int bundleForLinux(const fs::path& srcProject, const fs::path& chainPath,
                   const std::string& appName, const fs::path& outputDir,
                   const fs::path& buildDir) {
#if defined(__linux__)
    fs::path bundlePath = outputDir / appName;

    if (fs::exists(bundlePath)) {
        std::cerr << "Error: Output path already exists: " << bundlePath << "\n";
        return 1;
    }

    std::cout << "Bundling " << srcProject.filename().string() << " -> " << bundlePath.filename().string() << "\n";

    // Use the production build directory
    // Look for vivid-production (clean executable with Runtime class)
    fs::path exeDir = buildDir / "bin";
    fs::path exePath = exeDir / "vivid-production";

    if (!fs::exists(exePath)) {
        std::cerr << "Error: Production executable not found: " << exePath << "\n";
        std::cerr << "Make sure the build completed successfully.\n";
        return 1;
    }

    try {
        fs::path binPath = bundlePath / "bin";
        fs::path libPath = bundlePath / "lib";
        fs::path includePath = bundlePath / "include";
        fs::path projectPath = bundlePath / "project";

        fs::create_directories(binPath);
        fs::create_directories(libPath);
        fs::create_directories(includePath);

        // Copy production executable (renamed to appName)
        fs::copy_file(exePath, binPath / appName);
        fs::permissions(binPath / appName, fs::perms::owner_exec | fs::perms::group_exec |
                                            fs::perms::others_exec, fs::perm_options::add);

        // Copy only required shared libraries (based on chain analysis)
        auto requiredLibs = getRequiredLibraries(chainPath, exeDir);
        std::cout << "Required libraries: ";
        for (size_t i = 0; i < requiredLibs.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << requiredLibs[i];
        }
        std::cout << "\n";

        // Libraries are in lib/ directory
        fs::path srcLibDir = buildDir / "lib";
        for (const auto& libName : requiredLibs) {
            std::string soFile = getLibraryFilename(libName);
            fs::path srcLib = srcLibDir / soFile;
            if (fs::exists(srcLib)) {
                fs::copy_file(srcLib, libPath / soFile);
            } else {
                std::cerr << "Warning: Library not found: " << soFile << "\n";
            }
        }

        // Copy common resources
        copyCommonResources(exeDir, binPath, includePath);

        // Copy project files (including shared assets from root)
        fs::path rootDir = buildDir.parent_path();  // buildDir is rootDir/build-bundle
        copyProjectFiles(srcProject, chainPath, projectPath, rootDir);

        // Merge core assets (fonts) into project/assets/
        // This keeps all assets in one place - walk-up-hierarchy finds them automatically
        fs::path coreAssetsPath = buildDir / "assets";
        if (fs::exists(coreAssetsPath)) {
            fs::path projectAssets = projectPath / "assets";
            fs::create_directories(projectAssets);
            fs::copy(coreAssetsPath, projectAssets,
                     fs::copy_options::recursive | fs::copy_options::skip_existing);
        }

        // Create launcher script (sets up LD_LIBRARY_PATH)
        fs::path launcherPath = bundlePath / ("run-" + appName + ".sh");
        std::ofstream launcher(launcherPath);
        launcher << "#!/bin/bash\n";
        launcher << "SCRIPT_DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n";
        launcher << "export LD_LIBRARY_PATH=\"$SCRIPT_DIR/lib:$LD_LIBRARY_PATH\"\n";
        launcher << "exec \"$SCRIPT_DIR/bin/" << appName << "\" \"$@\"\n";
        launcher.close();

        fs::permissions(launcherPath, fs::perms::owner_exec | fs::perms::group_exec |
                                       fs::perms::others_exec | fs::perms::owner_read |
                                       fs::perms::group_read | fs::perms::others_read |
                                       fs::perms::owner_write, fs::perm_options::add);

        // Create .desktop file for desktop integration
        fs::path desktopPath = bundlePath / (appName + ".desktop");
        std::ofstream desktop(desktopPath);
        desktop << "[Desktop Entry]\n";
        desktop << "Type=Application\n";
        desktop << "Name=" << appName << "\n";
        desktop << "Exec=" << bundlePath.string() << "/run-" << appName << ".sh\n";
        desktop << "Terminal=false\n";
        desktop << "Categories=Graphics;AudioVideo;\n";
        desktop.close();

        std::cout << "\nBundle created: " << bundlePath << "\n\n";
        std::cout << "Contents:\n";
        std::cout << "  " << appName << "/run-" << appName << ".sh (launcher)\n";
        std::cout << "  " << appName << "/bin/" << appName << " (executable)\n";
        std::cout << "  " << appName << "/lib/ (shared libraries)\n";
        std::cout << "  " << appName << "/project/ (assets)\n";
        std::cout << "\nRun with:\n  ./" << appName << "/run-" << appName << ".sh\n";
        std::cout << "\nNote: Production bundle - NO hot-reload, NO MCP server, NO visualizer.\n";

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating bundle: " << e.what() << "\n";
        return 1;
    }

    return 0;
#else
    (void)srcProject; (void)chainPath; (void)appName; (void)outputDir; (void)buildDir;
    std::cerr << "Error: Linux bundling only available on Linux.\n";
    return 1;
#endif
}

// iOS bundling - placeholder for now
int bundleForIOS(const fs::path& srcProject, const fs::path& chainPath,
                 const std::string& appName, const fs::path& outputDir,
                 const fs::path& buildDir) {
    (void)srcProject; (void)chainPath; (void)appName; (void)outputDir; (void)buildDir;
    std::cerr << "Error: iOS export is not yet implemented.\n";
    std::cerr << "This requires Xcode and iOS provisioning profiles.\n";
    return 1;
}

// Helper: Handle 'operators' subcommand
static int handleOperatorsCommand(const std::string& operatorName, bool jsonOutput) {
    auto& registry = OperatorRegistry::instance();

    // If operator name specified, show details for that operator
    if (!operatorName.empty()) {
        const auto* meta = registry.find(operatorName);
        if (!meta) {
            std::cerr << "Error: Operator '" << operatorName << "' not found.\n";
            std::cerr << "Use 'vivid operators' to list all available operators.\n";
            return 1;
        }

        if (jsonOutput) {
            json op;
            op["name"] = meta->name;
            op["category"] = meta->category;
            op["description"] = meta->description;
            op["module"] = meta->module.empty() ? json(nullptr) : json(meta->module);
            op["requiresInput"] = meta->requiresInput;
            op["outputType"] = outputKindName(meta->outputKind);
            op["params"] = json::array();

            if (meta->factory) {
                try {
                    auto tempOp = meta->factory();
                    auto params = tempOp->params();
                    for (const auto& p : params) {
                        json param;
                        param["name"] = p.name;
                        param["min"] = p.minVal;
                        param["max"] = p.maxVal;
                        param["default"] = p.defaultVal[0];
                        op["params"].push_back(param);
                    }
                } catch (...) {}
            }
            std::cout << op.dump(2) << std::endl;
        } else {
            std::cout << "# " << meta->name << "\n\n";
            std::cout << meta->description << "\n\n";
            std::cout << "Category: " << meta->category << "\n";
            if (!meta->module.empty()) {
                std::cout << "Module: " << meta->module << "\n";
            }
            std::cout << "Output: " << outputKindName(meta->outputKind) << "\n";
            std::cout << "Requires input: " << (meta->requiresInput ? "Yes" : "No") << "\n";

            if (meta->factory) {
                try {
                    auto tempOp = meta->factory();
                    auto params = tempOp->params();
                    if (!params.empty()) {
                        std::cout << "\nParameters:\n";
                        for (const auto& p : params) {
                            std::cout << "  " << p.name;
                            std::cout << " (" << p.minVal << " - " << p.maxVal << ")";
                            std::cout << " default: " << p.defaultVal[0] << "\n";
                        }
                    }
                } catch (...) {
                    std::cout << "\n(Could not inspect parameters)\n";
                }
            }

            std::cout << "\nUsage:\n";
            std::cout << "  auto& op = chain.add<" << meta->name << ">(\"name\");\n";
            if (meta->requiresInput) {
                std::cout << "  op.input(&other);\n";
            }
        }
        return 0;
    }

    // List all operators
    if (jsonOutput) {
        registry.outputJson();
    } else {
        const auto& ops = registry.operators();
        std::cout << "Available operators (" << ops.size() << "):\n\n";

        std::string currentCategory;
        for (const auto& op : ops) {
            if (op.category != currentCategory) {
                if (!currentCategory.empty()) std::cout << "\n";
                currentCategory = op.category;
                std::cout << "## " << currentCategory << "\n";
            }
            std::cout << "  " << op.name;
            if (!op.module.empty()) {
                std::cout << " [" << op.module << "]";
            }
            std::cout << " - " << op.description << "\n";
        }

        if (ops.empty()) {
            std::cout << "No operators registered. This may be a build issue.\n";
        }

        std::cout << "\nFor details: vivid operators <name>\n";
    }
    return 0;
}

// Helper: Handle 'modules' subcommand
static int handleModulesCommand(CLI::App* installCmd, CLI::App* removeCmd, CLI::App* updateCmd, CLI::App* listCmd,
                                CLI::App* linkCmd, CLI::App* unlinkCmd,
                                const std::string& installUrl, const std::string& installRef,
                                const std::string& removeName, const std::string& updateName,
                                const std::string& linkPath, const std::string& unlinkName,
                                bool jsonOutput) {
    auto& moduleMgr = ModuleManager::instance();

    if (installCmd->parsed()) {
        return moduleMgr.install(installUrl, installRef) ? 0 : 1;
    }

    if (removeCmd->parsed()) {
        return moduleMgr.remove(removeName) ? 0 : 1;
    }

    if (updateCmd->parsed()) {
        return moduleMgr.update(updateName) ? 0 : 1;
    }

    if (linkCmd->parsed()) {
        return moduleMgr.linkModule(linkPath) ? 0 : 1;
    }

    if (unlinkCmd->parsed()) {
        return moduleMgr.unlinkModule(unlinkName) ? 0 : 1;
    }

    // Default: list
    if (jsonOutput) {
        moduleMgr.outputJson();
    } else {
        auto libs = moduleMgr.listInstalled();
        if (libs.empty()) {
            std::cout << "No modules installed.\n\n";
            std::cout << "Install with:\n";
            std::cout << "  vivid modules install <git-url>\n\n";
            std::cout << "Example:\n";
            std::cout << "  vivid modules install https://github.com/seethroughlab/vivid-onnx\n";
        } else {
            std::cout << "Installed modules (" << libs.size() << "):\n\n";
            for (const auto& lib : libs) {
                std::cout << "  " << lib.name << " v" << lib.version;
                if (lib.builtFrom == "linked") {
                    std::cout << " [linked]";
                } else if (!lib.gitRef.empty()) {
                    std::cout << " (" << lib.gitRef << ")";
                }
                std::cout << "\n";
                if (listCmd->parsed()) {
                    std::cout << "    Source: " << lib.builtFrom << "\n";
                    std::cout << "    Path: " << lib.installPath.string() << "\n";
                }
            }
        }
    }
    return 0;
}

// Helper to parse frame specification (e.g., "5", "0,5,10", "0-11", "0-20:2")
static bool parseFrameSpec(const std::string& spec, std::set<int>& frames) {
    frames.clear();
    size_t start = 0;
    while (start < spec.length()) {
        size_t comma = spec.find(',', start);
        std::string part = spec.substr(start, comma == std::string::npos ? std::string::npos : comma - start);

        // Check for range: N-M or N-M:S
        size_t dash = part.find('-');
        if (dash != std::string::npos && dash > 0) {
            int rangeStart, rangeEnd, step = 1;
            size_t colon = part.find(':', dash);

            try {
                rangeStart = std::stoi(part.substr(0, dash));
                if (colon != std::string::npos) {
                    rangeEnd = std::stoi(part.substr(dash + 1, colon - dash - 1));
                    step = std::stoi(part.substr(colon + 1));
                } else {
                    rangeEnd = std::stoi(part.substr(dash + 1));
                }
            } catch (...) {
                return false;
            }

            if (rangeStart < 0 || rangeEnd < 0 || step < 1 || rangeEnd < rangeStart) {
                return false;
            }

            for (int i = rangeStart; i <= rangeEnd; i += step) {
                frames.insert(i);
            }
        } else {
            try {
                int frame = std::stoi(part);
                if (frame < 0) return false;
                frames.insert(frame);
            } catch (...) {
                return false;
            }
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return !frames.empty();
}

// Parse all CLI arguments using CLI11
ParseResult parseArgs(int argc, char** argv) {
    ParseResult result;

    CLI::App app{"Vivid - Creative coding framework with hot-reload\n\n"
                 "USAGE:\n"
                 "  vivid <project-path> [OPTIONS]    Run a project\n"
                 "  vivid <SUBCOMMAND>                Run a subcommand\n"};
    app.set_version_flag("-v,--version", std::string(VERSION));
    app.set_help_flag("-h,--help", "Show this help");
    app.allow_extras(true);  // Allow project path as positional

    // === Runtime options (for running a project) ===
    std::string projectPath;
    std::string snapshotPath;
    std::string snapshotFrameSpec;
    bool headless = false;
    std::string renderSize;
    bool fullscreen = false;
    std::string recordPath;
    float recordFps = 60.0f;
    float recordDuration = 0.0f;
    bool recordAudio = false;
    std::string recordCodec = "h264";
    int maxFrames = 0;
    bool showUI = false;
    std::string audioSnapshotPath;
    float audioSnapshotDuration = 1.0f;

    app.add_option("project", projectPath, "Project directory to run")
       ->type_name("PATH");
    app.add_option("--snapshot", snapshotPath, "Save screenshot to file and exit")
       ->type_name("FILE");
    app.add_option("--snapshot-frame", snapshotFrameSpec,
                   "Frame(s) to capture: 5 | 0,5,10 | 0-11 | 0-20:2")
       ->type_name("SPEC");
    app.add_option("--audio-snapshot", audioSnapshotPath, "Capture audio to WAV file and exit")
       ->type_name("FILE");
    app.add_option("--audio-snapshot-duration", audioSnapshotDuration,
                   "Audio capture duration in seconds (default: 1.0)")
       ->check(CLI::Range(0.01f, 300.0f));
    app.add_flag("--headless", headless, "Run without window (requires --snapshot or --record)");
    app.add_option("--render", renderSize, "Render resolution (e.g., 1920x1080)")
       ->type_name("WxH");
    app.add_flag("--fullscreen", fullscreen, "Start in fullscreen mode");
    app.add_option("--record", recordPath, "Record video to file")
       ->type_name("FILE");
    app.add_option("--record-fps", recordFps, "Recording frame rate (default: 60)")
       ->check(CLI::Range(0.1f, 240.0f));
    app.add_option("--record-duration", recordDuration, "Recording duration in seconds")
       ->check(CLI::Range(0.01f, 86400.0f));
    app.add_flag("--record-audio", recordAudio, "Include audio in recording");
    app.add_option("--record-codec", recordCodec, "Video codec: h264, h265, prores")
       ->type_name("CODEC");
    app.add_option("--frames", maxFrames, "Exit after N frames")
       ->check(CLI::Range(1, 10000000));
    app.add_flag("--show-ui", showUI, "Show chain visualizer and IDE panel");
    bool snapshotUI = false;
    app.add_flag("--snapshot-ui", snapshotUI, "Include devtools UI in snapshot (implies --show-ui)");
    bool exitOnError = false;
    app.add_flag("--exit-on-error", exitOnError, "Exit immediately on compile error (for agent/CI workflows)");
    std::string scriptPath;
    app.add_option("--script", scriptPath, "Playback script JSON file (event injection)")
       ->type_name("FILE");

    // === Subcommands ===

    // 'new' subcommand
    std::string newProjectName;
    std::string newTemplate = "blank";
    std::vector<std::string> newModules;
    bool newMinimal = false;
    bool newYes = false;
    bool newInPlace = false;
    bool newNoInPlace = false;

    auto* newCmd = app.add_subcommand("new", "Create a new project");
    newCmd->add_option("name", newProjectName, "Project name")->required();
    newCmd->add_option("-t,--template", newTemplate, "Template: blank, noise-demo, feedback, audio-visualizer, 3d-orbit")
          ->default_val("blank");
    newCmd->add_option("-m,--modules", newModules,
                       "Modules to include (use 'vivid modules' to see available)")
          ->delimiter(',');
    newCmd->add_flag("--minimal", newMinimal, "Use minimal template");
    newCmd->add_flag("-y,--yes", newYes, "Skip confirmation prompts");
    newCmd->add_flag("--in-place", newInPlace, "Create files in current directory instead of subdirectory");
    newCmd->add_flag("--no-in-place", newNoInPlace, "Force creation in subdirectory (disable auto-detection)");

    // 'bundle' subcommand
    std::string bundleProjectPath;
    std::string bundleOutput;
    std::string bundleName;
    std::string bundlePlatform;

    auto* bundleCmd = app.add_subcommand("bundle", "Bundle project as standalone app");
    bundleCmd->add_option("project", bundleProjectPath, "Project path")->required();
    bundleCmd->add_option("-o,--output", bundleOutput, "Output directory");
    bundleCmd->add_option("-n,--name", bundleName, "App display name");
    bundleCmd->add_option("-p,--platform", bundlePlatform,
                          "Target platform: mac, windows, linux, ios (default: current platform)");

    // 'operators' subcommand
    bool operatorsJson = false;
    std::string operatorName;
    auto* operatorsCmd = app.add_subcommand("operators", "List available operators");
    operatorsCmd->add_option("name", operatorName, "Show details for specific operator");
    operatorsCmd->add_flag("--json", operatorsJson, "Output as JSON");

    // 'modules' subcommand group
    auto* modulesCmd = app.add_subcommand("modules", "Manage installed modules");
    modulesCmd->require_subcommand(0, 1);  // 0 or 1 subcommand

    // modules list (default when no subcommand)
    bool modulesJson = false;
    auto* modulesListCmd = modulesCmd->add_subcommand("list", "List installed modules");
    modulesListCmd->add_flag("--json", modulesJson, "Output as JSON");

    // modules install
    std::string modulesInstallUrl;
    std::string modulesInstallRef;
    auto* modulesInstallCmd = modulesCmd->add_subcommand("install", "Install module from git URL");
    modulesInstallCmd->add_option("url", modulesInstallUrl, "Git repository URL")->required();
    modulesInstallCmd->add_option("-r,--ref", modulesInstallRef, "Git ref (tag, branch, or commit)");

    // modules remove
    std::string modulesRemoveName;
    auto* modulesRemoveCmd = modulesCmd->add_subcommand("remove", "Remove an installed module");
    modulesRemoveCmd->add_option("name", modulesRemoveName, "Library name")->required();

    // modules update
    std::string modulesUpdateName;
    auto* modulesUpdateCmd = modulesCmd->add_subcommand("update", "Update module(s)");
    modulesUpdateCmd->add_option("name", modulesUpdateName, "Library name (empty = update all)");

    // modules link (for development)
    std::string modulesLinkPath;
    auto* modulesLinkCmd = modulesCmd->add_subcommand("link", "Link a local module for development");
    modulesLinkCmd->add_option("path", modulesLinkPath, "Path to module directory")->required();

    // modules unlink
    std::string modulesUnlinkName;
    auto* modulesUnlinkCmd = modulesCmd->add_subcommand("unlink", "Unlink a development module");
    modulesUnlinkCmd->add_option("name", modulesUnlinkName, "Module name")->required();

    // 'check' subcommand — run assertions against a project
    std::string checkProjectPath;
    std::string checkAssertionPath;
    std::string checkScript;
    int checkFrame = -1;
    bool checkVerbose = false;
    float checkDuration = 0.0f;

    auto* checkCmd = app.add_subcommand("check", "Run assertions against a project");
    checkCmd->add_option("project", checkProjectPath, "Project path")->required();
    checkCmd->add_option("--assertions", checkAssertionPath, "Assertion file (default: vivid-assertions.json in project)");
    checkCmd->add_option("--script", checkScript, "Playback script JSON file for event injection");
    checkCmd->add_option("--frame", checkFrame, "Frame to evaluate at (overrides assertion file)");
    checkCmd->add_flag("--verbose", checkVerbose, "Print each assertion result");
    checkCmd->add_option("--duration", checkDuration, "Run chain for N seconds before evaluating assertions")
           ->check(CLI::Range(0.01f, 300.0f));

    // 'inspect' subcommand — dump inspection data as JSON
    std::string inspectProjectPath;
    std::string inspectScript;
    int inspectFrame = -1;
    std::string inspectOutDir;
    bool inspectPerOperator = false;
    float inspectDuration = 0.0f;
    int inspectSamples = 1;

    std::string inspectResolution;

    auto* inspectCmd = app.add_subcommand("inspect", "Dump inspection data as JSON");
    inspectCmd->add_option("project", inspectProjectPath, "Project path")->required();
    inspectCmd->add_option("--script", inspectScript, "Playback script JSON file for event injection");
    inspectCmd->add_option("--frame", inspectFrame, "Frame to inspect (default: 10)");
    inspectCmd->add_option("--out", inspectOutDir, "Output directory for JSON + snapshot");
    inspectCmd->add_flag("--per-operator", inspectPerOperator, "Include texture analysis for each operator");
    inspectCmd->add_option("--duration", inspectDuration, "Capture duration in seconds for multi-sample inspect")
             ->check(CLI::Range(0.01f, 300.0f));
    inspectCmd->add_option("--samples", inspectSamples, "Number of inspection samples (requires --duration)")
             ->check(CLI::Range(1, 1000));
    inspectCmd->add_option("--resolution", inspectResolution, "Render resolution (e.g., 960x540)")
             ->type_name("WxH");

    // 'export' subcommand — headless A/V export with optional playback script
    std::string exportProjectPath;
    std::string exportOutput;
    std::string exportScript;
    float exportDuration = 0.0f;
    float exportFps = 60.0f;
    float exportStart = 0.0f;
    float exportEnd = 0.0f;
    bool exportAudio = false;
    bool exportAudioOnly = false;
    std::string exportCodec = "h264";
    std::string exportResolution;
    bool exportQuiet = false;

    auto* exportCmd = app.add_subcommand("export", "Export video with optional playback script");
    exportCmd->add_option("project", exportProjectPath, "Project path")->required();
    exportCmd->add_option("-o,--output", exportOutput, "Output video file path")->required();
    exportCmd->add_option("--script", exportScript, "Playback script JSON file");
    exportCmd->add_option("--duration", exportDuration, "Duration in seconds")
             ->check(CLI::Range(0.01f, 86400.0f));
    exportCmd->add_option("--start", exportStart, "Start time in seconds (skips ahead before recording)")
             ->check(CLI::Range(0.0f, 86400.0f));
    exportCmd->add_option("--end", exportEnd, "End time in seconds (alternative to --duration when used with --start)")
             ->check(CLI::Range(0.01f, 86400.0f));
    exportCmd->add_option("--fps", exportFps, "Frame rate (default: 60)")
             ->check(CLI::Range(0.1f, 240.0f));
    exportCmd->add_flag("--audio", exportAudio, "Include audio track");
    exportCmd->add_flag("--audio-only", exportAudioOnly, "Export audio only to WAV file (no video)");
    exportCmd->add_option("--codec", exportCodec, "Video codec: h264, h265, prores")
             ->type_name("CODEC");
    exportCmd->add_option("--resolution", exportResolution, "Render resolution (e.g., 1920x1080)")
             ->type_name("WxH");
    std::string exportSection;
    exportCmd->add_option("--section", exportSection, "Export a specific section by name (requires Song operator)");
    exportCmd->add_flag("--quiet", exportQuiet, "Suppress progress output");

    // 'build' subcommand — compile chain and report structured errors
    std::string buildProjectPath;
    auto* buildCmd = app.add_subcommand("build", "Compile a chain and report structured errors as JSON");
    buildCmd->add_option("project", buildProjectPath, "Project path")->required();

    // 'params' subcommand — list all tweakable parameters
    std::string paramsProjectPath;
    auto* paramsCmd = app.add_subcommand("params", "List all tweakable parameters as JSON");
    paramsCmd->add_option("project", paramsProjectPath, "Project path")->required();

    // 'graph' subcommand — dump chain topology
    std::string graphProjectPath;
    auto* graphCmd = app.add_subcommand("graph", "Dump chain topology as JSON");
    graphCmd->add_option("project", graphProjectPath, "Project path")->required();

    // 'docs' subcommand group — search documentation and recipes
    auto* docsCmd = app.add_subcommand("docs", "Search documentation, recipes, and examples");
    docsCmd->require_subcommand(1);

    std::string docsSearchQuery;
    auto* docsSearchCmd = docsCmd->add_subcommand("search", "Search documentation");
    docsSearchCmd->add_option("query", docsSearchQuery, "Search query")->required();

    std::string docsRecipeName;
    auto* docsRecipeCmd = docsCmd->add_subcommand("recipe", "List or show recipes");
    docsRecipeCmd->add_option("name", docsRecipeName, "Recipe name (omit to list all)");

    std::string docsExampleOp;
    auto* docsExampleCmd = docsCmd->add_subcommand("example", "Show code examples for an operator");
    docsExampleCmd->add_option("operator", docsExampleOp, "Operator name")->required();

    // 'mcp' subcommand - MCP server for Claude Code integration
#ifdef VIVID_ENABLE_MCP
    auto* mcpCmd = app.add_subcommand("mcp", "Run MCP server for Claude Code integration");
#endif

    // Parse arguments
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        result.handled = true;
        result.exitCode = app.exit(e);
        return result;
    }

    // Handle subcommands first
    if (newCmd->parsed()) {
        if (newMinimal) newTemplate = "minimal";
        // Convert explicit flags to int: -1=auto, 0=no, 1=yes
        int explicitInPlace = newInPlace ? 1 : (newNoInPlace ? 0 : -1);
        result.handled = true;
        result.exitCode = createProject(newProjectName, newTemplate, newMinimal, newYes, newModules, explicitInPlace);
        return result;
    }

    if (bundleCmd->parsed()) {
        result.handled = true;
        result.exitCode = bundleProject(bundleProjectPath, bundleOutput, bundleName, bundlePlatform);
        return result;
    }

#ifdef VIVID_ENABLE_MCP
    if (mcpCmd->parsed()) {
        result.handled = true;
        result.exitCode = mcp::runServer();
        return result;
    }
#endif

    if (operatorsCmd->parsed()) {
        loadAllModules();
        result.handled = true;
        result.exitCode = handleOperatorsCommand(operatorName, operatorsJson);
        return result;
    }

    if (modulesCmd->parsed()) {
        result.handled = true;
        result.exitCode = handleModulesCommand(modulesInstallCmd, modulesRemoveCmd, modulesUpdateCmd, modulesListCmd,
                                               modulesLinkCmd, modulesUnlinkCmd,
                                               modulesInstallUrl, modulesInstallRef, modulesRemoveName,
                                               modulesUpdateName, modulesLinkPath, modulesUnlinkName,
                                               modulesJson);
        return result;
    }

    if (docsCmd->parsed()) {
        result.handled = true;

        if (docsSearchCmd->parsed()) {
            auto matches = docs::searchDocs(docsSearchQuery);
            if (matches.empty()) {
                std::cout << "No matches found for '" << docsSearchQuery << "'" << std::endl;
                result.exitCode = 1;
            } else {
                std::cout << matches.dump(2) << std::endl;
            }
        } else if (docsRecipeCmd->parsed()) {
            auto recipes = docs::getRecipes(docsRecipeName);
            if (recipes.contains("error")) {
                std::cerr << recipes["error"].get<std::string>() << std::endl;
                result.exitCode = 1;
            } else {
                std::cout << recipes.dump(2) << std::endl;
            }
        } else if (docsExampleCmd->parsed()) {
            auto examples = docs::findExamples(docsExampleOp);
            std::cout << examples.dump(2) << std::endl;
            if (examples["count"].get<int>() == 0) {
                result.exitCode = 1;
            }
        }

        return result;
    }

    // check/inspect subcommands — need full app lifecycle, so return AppConfig
    if (checkCmd->parsed()) {
        AppConfig config;
        config.projectPath = checkProjectPath;
        config.checkMode = true;
        config.headless = true;
        config.checkFrame = checkFrame;
        config.checkDuration = checkDuration;
        config.verboseCheck = checkVerbose;
        if (!checkScript.empty()) config.exportScript = checkScript;

        // Resolve assertion file path
        if (!checkAssertionPath.empty()) {
            config.assertionPath = checkAssertionPath;
        } else {
            fs::path defaultPath = fs::path(checkProjectPath) / "vivid-assertions.json";
            if (fs::exists(defaultPath)) {
                config.assertionPath = defaultPath.string();
            } else {
                std::cerr << "Error: No assertion file found. Provide --assertions or create vivid-assertions.json in project.\n";
                result.handled = true;
                result.exitCode = 1;
                return result;
            }
        }

        result.config = config;
        return result;
    }

    if (inspectCmd->parsed()) {
        AppConfig config;
        config.projectPath = inspectProjectPath;
        config.inspectMode = true;
        config.headless = true;
        config.checkFrame = inspectFrame;
        config.inspectOutDir = inspectOutDir;
        config.inspectPerOperator = inspectPerOperator;
        config.inspectDuration = inspectDuration;
        config.inspectSamples = inspectSamples;
        if (!inspectScript.empty()) config.exportScript = inspectScript;

        // Parse inspect resolution
        if (!inspectResolution.empty()) {
            size_t x = inspectResolution.find('x');
            if (x == std::string::npos) x = inspectResolution.find('X');
            if (x != std::string::npos) {
                try {
                    config.renderWidth = std::stoi(inspectResolution.substr(0, x));
                    config.renderHeight = std::stoi(inspectResolution.substr(x + 1));
                } catch (...) {
                    std::cerr << "Error: Invalid --resolution format. Use WxH (e.g., 960x540)\n";
                    result.handled = true;
                    result.exitCode = 1;
                    return result;
                }
            }
        }

        result.config = config;
        return result;
    }

    if (exportCmd->parsed()) {
        AppConfig config;
        config.projectPath = exportProjectPath;
        config.exportMode = true;
        config.exportOutput = exportOutput;
        config.exportScript = exportScript;
        config.exportStart = exportStart;
        // --end computes duration relative to start
        if (exportEnd > 0.0f && exportDuration <= 0.0f) {
            if (exportEnd <= exportStart) {
                std::cerr << "Error: --end (" << exportEnd << ") must be greater than --start (" << exportStart << ")\n";
                result.handled = true;
                result.exitCode = 1;
                return result;
            }
            exportDuration = exportEnd - exportStart;
        }
        config.exportDuration = exportDuration;
        config.exportFps = exportFps;
        config.exportAudio = exportAudio;
        config.exportAudioOnly = exportAudioOnly;
        config.exportSection = exportSection;
        config.exportQuiet = exportQuiet;

        // Parse export resolution
        if (!exportResolution.empty()) {
            size_t x = exportResolution.find('x');
            if (x == std::string::npos) x = exportResolution.find('X');
            if (x != std::string::npos) {
                try {
                    config.renderWidth = std::stoi(exportResolution.substr(0, x));
                    config.renderHeight = std::stoi(exportResolution.substr(x + 1));
                } catch (...) {
                    std::cerr << "Error: Invalid --resolution format. Use WxH (e.g., 1920x1080)\n";
                    result.handled = true;
                    result.exitCode = 1;
                    return result;
                }
            }
        }

        // Parse export codec
        if (exportCodec == "h265" || exportCodec == "hevc") {
            config.exportCodec = ExportCodec::H265;
        } else if (exportCodec == "prores" || exportCodec == "animation") {
            config.exportCodec = ExportCodec::Animation;
        } else {
            config.exportCodec = ExportCodec::H264;
        }

        result.config = config;
        return result;
    }

    if (buildCmd->parsed()) {
        AppConfig config;
        config.projectPath = buildProjectPath;
        config.buildMode = true;
        result.config = config;
        return result;
    }

    if (paramsCmd->parsed()) {
        AppConfig config;
        config.projectPath = paramsProjectPath;
        config.paramsMode = true;
        result.config = config;
        return result;
    }

    if (graphCmd->parsed()) {
        AppConfig config;
        config.projectPath = graphProjectPath;
        config.graphMode = true;
        result.config = config;
        return result;
    }

    // No subcommand - need a project path
    if (projectPath.empty()) {
        printUsage();
        result.handled = true;
        result.exitCode = 0;
        return result;
    }

    // Build AppConfig from parsed options
    AppConfig config;
    config.projectPath = projectPath;
    config.snapshotPath = snapshotPath;
    config.audioSnapshotPath = audioSnapshotPath;
    config.audioSnapshotDuration = audioSnapshotDuration;
    config.headless = headless;
    config.startFullscreen = fullscreen;
    config.showUI = showUI;
    config.snapshotUI = snapshotUI;
    if (snapshotUI) config.showUI = true;  // --snapshot-ui implies --show-ui
    config.exitOnError = exitOnError;
    if (!scriptPath.empty()) config.exportScript = scriptPath;  // reuse exportScript field for event injection
    config.maxFrames = maxFrames;
    config.recordPath = recordPath;
    config.recordFps = recordFps;
    config.recordDuration = recordDuration;
    config.recordAudio = recordAudio;

    // Parse snapshot frames
    if (!snapshotFrameSpec.empty()) {
        if (!parseFrameSpec(snapshotFrameSpec, config.snapshotFrames)) {
            std::cerr << "Error: Invalid --snapshot-frame specification\n";
            std::cerr << "  Usage: --snapshot-frame 5 | 0,5,10 | 0-11 | 0-20:2\n";
            result.handled = true;
            result.exitCode = 1;
            return result;
        }
    }

    // Parse render size
    if (!renderSize.empty()) {
        size_t x = renderSize.find('x');
        if (x == std::string::npos) x = renderSize.find('X');
        if (x != std::string::npos) {
            try {
                config.renderWidth = std::stoi(renderSize.substr(0, x));
                config.renderHeight = std::stoi(renderSize.substr(x + 1));
            } catch (...) {
                std::cerr << "Error: Invalid --render size format. Use WxH (e.g., 1920x1080)\n";
                result.handled = true;
                result.exitCode = 1;
                return result;
            }
        }
    }

    // Parse record codec
    if (recordCodec == "h265" || recordCodec == "hevc") {
        config.recordCodec = ExportCodec::H265;
    } else if (recordCodec == "prores" || recordCodec == "animation") {
        config.recordCodec = ExportCodec::Animation;
    } else {
        config.recordCodec = ExportCodec::H264;
    }

    result.config = config;
    return result;
}

} // namespace vivid::cli
