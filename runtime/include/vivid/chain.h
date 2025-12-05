#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <typeindex>
#include <functional>
#include "export.h"

namespace vivid {

class Operator;
class Context;

/// Chain manages operator instances and their connections
class VIVID_API Chain {
public:
    Chain();
    ~Chain();

    // Non-copyable
    Chain(const Chain&) = delete;
    Chain& operator=(const Chain&) = delete;

    /// Set the output resolution
    void setResolution(int width, int height);

    /// Get the output resolution
    int width() const { return width_; }
    int height() const { return height_; }

    /// Add an operator to the chain
    template<typename T>
    T& add(const std::string& name) {
        auto op = std::make_unique<T>();
        op->instanceName = name;
        T& ref = *op;
        addOperator(name, std::move(op));
        return ref;
    }

    /// Get an operator by name
    template<typename T>
    T& get(const std::string& name) {
        return static_cast<T&>(*getOperator(name));
    }

    /// Check if an operator exists
    bool has(const std::string& name) const;

    /// Connect output of one operator to input of another
    void connect(const std::string& from, const std::string& to);

    /// Set which operator provides the final output
    void setOutput(const std::string& name);

    /// Get the output operator name
    const std::string& outputName() const { return outputName_; }

    /// Compute execution order based on dependencies
    void computeExecutionOrder();

    /// Initialize all operators
    void init(Context& ctx);

    /// Process all operators in dependency order
    void process(Context& ctx);

    /// Cleanup all operators
    void cleanup();

    /// Get all operators in execution order
    const std::vector<Operator*>& operators() const { return executionOrder_; }

private:
    void addOperator(const std::string& name, std::unique_ptr<Operator> op);
    Operator* getOperator(const std::string& name);

    int width_ = 1920;
    int height_ = 1080;

    std::unordered_map<std::string, std::unique_ptr<Operator>> operators_;
    std::vector<std::pair<std::string, std::string>> connections_;
    std::vector<Operator*> executionOrder_;
    std::string outputName_;
};

} // namespace vivid
