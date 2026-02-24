#ifndef VIVID_RUNTIME_GRAPH_H
#define VIVID_RUNTIME_GRAPH_H

#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

struct NodeDef {
    std::string id;
    std::string type;
    std::unordered_map<std::string, float> params;
};

struct ConnectionDef {
    std::string from_node, from_port;
    std::string to_node, to_port;
};

class Graph {
public:
    bool load(const char* path);
    const std::vector<NodeDef>& nodes() const { return nodes_; }
    const std::vector<ConnectionDef>& connections() const { return connections_; }

private:
    std::vector<NodeDef> nodes_;
    std::vector<ConnectionDef> connections_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_GRAPH_H
