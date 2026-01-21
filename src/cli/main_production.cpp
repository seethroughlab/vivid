// Vivid - Production Main Entry Point
// Minimal main() for production bundles - no HotReload, no MCP, no dev tools
//
// Production bundles compile chain.cpp directly into the executable.
// The chain exports vivid_setup() and vivid_update() via VIVID_CHAIN macro.

#include <vivid/runtime.h>
#include <vivid/vivid.h>
#include <iostream>
#include <filesystem>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace fs = std::filesystem;

// Forward declarations for statically linked chain functions
// These are defined in the chain.cpp that's compiled into this executable
extern "C" void vivid_setup(vivid::Context& ctx);
extern "C" void vivid_update(vivid::Context& ctx);

// Optional config function (may or may not be defined by chain.cpp)
// Chains using VIVID_CHAIN_CONFIG export this, chains using VIVID_CHAIN don't.
//
// We provide a WEAK default implementation here. If the chain defines vivid_config
// via VIVID_CHAIN_CONFIG, the linker will use that instead.
// Note: The weak attribute must be on the definition, not the declaration.

// Default config implementation (weak - can be overridden by chain.cpp)
extern "C" __attribute__((weak)) vivid::ChainConfig vivid_config() {
    // Return default config if chain doesn't provide one
    return vivid::ChainConfig{};
}

static vivid::ChainConfig getChainConfig() {
    return vivid_config();
}

// Get executable directory for finding assets
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

// Find assets path relative to executable
// In bundles: Contents/MacOS/AppName -> Contents/Resources/project/assets
// In dev: ./project/assets
static fs::path findAssetsPath() {
    fs::path exeDir = getExecutableDir();

#ifdef __APPLE__
    // macOS bundle structure: Contents/MacOS/AppName
    // Resources are at: Contents/Resources/project/
    fs::path resourcesPath = exeDir.parent_path() / "Resources" / "project";
    if (fs::exists(resourcesPath)) {
        return resourcesPath;
    }
    // Check for assets subfolder
    fs::path assetsPath = resourcesPath / "assets";
    if (fs::exists(assetsPath)) {
        return resourcesPath;  // Return project root, not assets subfolder
    }
#endif

    // Fallback: look for assets in current directory or near executable
    if (fs::exists("assets")) {
        return fs::current_path();
    }
    if (fs::exists(exeDir / "assets")) {
        return exeDir;
    }

    return fs::current_path();
}

int main(int argc, char* argv[]) {
    // Get chain config (uses default if chain doesn't provide one via VIVID_CHAIN_CONFIG)
    vivid::ChainConfig chainConfig = getChainConfig();

    vivid::RuntimeConfig config;
    config.windowWidth = chainConfig.windowWidth;
    config.windowHeight = chainConfig.windowHeight;
    config.fullscreen = chainConfig.fullscreen;
    config.resizable = chainConfig.resizable;
    config.displayMode = chainConfig.displayMode;

    // Set assets path
    config.assetsPath = findAssetsPath();

    // Parse command-line arguments (minimal set for production)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--fullscreen" || arg == "-f") {
            config.fullscreen = true;
        } else if (arg == "--windowed" || arg == "-w") {
            config.fullscreen = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --fullscreen, -f    Start in fullscreen mode\n"
                      << "  --windowed, -w      Start in windowed mode\n"
                      << "  --help, -h          Show this help\n";
            return 0;
        }
    }

    // Create and run runtime
    vivid::Runtime runtime;

    int result = runtime.init(config);
    if (result != 0) {
        std::cerr << "Failed to initialize runtime" << std::endl;
        return result;
    }

    return runtime.run(vivid_setup, vivid_update);
}
