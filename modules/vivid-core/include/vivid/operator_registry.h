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

// Forward declare nlohmann::json to avoid including heavy header
#include <nlohmann/json_fwd.hpp>

namespace vivid {

/**
 * @brief Deduce module name from file path
 *
 * Extracts module name from paths like "modules/vivid-audio/include/..."
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
 * @brief Describes an input method for multi-input operators
 */
struct InputMeta {
    std::string method;       ///< Method name (e.g., "source", "map", "inputA")
    std::string description;  ///< What this input is for
    bool required = true;     ///< Whether this input must be connected

    InputMeta() = default;
    InputMeta(std::string m, std::string d, bool r = true)
        : method(std::move(m)), description(std::move(d)), required(r) {}
};

/**
 * @brief Describes an example that demonstrates an operator
 */
struct ExampleMeta {
    std::string path;         ///< Example path (e.g., "modules/vivid-audio/examples/envelope-modulation")
    std::string description;  ///< Optional description of what the example shows

    ExampleMeta() = default;
    ExampleMeta(std::string p) : path(std::move(p)) {}
    ExampleMeta(std::string p, std::string desc)
        : path(std::move(p)), description(std::move(desc)) {}
};

/**
 * @brief Self-describing operator descriptor with fluent builder API (C++17)
 *
 * Used by operators that implement a static describe() method:
 * @code
 * class Displace : public TextureOperator {
 * public:
 *     static OperatorDescriptor describe() {
 *         return OperatorDescriptor("Displace", "Effects", "Texture displacement")
 *             .requireInput()
 *             .withInputs({{"source", "Texture to distort"},
 *                          {"map", "Displacement map (R=X, G=Y)"}})
 *             .withUsage("auto& d = chain.add<Displace>(\"d\");\n"
 *                        "d.source(\"input\");\n"
 *                        "d.map(\"noise\");");
 *     }
 * };
 * @endcode
 */
struct OperatorDescriptor {
    std::string name;
    std::string category;
    std::string description;
    OutputKind outputKind = OutputKind::Texture;
    bool requiresInput = false;
    std::string module;
    std::vector<std::string> aliases;
    std::vector<InputMeta> inputs;
    std::string usage;
    std::vector<ExampleMeta> examples;  ///< Example projects demonstrating this operator

    /// Constructor with required fields
    OperatorDescriptor(const char* n, const char* cat, const char* desc)
        : name(n), category(cat), description(desc) {}

    /// Fluent builder methods
    OperatorDescriptor& output(OutputKind k) { outputKind = k; return *this; }
    OperatorDescriptor& requireInput(bool r = true) { requiresInput = r; return *this; }
    OperatorDescriptor& inModule(const char* m) { module = m; return *this; }
    OperatorDescriptor& withAliases(std::vector<std::string> a) { aliases = std::move(a); return *this; }
    OperatorDescriptor& withInputs(std::vector<InputMeta> i) { inputs = std::move(i); return *this; }
    OperatorDescriptor& withUsage(std::string u) { usage = std::move(u); return *this; }
    OperatorDescriptor& withExamples(std::vector<ExampleMeta> e) { examples = std::move(e); return *this; }

    /// Auto-detect module from file path (called by REGISTER macro with __FILE__)
    /// Only sets module if not already set (explicit .inModule() takes precedence)
    OperatorDescriptor& autoModule(const char* file) {
        if (module.empty()) {
            module = deduceModuleFromPath(file);
        }
        return *this;
    }
};

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

    // Factory function to create instance for param introspection
    std::function<std::unique_ptr<Operator>()> factory;

    // Extended metadata for LLM/MCP integration
    std::string usage;                    ///< Explicit usage example (overrides auto-generated)
    std::vector<std::string> aliases;     ///< Alternative names (e.g., "Grain" for FilmGrain)
    std::vector<InputMeta> inputs;        ///< Input method documentation for multi-input operators
    std::vector<ExampleMeta> examples;    ///< Example projects demonstrating this operator
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

    /// @brief Find operator by name (also checks aliases)
    const OperatorMeta* find(const std::string& name) const;

    /// @brief Set extended metadata for an operator (usage example)
    void setUsage(const std::string& name, const std::string& usage);

    /// @brief Set aliases for an operator (alternative names for discovery)
    void setAliases(const std::string& name, std::vector<std::string> aliases);

    /// @brief Set input method documentation for multi-input operators
    void setInputs(const std::string& name, std::vector<InputMeta> inputs);

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
    OperatorMeta* findMutable(const std::string& name);
    std::vector<OperatorMeta> m_operators;
};

/**
 * @brief Helper for static registration
 */
struct OperatorRegistrar {
    /// Legacy constructor for REGISTER_OPERATOR macros
    OperatorRegistrar(const OperatorMeta& meta) {
        OperatorRegistry::instance().registerOperator(meta);
    }

    /// New constructor for self-describing operators (REGISTER macro)
    OperatorRegistrar(const OperatorDescriptor& info,
                      std::function<std::unique_ptr<Operator>()> factory) {
        OperatorMeta meta;
        meta.name = info.name;
        meta.category = info.category;
        meta.description = info.description;
        meta.module = info.module;
        meta.requiresInput = info.requiresInput;
        meta.outputKind = info.outputKind;
        meta.factory = std::move(factory);
        meta.usage = info.usage;
        meta.aliases = info.aliases;
        meta.inputs = info.inputs;
        meta.examples = info.examples;
        OperatorRegistry::instance().registerOperator(meta);
    }
};

/**
 * @brief Macro to register an operator type
 *
 * Use in the operator's .cpp file after the class definition:
 * @code
 * REGISTER_OPERATOR(Noise, "Generators", "Fractal noise generator", false);
 * @endcode
 *
 * @param Type The operator class name
 * @param Category Category string (e.g., "Generators", "Effects", "Audio Synthesis")
 * @param Description Brief description of what the operator does
 * @param RequiresInput True if operator needs .input() connection
 */
#define REGISTER_OPERATOR(Type, Category, Description, RequiresInput) \
    static ::vivid::OperatorRegistrar s_reg_##Type({ \
        #Type, \
        Category, \
        Description, \
        "", \
        RequiresInput, \
        ::vivid::OutputKind::Texture, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }); \
    static_assert(true, "")

/**
 * @brief Macro to register an operator with custom output kind
 */
#define REGISTER_OPERATOR_EX(Type, Category, Description, RequiresInput, OutKind) \
    static ::vivid::OperatorRegistrar s_reg_##Type({ \
        #Type, \
        Category, \
        Description, \
        "", \
        RequiresInput, \
        OutKind, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }); \
    static_assert(true, "")

/**
 * @brief Register an operator from a specific module (texture output)
 *
 * Use in module operator .cpp files to indicate which module provides the operator:
 * @code
 * REGISTER_MODULE_OPERATOR(Webcam, "Video Input", "Live webcam input", false, "vivid-video");
 * @endcode
 */
#define REGISTER_MODULE_OPERATOR(Type, Category, Description, RequiresInput, ModuleName) \
    static ::vivid::OperatorRegistrar s_reg_##Type({ \
        #Type, \
        Category, \
        Description, \
        ModuleName, \
        RequiresInput, \
        ::vivid::OutputKind::Texture, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }); \
    static_assert(true, "")

/**
 * @brief Register a module operator with custom output kind
 *
 * Use for operators that produce non-Texture output (Audio, Value, etc.):
 * @code
 * REGISTER_MODULE_OPERATOR_EX(Oscillator, "Audio Synthesis", "Basic waveform oscillator",
 *                             false, OutputKind::Audio, "vivid-audio");
 * @endcode
 */
#define REGISTER_MODULE_OPERATOR_EX(Type, Category, Description, RequiresInput, OutKind, ModuleName) \
    static ::vivid::OperatorRegistrar s_reg_##Type({ \
        #Type, \
        Category, \
        Description, \
        ModuleName, \
        RequiresInput, \
        OutKind, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }); \
    static_assert(true, "")

/**
 * @brief Register a self-describing operator
 *
 * Use with operators that implement a static describe() method:
 * @code
 * // In displace.h
 * class Displace : public TextureOperator {
 * public:
 *     static OperatorDescriptor describe() {
 *         return OperatorDescriptor("Displace", "Effects", "Texture displacement")
 *             .requireInput()
 *             .withInputs({{"source", "Texture to distort"},
 *                          {"map", "Displacement map"}});
 *     }
 *     // ... rest of class
 * };
 *
 * // In operator_registrations.cpp (or displace.cpp)
 * REGISTER(Displace);
 * @endcode
 */
#define REGISTER(Type) \
    static ::vivid::OperatorRegistrar s_reg_##Type{ \
        Type::describe().autoModule(__FILE__), \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }; \
    static_assert(true, "")

} // namespace vivid
