#pragma once

/**
 * @file shader_preprocessor.h
 * @brief Runtime shader preprocessing with include directive support
 *
 * Provides a simple `// @include "file.wgsl"` directive that allows shader
 * code sharing without build system changes. The directive is processed at
 * load time, keeping raw WGSL valid (comments are ignored by the compiler).
 *
 * Usage:
 * @code
 * // In your shader file:
 * // @include "lib/constants.wgsl"
 * // @include "lib/pbr.wgsl"
 *
 * // In C++:
 * std::string source = ShaderPreprocessor::instance().process(rawSource, basePath);
 * @endcode
 */

#include <string>
#include <unordered_set>
#include <filesystem>
#include <functional>

namespace vivid {

/**
 * @brief Runtime shader preprocessor with include directive support
 *
 * Features:
 * - Processes `// @include "path.wgsl"` directives
 * - Resolves paths relative to the including file
 * - Detects and prevents circular includes
 * - Tracks line numbers for error messages
 * - Works with hot-reload (processed on each load)
 */
class ShaderPreprocessor {
public:
    /// @brief Get singleton instance
    static ShaderPreprocessor& instance();

    /**
     * @brief Process shader source, expanding @include directives
     * @param source Raw shader source code
     * @param basePath Directory containing the shader (for relative include resolution)
     * @return Processed shader with includes expanded, or original source on error
     *
     * Include syntax: `// @include "path/to/file.wgsl"`
     * - Paths are relative to the including file's directory
     * - Circular includes are detected and reported as errors
     * - Missing includes log a warning and continue (to support hot-reload)
     */
    std::string process(const std::string& source, const std::filesystem::path& basePath);

    /**
     * @brief Process shader source with a custom file loader
     * @param source Raw shader source code
     * @param basePath Directory containing the shader
     * @param fileLoader Custom function to load include files (path -> content)
     * @return Processed shader with includes expanded
     *
     * Useful for testing or when files aren't on disk.
     */
    std::string process(
        const std::string& source,
        const std::filesystem::path& basePath,
        std::function<std::string(const std::filesystem::path&)> fileLoader
    );

    /**
     * @brief Clear include cache (for hot-reload)
     */
    void clearCache();

    /**
     * @brief Check if last process() call had errors
     * @return True if there were errors (circular includes, missing files)
     */
    bool hasErrors() const { return m_hasErrors; }

    /**
     * @brief Get error message from last process() call
     * @return Error description, or empty string if no errors
     */
    const std::string& lastError() const { return m_lastError; }

private:
    ShaderPreprocessor() = default;
    ~ShaderPreprocessor() = default;

    // Non-copyable
    ShaderPreprocessor(const ShaderPreprocessor&) = delete;
    ShaderPreprocessor& operator=(const ShaderPreprocessor&) = delete;

    /**
     * @brief Internal recursive processing with include tracking
     */
    std::string processInternal(
        const std::string& source,
        const std::filesystem::path& basePath,
        std::unordered_set<std::string>& includedFiles,
        std::function<std::string(const std::filesystem::path&)>& fileLoader,
        int depth
    );

    /**
     * @brief Parse an @include directive from a line
     * @param line Line to parse
     * @param outPath Output: extracted include path
     * @return True if line contains a valid @include directive
     */
    bool parseIncludeDirective(const std::string& line, std::string& outPath);

    bool m_hasErrors = false;
    std::string m_lastError;

    // Maximum include depth to prevent infinite recursion
    static constexpr int MAX_INCLUDE_DEPTH = 16;
};

} // namespace vivid
