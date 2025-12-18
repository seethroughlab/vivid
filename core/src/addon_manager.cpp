// Vivid - Addon Manager Implementation

#include <vivid/addon_manager.h>
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

// Simple JSON parsing (we don't want to add a dependency for this)
// For a production system, consider using nlohmann/json or similar

namespace vivid {

// -----------------------------------------------------------------------------
// JSON Helpers (minimal implementation)
// -----------------------------------------------------------------------------

static std::string jsonEscape(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]*)\"";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re)) {
        return match[1].str();
    }
    return "";
}

static std::vector<std::string> extractJsonStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::string pattern = "\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re)) {
        std::string arrayContent = match[1].str();
        std::regex itemRe("\"([^\"]*)\"");
        auto begin = std::sregex_iterator(arrayContent.begin(), arrayContent.end(), itemRe);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            result.push_back((*it)[1].str());
        }
    }
    return result;
}

// Extract nested object value: "prebuilt": { "darwin-arm64": "url" }
static std::string extractNestedJsonString(const std::string& json,
                                           const std::string& objKey,
                                           const std::string& key) {
    // Find the object
    std::string objPattern = "\"" + objKey + "\"\\s*:\\s*\\{([^}]*)\\}";
    std::regex objRe(objPattern);
    std::smatch objMatch;
    if (std::regex_search(json, objMatch, objRe)) {
        std::string objContent = objMatch[1].str();
        return extractJsonString("{" + objContent + "}", key);
    }
    return "";
}

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
// AddonManager Implementation
// -----------------------------------------------------------------------------

AddonManager& AddonManager::instance() {
    static AddonManager instance;
    return instance;
}

AddonManager::AddonManager() {
    // Set up addons directory
#ifdef _WIN32
    const char* homeDir = std::getenv("USERPROFILE");
#else
    const char* homeDir = std::getenv("HOME");
#endif
    if (homeDir) {
        m_addonsDir = fs::path(homeDir) / ".vivid" / "addons";
    } else {
        m_addonsDir = fs::current_path() / ".vivid" / "addons";
    }

    // Create directory if needed
    std::error_code ec;
    fs::create_directories(m_addonsDir, ec);

    // Load manifest
    loadManifest();
}

std::string AddonManager::getPlatform() const {
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

std::optional<AddonJson> AddonManager::parseAddonJson(const fs::path& path) const {
    if (!fs::exists(path)) {
        return std::nullopt;
    }

    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    AddonJson addon;
    addon.name = extractJsonString(json, "name");
    addon.version = extractJsonString(json, "version");
    addon.description = extractJsonString(json, "description");
    addon.repository = extractJsonString(json, "repository");
    addon.license = extractJsonString(json, "license");
    addon.dependencies = extractJsonStringArray(json, "dependencies");
    addon.operators = extractJsonStringArray(json, "operators");

    // Parse prebuilt URLs
    addon.prebuilt.darwinArm64 = extractNestedJsonString(json, "prebuilt", "darwin-arm64");
    addon.prebuilt.darwinX64 = extractNestedJsonString(json, "prebuilt", "darwin-x64");
    addon.prebuilt.linuxX64 = extractNestedJsonString(json, "prebuilt", "linux-x64");
    addon.prebuilt.win32X64 = extractNestedJsonString(json, "prebuilt", "win32-x64");

    if (addon.name.empty()) {
        return std::nullopt;
    }

    return addon;
}

void AddonManager::loadManifest() {
    m_installedAddons.clear();

    fs::path manifestPath = m_addonsDir / "manifest.json";
    if (!fs::exists(manifestPath)) {
        return;
    }

    std::ifstream file(manifestPath);
    if (!file) {
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    // Parse addons object - find each addon entry
    // Format: "addons": { "name": { ... }, ... }
    std::regex addonRe("\"([^\"]+)\"\\s*:\\s*\\{([^}]+)\\}");
    auto begin = std::sregex_iterator(json.begin(), json.end(), addonRe);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string name = (*it)[1].str();
        std::string content = (*it)[2].str();

        // Skip the "addons" key itself
        if (name == "addons" || name == "version") continue;

        InstalledAddon addon;
        addon.name = name;
        addon.version = extractJsonString("{" + content + "}", "version");
        addon.gitUrl = extractJsonString("{" + content + "}", "gitUrl");
        addon.gitRef = extractJsonString("{" + content + "}", "gitRef");
        addon.installedAt = extractJsonString("{" + content + "}", "installedAt");
        addon.builtFrom = extractJsonString("{" + content + "}", "builtFrom");
        addon.installPath = m_addonsDir / name;

        if (!addon.name.empty()) {
            m_installedAddons.push_back(addon);
        }
    }
}

void AddonManager::saveManifest() const {
    fs::path manifestPath = m_addonsDir / "manifest.json";

    std::ofstream file(manifestPath);
    if (!file) {
        std::cerr << "Failed to save manifest.json" << std::endl;
        return;
    }

    file << "{\n";
    file << "  \"version\": 1,\n";
    file << "  \"addons\": {\n";

    for (size_t i = 0; i < m_installedAddons.size(); ++i) {
        const auto& addon = m_installedAddons[i];
        file << "    \"" << jsonEscape(addon.name) << "\": {\n";
        file << "      \"version\": \"" << jsonEscape(addon.version) << "\",\n";
        file << "      \"gitUrl\": \"" << jsonEscape(addon.gitUrl) << "\",\n";
        file << "      \"gitRef\": \"" << jsonEscape(addon.gitRef) << "\",\n";
        file << "      \"installedAt\": \"" << jsonEscape(addon.installedAt) << "\",\n";
        file << "      \"builtFrom\": \"" << jsonEscape(addon.builtFrom) << "\"\n";
        file << "    }";
        if (i < m_installedAddons.size() - 1) {
            file << ",";
        }
        file << "\n";
    }

    file << "  }\n";
    file << "}\n";
}

void AddonManager::addToManifest(const InstalledAddon& addon) {
    // Remove existing entry if present
    removeFromManifest(addon.name);
    m_installedAddons.push_back(addon);
    saveManifest();
}

void AddonManager::removeFromManifest(const std::string& name) {
    m_installedAddons.erase(
        std::remove_if(m_installedAddons.begin(), m_installedAddons.end(),
                       [&name](const InstalledAddon& a) { return a.name == name; }),
        m_installedAddons.end());
    saveManifest();
}

bool AddonManager::downloadFile(const std::string& url, const fs::path& dest) {
    std::cout << "Downloading: " << url << std::endl;

    std::stringstream cmd;
#ifdef _WIN32
    cmd << "curl -fsSL -o \"" << dest.string() << "\" \"" << url << "\" 2>&1";
#else
    cmd << "curl -fsSL -o \"" << dest.string() << "\" \"" << url << "\" 2>&1";
#endif

    auto [exitCode, output] = executeCommand(cmd.str());
    if (exitCode != 0) {
        m_error = "Download failed: " + output;
        return false;
    }
    return true;
}

bool AddonManager::extractArchive(const fs::path& archive, const fs::path& dest) {
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

bool AddonManager::cloneRepo(const std::string& url, const std::string& ref, const fs::path& dest) {
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

bool AddonManager::cmakeBuild(const fs::path& sourceDir, const fs::path& buildDir,
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

bool AddonManager::tryPrebuiltRelease(const std::string& gitUrl, const std::string& ref,
                                      const fs::path& addonDir) {
    // First, we need to get the addon.json to find prebuilt URLs
    // Try to fetch it from the repo's default branch

    // Extract owner/repo from URL
    std::regex repoRe("github\\.com[/:]([^/]+)/([^/.]+)");
    std::smatch match;
    if (!std::regex_search(gitUrl, match, repoRe)) {
        return false;  // Not a GitHub URL
    }

    std::string owner = match[1].str();
    std::string repo = match[2].str();

    // Fetch addon.json from GitHub raw
    std::string branch = ref.empty() ? "main" : ref;
    std::string addonJsonUrl = "https://raw.githubusercontent.com/" + owner + "/" + repo +
                               "/" + branch + "/addon.json";

    fs::path tempJson = m_addonsDir / "temp_addon.json";
    if (!downloadFile(addonJsonUrl, tempJson)) {
        // Try master branch
        branch = "master";
        addonJsonUrl = "https://raw.githubusercontent.com/" + owner + "/" + repo +
                       "/" + branch + "/addon.json";
        if (!downloadFile(addonJsonUrl, tempJson)) {
            return false;
        }
    }

    auto addonJson = parseAddonJson(tempJson);
    fs::remove(tempJson);

    if (!addonJson) {
        return false;
    }

    // Get prebuilt URL for current platform
    std::string platform = getPlatform();
    std::string prebuiltUrl;

    if (platform == "darwin-arm64") {
        prebuiltUrl = addonJson->prebuilt.darwinArm64;
    } else if (platform == "darwin-x64") {
        prebuiltUrl = addonJson->prebuilt.darwinX64;
    } else if (platform == "linux-x64") {
        prebuiltUrl = addonJson->prebuilt.linuxX64;
    } else if (platform == "win32-x64") {
        prebuiltUrl = addonJson->prebuilt.win32X64;
    }

    if (prebuiltUrl.empty()) {
        std::cout << "No prebuilt binary for " << platform << ", will build from source" << std::endl;
        return false;
    }

    // Replace ${version} placeholder
    std::string version = ref.empty() ? ("v" + addonJson->version) : ref;
    size_t pos = prebuiltUrl.find("${version}");
    if (pos != std::string::npos) {
        prebuiltUrl.replace(pos, 10, version);
    }

    // Download prebuilt archive
    std::string ext = prebuiltUrl.substr(prebuiltUrl.rfind('.'));
    fs::path archivePath = m_addonsDir / ("temp_prebuilt" + ext);

    if (!downloadFile(prebuiltUrl, archivePath)) {
        return false;
    }

    // Create addon directory
    std::error_code ec;
    fs::create_directories(addonDir, ec);

    // Extract to temp, then move contents
    fs::path tempExtract = m_addonsDir / "temp_extract";
    fs::create_directories(tempExtract, ec);

    if (!extractArchive(archivePath, tempExtract)) {
        fs::remove(archivePath);
        fs::remove_all(tempExtract);
        return false;
    }

    fs::remove(archivePath);

    // Find the extracted directory (usually addon-name-platform/)
    fs::path extractedDir;
    for (const auto& entry : fs::directory_iterator(tempExtract)) {
        if (entry.is_directory()) {
            extractedDir = entry.path();
            break;
        }
    }

    if (extractedDir.empty()) {
        m_error = "Could not find extracted addon directory";
        fs::remove_all(tempExtract);
        return false;
    }

    // Move contents to addon directory
    for (const auto& entry : fs::directory_iterator(extractedDir)) {
        fs::rename(entry.path(), addonDir / entry.path().filename(), ec);
    }

    fs::remove_all(tempExtract);

    std::cout << "Installed prebuilt " << addonJson->name << " v" << addonJson->version << std::endl;
    return true;
}

bool AddonManager::buildFromSource(const std::string& gitUrl, const std::string& ref,
                                   const fs::path& addonDir) {
    // Create addon directory structure
    std::error_code ec;
    fs::create_directories(addonDir, ec);

    fs::path srcDir = addonDir / "src";
    fs::path buildDir = addonDir / "build";

    // Clone repository
    if (!cloneRepo(gitUrl, ref, srcDir)) {
        return false;
    }

    // Build with cmake
    if (!cmakeBuild(srcDir, buildDir, addonDir)) {
        return false;
    }

    return true;
}

bool AddonManager::install(const std::string& gitUrl, const std::string& ref) {
    m_error.clear();

    // Extract addon name from URL
    std::regex repoRe("[/:]([^/]+)/([^/.]+?)(?:\\.git)?$");
    std::smatch match;
    if (!std::regex_search(gitUrl, match, repoRe)) {
        m_error = "Invalid git URL: " + gitUrl;
        return false;
    }

    std::string repoName = match[2].str();

    // Check if already installed
    for (const auto& addon : m_installedAddons) {
        if (addon.name == repoName) {
            std::cout << repoName << " is already installed. Use 'vivid addons update " << repoName << "' to update." << std::endl;
            return true;
        }
    }

    fs::path addonDir = m_addonsDir / repoName;

    std::cout << "Installing " << repoName << "..." << std::endl;

    // Try prebuilt first, fall back to source
    bool prebuiltSuccess = tryPrebuiltRelease(gitUrl, ref, addonDir);
    std::string builtFrom = "prebuilt";

    if (!prebuiltSuccess) {
        builtFrom = "source";
        if (!buildFromSource(gitUrl, ref, addonDir)) {
            // Cleanup on failure
            std::error_code ec;
            fs::remove_all(addonDir, ec);
            return false;
        }
    }

    // Read addon.json for version info
    fs::path addonJsonPath = addonDir / "addon.json";
    auto addonJson = parseAddonJson(addonJsonPath);

    // Create manifest entry
    InstalledAddon entry;
    entry.name = repoName;
    entry.version = addonJson ? addonJson->version : "unknown";
    entry.gitUrl = gitUrl;
    entry.gitRef = ref;
    entry.builtFrom = builtFrom;
    entry.installPath = addonDir;

    // Get current timestamp
    std::time_t now = std::time(nullptr);
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    entry.installedAt = timeBuf;

    addToManifest(entry);

    std::cout << "Successfully installed " << repoName << " v" << entry.version << std::endl;

    // Print operators added
    if (addonJson && !addonJson->operators.empty()) {
        std::cout << "Operators added:";
        for (const auto& op : addonJson->operators) {
            std::cout << " " << op;
        }
        std::cout << std::endl;
    }

    return true;
}

bool AddonManager::remove(const std::string& name) {
    m_error.clear();

    // Find the addon
    bool found = false;
    for (const auto& addon : m_installedAddons) {
        if (addon.name == name) {
            found = true;
            break;
        }
    }

    if (!found) {
        m_error = "Addon not found: " + name;
        std::cerr << m_error << std::endl;
        return false;
    }

    fs::path addonDir = m_addonsDir / name;

    std::cout << "Removing " << name << "..." << std::endl;

    // Remove directory
    std::error_code ec;
    fs::remove_all(addonDir, ec);
    if (ec) {
        m_error = "Failed to remove addon directory: " + ec.message();
        std::cerr << m_error << std::endl;
        return false;
    }

    // Remove from manifest
    removeFromManifest(name);

    std::cout << "Successfully removed " << name << std::endl;
    return true;
}

bool AddonManager::update(const std::string& name) {
    m_error.clear();

    std::vector<InstalledAddon> toUpdate;

    if (name.empty()) {
        // Update all
        toUpdate = m_installedAddons;
    } else {
        // Find specific addon
        for (const auto& addon : m_installedAddons) {
            if (addon.name == name) {
                toUpdate.push_back(addon);
                break;
            }
        }

        if (toUpdate.empty()) {
            m_error = "Addon not found: " + name;
            std::cerr << m_error << std::endl;
            return false;
        }
    }

    bool allSuccess = true;
    for (const auto& addon : toUpdate) {
        std::cout << "Updating " << addon.name << "..." << std::endl;

        // Remove and reinstall
        fs::path addonDir = m_addonsDir / addon.name;
        std::error_code ec;
        fs::remove_all(addonDir, ec);

        removeFromManifest(addon.name);

        if (!install(addon.gitUrl, addon.gitRef)) {
            std::cerr << "Failed to update " << addon.name << ": " << m_error << std::endl;
            allSuccess = false;
        }
    }

    return allSuccess;
}

std::vector<InstalledAddon> AddonManager::listInstalled() const {
    return m_installedAddons;
}

void AddonManager::outputJson() const {
    std::cout << "{\n";
    std::cout << "  \"addons\": [\n";

    for (size_t i = 0; i < m_installedAddons.size(); ++i) {
        const auto& addon = m_installedAddons[i];
        std::cout << "    {\n";
        std::cout << "      \"name\": \"" << jsonEscape(addon.name) << "\",\n";
        std::cout << "      \"version\": \"" << jsonEscape(addon.version) << "\",\n";
        std::cout << "      \"gitUrl\": \"" << jsonEscape(addon.gitUrl) << "\",\n";
        std::cout << "      \"gitRef\": \"" << jsonEscape(addon.gitRef) << "\",\n";
        std::cout << "      \"installedAt\": \"" << jsonEscape(addon.installedAt) << "\",\n";
        std::cout << "      \"builtFrom\": \"" << jsonEscape(addon.builtFrom) << "\"\n";
        std::cout << "    }";
        if (i < m_installedAddons.size() - 1) {
            std::cout << ",";
        }
        std::cout << "\n";
    }

    std::cout << "  ]\n";
    std::cout << "}\n";
}

void AddonManager::loadUserAddons() {
    if (m_installedAddons.empty()) {
        return;
    }

    std::cout << "Loading user addons..." << std::endl;

    for (const auto& addon : m_installedAddons) {
        fs::path libDir = addon.installPath / "lib";
        if (!fs::exists(libDir)) {
            std::cerr << "Warning: No lib directory for " << addon.name << std::endl;
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
            HMODULE lib = LoadLibraryA(libPath.c_str());
            if (!lib) {
                std::cerr << "  Failed to load " << filename << ": " << GetLastError() << std::endl;
                continue;
            }
            m_loadedLibraries.push_back(lib);
#else
            // Use RTLD_GLOBAL so static initializers can register with OperatorRegistry
            void* lib = dlopen(libPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
            if (!lib) {
                std::cerr << "  Failed to load " << filename << ": " << dlerror() << std::endl;
                continue;
            }
            m_loadedLibraries.push_back(lib);
#endif
        }
    }

    if (!m_loadedLibraries.empty()) {
        std::cout << "Loaded " << m_loadedLibraries.size() << " addon libraries" << std::endl;
    }
}

std::vector<fs::path> AddonManager::getIncludePaths() const {
    std::vector<fs::path> paths;
    for (const auto& addon : m_installedAddons) {
        fs::path includePath = addon.installPath / "include";
        if (fs::exists(includePath)) {
            paths.push_back(includePath);
        }
    }
    return paths;
}

std::vector<fs::path> AddonManager::getLibraryPaths() const {
    std::vector<fs::path> paths;
    for (const auto& addon : m_installedAddons) {
        fs::path libPath = addon.installPath / "lib";
        if (fs::exists(libPath)) {
            paths.push_back(libPath);
        }
    }
    return paths;
}

} // namespace vivid
