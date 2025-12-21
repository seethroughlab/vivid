// Vivid - Asset Loader Implementation

#include <vivid/asset_loader.h>
#include <fstream>
#include <sstream>
#include <iostream>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace vivid {

AssetLoader& AssetLoader::instance() {
    static AssetLoader instance;
    return instance;
}

AssetLoader::AssetLoader() {
    detectExecutableDir();

    // Add default search paths
    // 1. Executable directory (installed builds)
    m_searchPaths.push_back(m_executableDir);

    // 2. Current working directory (development)
    m_searchPaths.push_back(fs::current_path());

    // 3. Common development paths
    m_searchPaths.push_back(fs::current_path() / "core");
    m_searchPaths.push_back(fs::current_path() / "addons");
}

void AssetLoader::detectExecutableDir() {
#ifdef __APPLE__
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        m_executableDir = fs::path(path).parent_path();
        return;
    }
#elif defined(_WIN32)
    char path[MAX_PATH];
    if (GetModuleFileNameA(NULL, path, MAX_PATH) > 0) {
        m_executableDir = fs::path(path).parent_path();
        return;
    }
#else
    // Linux: /proc/self/exe
    try {
        m_executableDir = fs::read_symlink("/proc/self/exe").parent_path();
        return;
    } catch (...) {
        // Fall through to default
    }
#endif
    m_executableDir = fs::current_path();
}

void AssetLoader::addSearchPath(const fs::path& path) {
    // Avoid duplicates
    for (const auto& existing : m_searchPaths) {
        if (existing == path) return;
    }
    m_searchPaths.push_back(path);
}

void AssetLoader::setExecutableDir(const fs::path& path) {
    m_executableDir = path;
    // Update first search path
    if (!m_searchPaths.empty()) {
        m_searchPaths[0] = path;
    }
}

void AssetLoader::setProjectDir(const fs::path& path) {
    m_projectDir = path;

    // Add project-related search paths (highest priority - insert at front)
    // We insert in reverse order so the most specific paths are searched first

    // 1. Project's assets/ folder (most specific)
    fs::path projectAssets = path / "assets";
    if (fs::exists(projectAssets) && fs::is_directory(projectAssets)) {
        m_searchPaths.insert(m_searchPaths.begin(), projectAssets);
    }

    // 2. Project directory itself
    m_searchPaths.insert(m_searchPaths.begin(), path);

    // 3. Search parent directories for assets/ folders (up to 3 levels)
    // This allows shared asset folders for multiple projects
    fs::path parent = path.parent_path();
    for (int i = 0; i < 3 && !parent.empty() && parent != parent.parent_path(); ++i) {
        fs::path parentAssets = parent / "assets";
        if (fs::exists(parentAssets) && fs::is_directory(parentAssets)) {
            addSearchPath(parentAssets);
        }
        parent = parent.parent_path();
    }
}

void AssetLoader::clearCache() {
    m_textCache.clear();
    m_binaryCache.clear();
}

fs::path AssetLoader::findAsset(const std::string& path) {
    // If absolute path, use directly
    fs::path assetPath(path);
    if (assetPath.is_absolute()) {
        if (fs::exists(assetPath)) {
            return assetPath;
        }
        return {};
    }

    // Search in all paths
    for (const auto& searchPath : m_searchPaths) {
        fs::path fullPath = searchPath / path;
        if (fs::exists(fullPath)) {
            return fullPath;
        }
    }

    return {};
}

fs::path AssetLoader::resolve(const std::string& path) {
    return findAsset(path);
}

bool AssetLoader::exists(const std::string& path) {
    return !findAsset(path).empty();
}

std::string AssetLoader::loadText(const std::string& path) {
    // Check cache first
    if (m_cacheEnabled) {
        auto it = m_textCache.find(path);
        if (it != m_textCache.end()) {
            return it->second;
        }
    }

    fs::path fullPath = findAsset(path);
    if (fullPath.empty()) {
        return "";
    }

    std::ifstream file(fullPath);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Cache if enabled
    if (m_cacheEnabled) {
        m_textCache[path] = content;
    }

    return content;
}

std::string AssetLoader::loadShader(const std::string& name) {
    // Try shader-specific paths
    std::vector<std::string> shaderPaths = {
        "shaders/" + name,                          // Installed build
        "core/shaders/" + name,                     // Development
        "addons/vivid-effects-2d/shaders/" + name,  // Legacy addon path
        "addons/vivid-render3d/shaders/" + name,    // 3D addon
        "addons/vivid-video/shaders/" + name,       // Video addon
    };

    for (const auto& path : shaderPaths) {
        std::string content = loadText(path);
        if (!content.empty()) {
            return content;
        }
    }

    // Fallback: try the name directly
    return loadText(name);
}

std::vector<uint8_t> AssetLoader::loadBinary(const std::string& path) {
    // Check cache first
    if (m_cacheEnabled) {
        auto it = m_binaryCache.find(path);
        if (it != m_binaryCache.end()) {
            return it->second;
        }
    }

    fs::path fullPath = findAsset(path);
    if (fullPath.empty()) {
        return {};
    }

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        return {};
    }

    // Cache if enabled
    if (m_cacheEnabled) {
        m_binaryCache[path] = data;
    }

    return data;
}

} // namespace vivid
