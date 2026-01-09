#pragma once

/**
 * @file operator_registry.h
 * @brief Static registry for operator metadata
 *
 * Provides compile-time registration of operator types with metadata
 * for introspection and CLI/extension use.
 */

#include <vivid/operator.h>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <algorithm>
#include <cctype>

// Forward declare nlohmann::json to avoid including heavy header
#include <nlohmann/json_fwd.hpp>

namespace vivid {

/**
 * @brief Deduce module name from file path
 *
 * Extracts module name from paths like "modules/vivid-audio/src/..."
 * Returns empty string if not in a module directory.
 */
inline std::string deduceModuleFromPath(const char* file) {
    std::string path(file);
    // Look for "modules/<module-name>/" pattern
    auto pos = path.find("modules/");
    if (pos != std::string::npos) {
        auto start = pos + 8; // length of "modules/"
        auto end = path.find('/', start);
        if (end != std::string::npos) {
            return path.substr(start, end - start);
        }
    }
    return "";
}

/**
 * @brief Compute header path from module and operator name
 *
 * Returns the conventional header path for an operator:
 * - Core: modules/vivid-core/include/vivid/effects/<name>.h
 * - Module: modules/<module>/include/vivid/<short>/<name>.h
 */
inline std::string computeHeaderPath(const std::string& module, const std::string& operatorName) {
    // Convert operator name to lowercase for file name
    std::string filename = operatorName;
    std::transform(filename.begin(), filename.end(), filename.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Handle special cases where class name doesn't match file name
    // (e.g., class names with numbers or abbreviations)

    if (module.empty() || module == "vivid-core") {
        return "modules/vivid-core/include/vivid/effects/" + filename + ".h";
    }

    // Extract short module name (vivid-audio -> audio)
    std::string shortModule = module;
    if (shortModule.rfind("vivid-", 0) == 0) {
        shortModule = shortModule.substr(6);
    }

    return "modules/" + module + "/include/vivid/" + shortModule + "/" + filename + ".h";
}

/**
 * @brief Metadata about an operator type
 */
struct OperatorMeta {
    std::string name;           ///< Operator class name (e.g., "Noise")
    std::string category;       ///< Category (e.g., "Generators", "Effects")
    std::string description;    ///< Brief description
    std::string module;         ///< Module name if not core (e.g., "vivid-audio")
    bool requiresInput;         ///< True if operator needs input connection
    OutputKind outputKind;      ///< Output type (Texture, Audio, etc.)
    std::string headerPath;     ///< Path to header file (for Claude to read)

    /// Factory function to create instance for param introspection
    std::function<std::unique_ptr<Operator>()> factory;
};

/**
 * @brief Global operator registry
 *
 * Singleton that collects operator metadata from static registrations.
 */
class OperatorRegistry {
public:
    /// @brief Get the singleton instance
    static OperatorRegistry& instance();

    /// @brief Register an operator type
    void registerOperator(const OperatorMeta& meta);

    /// @brief Get all registered operators
    const std::vector<OperatorMeta>& operators() const { return m_operators; }

    /// @brief Get operators by category
    std::vector<const OperatorMeta*> operatorsByCategory(const std::string& category) const;

    /// @brief Get all categories
    std::vector<std::string> categories() const;

    /// @brief Find operator by name
    const OperatorMeta* find(const std::string& name) const;

    /// @brief Output all operators as JSON to stdout
    void outputJson() const;

    /// @brief Get all operators as JSON (flat array format)
    /// Returns: {"version": "1.0.0", "operators": [...]}
    nlohmann::json toJson() const;

    /// @brief Get all operators as JSON grouped by category
    /// Returns: {"Generators": [...], "Effects": [...], ...}
    nlohmann::json toJsonGrouped() const;

private:
    OperatorRegistry() = default;
    std::vector<OperatorMeta> m_operators;
};

/**
 * @brief Helper for static registration
 */
struct OperatorRegistrar {
    OperatorRegistrar(const OperatorMeta& meta) {
        OperatorRegistry::instance().registerOperator(meta);
    }
};

/**
 * @brief Macro to register an operator type (texture output)
 *
 * Use in the operator's .cpp file:
 * @code
 * REGISTER_OPERATOR(Noise, "Generators", "Fractal noise generator", false);
 * @endcode
 *
 * Module and headerPath are auto-detected from __FILE__.
 */
#define REGISTER_OPERATOR(Type, Category, Description, RequiresInput) \
    static ::vivid::OperatorRegistrar s_reg_##Type([]() { \
        ::vivid::OperatorMeta meta; \
        meta.name = #Type; \
        meta.category = Category; \
        meta.description = Description; \
        meta.module = ::vivid::deduceModuleFromPath(__FILE__); \
        meta.requiresInput = RequiresInput; \
        meta.outputKind = ::vivid::OutputKind::Texture; \
        meta.headerPath = ::vivid::computeHeaderPath(meta.module, #Type); \
        meta.factory = []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); }; \
        return meta; \
    }()); \
    static_assert(true, "")

/**
 * @brief Macro to register an operator with custom output kind
 *
 * Use for non-texture output (Audio, Value, etc.):
 * @code
 * REGISTER_OPERATOR_EX(Oscillator, "Audio Synthesis", "Audio oscillator", false, OutputKind::Audio);
 * @endcode
 */
#define REGISTER_OPERATOR_EX(Type, Category, Description, RequiresInput, OutKind) \
    static ::vivid::OperatorRegistrar s_reg_##Type([]() { \
        ::vivid::OperatorMeta meta; \
        meta.name = #Type; \
        meta.category = Category; \
        meta.description = Description; \
        meta.module = ::vivid::deduceModuleFromPath(__FILE__); \
        meta.requiresInput = RequiresInput; \
        meta.outputKind = OutKind; \
        meta.headerPath = ::vivid::computeHeaderPath(meta.module, #Type); \
        meta.factory = []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); }; \
        return meta; \
    }()); \
    static_assert(true, "")

} // namespace vivid
