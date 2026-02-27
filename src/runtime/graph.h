#ifndef VIVID_RUNTIME_GRAPH_H
#define VIVID_RUNTIME_GRAPH_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>

namespace vivid {

struct NodeDef {
    std::string id;
    std::string type;
    std::unordered_map<std::string, float> params;
    std::unordered_map<std::string, std::string> string_params;
    // Optional layout position (NaN = use auto-layout)
    float layout_x = NAN;
    float layout_y = NAN;
    bool has_layout() const { return !std::isnan(layout_x); }

    // Per-node GPU texture resolution (0 = inherit or default 800x600)
    uint32_t tex_width  = 0;
    uint32_t tex_height = 0;
};

struct ConnectionDef {
    std::string from_node, from_port;
    std::string to_node, to_port;
};

struct FilterDef {
    std::string name;
    std::string source;         // which built-in it was copied from
    bool time_dependent = false;
    struct ParamDef {
        std::string name;
        float default_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 1.0f;
    };
    std::vector<ParamDef> params;
    std::string shader;         // WGSL source (inline in JSON)
};

struct MidiMappingDef {
    std::string node_id;
    std::string param_name;
    int cc_number = 0;        // 0-127
    int channel = 0;          // 0 = omni, 1-16 = specific
    float range_min = 0.0f;
    float range_max = 1.0f;
};

class Graph {
public:
    bool load(const char* path);
    const std::vector<NodeDef>& nodes() const { return nodes_; }
    const std::vector<ConnectionDef>& connections() const { return connections_; }
    const std::vector<MidiMappingDef>& midi_mappings() const { return midi_mappings_; }
    const std::vector<FilterDef>& filters() const { return filters_; }
    const std::string& source_path() const { return source_path_; }

    // Mutation
    bool add_node(const std::string& id, const std::string& type,
                  const std::unordered_map<std::string, float>& params = {},
                  const std::unordered_map<std::string, std::string>& string_params = {});
    bool remove_node(const std::string& id);
    bool add_connection(const std::string& from_node, const std::string& from_port,
                        const std::string& to_node, const std::string& to_port);
    bool remove_connection(const std::string& from_node, const std::string& from_port,
                           const std::string& to_node, const std::string& to_port);

    // Filter mutation
    void add_filter(FilterDef filter);
    const FilterDef* find_filter(const std::string& name) const;
    FilterDef* find_filter(const std::string& name);
    bool remove_filter(const std::string& name);
    void update_filter_shader(const std::string& name, const std::string& source);

    // MIDI mapping mutation
    bool add_midi_mapping(const std::string& node_id, const std::string& param,
                          int cc, int channel, float range_min, float range_max);
    bool remove_midi_mapping(const std::string& node_id, const std::string& param);
    bool update_midi_mapping(const std::string& node_id, const std::string& param,
                             float range_min, float range_max);
    const MidiMappingDef* find_midi_mapping(const std::string& node_id,
                                            const std::string& param) const;

    // Lookup
    const NodeDef* find_node(const std::string& id) const;
    NodeDef* find_node(const std::string& id);

    // Viewport (NaN = not set, use default)
    float viewport_pan_x = NAN;
    float viewport_pan_y = NAN;
    float viewport_zoom  = NAN;
    bool has_viewport() const { return !std::isnan(viewport_pan_x); }
    void set_viewport(float px, float py, float z) { viewport_pan_x = px; viewport_pan_y = py; viewport_zoom = z; }

    // Serialization
    bool save(const char* path) const;

private:
    std::vector<NodeDef> nodes_;
    std::vector<ConnectionDef> connections_;
    std::vector<MidiMappingDef> midi_mappings_;
    std::vector<FilterDef> filters_;
    std::string source_path_;
};

} // namespace vivid

#endif // VIVID_RUNTIME_GRAPH_H
