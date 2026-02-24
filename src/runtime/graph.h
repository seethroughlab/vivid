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
    const std::string& source_path() const { return source_path_; }

    // Mutation
    bool add_node(const std::string& id, const std::string& type,
                  const std::unordered_map<std::string, float>& params = {});
    bool remove_node(const std::string& id);
    bool add_connection(const std::string& from_node, const std::string& from_port,
                        const std::string& to_node, const std::string& to_port);
    bool remove_connection(const std::string& from_node, const std::string& from_port,
                           const std::string& to_node, const std::string& to_port);

    // Lookup
    const NodeDef* find_node(const std::string& id) const;
    NodeDef* find_node(const std::string& id);

    // Serialization
    bool save(const char* path) const;

private:
    std::vector<NodeDef> nodes_;
    std::vector<ConnectionDef> connections_;
    std::string source_path_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_GRAPH_H
