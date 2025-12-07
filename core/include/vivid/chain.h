#pragma once

// Vivid - Chain API
// Manages operator graph with dependency resolution and state preservation

#include <vivid/operator.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <stdexcept>

namespace vivid {

class Context;

class Chain {
public:
    Chain() = default;
    ~Chain() = default;

    // Non-copyable but movable
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;
    Chain(Chain&&) = default;
    Chain& operator=(Chain&&) = default;

    // Add an operator with a name
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

    // Get an operator by name with type
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

    // Get operator by name (untyped)
    Operator* getByName(const std::string& name);

    // Get name of operator
    std::string getName(Operator* op) const;

    // Set which operator provides the output
    void setOutput(const std::string& name) { outputName_ = name; }
    void setOutput(Operator* op);

    // Initialize all operators (call once at setup)
    void init(Context& ctx);

    // Process all operators in dependency order
    void process(Context& ctx);

    // State preservation for hot-reload
    std::map<std::string, std::unique_ptr<OperatorState>> saveAllStates();
    void restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states);

    // Check for errors
    bool hasError() const { return !error_.empty(); }
    const std::string& error() const { return error_; }
    void clearError() { error_.clear(); }

    // Get all operator names
    const std::vector<std::string>& operatorNames() const { return orderedNames_; }

private:
    // Compute execution order using topological sort (Kahn's algorithm)
    void computeExecutionOrder();

    // Build dependency graph from operator inputs
    void buildDependencyGraph();

    // Check for circular dependencies
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
