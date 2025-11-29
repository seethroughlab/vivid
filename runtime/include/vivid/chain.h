#pragma once
#include "operator.h"
#include "context.h"
#include <memory>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <stdexcept>

namespace vivid {

/**
 * @brief Registry for operator types, enabling creation by name.
 *
 * Operators register themselves using VIVID_REGISTER_OPERATOR macro.
 * The Chain uses this to instantiate operators via add<T>().
 */
class OperatorRegistry {
public:
    using FactoryFunc = std::function<std::unique_ptr<Operator>()>;

    static OperatorRegistry& instance() {
        static OperatorRegistry registry;
        return registry;
    }

    /// Register an operator type with a factory function
    void registerOperator(const std::string& typeName, FactoryFunc factory) {
        factories_[typeName] = std::move(factory);
    }

    /// Create an operator by type name
    std::unique_ptr<Operator> create(const std::string& typeName) {
        auto it = factories_.find(typeName);
        if (it != factories_.end()) {
            return it->second();
        }
        return nullptr;
    }

    /// Check if an operator type is registered
    bool isRegistered(const std::string& typeName) const {
        return factories_.find(typeName) != factories_.end();
    }

    /// Get list of registered operator types
    std::vector<std::string> registeredTypes() const {
        std::vector<std::string> types;
        types.reserve(factories_.size());
        for (const auto& [name, _] : factories_) {
            types.push_back(name);
        }
        return types;
    }

private:
    OperatorRegistry() = default;
    std::unordered_map<std::string, FactoryFunc> factories_;
};

/**
 * @brief Helper for auto-registration at static initialization time.
 */
template<typename T>
struct OperatorRegistrar {
    explicit OperatorRegistrar(const char* name) {
        OperatorRegistry::instance().registerOperator(name, []() {
            return std::make_unique<T>();
        });
    }
};

/**
 * @brief Declarative operator chain for composing Vivid pipelines.
 *
 * The Chain class enables a clean, declarative API for building operator graphs:
 *
 * @code
 * void setup(Chain& chain) {
 *     chain.add<Noise>("noise").scale(4.0).speed(0.3);
 *     chain.add<Feedback>("fb").input("noise").decay(0.9);
 *     chain.add<Mirror>("mirror").input("fb").kaleidoscope(6);
 *     chain.setOutput("mirror");
 * }
 *
 * void update(Chain& chain, Context& ctx) {
 *     chain.get<Feedback>("fb").rotate(ctx.mouseNormX() * 0.1);
 * }
 * @endcode
 *
 * The Chain handles:
 * - Operator instantiation and lifecycle
 * - Dependency resolution from input() calls
 * - Execution in correct topological order
 * - State preservation across hot-reload
 */

/// Represents a connection between two operators
struct Connection {
    std::string from;       ///< Source operator name
    std::string to;         ///< Destination operator name
    std::string fromOutput; ///< Source output port (default "out")
    std::string toInput;    ///< Destination input port (default "in")
};

class Chain {
public:
    Chain() = default;
    ~Chain() = default;

    // Non-copyable, movable
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;
    Chain(Chain&&) = default;
    Chain& operator=(Chain&&) = default;

    /**
     * @brief Add an operator to the chain.
     * @tparam T The operator type (must derive from Operator).
     * @param name Unique name for this operator instance.
     * @return Reference to the created operator for fluent configuration.
     *
     * @code
     * chain.add<Noise>("noise").scale(4.0).speed(0.3);
     * @endcode
     */
    template<typename T>
    T& add(const std::string& name) {
        static_assert(std::is_base_of<Operator, T>::value,
                      "T must derive from Operator");

        auto op = std::make_unique<T>();
        op->setId(name);
        T* ptr = op.get();
        operators_[name] = std::move(op);
        executionOrder_.push_back(name);

        return *ptr;
    }

    /**
     * @brief Get an operator by name for parameter updates.
     * @tparam T The operator type.
     * @param name The operator's name.
     * @return Reference to the operator.
     * @throws std::runtime_error if operator not found or wrong type.
     */
    template<typename T>
    T& get(const std::string& name) {
        auto it = operators_.find(name);
        if (it == operators_.end()) {
            throw std::runtime_error("Operator not found: " + name);
        }
        T* ptr = dynamic_cast<T*>(it->second.get());
        if (!ptr) {
            throw std::runtime_error("Operator type mismatch: " + name);
        }
        return *ptr;
    }

    /**
     * @brief Get an operator by name (const version).
     */
    template<typename T>
    const T& get(const std::string& name) const {
        auto it = operators_.find(name);
        if (it == operators_.end()) {
            throw std::runtime_error("Operator not found: " + name);
        }
        const T* ptr = dynamic_cast<const T*>(it->second.get());
        if (!ptr) {
            throw std::runtime_error("Operator type mismatch: " + name);
        }
        return *ptr;
    }

    /**
     * @brief Check if an operator exists.
     * @param name The operator's name.
     * @return true if the operator exists.
     */
    bool has(const std::string& name) const {
        return operators_.find(name) != operators_.end();
    }

    /**
     * @brief Get operator base class by name (for generic access).
     * @param name The operator's name.
     * @return Pointer to operator, or nullptr if not found.
     */
    Operator* getOperator(const std::string& name) {
        auto it = operators_.find(name);
        return (it != operators_.end()) ? it->second.get() : nullptr;
    }

    /**
     * @brief Set which operator's output is the final chain output.
     * @param name The name of the output operator.
     */
    void setOutput(const std::string& name) {
        outputNode_ = name;
    }

    /**
     * @brief Get the name of the output operator.
     */
    const std::string& outputNode() const {
        return outputNode_;
    }

    /**
     * @brief Connect one operator's output to another's input.
     * @param from Source operator name.
     * @param to Destination operator name.
     * @param fromOutput Source output name (default "out").
     * @param toInput Destination input name (default "in").
     *
     * Alternative to using .input() on operators. Useful for:
     * - External graph definition (e.g., from JSON)
     * - Multiple inputs to one operator
     * - Named input/output ports
     *
     * @code
     * chain.add<Noise>("noise");
     * chain.add<Blur>("blur");
     * chain.connect("noise", "blur");  // noise.out -> blur.in
     * @endcode
     */
    void connect(const std::string& from, const std::string& to,
                 const std::string& fromOutput = "out",
                 const std::string& toInput = "in") {
        connections_.push_back({from, to, fromOutput, toInput});
        // Track dependencies for topological sort
        dependencies_[to].push_back(from);
    }

    /**
     * @brief Get all connections in the chain.
     */
    const std::vector<Connection>& connections() const {
        return connections_;
    }

    /**
     * @brief Initialize all operators in the chain.
     * @param ctx The context for initialization.
     *
     * Called once after setup() to initialize all operators.
     * Operators are initialized in the order they were added.
     */
    void init(Context& ctx) {
        ctx_ = &ctx;
        for (const auto& name : executionOrder_) {
            auto it = operators_.find(name);
            if (it != operators_.end()) {
                ctx.setCurrentNode(name);
                it->second->init(ctx);
            }
        }
        ctx.clearCurrentNode();
        initialized_ = true;
    }

    /**
     * @brief Process all operators in the chain.
     * @param ctx The context for processing.
     *
     * Called each frame to process all operators in dependency order.
     */
    void process(Context& ctx) {
        ctx_ = &ctx;
        for (const auto& name : executionOrder_) {
            auto it = operators_.find(name);
            if (it != operators_.end()) {
                // Set current node so outputs are prefixed correctly
                ctx.setCurrentNode(name);
                it->second->process(ctx);
            }
        }
        ctx.clearCurrentNode();
    }

    /**
     * @brief Clean up all operators in the chain.
     *
     * Called before the chain is destroyed.
     */
    void cleanup() {
        // Cleanup in reverse order
        for (auto it = executionOrder_.rbegin(); it != executionOrder_.rend(); ++it) {
            auto opIt = operators_.find(*it);
            if (opIt != operators_.end()) {
                opIt->second->cleanup();
            }
        }
        operators_.clear();
        executionOrder_.clear();
        initialized_ = false;
    }

    /**
     * @brief Get the final output texture from the chain.
     * @param ctx The context to retrieve from.
     * @return Pointer to output texture, or nullptr if not available.
     */
    Texture* getOutput(Context& ctx) {
        if (outputNode_.empty()) {
            // Default to last operator
            if (!executionOrder_.empty()) {
                return ctx.getInputTexture(executionOrder_.back());
            }
            return nullptr;
        }
        return ctx.getInputTexture(outputNode_);
    }

    /**
     * @brief Save state from all operators for hot-reload.
     * @return Map of operator name to saved state.
     */
    std::map<std::string, std::unique_ptr<OperatorState>> saveAllStates() {
        std::map<std::string, std::unique_ptr<OperatorState>> states;
        for (auto& [name, op] : operators_) {
            auto state = op->saveState();
            if (state) {
                states[name] = std::move(state);
            }
        }
        return states;
    }

    /**
     * @brief Restore state to operators after hot-reload.
     * @param states Map of operator name to saved state.
     */
    void restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states) {
        for (auto& [name, state] : states) {
            auto it = operators_.find(name);
            if (it != operators_.end() && state) {
                it->second->loadState(std::move(state));
            }
        }
    }

    /**
     * @brief Get all operator names in execution order.
     */
    const std::vector<std::string>& operatorNames() const {
        return executionOrder_;
    }

    /**
     * @brief Get number of operators in the chain.
     */
    size_t size() const {
        return operators_.size();
    }

    /**
     * @brief Check if chain has been initialized.
     */
    bool isInitialized() const {
        return initialized_;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<Operator>> operators_;
    std::vector<std::string> executionOrder_;
    std::string outputNode_;
    Context* ctx_ = nullptr;
    bool initialized_ = false;
};

} // namespace vivid

// Macro to register an operator type with the registry
#define VIVID_REGISTER_OPERATOR(ClassName) \
    static vivid::OperatorRegistrar<ClassName> vivid_registrar_##ClassName(#ClassName);
