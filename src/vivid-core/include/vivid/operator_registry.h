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

    // Extended metadata (optional, for MCP/documentation)
    std::vector<std::string> limitations;  ///< Known limitations or caveats
    std::vector<std::string> related;      ///< Related operator names
    std::vector<std::string> examples;     ///< Example project paths
    std::vector<std::string> api;          ///< Method signatures for LLM discovery
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
 * @brief Builder for operator metadata with fluent API
 *
 * Allows adding extended metadata (limitations, related, examples) using
 * a fluent interface. Registration happens in destructor.
 *
 * Methods are rvalue-qualified and return by value, enabling chains like:
 *   REGISTER_OPERATOR_FULL(...).limitations({...}).related({...});
 */
class OperatorMetaBuilder {
public:
    OperatorMetaBuilder(OperatorMeta meta) : m_meta(std::move(meta)) {}

    // Move constructor (required for return-by-value from methods)
    OperatorMetaBuilder(OperatorMetaBuilder&& other) noexcept
        : m_meta(std::move(other.m_meta)), m_registered(other.m_registered) {
        other.m_registered = true; // Prevent moved-from object from registering
    }

    // Delete copy operations
    OperatorMetaBuilder(const OperatorMetaBuilder&) = delete;
    OperatorMetaBuilder& operator=(const OperatorMetaBuilder&) = delete;
    OperatorMetaBuilder& operator=(OperatorMetaBuilder&&) = delete;

    // Rvalue-qualified methods return by value for proper chaining on temporaries
    OperatorMetaBuilder limitations(std::initializer_list<std::string> items) && {
        m_meta.limitations = items;
        return std::move(*this);
    }

    OperatorMetaBuilder related(std::initializer_list<std::string> items) && {
        m_meta.related = items;
        return std::move(*this);
    }

    OperatorMetaBuilder examples(std::initializer_list<std::string> items) && {
        m_meta.examples = items;
        return std::move(*this);
    }

    OperatorMetaBuilder api(std::initializer_list<std::string> items) && {
        m_meta.api = items;
        return std::move(*this);
    }

    // Explicit registration
    void reg() {
        if (!m_registered) {
            OperatorRegistry::instance().registerOperator(std::move(m_meta));
            m_registered = true;
        }
    }

    // Conversion to int triggers registration - allows static init to work
    // Usage: static int s_reg = OperatorMetaBuilder(...).method1().method2();
    operator int() && {
        reg();
        return 0;
    }

    ~OperatorMetaBuilder() {
        reg();
    }

private:
    OperatorMeta m_meta;
    bool m_registered = false;
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
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); }, \
        {}, {}, {}, {} \
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
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); }, \
        {}, {}, {}, {} \
    }); \
    static_assert(true, "")


/**
 * @brief Macro to register an operator with extended metadata
 *
 * Returns a builder that allows chaining .limitations(), .related(), .examples()
 * Usage: REGISTER_OPERATOR_FULL(Noise, "Generators", "Noise", false)
 *            .related({"Gradient"}).examples({"projects/noise/"});
 */
#define REGISTER_OPERATOR_FULL(Type, Category, Description, RequiresInput) \
    static int s_reg_##Type = ::vivid::OperatorMetaBuilder(::vivid::OperatorMeta{ \
        #Type, \
        Category, \
        Description, \
        "", \
        RequiresInput, \
        ::vivid::OutputKind::Texture, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); }, \
        {}, {}, {}, {} \
    })

/**
 * @brief Macro to register an operator with extended metadata and custom output kind
 *
 * Returns a builder that allows chaining .limitations(), .related(), .examples()
 * Usage: REGISTER_OPERATOR_FULL_EX(Oscillator, "Audio Synthesis", "Basic oscillator", false, OutputKind::Audio)
 *            .related({"NoiseGen", "FMSynth"}).limitations({"Mono output only"});
 */
#define REGISTER_OPERATOR_FULL_EX(Type, Category, Description, RequiresInput, OutKind) \
    static int s_reg_##Type = ::vivid::OperatorMetaBuilder(::vivid::OperatorMeta{ \
        #Type, \
        Category, \
        Description, \
        "", \
        RequiresInput, \
        OutKind, \
        []() -> std::unique_ptr<::vivid::Operator> { return std::make_unique<Type>(); }, \
        {}, {}, {}, {} \
    })

} // namespace vivid
