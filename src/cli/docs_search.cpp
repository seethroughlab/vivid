// Vivid Documentation Search
// Shared utilities extracted from mcp_server.cpp for use by both CLI and MCP.

#include <vivid/docs_search.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cctype>
#include <set>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <linux/limits.h>
#endif

using json = nlohmann::json;

namespace vivid::docs {

// Get the executable directory (shared with cli.cpp's getExecutableDir)
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

// Format module directory name: "vivid-audio" -> "Audio"
static std::string formatModuleName(const std::string& dirName) {
    std::string name = dirName;
    if (name.rfind("vivid-", 0) == 0) {
        name = name.substr(6);
    }
    if (!name.empty()) {
        name[0] = std::toupper(static_cast<unsigned char>(name[0]));
    }
    return name;
}

// Format example name: "drum-machine" -> "Drum Machine"
static std::string formatExampleName(const std::string& dirName) {
    std::string name = dirName;
    std::replace(name.begin(), name.end(), '-', ' ');
    std::replace(name.begin(), name.end(), '_', ' ');
    bool capitalizeNext = true;
    for (char& c : name) {
        if (c == ' ') {
            capitalizeNext = true;
        } else if (capitalizeNext) {
            c = std::toupper(static_cast<unsigned char>(c));
            capitalizeNext = false;
        }
    }
    return name;
}

static std::string getHomeDir() {
    const char* home = getenv("HOME");
#ifdef _WIN32
    if (!home) home = getenv("USERPROFILE");
#endif
    return home ? home : "";
}

// Scan a modules directory for READMEs and example AGENTS.md files
static void scanModulesDir(const fs::path& modulesDir,
                           std::vector<std::pair<std::string, std::string>>& docs,
                           bool isUserModules = false) {
    if (!fs::exists(modulesDir) || !fs::is_directory(modulesDir)) return;

    try {
        for (const auto& moduleEntry : fs::directory_iterator(modulesDir)) {
            if (!moduleEntry.is_directory()) continue;

            std::string moduleDirName = moduleEntry.path().filename().string();
            std::string moduleName = formatModuleName(moduleDirName);
            std::string prefix = isUserModules ? "[User] " : "";

            fs::path readme = moduleEntry.path() / "README.md";
            if (fs::exists(readme)) {
                docs.push_back({readme.string(), prefix + moduleName + " Module"});
            }

            fs::path examplesDir = moduleEntry.path() / "examples";
            if (fs::exists(examplesDir) && fs::is_directory(examplesDir)) {
                for (const auto& exampleEntry : fs::directory_iterator(examplesDir)) {
                    if (!exampleEntry.is_directory()) continue;

                    fs::path agentsMd = exampleEntry.path() / "AGENTS.md";
                    fs::path claudeMd = exampleEntry.path() / "CLAUDE.md";
                    fs::path docFile = fs::exists(agentsMd) ? agentsMd : claudeMd;
                    if (fs::exists(docFile)) {
                        std::string exampleName = formatExampleName(exampleEntry.path().filename().string());
                        docs.push_back({docFile.string(), prefix + moduleName + ": " + exampleName});
                    }
                }
            }
        }
    } catch (...) {
        // Ignore errors (permission issues, etc.)
    }
}

fs::path findDocsDir() {
    std::vector<fs::path> searchPaths;

    searchPaths.push_back(fs::current_path() / "docs");

    fs::path exeDir = getExecutableDir();
    searchPaths.push_back(exeDir.parent_path().parent_path() / "docs");
    searchPaths.push_back(exeDir.parent_path() / "docs");

    std::string home = getHomeDir();
    if (!home.empty()) {
        searchPaths.push_back(fs::path(home) / ".vivid" / "docs");
    }

    for (const auto& path : searchPaths) {
        if (fs::exists(path) && fs::is_directory(path)) {
            return path;
        }
    }
    return {};
}

fs::path findModulesDir() {
    std::vector<fs::path> searchPaths;

    searchPaths.push_back(fs::current_path() / "modules");

    fs::path exeDir = getExecutableDir();
    searchPaths.push_back(exeDir.parent_path() / "modules");
    searchPaths.push_back(exeDir.parent_path().parent_path() / "modules");

    for (const auto& path : searchPaths) {
        if (fs::exists(path) && fs::is_directory(path)) {
            return path;
        }
    }
    return {};
}

std::vector<std::pair<std::string, std::string>> getDocFiles() {
    std::vector<std::pair<std::string, std::string>> docs;

    // 1. Scan docs/ directory
    fs::path docsDir = findDocsDir();
    if (!docsDir.empty()) {
        for (const auto& entry : fs::directory_iterator(docsDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".md") {
                std::string filename = entry.path().filename().string();
                if (filename == "README.md") continue;

                std::string name = filename.substr(0, filename.length() - 3);
                std::replace(name.begin(), name.end(), '-', ' ');
                std::replace(name.begin(), name.end(), '_', ' ');

                docs.push_back({entry.path().string(), name});
            }
        }
    }

    // 2. Scan built-in modules
    fs::path modulesDir = findModulesDir();
    scanModulesDir(modulesDir, docs, false);

    // 3. Scan user-installed modules
    std::string homeDir = getHomeDir();
    if (!homeDir.empty()) {
        fs::path userModulesDir = fs::path(homeDir) / ".vivid" / "modules";
        scanModulesDir(userModulesDir, docs, true);
    }

    return docs;
}

std::string loadDocFile(const std::string& filename) {
    // Check if filename is already a full/absolute path
    fs::path directPath(filename);
    if (fs::exists(directPath)) {
        std::ifstream file(directPath);
        if (file) {
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
    }

    // Search multiple locations
    std::vector<fs::path> searchPaths;

    searchPaths.push_back(fs::current_path() / "docs" / filename);

    std::string home = getHomeDir();
    if (!home.empty()) {
        searchPaths.push_back(fs::path(home) / ".vivid" / "docs" / filename);
    }

    fs::path exeDir = getExecutableDir();
    if (!exeDir.empty()) {
        searchPaths.push_back(exeDir.parent_path().parent_path() / "docs" / filename);
        searchPaths.push_back(exeDir.parent_path().parent_path().parent_path() / "docs" / filename);
        searchPaths.push_back(exeDir.parent_path() / "share" / "vivid" / "docs" / filename);
        searchPaths.push_back(exeDir.parent_path() / "Resources" / "docs" / filename);
    }

    for (const auto& path : searchPaths) {
        if (fs::exists(path)) {
            std::ifstream file(path);
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }
    }

    return "";
}

json searchDocs(const std::string& query, int maxResults) {
    // Split query into words (lowercase, strip punctuation)
    std::vector<std::string> queryWords;
    {
        std::string queryLower = query;
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
        std::istringstream iss(queryLower);
        std::string word;
        while (iss >> word) {
            word.erase(std::remove_if(word.begin(), word.end(),
                [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }), word.end());
            if (!word.empty()) {
                queryWords.push_back(word);
            }
        }
    }

    if (queryWords.empty()) {
        return json::array();
    }

    auto docs = getDocFiles();
    json matches = json::array();

    for (const auto& [filename, title] : docs) {
        std::string content = loadDocFile(filename);
        if (content.empty()) continue;

        std::string contentLower = content;
        std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), ::tolower);

        // Split into sections by markdown headers
        std::vector<std::tuple<std::string, size_t, size_t>> sections;
        size_t pos = 0;
        std::string currentHeader = title;
        size_t currentStart = 0;

        while ((pos = content.find("\n#", pos)) != std::string::npos) {
            if (pos > currentStart) {
                sections.emplace_back(currentHeader, currentStart, pos);
            }
            size_t headerEnd = content.find('\n', pos + 1);
            if (headerEnd != std::string::npos) {
                std::string header = content.substr(pos + 1, headerEnd - pos - 1);
                size_t hashEnd = header.find_first_not_of("# ");
                if (hashEnd != std::string::npos) {
                    currentHeader = title + " > " + header.substr(hashEnd);
                } else {
                    currentHeader = title + " > " + header;
                }
            }
            currentStart = pos + 1;
            pos++;
        }
        sections.emplace_back(currentHeader, currentStart, content.length());

        // Search each section with OR logic and relevance scoring
        for (const auto& [section, sectionStart, sectionEnd] : sections) {
            std::string sectionContent = contentLower.substr(sectionStart, sectionEnd - sectionStart);

            int matchScore = 0;
            size_t firstWordPos = std::string::npos;
            for (const auto& word : queryWords) {
                size_t wordPos = sectionContent.find(word);
                if (wordPos != std::string::npos) {
                    matchScore++;
                    if (firstWordPos == std::string::npos || wordPos < firstWordPos) {
                        firstWordPos = wordPos;
                    }
                }
            }

            if (matchScore > 0 && firstWordPos != std::string::npos) {
                size_t contextStart = (firstWordPos > 150) ? firstWordPos - 150 : 0;
                size_t contextLen = std::min(size_t(400), sectionEnd - sectionStart - contextStart);

                std::string context = content.substr(sectionStart + contextStart, contextLen);
                if (contextStart > 0) {
                    size_t firstSpace = context.find(' ');
                    if (firstSpace != std::string::npos) context = "..." + context.substr(firstSpace);
                }
                if (contextStart + contextLen < sectionEnd - sectionStart) {
                    size_t lastSpace = context.rfind(' ');
                    if (lastSpace != std::string::npos) context = context.substr(0, lastSpace) + "...";
                }

                matches.push_back({
                    {"file", filename},
                    {"section", section},
                    {"context", context},
                    {"score", matchScore}
                });
            }
        }
    }

    // Sort by score (highest first), then limit
    std::sort(matches.begin(), matches.end(), [](const json& a, const json& b) {
        return a["score"].get<int>() > b["score"].get<int>();
    });
    if (static_cast<int>(matches.size()) > maxResults) {
        matches = json::array_t(matches.begin(), matches.begin() + maxResults);
    }

    return matches;
}

json getRecipes(const std::string& name) {
    std::string recipes = loadDocFile("RECIPES.md");
    if (recipes.empty()) {
        return json({{"error", "Could not load RECIPES.md"}});
    }

    struct Recipe {
        std::string title;
        std::string code;
        std::string description;
    };
    std::vector<Recipe> allRecipes;

    size_t pos = 0;
    while ((pos = recipes.find("\n## ", pos)) != std::string::npos) {
        size_t titleStart = pos + 4;
        size_t titleEnd = recipes.find('\n', titleStart);
        if (titleEnd == std::string::npos) break;

        std::string title = recipes.substr(titleStart, titleEnd - titleStart);

        size_t nextSection = recipes.find("\n## ", titleEnd);
        if (nextSection == std::string::npos) nextSection = recipes.length();

        std::string section = recipes.substr(titleEnd, nextSection - titleEnd);

        size_t codeStart = section.find("```cpp");
        if (codeStart != std::string::npos) {
            codeStart += 6;
            size_t codeEnd = section.find("```", codeStart);
            if (codeEnd != std::string::npos) {
                std::string code = section.substr(codeStart, codeEnd - codeStart);
                while (!code.empty() && (code.front() == '\n' || code.front() == ' '))
                    code.erase(0, 1);
                while (!code.empty() && (code.back() == '\n' || code.back() == ' '))
                    code.pop_back();

                std::string desc;
                size_t descEnd = section.find("```cpp");
                if (descEnd != std::string::npos && descEnd > 0) {
                    desc = section.substr(0, descEnd);
                    while (!desc.empty() && (desc.front() == '\n' || desc.front() == ' '))
                        desc.erase(0, 1);
                    while (!desc.empty() && (desc.back() == '\n' || desc.back() == ' '))
                        desc.pop_back();
                    if (desc.length() > 200)
                        desc = desc.substr(0, 197) + "...";
                }

                allRecipes.push_back({title, code, desc});
            }
        }
        pos = titleEnd;
    }

    if (name.empty()) {
        // List all recipes
        json response;
        response["count"] = allRecipes.size();
        response["recipes"] = json::array();
        for (const auto& r : allRecipes) {
            json recipe;
            recipe["name"] = r.title;
            if (!r.description.empty()) {
                recipe["description"] = r.description;
            }
            response["recipes"].push_back(recipe);
        }
        return response;
    }

    // Find recipe by name (case-insensitive partial match)
    std::string searchLower = name;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    for (const auto& r : allRecipes) {
        std::string nameLower = r.title;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        if (nameLower.find(searchLower) != std::string::npos) {
            json response;
            response["name"] = r.title;
            response["code"] = r.code;
            if (!r.description.empty()) {
                response["description"] = r.description;
            }
            return response;
        }
    }

    return json({{"error", "Recipe not found: " + name}});
}

json findExamples(const std::string& operatorName) {
    json examples = json::array();

    // Search RECIPES.md for code blocks containing the operator
    std::string recipes = loadDocFile("RECIPES.md");
    if (!recipes.empty()) {
        size_t pos = 0;
        while ((pos = recipes.find("```cpp", pos)) != std::string::npos) {
            size_t codeStart = pos + 6;
            size_t codeEnd = recipes.find("```", codeStart);
            if (codeEnd == std::string::npos) break;

            std::string codeBlock = recipes.substr(codeStart, codeEnd - codeStart);
            std::string pattern1 = "add<" + operatorName + ">";
            std::string pattern2 = operatorName + "(";

            if (codeBlock.find(pattern1) != std::string::npos ||
                codeBlock.find(pattern2) != std::string::npos) {

                // Trim leading/trailing whitespace
                while (!codeBlock.empty() && (codeBlock.front() == '\n' || codeBlock.front() == ' '))
                    codeBlock.erase(0, 1);
                while (!codeBlock.empty() && (codeBlock.back() == '\n' || codeBlock.back() == ' '))
                    codeBlock.pop_back();

                // Find recipe title
                std::string recipeTitle;
                size_t searchStart = (pos > 500) ? pos - 500 : 0;
                std::string beforeCode = recipes.substr(searchStart, pos - searchStart);
                size_t headingPos = beforeCode.rfind("\n## ");
                if (headingPos != std::string::npos) {
                    size_t titleStart = headingPos + 4;
                    size_t titleEnd = beforeCode.find('\n', titleStart);
                    if (titleEnd != std::string::npos) {
                        recipeTitle = beforeCode.substr(titleStart, titleEnd - titleStart);
                    }
                }

                json example = {
                    {"source", "docs/RECIPES.md"},
                    {"code", codeBlock}
                };
                if (!recipeTitle.empty()) {
                    example["recipe"] = recipeTitle;
                }
                examples.push_back(example);
            }
            pos = codeEnd + 3;
        }
    }

    // Search example chain.cpp files in module directories
    std::vector<fs::path> moduleSearchPaths;
    moduleSearchPaths.push_back(fs::current_path() / "modules");

    fs::path exeDir = getExecutableDir();
    if (!exeDir.empty()) {
        moduleSearchPaths.push_back(exeDir.parent_path().parent_path() / "modules");
    }

    for (const auto& modulesPath : moduleSearchPaths) {
        if (!fs::exists(modulesPath)) continue;

        for (const auto& moduleDir : fs::directory_iterator(modulesPath)) {
            if (!fs::is_directory(moduleDir)) continue;
            fs::path examplesPath = moduleDir.path() / "examples";
            if (!fs::exists(examplesPath)) continue;

            for (const auto& exampleDir : fs::directory_iterator(examplesPath)) {
                if (!fs::is_directory(exampleDir)) continue;
                fs::path chainFile = exampleDir.path() / "chain.cpp";
                if (!fs::exists(chainFile)) continue;

                std::ifstream file(chainFile);
                if (!file.is_open()) continue;

                std::string content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

                std::string pattern1 = "add<" + operatorName + ">";
                if (content.find(pattern1) == std::string::npos) continue;

                // Trim trailing whitespace
                while (!content.empty() && (content.back() == '\n' || content.back() == ' '))
                    content.pop_back();

                std::string relPath = fs::relative(chainFile, modulesPath.parent_path()).string();
                examples.push_back({
                    {"source", relPath},
                    {"code", content}
                });

                if (examples.size() >= 3) break;
            }
            if (examples.size() >= 3) break;
        }
        if (examples.size() >= 3) break;
    }

    json response;
    response["operator"] = operatorName;
    response["examples"] = examples;
    response["count"] = examples.size();
    if (examples.empty()) {
        response["message"] = "No examples found for '" + operatorName + "'. Try 'vivid operators' to verify the name.";
    }
    return response;
}

} // namespace vivid::docs
