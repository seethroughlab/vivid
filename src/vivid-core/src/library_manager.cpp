// Vivid - Library Manager Implementation

#include <vivid/library_manager.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <array>
#include <ctime>
#include <regex>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

using json = nlohmann::json;

namespace vivid {

// -----------------------------------------------------------------------------
// Command Execution
// -----------------------------------------------------------------------------

static std::pair<int, std::string> executeCommand(const std::string& cmd) {
    std::string output;
    int exitCode = -1;

#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (pipe) {
        std::array<char, 256> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        exitCode = _pclose(pipe);
    }
#else
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe) {
        std::array<char, 256> buffer;
        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
            output += buffer.data();
        }
        exitCode = pclose(pipe);
        if (WIFEXITED(exitCode)) {
            exitCode = WEXITSTATUS(exitCode);
        }
    }
#endif

    return {exitCode, output};
}

// -----------------------------------------------------------------------------
// LibraryManager Implementation
// -----------------------------------------------------------------------------

LibraryManager& LibraryManager::instance() {
    static LibraryManager instance;
    return instance;
}

LibraryManager::LibraryManager() {
    // Set up libs directory
#ifdef _WIN32
    const char* homeDir = std::getenv("USERPROFILE");
#else
    const char* homeDir = std::getenv("HOME");
#endif
    if (homeDir) {
        m_libsDir = fs::path(homeDir) / ".vivid" / "libs";
    } else {
        m_libsDir = fs::current_path() / ".vivid" / "libs";
    }

    // Create directory if needed
    std::error_code ec;
    fs::create_directories(m_libsDir, ec);

    // Load manifest
    loadManifest();
}

std::string LibraryManager::getPlatform() const {
#ifdef __APPLE__
    #if defined(__arm64__) || defined(__aarch64__)
        return "darwin-arm64";
    #else
        return "darwin-x64";
    #endif
#elif defined(_WIN32)
    return "win32-x64";
#else
    return "linux-x64";
#endif
}

std::optional<LibraryJson> LibraryManager::parseLibraryJson(const fs::path& path) const {
    if (!fs::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    try {
        json j = json::parse(file);

        LibraryJson lib;
        lib.name = j.value("name", "");
        lib.version = j.value("version", "");
        lib.description = j.value("description", "");
        lib.repository = j.value("repository", "");
        lib.license = j.value("license", "");

        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            for (const auto& dep : j["dependencies"]) {
                lib.dependencies.push_back(dep.get<std::string>());
            }
        }

        // Parse prebuilt URLs
        if (j.contains("prebuilt") && j["prebuilt"].is_object()) {
            const auto& prebuilt = j["prebuilt"];
            lib.prebuilt.darwinArm64 = prebuilt.value("darwin-arm64", "");
            lib.prebuilt.darwinX64 = prebuilt.value("darwin-x64", "");
            lib.prebuilt.linuxX64 = prebuilt.value("linux-x64", "");
            lib.prebuilt.win32X64 = prebuilt.value("win32-x64", "");
        }

        if (lib.name.empty()) {
            return std::nullopt;
        }

        return lib;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

void LibraryManager::loadManifest() {
    m_installedLibraries.clear();

    fs::path manifestPath = m_libsDir / "manifest.json";
    if (!fs::exists(manifestPath)) {
        return;
    }

    std::ifstream file(manifestPath);
    if (!file) {
        return;
    }

    try {
        json j = json::parse(file);

        if (j.contains("libraries") && j["libraries"].is_object()) {
            for (const auto& [name, libData] : j["libraries"].items()) {
                InstalledLibrary lib;
                lib.name = name;
                lib.version = libData.value("version", "");
                lib.gitUrl = libData.value("gitUrl", "");
                lib.gitRef = libData.value("gitRef", "");
                lib.installedAt = libData.value("installedAt", "");
                lib.builtFrom = libData.value("builtFrom", "");
                lib.installPath = m_libsDir / name;

                if (!lib.name.empty()) {
                    m_installedLibraries.push_back(lib);
                }
            }
        }
    } catch (const json::exception&) {
        // Invalid JSON, start fresh
    }
}

void LibraryManager::saveManifest() const {
    fs::path manifestPath = m_libsDir / "manifest.json";

    json j;
    j["version"] = 1;
    j["libraries"] = json::object();

    for (const auto& lib : m_installedLibraries) {
        j["libraries"][lib.name] = {
            {"version", lib.version},
            {"gitUrl", lib.gitUrl},
            {"gitRef", lib.gitRef},
            {"installedAt", lib.installedAt},
            {"builtFrom", lib.builtFrom}
        };
    }

    std::ofstream file(manifestPath);
    if (!file) {
        std::cerr << "Failed to save manifest.json" << std::endl;
        return;
    }

    file << j.dump(2) << std::endl;
}

void LibraryManager::addToManifest(const InstalledLibrary& lib) {
    // Remove existing entry if present
    removeFromManifest(lib.name);
    m_installedLibraries.push_back(lib);
    saveManifest();
}

void LibraryManager::removeFromManifest(const std::string& name) {
    m_installedLibraries.erase(
        std::remove_if(m_installedLibraries.begin(), m_installedLibraries.end(),
                       [&name](const InstalledLibrary& l) { return l.name == name; }),
        m_installedLibraries.end());
    saveManifest();
}

bool LibraryManager::downloadFile(const std::string& url, const fs::path& dest) {
    std::cout << "Downloading: " << url << std::endl;

    std::stringstream cmd;
    cmd << "curl -fsSL -o \"" << dest.string() << "\" \"" << url << "\" 2>&1";

    auto [exitCode, output] = executeCommand(cmd.str());
    if (exitCode != 0) {
        m_error = "Download failed: " + output;
        return false;
    }
    return true;
}

bool LibraryManager::extractArchive(const fs::path& archive, const fs::path& dest) {
    std::cout << "Extracting: " << archive.filename() << std::endl;

    std::stringstream cmd;
    std::string ext = archive.extension().string();

    if (ext == ".gz" || ext == ".tgz") {
        // Handle .tar.gz
        cmd << "tar -xzf \"" << archive.string() << "\" -C \"" << dest.string() << "\" 2>&1";
    } else if (ext == ".zip") {
#ifdef _WIN32
        cmd << "powershell -Command \"Expand-Archive -Path '" << archive.string()
            << "' -DestinationPath '" << dest.string() << "'\" 2>&1";
#else
        cmd << "unzip -q \"" << archive.string() << "\" -d \"" << dest.string() << "\" 2>&1";
#endif
    } else {
        m_error = "Unknown archive format: " + ext;
        return false;
    }

    auto [exitCode, output] = executeCommand(cmd.str());
    if (exitCode != 0) {
        m_error = "Extraction failed: " + output;
        return false;
    }
    return true;
}

bool LibraryManager::cloneRepo(const std::string& url, const std::string& ref, const fs::path& dest) {
    std::cout << "Cloning: " << url << std::endl;

    std::stringstream cmd;
    cmd << "git clone --depth 1";
    if (!ref.empty()) {
        cmd << " --branch \"" << ref << "\"";
    }
    cmd << " \"" << url << "\" \"" << dest.string() << "\" 2>&1";

    auto [exitCode, output] = executeCommand(cmd.str());
    if (exitCode != 0) {
        m_error = "Git clone failed: " + output;
        return false;
    }
    return true;
}

bool LibraryManager::cmakeBuild(const fs::path& sourceDir, const fs::path& buildDir,
                              const fs::path& installDir) {
    std::cout << "Building from source..." << std::endl;

    // Find VIVID_ROOT (our runtime installation or build)
    fs::path vividRoot;
#ifdef _WIN32
    const char* homeDir = std::getenv("USERPROFILE");
#else
    const char* homeDir = std::getenv("HOME");
#endif
    if (homeDir) {
        fs::path homeVivid = fs::path(homeDir) / ".vivid";
        if (fs::exists(homeVivid / "include" / "vivid")) {
            vividRoot = homeVivid;
        }
    }

    if (vividRoot.empty()) {
        m_error = "Could not find Vivid SDK. Install the runtime first.";
        return false;
    }

    // Create build directory
    std::error_code ec;
    fs::create_directories(buildDir, ec);

    // Configure
    std::stringstream configCmd;
    configCmd << "cmake -B \"" << buildDir.string() << "\" "
              << "-S \"" << sourceDir.string() << "\" "
              << "-DCMAKE_BUILD_TYPE=Release "
              << "-DVIVID_ROOT=\"" << vividRoot.string() << "\" "
              << "-DCMAKE_INSTALL_PREFIX=\"" << installDir.string() << "\" "
              << "2>&1";

    std::cout << "Configuring CMake..." << std::endl;
    auto [configExit, configOutput] = executeCommand(configCmd.str());
    if (configExit != 0) {
        m_error = "CMake configure failed: " + configOutput;
        return false;
    }

    // Build
    std::stringstream buildCmd;
    buildCmd << "cmake --build \"" << buildDir.string() << "\" --config Release --parallel 2>&1";

    std::cout << "Building..." << std::endl;
    auto [buildExit, buildOutput] = executeCommand(buildCmd.str());
    if (buildExit != 0) {
        m_error = "CMake build failed: " + buildOutput;
        return false;
    }

    // Install
    std::stringstream installCmd;
    installCmd << "cmake --install \"" << buildDir.string() << "\" --config Release 2>&1";

    std::cout << "Installing..." << std::endl;
    auto [installExit, installOutput] = executeCommand(installCmd.str());
    if (installExit != 0) {
        m_error = "CMake install failed: " + installOutput;
        return false;
    }

    return true;
}

bool LibraryManager::tryPrebuiltRelease(const std::string& gitUrl, const std::string& ref,
                                      const fs::path& libDir) {
    // First, we need to get the library.json to find prebuilt URLs
    // Try to fetch it from the repo's default branch

    // Extract owner/repo from URL
    std::regex repoRe("github\\.com[/:]([^/]+)/([^/.]+)");
    std::smatch match;
    if (!std::regex_search(gitUrl, match, repoRe)) {
        return false;  // Not a GitHub URL
    }

    std::string owner = match[1].str();
    std::string repo = match[2].str();

    // Fetch library.json from GitHub raw
    std::string branch = ref.empty() ? "main" : ref;
    std::string libraryJsonUrl = "https://raw.githubusercontent.com/" + owner + "/" + repo +
                               "/" + branch + "/library.json";

    fs::path tempJson = m_libsDir / "temp_library.json";
    if (!downloadFile(libraryJsonUrl, tempJson)) {
        // Try master branch
        branch = "master";
        libraryJsonUrl = "https://raw.githubusercontent.com/" + owner + "/" + repo +
                       "/" + branch + "/library.json";
        if (!downloadFile(libraryJsonUrl, tempJson)) {
            return false;
        }
    }

    auto libraryJson = parseLibraryJson(tempJson);
    fs::remove(tempJson);

    if (!libraryJson) {
        return false;
    }

    // Get prebuilt URL for current platform
    std::string platform = getPlatform();
    std::string prebuiltUrl;

    if (platform == "darwin-arm64") {
        prebuiltUrl = libraryJson->prebuilt.darwinArm64;
    } else if (platform == "darwin-x64") {
        prebuiltUrl = libraryJson->prebuilt.darwinX64;
    } else if (platform == "linux-x64") {
        prebuiltUrl = libraryJson->prebuilt.linuxX64;
    } else if (platform == "win32-x64") {
        prebuiltUrl = libraryJson->prebuilt.win32X64;
    }

    if (prebuiltUrl.empty()) {
        std::cout << "No prebuilt binary for " << platform << ", will build from source" << std::endl;
        return false;
    }

    // Replace ${version} placeholder
    std::string version = ref.empty() ? ("v" + libraryJson->version) : ref;
    size_t pos = prebuiltUrl.find("${version}");
    if (pos != std::string::npos) {
        prebuiltUrl.replace(pos, 10, version);
    }

    // Download prebuilt archive
    std::string ext = prebuiltUrl.substr(prebuiltUrl.rfind('.'));
    fs::path archivePath = m_libsDir / ("temp_prebuilt" + ext);

    if (!downloadFile(prebuiltUrl, archivePath)) {
        return false;
    }

    // Create library directory
    std::error_code ec;
    fs::create_directories(libDir, ec);

    // Extract to temp, then move contents
    fs::path tempExtract = m_libsDir / "temp_extract";
    fs::create_directories(tempExtract, ec);

    if (!extractArchive(archivePath, tempExtract)) {
        fs::remove(archivePath);
        fs::remove_all(tempExtract);
        return false;
    }

    fs::remove(archivePath);

    // Find the extracted directory (usually lib-name-platform/)
    fs::path extractedDir;
    for (const auto& entry : fs::directory_iterator(tempExtract)) {
        if (entry.is_directory()) {
            extractedDir = entry.path();
            break;
        }
    }

    if (extractedDir.empty()) {
        m_error = "Could not find extracted library directory";
        fs::remove_all(tempExtract);
        return false;
    }

    // Move contents to library directory
    for (const auto& entry : fs::directory_iterator(extractedDir)) {
        fs::rename(entry.path(), libDir / entry.path().filename(), ec);
    }

    fs::remove_all(tempExtract);

    std::cout << "Installed prebuilt " << libraryJson->name << " v" << libraryJson->version << std::endl;
    return true;
}

bool LibraryManager::buildFromSource(const std::string& gitUrl, const std::string& ref,
                                   const fs::path& libDir) {
    // Create library directory structure
    std::error_code ec;
    fs::create_directories(libDir, ec);

    fs::path srcDir = libDir / "src";
    fs::path buildDir = libDir / "build";

    // Clone repository
    if (!cloneRepo(gitUrl, ref, srcDir)) {
        return false;
    }

    // Build with cmake
    if (!cmakeBuild(srcDir, buildDir, libDir)) {
        return false;
    }

    return true;
}

bool LibraryManager::install(const std::string& gitUrl, const std::string& ref) {
    m_error.clear();

    // Extract library name from URL
    std::regex repoRe("[/:]([^/]+)/([^/.]+?)(?:\\.git)?$");
    std::smatch match;
    if (!std::regex_search(gitUrl, match, repoRe)) {
        m_error = "Invalid git URL: " + gitUrl;
        return false;
    }

    std::string repoName = match[2].str();

    // Check if already installed
    for (const auto& lib : m_installedLibraries) {
        if (lib.name == repoName) {
            std::cout << repoName << " is already installed. Use 'vivid libs update " << repoName << "' to update." << std::endl;
            return true;
        }
    }

    fs::path libDir = m_libsDir / repoName;

    std::cout << "Installing " << repoName << "..." << std::endl;

    // Try prebuilt first, fall back to source
    bool prebuiltSuccess = tryPrebuiltRelease(gitUrl, ref, libDir);
    std::string builtFrom = "prebuilt";

    if (!prebuiltSuccess) {
        builtFrom = "source";
        if (!buildFromSource(gitUrl, ref, libDir)) {
            // Cleanup on failure
            std::error_code ec;
            fs::remove_all(libDir, ec);
            return false;
        }
    }

    // Read library.json for version info
    fs::path libraryJsonPath = libDir / "library.json";
    auto libraryJson = parseLibraryJson(libraryJsonPath);

    // Create manifest entry
    InstalledLibrary entry;
    entry.name = repoName;
    entry.version = libraryJson ? libraryJson->version : "unknown";
    entry.gitUrl = gitUrl;
    entry.gitRef = ref;
    entry.builtFrom = builtFrom;
    entry.installPath = libDir;

    // Get current timestamp
    std::time_t now = std::time(nullptr);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    entry.installedAt = timeBuf;

    addToManifest(entry);

    std::cout << "Successfully installed " << repoName << " v" << entry.version << std::endl;

    return true;
}

bool LibraryManager::remove(const std::string& name) {
    m_error.clear();

    // Find the library
    bool found = false;
    for (const auto& lib : m_installedLibraries) {
        if (lib.name == name) {
            found = true;
            break;
        }
    }

    if (!found) {
        m_error = "Library not found: " + name;
        std::cerr << m_error << std::endl;
        return false;
    }

    fs::path libDir = m_libsDir / name;

    std::cout << "Removing " << name << "..." << std::endl;

    // Remove directory
    std::error_code ec;
    fs::remove_all(libDir, ec);
    if (ec) {
        m_error = "Failed to remove library directory: " + ec.message();
        std::cerr << m_error << std::endl;
        return false;
    }

    // Remove from manifest
    removeFromManifest(name);

    std::cout << "Successfully removed " << name << std::endl;
    return true;
}

bool LibraryManager::update(const std::string& name) {
    m_error.clear();

    std::vector<InstalledLibrary> toUpdate;

    if (name.empty()) {
        // Update all
        toUpdate = m_installedLibraries;
    } else {
        // Find specific library
        for (const auto& lib : m_installedLibraries) {
            if (lib.name == name) {
                toUpdate.push_back(lib);
                break;
            }
        }

        if (toUpdate.empty()) {
            m_error = "Library not found: " + name;
            std::cerr << m_error << std::endl;
            return false;
        }
    }

    bool allSuccess = true;
    for (const auto& lib : toUpdate) {
        std::cout << "Updating " << lib.name << "..." << std::endl;

        // Remove and reinstall
        fs::path libDir = m_libsDir / lib.name;
        std::error_code ec;
        fs::remove_all(libDir, ec);

        removeFromManifest(lib.name);

        if (!install(lib.gitUrl, lib.gitRef)) {
            std::cerr << "Failed to update " << lib.name << ": " << m_error << std::endl;
            allSuccess = false;
        }
    }

    return allSuccess;
}

std::vector<InstalledLibrary> LibraryManager::listInstalled() const {
    return m_installedLibraries;
}

void LibraryManager::outputJson() const {
    json j;
    j["libraries"] = json::array();

    for (const auto& lib : m_installedLibraries) {
        j["libraries"].push_back({
            {"name", lib.name},
            {"version", lib.version},
            {"gitUrl", lib.gitUrl},
            {"gitRef", lib.gitRef},
            {"installedAt", lib.installedAt},
            {"builtFrom", lib.builtFrom}
        });
    }

    std::cout << j.dump(2) << std::endl;
}

void LibraryManager::loadUserLibraries() {
    if (m_installedLibraries.empty()) {
        return;
    }

    std::cout << "Loading user libraries..." << std::endl;

    for (const auto& lib : m_installedLibraries) {
        fs::path libDir = lib.installPath / "lib";
        if (!fs::exists(libDir)) {
            std::cerr << "Warning: No lib directory for " << lib.name << std::endl;
            continue;
        }

        // Find and load library
        for (const auto& entry : fs::directory_iterator(libDir)) {
            std::string filename = entry.path().filename().string();

#ifdef __APPLE__
            if (filename.find(".dylib") == std::string::npos) continue;
#elif defined(_WIN32)
            if (filename.find(".dll") == std::string::npos) continue;
#else
            if (filename.find(".so") == std::string::npos) continue;
#endif

            // Skip ONNX Runtime and other dependencies
            if (filename.find("onnxruntime") != std::string::npos) continue;

            std::string libPath = entry.path().string();
            std::cout << "  Loading: " << filename << std::endl;

#ifdef _WIN32
            HMODULE handle = LoadLibraryA(libPath.c_str());
            if (!handle) {
                std::cerr << "  Failed to load " << filename << ": " << GetLastError() << std::endl;
                continue;
            }
            m_loadedLibraries.push_back(handle);
#else
            // Use RTLD_GLOBAL so static initializers can register with OperatorRegistry
            void* handle = dlopen(libPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (!handle) {
                std::cerr << "  Failed to load " << filename << ": " << dlerror() << std::endl;
                continue;
            }
            m_loadedLibraries.push_back(handle);
#endif
        }
    }

    if (!m_loadedLibraries.empty()) {
        std::cout << "Loaded " << m_loadedLibraries.size() << " user libraries" << std::endl;
    }
}

std::vector<fs::path> LibraryManager::getIncludePaths() const {
    std::vector<fs::path> paths;
    for (const auto& lib : m_installedLibraries) {
        fs::path includePath = lib.installPath / "include";
        if (fs::exists(includePath)) {
            paths.push_back(includePath);
        }
    }
    return paths;
}

std::vector<fs::path> LibraryManager::getLibraryPaths() const {
    std::vector<fs::path> paths;
    for (const auto& lib : m_installedLibraries) {
        fs::path libPath = lib.installPath / "lib";
        if (fs::exists(libPath)) {
            paths.push_back(libPath);
        }
    }
    return paths;
}

} // namespace vivid
