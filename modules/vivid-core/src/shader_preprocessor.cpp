// Vivid - Shader Preprocessor Implementation

#include <vivid/shader_preprocessor.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>

namespace fs = std::filesystem;

namespace vivid {

ShaderPreprocessor& ShaderPreprocessor::instance() {
    static ShaderPreprocessor instance;
    return instance;
}

void ShaderPreprocessor::clearCache() {
    // Currently stateless - cache clearing is a no-op
    // Future: could cache processed includes for performance
}

std::string ShaderPreprocessor::process(const std::string& source, const fs::path& basePath) {
    // Default file loader: read from disk
    auto fileLoader = [](const fs::path& path) -> std::string {
        std::ifstream file(path);
        if (!file.is_open()) {
            return "";
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return buffer.str();
    };

    return process(source, basePath, fileLoader);
}

std::string ShaderPreprocessor::process(
    const std::string& source,
    const fs::path& basePath,
    std::function<std::string(const fs::path&)> fileLoader
) {
    m_hasErrors = false;
    m_lastError.clear();

    std::unordered_set<std::string> includedFiles;
    return processInternal(source, basePath, includedFiles, fileLoader, 0);
}

std::string ShaderPreprocessor::processInternal(
    const std::string& source,
    const fs::path& basePath,
    std::unordered_set<std::string>& includedFiles,
    std::function<std::string(const fs::path&)>& fileLoader,
    int depth
) {
    // Check recursion depth
    if (depth > MAX_INCLUDE_DEPTH) {
        m_hasErrors = true;
        m_lastError = "Maximum include depth exceeded (" + std::to_string(MAX_INCLUDE_DEPTH) + ")";
        std::cerr << "[ShaderPreprocessor] Error: " << m_lastError << std::endl;
        return source;
    }

    std::stringstream output;
    std::istringstream input(source);
    std::string line;
    int lineNumber = 0;

    while (std::getline(input, line)) {
        lineNumber++;

        std::string includePath;
        if (parseIncludeDirective(line, includePath)) {
            // Resolve the include path relative to the current file
            fs::path resolvedPath;
            if (fs::path(includePath).is_absolute()) {
                resolvedPath = includePath;
            } else {
                resolvedPath = basePath / includePath;
            }

            // Normalize path for circular include detection
            std::string normalizedPath;
            try {
                normalizedPath = fs::weakly_canonical(resolvedPath).string();
            } catch (...) {
                normalizedPath = resolvedPath.string();
            }

            // Check for circular includes
            if (includedFiles.count(normalizedPath) > 0) {
                // Already included - skip (this is not an error, just prevents duplication)
                output << "// [Already included: " << includePath << "]\n";
                continue;
            }

            // Mark as included
            includedFiles.insert(normalizedPath);

            // Load the included file
            std::string includeContent = fileLoader(resolvedPath);
            if (includeContent.empty()) {
                // Try with lib/ prefix if not found
                fs::path libPath = basePath / "lib" / includePath;
                includeContent = fileLoader(libPath);

                if (includeContent.empty()) {
                    m_hasErrors = true;
                    m_lastError = "Include file not found: " + includePath + " (at line " + std::to_string(lineNumber) + ")";
                    std::cerr << "[ShaderPreprocessor] Warning: " << m_lastError << std::endl;
                    output << "// [Include not found: " << includePath << "]\n";
                    continue;
                }
                resolvedPath = libPath;
            }

            // Emit a marker comment for debugging
            output << "// ---- Begin include: " << includePath << " ----\n";

            // Recursively process the included content
            fs::path includeBasePath = resolvedPath.parent_path();
            std::string processedInclude = processInternal(
                includeContent, includeBasePath, includedFiles, fileLoader, depth + 1
            );

            output << processedInclude;

            // Ensure newline at end
            if (!processedInclude.empty() && processedInclude.back() != '\n') {
                output << '\n';
            }

            output << "// ---- End include: " << includePath << " ----\n";
        } else {
            output << line << '\n';
        }
    }

    return output.str();
}

bool ShaderPreprocessor::parseIncludeDirective(const std::string& line, std::string& outPath) {
    // Match: // @include "path" or // @include 'path'
    // Also support:  //  @include "path"  (with extra whitespace)
    static const std::regex includePattern(
        R"(^\s*//\s*@include\s+["']([^"']+)["']\s*$)"
    );

    std::smatch match;
    if (std::regex_match(line, match, includePattern)) {
        outPath = match[1].str();
        return true;
    }

    return false;
}

} // namespace vivid
