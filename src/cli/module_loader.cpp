// Module Loader Implementation
// Loads dynamic libraries to populate OperatorRegistry

#include "vivid/module_loader.h"

#include <iostream>
#include <vector>
#include <string>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
        #include <climits>
    #else
        #include <linux/limits.h>
        #include <unistd.h>
    #endif
#endif

namespace fs = std::filesystem;

namespace vivid {

void loadModuleLibrary(const fs::path& libPath) {
#ifdef _WIN32
    HMODULE h = LoadLibraryA(libPath.string().c_str());
    if (!h) {
        std::cerr << "[vivid] Warning: Failed to load " << libPath.filename().string() << std::endl;
    }
#else
    // Use RTLD_GLOBAL so static initializers register with OperatorRegistry
    void* h = dlopen(libPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        std::cerr << "[vivid] Warning: Failed to load " << libPath.filename().string() << ": " << dlerror() << std::endl;
    }
#endif
}

void loadAllModules() {
    // Get executable directory for built-in modules
    fs::path exeDir;
#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        exeDir = fs::path(pathBuf).parent_path();
    }
#elif defined(_WIN32)
    char pathBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, pathBuf, MAX_PATH);
    exeDir = fs::path(pathBuf).parent_path();
#else
    char pathBuf[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", pathBuf, PATH_MAX);
    if (count > 0) {
        pathBuf[count] = '\0';
        exeDir = fs::path(pathBuf).parent_path();
    }
#endif

    // Load built-in modules
    if (!exeDir.empty()) {
        const std::vector<std::string> builtinModules = {
            "vivid-audio",
            "vivid-video",
            "vivid-render3d",
            "vivid-network",
            "vivid-serial",
            "vivid-midi",
            "vivid-opencv"
        };

        // Possible library locations to search
        std::vector<fs::path> searchPaths;
#ifdef _WIN32
        // On Windows, DLLs are typically in the same directory as the executable
        searchPaths.push_back(exeDir);
        // Also check ../lib/ for installed layouts
        searchPaths.push_back(exeDir.parent_path() / "lib");
#else
        // On Unix, libraries are in ../lib/ relative to the executable in bin/
        searchPaths.push_back(exeDir.parent_path() / "lib");
        // Also check same directory as fallback
        searchPaths.push_back(exeDir);
#endif

        for (const auto& moduleName : builtinModules) {
            bool loaded = false;
            for (const auto& searchDir : searchPaths) {
                if (!fs::exists(searchDir)) continue;

#ifdef __APPLE__
                fs::path libPath = searchDir / ("lib" + moduleName + ".dylib");
#elif defined(_WIN32)
                fs::path libPath = searchDir / (moduleName + ".dll");
#else
                fs::path libPath = searchDir / ("lib" + moduleName + ".so");
#endif
                if (fs::exists(libPath)) {
                    loadModuleLibrary(libPath);
                    loaded = true;
                    break;
                }
            }
        }
    }

    // Load user-installed modules from ~/.vivid/modules/
    fs::path userModulesDir;
#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        userModulesDir = fs::path(userProfile) / ".vivid" / "modules";
    }
#else
    const char* home = std::getenv("HOME");
    if (home) {
        userModulesDir = fs::path(home) / ".vivid" / "modules";
    }
#endif

    if (!userModulesDir.empty() && fs::exists(userModulesDir)) {
        for (const auto& moduleDir : fs::directory_iterator(userModulesDir)) {
            if (!moduleDir.is_directory()) continue;

            fs::path libDir = moduleDir.path() / "lib";
            if (!fs::exists(libDir)) continue;

            for (const auto& entry : fs::directory_iterator(libDir)) {
                std::string filename = entry.path().filename().string();

#ifdef __APPLE__
                if (filename.find(".dylib") == std::string::npos) continue;
#elif defined(_WIN32)
                if (filename.find(".dll") == std::string::npos) continue;
#else
                if (filename.find(".so") == std::string::npos) continue;
#endif
                // Skip dependency libraries like onnxruntime
                if (filename.find("onnxruntime") != std::string::npos) continue;

                loadModuleLibrary(entry.path());
            }
        }
    }
}

} // namespace vivid
