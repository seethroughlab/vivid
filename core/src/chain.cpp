// Vivid - Chain Implementation

#include <vivid/chain.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <queue>
#include <unordered_set>
#include <algorithm>

namespace vivid {

Operator* Chain::getByName(const std::string& name) {
    auto it = operators_.find(name);
    if (it == operators_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::string Chain::getName(Operator* op) const {
    auto it = operatorNames_.find(op);
    if (it == operatorNames_.end()) {
        return "";
    }
    return it->second;
}

void Chain::setOutput(Operator* op) {
    std::string name = getName(op);
    if (!name.empty()) {
        outputName_ = name;
    }
}

void Chain::buildDependencyGraph() {
    // For each operator, find which other operators it depends on
    // by looking at its inputs
    for (const auto& [name, op] : operators_) {
        for (size_t i = 0; i < op->inputCount(); ++i) {
            Operator* input = op->getInput(i);
            if (input) {
                // This operator depends on the input operator
                // We need this for topological sort
            }
        }
    }
}

bool Chain::detectCycle() {
    // Use DFS with three-color marking to detect cycles
    enum class Color { White, Gray, Black };
    std::unordered_map<Operator*, Color> colors;

    for (const auto& [name, op] : operators_) {
        colors[op.get()] = Color::White;
    }

    std::function<bool(Operator*)> hasCycle = [&](Operator* node) -> bool {
        colors[node] = Color::Gray;

        for (size_t i = 0; i < node->inputCount(); ++i) {
            Operator* input = node->getInput(i);
            if (input && colors.count(input)) {
                if (colors[input] == Color::Gray) {
                    // Found a back edge - cycle detected
                    return true;
                }
                if (colors[input] == Color::White && hasCycle(input)) {
                    return true;
                }
            }
        }

        colors[node] = Color::Black;
        return false;
    };

    for (const auto& [name, op] : operators_) {
        if (colors[op.get()] == Color::White) {
            if (hasCycle(op.get())) {
                return true;
            }
        }
    }

    return false;
}

void Chain::computeExecutionOrder() {
    if (!needsSort_) {
        return;
    }

    executionOrder_.clear();
    error_.clear();

    // Check for cycles first
    if (detectCycle()) {
        error_ = "Circular dependency detected in operator chain";
        return;
    }

    // Kahn's algorithm for topological sort
    // Count incoming edges (dependencies) for each operator
    std::unordered_map<Operator*, int> inDegree;
    std::unordered_map<Operator*, std::vector<Operator*>> dependents;

    for (const auto& [name, op] : operators_) {
        inDegree[op.get()] = 0;
    }

    // Build reverse dependency graph and count in-degrees
    for (const auto& [name, op] : operators_) {
        for (size_t i = 0; i < op->inputCount(); ++i) {
            Operator* input = op->getInput(i);
            if (input && operators_.count(getName(input))) {
                inDegree[op.get()]++;
                dependents[input].push_back(op.get());
            }
        }
    }

    // Start with operators that have no dependencies
    std::queue<Operator*> ready;
    for (const auto& [op, degree] : inDegree) {
        if (degree == 0) {
            ready.push(op);
        }
    }

    // Process operators in dependency order
    while (!ready.empty()) {
        Operator* current = ready.front();
        ready.pop();
        executionOrder_.push_back(current);

        // Reduce in-degree for all dependents
        for (Operator* dependent : dependents[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }

    // Check if all operators were processed
    if (executionOrder_.size() != operators_.size()) {
        error_ = "Could not resolve operator dependencies (possible cycle)";
        return;
    }

    needsSort_ = false;
}

void Chain::init(Context& ctx) {
    computeExecutionOrder();

    if (hasError()) {
        ctx.setError(error_);
        return;
    }

    // Auto-register all operators for visualization
    for (Operator* op : executionOrder_) {
        std::string name = getName(op);
        if (!name.empty()) {
            ctx.registerOperator(name, op);
        }
    }

    for (Operator* op : executionOrder_) {
        op->init(ctx);
    }

    initialized_ = true;
}

void Chain::process(Context& ctx) {
    if (!initialized_) {
        init(ctx);
    }

    if (hasError()) {
        ctx.setError(error_);
        return;
    }

    // Process all operators in dependency order
    for (Operator* op : executionOrder_) {
        op->process(ctx);
    }

    // Register output texture if specified
    if (!outputName_.empty()) {
        Operator* output = getByName(outputName_);
        if (output && output->outputKind() == OutputKind::Texture) {
            // Get the output texture view - this requires a TextureOperator
            // We'll need to use dynamic_cast or a virtual method
            // For now, let the Output operator handle this
        }
    }
}

std::map<std::string, std::unique_ptr<OperatorState>> Chain::saveAllStates() {
    std::map<std::string, std::unique_ptr<OperatorState>> states;

    for (const auto& [name, op] : operators_) {
        auto state = op->saveState();
        if (state) {
            states[name] = std::move(state);
        }
    }

    return states;
}

void Chain::restoreAllStates(std::map<std::string, std::unique_ptr<OperatorState>>& states) {
    for (auto& [name, state] : states) {
        auto it = operators_.find(name);
        if (it != operators_.end() && state) {
            it->second->loadState(std::move(state));
        }
    }
}

} // namespace vivid
