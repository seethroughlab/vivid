#include <vivid/audio/preset.h>
#include <fstream>
#include <iostream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <climits>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace fs = std::filesystem;

namespace vivid::audio {

// Get executable directory
static fs::path getExecutableDir() {
#ifdef __APPLE__
    char pathBuf[PATH_MAX];
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        return fs::path(pathBuf).parent_path();
    }
#elif defined(_WIN32)
    char pathBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, pathBuf, MAX_PATH);
    return fs::path(pathBuf).parent_path();
#elif defined(__linux__)
    char pathBuf[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", pathBuf, PATH_MAX);
    if (count > 0) {
        pathBuf[count] = '\0';
        return fs::path(pathBuf).parent_path();
    }
#endif
    return fs::current_path();
}

// Get home directory
static fs::path getHomeDir() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home ? fs::path(home) : fs::current_path();
}

fs::path PresetCapable::userPresetDir() {
    return getHomeDir() / ".vivid" / "presets";
}

fs::path PresetCapable::factoryPresetDir() {
    return getExecutableDir() / "presets";
}

std::vector<PresetInfo> PresetCapable::listPresets(const std::string& synthType) {
    std::vector<PresetInfo> presets;

    auto scanDir = [&](const fs::path& dir, bool isFactory) {
        if (!fs::exists(dir)) return;

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() != ".json") continue;

            try {
                std::ifstream file(entry.path());
                if (!file) continue;

                nlohmann::json j;
                file >> j;

                // Verify it's for this synth type
                if (j.value("synth", "") != synthType) continue;

                PresetInfo info;
                info.name = j.value("name", entry.path().stem().string());
                info.path = entry.path().string();
                info.author = j.value("author", "");
                info.category = j.value("category", "");
                info.isFactory = isFactory;
                presets.push_back(info);
            } catch (const std::exception& e) {
                std::cerr << "[Preset] Error loading " << entry.path() << ": " << e.what() << std::endl;
            }
        }
    };

    // Scan factory presets (in synth-specific subdirectory)
    fs::path factoryDir = factoryPresetDir() / synthType;
    scanDir(factoryDir, true);

    // Scan user presets
    fs::path userDir = userPresetDir() / synthType;
    scanDir(userDir, false);

    return presets;
}

} // namespace vivid::audio
