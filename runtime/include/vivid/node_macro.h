#pragma once
#include "operator.h"
#include <memory>
#include <vector>

namespace vivid {

// Global registry for nodes (populated by NODE macro)
class NodeRegistry {
public:
    struct NodeEntry {
        std::string id;
        int sourceLine;
        std::unique_ptr<Operator> op;
    };

    static NodeRegistry& instance() {
        static NodeRegistry registry;
        return registry;
    }

    void registerNode(const std::string& id, int line, std::unique_ptr<Operator> op) {
        NodeEntry entry;
        entry.id = id;
        entry.sourceLine = line;
        entry.op = std::move(op);
        nodes_.push_back(std::move(entry));
    }

    std::vector<NodeEntry>& nodes() { return nodes_; }

    void clear() { nodes_.clear(); }

private:
    NodeRegistry() = default;
    std::vector<NodeEntry> nodes_;
};

// Helper for NODE macro - registers and returns reference
template<typename T>
T& registerNode(const char* id, int line, T* op) {
    op->setId(id);
    op->setSourceLine(line);
    NodeRegistry::instance().registerNode(id, line, std::unique_ptr<Operator>(op));
    return *op;
}

} // namespace vivid

// NODE macro: registers operator with source location
// Usage: auto& noise = NODE(Noise()).scale(4.0);
#define NODE(op) vivid::registerNode(#op, __LINE__, new op)

// Named node variant for explicit naming
// Usage: auto& myNoise = NODE_AS("noise", Noise()).scale(4.0);
#define NODE_AS(name, op) vivid::registerNode(name, __LINE__, new op)
