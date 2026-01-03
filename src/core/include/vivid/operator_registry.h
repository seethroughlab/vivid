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

namespace vivid {

/**
 * @brief Metadata about an operator type
 */
struct OperatorMeta {
    std::string name;           ///< Operator class name (e.g., "Noise")
    std::string category;       ///< Category (e.g., "Generators", "Effects")
    std::string description;    ///< Brief description
    std::string addon;          ///< Addon name if not core (e.g., "vivid-audio")
    bool requiresInput;         ///< True if operator needs input connection
    OutputKind outputKind;      ///< Output type (Texture, Audio, etc.)

    // Factory function to create instance for param introspection
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
 * @brief Macro to register an addon operator
 */
#define REGISTER_ADDON_OPERATOR(Type, Category, Description, RequiresInput, Addon) \
    static ::vivid::OperatorRegistrar s_reg_##Type({ \
        #Type, \
        Category, \
        Description, \
        Addon, \
        RequiresInput, \
        ::vivid::OutputKind::Texture, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }); \
    static_assert(true, "")

/**
 * @brief Macro to register an addon operator with custom output kind
 */
#define REGISTER_ADDON_OPERATOR_EX(Type, Category, Description, RequiresInput, Addon, OutKind) \
    static ::vivid::OperatorRegistrar s_reg_##Type({ \
        #Type, \
        Category, \
        Description, \
        Addon, \
        RequiresInput, \
        OutKind, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); } \
    }); \
    static_assert(true, "")

} // namespace vivid
