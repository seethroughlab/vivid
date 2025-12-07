#pragma once

/**
 * @file chain.h
 * @brief Chain API for managing operator graphs
 *
 * Chain manages a collection of operators with automatic dependency resolution
 * and state preservation across hot-reloads.
 */

#include <vivid/operator.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <stdexcept>

namespace vivid {

class Context;

/**
 * @brief Manages an operator graph with dependency resolution
 *
 * Chain is the primary way to build vivid projects. Add operators with add<T>(),
 * connect them with input(), and call process() each frame.
 *
 * @par Example
 * @code
 * Chain* chain = nullptr;
 *
 * void setup(Context& ctx) {
 *     delete chain;
 *     chain = new Chain(ctx, 1280, 720);
 *
 *     chain->add<Noise>("noise").scale(4.0f);
 *     chain->add<HSV>("color").input("noise").hueShift(0.3f);
 *     chain->add<Output>("out").input("color");
 * }
 *
 * void update(Context& ctx) {
 *     chain->process();
 * }
 * @endcode
 */
class Chain {
public:
    Chain() = default;
    ~Chain() = default;

    // Non-copyable but movable
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;
    Chain(Chain&&) = default;
    Chain& operator=(Chain&&) = default;

    /**
     * @brief Add an operator to the chain
     * @tparam T Operator type (e.g., Noise, Blur, Output)
     * @tparam Args Constructor argument types
     * @param name Unique name for this operator
     * @param args Constructor arguments forwarded to T
     * @return Reference to the new operator for method chaining
     *
     * @par Example
     * @code
     * chain->add<Noise>("noise").scale(4.0f).speed(0.5f);
     * chain->add<Blur>("blur").input("noise").radius(5.0f);
     * @endcode
     */
    template<typename T, typename... Args>
    T& add(const std::string& name, Args&&... args) {
        auto op = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *op;
        operators_[name] = std::move(op);
        orderedNames_.push_back(name);
        operatorNames_[&ref] = name;
        needsSort_ = true;
        return ref;
    }

    /**
     * @brief Get an operator by name with type checking
     * @tparam T Expected operator type
     * @param name Operator name
     * @return Reference to the operator
     * @throw std::runtime_error if not found or type mismatch
     *
     * @par Example
     * @code
     * chain->get<Noise>("noise").scale(8.0f);  // Modify existing operator
     * @endcode
     */
    template<typename T>
    T& get(const std::string& name) {
        auto it = operators_.find(name);
        if (it == operators_.end()) {
            throw std::runtime_error("Operator not found: " + name);
        }
        T* typed = dynamic_cast<T*>(it->second.get());
        if (!typed) {
            throw std::runtime_error("Operator type mismatch: " + name);
        }
        return *typed;
    }

    /**
     * @brief Get operator by name (untyped)
     * @param name Operator name
     * @return Pointer to operator, or nullptr if not found
     */
    Operator* getByName(const std::string& name);

    /**
     * @brief Get name of an operator
     * @param op Pointer to operator
     * @return Operator name, or empty string if not found
     */
    std::string getName(Operator* op) const;

    /**
     * @brief Specify which operator provides the final output
     * @param name Name of the output operator
     *
     * This is the recommended way to specify output (instead of adding an Output operator).
     *
     * @par Example
     * @code
     * chain.add<Noise>("noise").scale(4.0f);
     * chain.add<HSV>("color").input("noise");
     * chain.output("color");  // Display the color operator
     * @endcode
     */
    void output(const std::string& name) { outputName_ = name; }

    /**
     * @brief Get the designated output operator
     * @return Pointer to output operator, or nullptr if not set
     */
    Operator* getOutput() const;

    /// @brief Legacy method - prefer output() instead
    void setOutput(const std::string& name) { outputName_ = name; }

    /// @brief Legacy method - prefer output() instead
    void setOutput(Operator* op);

    /**
     * @brief Initialize all operators
     * @param ctx Runtime context
     *
     * Called automatically on first process(). Can be called explicitly
     * if you need operators initialized before the first frame.
     */
    void init(Context& ctx);

    /**
     * @brief Process all operators in dependency order
     * @param ctx Runtime context
     *
     * Automatically initializes operators on first call, computes
     * execution order, and processes each operator.
     */
    void process(Context& ctx);

    // -------------------------------------------------------------------------
    /// @name State Preservation
    /// @{

    /**
     * @brief Save states from all operators
     * @return Map of operator names to state objects
     */
    std::map<std::string, std::unique_ptr<OperatorState>> saveAllStates();

    /**
     * @brief Restore states to matching operators
     * @param states Map of operator names to state objects
     */
    void restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Error Handling
    /// @{

    /// @brief Check if an error has occurred
    bool hasError() const { return !error_.empty(); }

    /// @brief Get the error message
    const std::string& error() const { return error_; }

    /// @brief Clear the error state
    void clearError() { error_.clear(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Introspection
    /// @{

    /**
     * @brief Get all operator names in add order
     * @return Vector of operator names
     */
    const std::vector<std::string>& operatorNames() const { return orderedNames_; }

    /// @}

private:
    void computeExecutionOrder();
    void buildDependencyGraph();
    bool detectCycle();

    std::unordered_map<std::string, std::unique_ptr<Operator>> operators_;
    std::unordered_map<Operator*, std::string> operatorNames_;
    std::vector<std::string> orderedNames_;
    std::vector<Operator*> executionOrder_;
    std::string outputName_;
    std::string error_;
    bool needsSort_ = true;
    bool initialized_ = false;
};

} // namespace vivid
