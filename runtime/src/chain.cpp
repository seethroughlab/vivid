// Vivid Chain Implementation

#include "vivid/chain.h"
#include "vivid/operator.h"
#include "vivid/context.h"

#include <algorithm>
#include <stdexcept>

namespace vivid {

Chain::Chain() = default;
Chain::~Chain() = default;

void Chain::setResolution(int width, int height) {
    width_ = width;
    height_ = height;
}

void Chain::addOperator(const std::string& name, std::unique_ptr<Operator> op) {
    if (operators_.count(name)) {
        throw std::runtime_error("Operator with name '" + name + "' already exists");
    }
    operators_[name] = std::move(op);

    // If no output set yet, use this one
    if (outputName_.empty()) {
        outputName_ = name;
    }
}

Operator* Chain::getOperator(const std::string& name) {
    auto it = operators_.find(name);
    if (it == operators_.end()) {
        throw std::runtime_error("Operator '" + name + "' not found");
    }
    return it->second.get();
}

bool Chain::has(const std::string& name) const {
    return operators_.count(name) > 0;
}

void Chain::connect(const std::string& from, const std::string& to) {
    connections_.emplace_back(from, to);
}

void Chain::setOutput(const std::string& name) {
    if (!has(name)) {
        throw std::runtime_error("Cannot set output: operator '" + name + "' not found");
    }
    outputName_ = name;
}

void Chain::computeExecutionOrder() {
    // Simple topological sort
    // For now, just use insertion order
    executionOrder_.clear();
    for (auto& [name, op] : operators_) {
        executionOrder_.push_back(op.get());
    }

    // TODO: Proper topological sort based on connections
}

void Chain::init(Context& ctx) {
    computeExecutionOrder();
    for (auto* op : executionOrder_) {
        op->init(ctx);
    }
}

void Chain::process(Context& ctx) {
    for (auto* op : executionOrder_) {
        op->process(ctx);
    }
}

void Chain::cleanup() {
    for (auto* op : executionOrder_) {
        op->cleanup();
    }
}

} // namespace vivid
