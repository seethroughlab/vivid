// Vivid CLI Commands
// Handles: vivid new, vivid --help, vivid --version, vivid bundle, vivid operators, vivid addons

#include <vivid/cli.h>
#include <vivid/operator_registry.h>
#include <vivid/addon_manager.h>
#include <CLI/CLI.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <vector>
#include <cctype>

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
    fs::path templatePath = exeDir / "templates" / templateName / "chain.cpp";

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

// CLAUDE.md template for AI assistance
static const char* CLAUDE_MD_TEMPLATE = R"(# %PROJECT_NAME%

## What I Want to Create

[Describe your visual effect, installation, or creative coding project here. Be specific about:
- What it should look like
- How it should move/animate
- What inputs it responds to (audio, MIDI, mouse, etc.)
- The mood or aesthetic you're going for]

## Current State

- Working on: [current task]
- Issues: [any problems]

## Addons Enabled

%ADDONS_LIST%

## Style Preferences

- [Add any preferences for how you want code written]

## Resources

- Run with: `vivid .` (from this directory)
- Operator reference: https://github.com/jeff/vivid/blob/main/docs/LLM-REFERENCE.md
- Effect recipes: https://github.com/jeff/vivid/blob/main/docs/RECIPES.md

## Notes for AI Assistants

When helping with this project:
1. Read chain.cpp first to understand the current effect chain
2. Suggest changes by showing the modified code
3. Explain what each operator does when adding new ones
4. Keep chains simple - fewer operators is usually better
)";

void printUsage() {
    std::cout << "Vivid - Creative coding framework with hot-reload\n\n";
    std::cout << "Usage:\n";
    std::cout << "  vivid <project-path>              Run a project\n";
    std::cout << "  vivid new <name> [options]        Create a new project\n";
    std::cout << "  vivid bundle <project> [options]  Bundle project as standalone app\n";
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

// Available addons with descriptions
struct AddonInfo {
    std::string name;
    std::string description;
};

static const std::vector<AddonInfo> AVAILABLE_ADDONS = {
    {"vivid-audio", "Audio input, FFT analysis, beat detection, oscillators"},
    {"vivid-video", "Video playback (HAP codec, platform decoders)"},
    {"vivid-render3d", "3D rendering with PBR materials, GLTF loading, CSG"}
};

int createProject(const std::string& name, const std::string& templateName,
                  bool minimal, bool skipPrompts, const std::vector<std::string>& addons) {
    fs::path projectPath = fs::current_path() / name;

    // Check if directory already exists
    if (fs::exists(projectPath)) {
        std::cerr << "Error: Directory '" << name << "' already exists.\n";
        return 1;
    }

    // Validate addon names
    for (const auto& addon : addons) {
        bool valid = false;
        for (const auto& available : AVAILABLE_ADDONS) {
            if (addon == available.name) {
                valid = true;
                break;
            }
        }
        if (!valid) {
            std::cerr << "Error: Unknown addon '" << addon << "'\n";
            std::cerr << "Available addons:\n";
            for (const auto& a : AVAILABLE_ADDONS) {
                std::cerr << "  " << a.name << " - " << a.description << "\n";
            }
            return 1;
        }
    }

    // Confirm creation (unless --yes flag)
    if (!skipPrompts && !minimal) {
        std::cout << "Creating project '" << name << "' with template '" << templateName << "'";
        if (!addons.empty()) {
            std::cout << " and addons: ";
            for (size_t i = 0; i < addons.size(); i++) {
                if (i > 0) std::cout << ", ";
                std::cout << addons[i];
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
        gitignore << "\n# ImGui state\n";
        gitignore << "imgui.ini\n";
        gitignore.close();

        // Build addons list for CLAUDE.md
        std::string addonsList;
        addonsList += "- **Core** (always included): 2D effects, noise, blur, composite, feedback\n";
        if (addons.empty()) {
            addonsList += "\nNo additional addons selected. Add with `vivid new --addons vivid-audio,vivid-video`\n";
        } else {
            for (const auto& addon : addons) {
                for (const auto& info : AVAILABLE_ADDONS) {
                    if (addon == info.name) {
                        addonsList += "- **" + info.name + "**: " + info.description + "\n";
                        break;
                    }
                }
            }
        }

        // Write CLAUDE.md
        std::string claudeMd = replaceAll(CLAUDE_MD_TEMPLATE, "%PROJECT_NAME%", name);
        claudeMd = replaceAll(claudeMd, "%ADDONS_LIST%", addonsList);
        std::ofstream claudeFile(projectPath / "CLAUDE.md");
        if (claudeFile) {
            claudeFile << claudeMd;
            claudeFile.close();
        }

        std::cout << "\n";
        std::cout << "  Created " << name << "/\n";
        std::cout << "  Created " << name << "/chain.cpp\n";
        std::cout << "  Created " << name << "/CLAUDE.md\n";
        std::cout << "  Created " << name << "/assets/\n";
        std::cout << "  Created " << name << "/shaders/\n";
        std::cout << "  Created " << name << "/.gitignore\n";
        std::cout << "\n";
        std::cout << "Project created successfully!\n\n";
        std::cout << "Next steps:\n";
        std::cout << "  cd " << name << "\n";
        std::cout << "  vivid .\n";
        std::cout << "\n";
        std::cout << "Edit CLAUDE.md to describe what you want to create!\n";
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

int bundleProject(const std::string& projectPath, const std::string& outputPath, const std::string& appName) {
#ifdef __APPLE__
    fs::path srcProject = fs::absolute(projectPath);

    // Validate project exists and has chain.cpp
    fs::path chainPath = srcProject / "chain.cpp";
    if (!fs::exists(chainPath)) {
        // Maybe it's the chain.cpp itself
        if (fs::is_regular_file(srcProject) && srcProject.filename() == "chain.cpp") {
            chainPath = srcProject;
            srcProject = srcProject.parent_path();
        } else {
            std::cerr << "Error: No chain.cpp found in " << projectPath << "\n";
            return 1;
        }
    }

    // Determine app name and output path
    std::string finalAppName = appName.empty() ? toCamelCase(srcProject.filename().string()) : appName;
    fs::path appPath = outputPath.empty()
        ? (fs::current_path() / (finalAppName + ".app"))
        : fs::path(outputPath);

    if (fs::exists(appPath)) {
        std::cerr << "Error: Output path already exists: " << appPath << "\n";
        return 1;
    }

    std::cout << "Bundling " << srcProject.filename().string() << " -> " << appPath.filename().string() << "\n";

    // Get vivid executable path (we're running from it)
    char pathBuf[4096];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) != 0) {
        std::cerr << "Error: Could not determine executable path\n";
        return 1;
    }
    fs::path exePath = fs::canonical(pathBuf);
    fs::path exeDir = exePath.parent_path();

    try {
        // Create .app bundle structure
        fs::path contentsPath = appPath / "Contents";
        fs::path macosPath = contentsPath / "MacOS";
        fs::path resourcesPath = contentsPath / "Resources";
        fs::path frameworksPath = contentsPath / "Frameworks";

        fs::create_directories(macosPath);
        fs::create_directories(resourcesPath);
        fs::create_directories(frameworksPath);

        // Copy vivid executable
        fs::copy_file(exePath, macosPath / "vivid");

        // Copy shaders
        fs::path shadersDir = exeDir / "shaders";
        if (fs::exists(shadersDir)) {
            fs::copy(shadersDir, macosPath / "shaders", fs::copy_options::recursive);
        }

        // Copy templates (for `vivid new` command in bundled app)
        fs::path templatesDir = exeDir / "templates";
        if (fs::exists(templatesDir)) {
            fs::copy(templatesDir, macosPath / "templates", fs::copy_options::recursive);
        }

        // Copy dylibs
        for (const auto& lib : {"libvivid-core.dylib", "libvivid-audio.dylib",
                                "libvivid-render3d.dylib", "libvivid-video.dylib"}) {
            fs::path libPath = exeDir / lib;
            if (fs::exists(libPath)) {
                fs::copy_file(libPath, macosPath / lib);
            }
        }

        // Copy headers for chain compilation (hot-reload expects at ../include relative to exe)
        fs::path includeDir = exeDir.parent_path().parent_path() / "core" / "include";
        if (!fs::exists(includeDir)) {
            includeDir = exeDir.parent_path().parent_path() / "include";
        }

        fs::path bundleInclude = contentsPath / "include";
        if (fs::exists(includeDir)) {
            fs::copy(includeDir, bundleInclude, fs::copy_options::recursive);
        }

        // Copy addon headers
        for (const auto& addon : {"vivid-audio", "vivid-render3d", "vivid-video"}) {
            fs::path addonInclude = exeDir.parent_path().parent_path() / "addons" / addon / "include";
            if (fs::exists(addonInclude)) {
                fs::copy(addonInclude, bundleInclude,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            }
        }

        // Copy GLM headers
        fs::path glmInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "glm-src";
        if (fs::exists(glmInclude / "glm")) {
            fs::copy(glmInclude / "glm", bundleInclude / "glm", fs::copy_options::recursive);
        }

        // Copy webgpu headers
        fs::path wgpuInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "wgpu" / "include";
        if (fs::exists(wgpuInclude / "webgpu")) {
            fs::copy(wgpuInclude / "webgpu", bundleInclude / "webgpu", fs::copy_options::recursive);
        }
        if (fs::exists(wgpuInclude / "wgpu.h")) {
            fs::copy_file(wgpuInclude / "wgpu.h", bundleInclude / "wgpu.h");
        }

        // Copy GLFW headers
        fs::path glfwInclude = exeDir.parent_path().parent_path() / "build" / "_deps" / "glfw-src" / "include";
        if (fs::exists(glfwInclude / "GLFW")) {
            fs::copy(glfwInclude / "GLFW", bundleInclude / "GLFW", fs::copy_options::recursive);
        }

        // Copy glfw3webgpu header
        fs::path glfw3wgpuDir = exeDir.parent_path().parent_path() / "deps" / "glfw3webgpu";
        if (fs::exists(glfw3wgpuDir / "glfw3webgpu.h")) {
            fs::copy_file(glfw3wgpuDir / "glfw3webgpu.h", bundleInclude / "glfw3webgpu.h");
        }

        // Copy project files to Resources
        fs::path projectDest = resourcesPath / "project";
        fs::create_directories(projectDest);

        fs::copy_file(chainPath, projectDest / "chain.cpp");

        fs::path assetsDir = srcProject / "assets";
        if (fs::exists(assetsDir) && fs::is_directory(assetsDir)) {
            fs::copy(assetsDir, projectDest / "assets", fs::copy_options::recursive);
        }

        fs::path projectShaders = srcProject / "shaders";
        if (fs::exists(projectShaders) && fs::is_directory(projectShaders)) {
            fs::copy(projectShaders, projectDest / "shaders", fs::copy_options::recursive);
        }

        // Create launcher script
        fs::path launcherPath = macosPath / finalAppName;
        std::ofstream launcher(launcherPath);
        launcher << "#!/bin/bash\n";
        launcher << "cd \"$(dirname \"$0\")\"\n";
        launcher << "exec ./vivid \"../Resources/project\" \"$@\"\n";
        launcher.close();

        fs::permissions(launcherPath, fs::perms::owner_exec | fs::perms::group_exec |
                                       fs::perms::others_exec | fs::perms::owner_read |
                                       fs::perms::group_read | fs::perms::others_read |
                                       fs::perms::owner_write, fs::perm_options::add);

        // Create Info.plist
        std::ofstream plist(contentsPath / "Info.plist");
        plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        plist << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
              << "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
        plist << "<plist version=\"1.0\">\n<dict>\n";
        plist << "    <key>CFBundleName</key><string>" << finalAppName << "</string>\n";
        plist << "    <key>CFBundleDisplayName</key><string>" << finalAppName << "</string>\n";
        plist << "    <key>CFBundleIdentifier</key><string>com.vivid."
              << srcProject.filename().string() << "</string>\n";
        plist << "    <key>CFBundleVersion</key><string>" << VERSION << "</string>\n";
        plist << "    <key>CFBundleShortVersionString</key><string>" << VERSION << "</string>\n";
        plist << "    <key>CFBundleExecutable</key><string>" << finalAppName << "</string>\n";
        plist << "    <key>CFBundlePackageType</key><string>APPL</string>\n";
        plist << "    <key>NSHighResolutionCapable</key><true/>\n";
        plist << "    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>\n";
        plist << "</dict>\n</plist>\n";
        plist.close();

        std::ofstream pkginfo(contentsPath / "PkgInfo");
        pkginfo << "APPL????";
        pkginfo.close();

        std::cout << "\nBundle created: " << appPath << "\n\n";
        std::cout << "Contents:\n";
        std::cout << "  " << appPath.filename().string() << "/Contents/MacOS/" << finalAppName << " (launcher)\n";
        std::cout << "  " << appPath.filename().string() << "/Contents/MacOS/vivid (runtime)\n";
        std::cout << "  " << appPath.filename().string() << "/Contents/Resources/project/ (your code)\n";
        std::cout << "\nRun with:\n  open " << appPath.filename().string() << "\n";

    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error creating bundle: " << e.what() << "\n";
        return 1;
    }

    return 0;
#else
    (void)projectPath; (void)outputPath; (void)appName;
    std::cerr << "Error: Bundle command is only supported on macOS currently.\n";
    std::cerr << "For Windows and Linux, distribution is manual.\n";
    return 1;
#endif
}

// Returns: 0 = handled (exit with this code), -1 = not a CLI command (continue to main)
int handleCommand(int argc, char** argv) {
    CLI::App app{"Vivid - Creative coding framework with hot-reload"};
    app.set_version_flag("-v,--version", std::string(VERSION));
    app.set_help_flag("-h,--help", "Show this help");

    // Allow positional argument for project path (handled by main runtime, not here)
    // We need to detect if the first arg is a subcommand or a project path

    // 'new' subcommand
    std::string newProjectName;
    std::string newTemplate = "blank";
    std::vector<std::string> newAddons;
    bool newMinimal = false;
    bool newYes = false;

    auto* newCmd = app.add_subcommand("new", "Create a new project");
    newCmd->add_option("name", newProjectName, "Project name")->required();
    newCmd->add_option("-t,--template", newTemplate, "Template: blank, noise-demo, feedback, audio-visualizer, 3d-orbit")
          ->default_val("blank");
    newCmd->add_option("-a,--addons", newAddons,
                       "Addons to include (comma-separated): vivid-audio, vivid-video, vivid-render3d")
          ->delimiter(',');
    newCmd->add_flag("--minimal", newMinimal, "Use minimal template");
    newCmd->add_flag("-y,--yes", newYes, "Skip confirmation prompts");

    // 'bundle' subcommand
    std::string bundleProjectPath;
    std::string bundleOutput;
    std::string bundleName;

    auto* bundleCmd = app.add_subcommand("bundle", "Bundle project as standalone app (macOS)");
    bundleCmd->add_option("project", bundleProjectPath, "Project path")->required();
    bundleCmd->add_option("-o,--output", bundleOutput, "Output path (.app)");
    bundleCmd->add_option("-n,--name", bundleName, "App display name");

    // 'operators' subcommand
    bool operatorsJson = false;
    auto* operatorsCmd = app.add_subcommand("operators", "List available operators");
    operatorsCmd->add_flag("--json", operatorsJson, "Output as JSON");

    // 'addons' subcommand group
    auto* addonsCmd = app.add_subcommand("addons", "Manage installed addons");
    addonsCmd->require_subcommand(0, 1);  // 0 or 1 subcommand

    // addons list (default when no subcommand)
    bool addonsJson = false;
    auto* addonsListCmd = addonsCmd->add_subcommand("list", "List installed addons");
    addonsListCmd->add_flag("--json", addonsJson, "Output as JSON");

    // addons install
    std::string addonsInstallUrl;
    std::string addonsInstallRef;
    auto* addonsInstallCmd = addonsCmd->add_subcommand("install", "Install addon from git URL");
    addonsInstallCmd->add_option("url", addonsInstallUrl, "Git repository URL")->required();
    addonsInstallCmd->add_option("-r,--ref", addonsInstallRef, "Git ref (tag, branch, or commit)");

    // addons remove
    std::string addonsRemoveName;
    auto* addonsRemoveCmd = addonsCmd->add_subcommand("remove", "Remove an installed addon");
    addonsRemoveCmd->add_option("name", addonsRemoveName, "Addon name")->required();

    // addons update
    std::string addonsUpdateName;
    auto* addonsUpdateCmd = addonsCmd->add_subcommand("update", "Update addon(s)");
    addonsUpdateCmd->add_option("name", addonsUpdateName, "Addon name (empty = update all)");

    // Check if first argument is a known subcommand or flag
    if (argc < 2) {
        printUsage();
        return 0;
    }

    std::string firstArg = argv[1];

    // If first arg doesn't look like a subcommand or flag, it's a project path
    // Let main runtime handle it
    if (firstArg != "new" && firstArg != "bundle" && firstArg != "operators" &&
        firstArg != "addons" &&
        firstArg != "-h" && firstArg != "--help" &&
        firstArg != "-v" && firstArg != "--version") {
        return -1;  // Continue to main runtime
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Handle subcommands
    if (newCmd->parsed()) {
        if (newMinimal) {
            newTemplate = "minimal";
        }
        return createProject(newProjectName, newTemplate, newMinimal, newYes, newAddons);
    }

    if (bundleCmd->parsed()) {
        return bundleProject(bundleProjectPath, bundleOutput, bundleName);
    }

    if (operatorsCmd->parsed()) {
        if (operatorsJson) {
            OperatorRegistry::instance().outputJson();
        } else {
            // Human-readable output
            const auto& ops = OperatorRegistry::instance().operators();
            std::cout << "Available operators (" << ops.size() << "):\n\n";

            std::string currentCategory;
            for (const auto& op : ops) {
                if (op.category != currentCategory) {
                    if (!currentCategory.empty()) std::cout << "\n";
                    currentCategory = op.category;
                    std::cout << "## " << currentCategory << "\n";
                }
                std::cout << "  " << op.name;
                if (!op.addon.empty()) {
                    std::cout << " [" << op.addon << "]";
                }
                std::cout << " - " << op.description << "\n";
            }

            if (ops.empty()) {
                std::cout << "No operators registered. This may be a build issue.\n";
            }
        }
        return 0;
    }

    if (addonsCmd->parsed()) {
        auto& addonMgr = AddonManager::instance();

        if (addonsInstallCmd->parsed()) {
            return addonMgr.install(addonsInstallUrl, addonsInstallRef) ? 0 : 1;
        }

        if (addonsRemoveCmd->parsed()) {
            return addonMgr.remove(addonsRemoveName) ? 0 : 1;
        }

        if (addonsUpdateCmd->parsed()) {
            return addonMgr.update(addonsUpdateName) ? 0 : 1;
        }

        // Default: list (or explicit 'addons list')
        if (addonsJson || addonsListCmd->parsed()) {
            if (addonsJson) {
                addonMgr.outputJson();
            } else {
                auto addons = addonMgr.listInstalled();
                if (addons.empty()) {
                    std::cout << "No addons installed.\n\n";
                    std::cout << "Install an addon with:\n";
                    std::cout << "  vivid addons install <git-url>\n\n";
                    std::cout << "Example:\n";
                    std::cout << "  vivid addons install https://github.com/seethroughlab/vivid-ml\n";
                } else {
                    std::cout << "Installed addons (" << addons.size() << "):\n\n";
                    for (const auto& addon : addons) {
                        std::cout << "  " << addon.name << " v" << addon.version;
                        if (!addon.gitRef.empty()) {
                            std::cout << " (" << addon.gitRef << ")";
                        }
                        std::cout << "\n";
                        std::cout << "    Source: " << addon.builtFrom << "\n";
                        std::cout << "    Path: " << addon.installPath.string() << "\n";
                    }
                }
            }
            return 0;
        }

        // No subcommand - show list
        auto addons = addonMgr.listInstalled();
        if (addons.empty()) {
            std::cout << "No addons installed.\n\n";
            std::cout << "Install an addon with:\n";
            std::cout << "  vivid addons install <git-url>\n";
        } else {
            std::cout << "Installed addons (" << addons.size() << "):\n\n";
            for (const auto& addon : addons) {
                std::cout << "  " << addon.name << " v" << addon.version << "\n";
            }
        }
        return 0;
    }

    // If we got here with --help or --version, CLI11 already handled it
    return 0;
}

} // namespace vivid::cli
