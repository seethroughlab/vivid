#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/subgraph_module.h"
#include <sstream>
#include <nlohmann/json.hpp>

namespace vivid {

CommandResult RuntimeAPI::add_mod_assignment(const std::string& node_id, const std::string& source,
                                             const std::string& destination, float amount,
                                             const std::string& polarity, const std::string& curve) {
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    if (!subgraph_modules_) return {false, "no module registry"};
    const auto* mod = subgraph_modules_->find(ndef->type);
    if (!mod) return {false, "node '" + node_id + "' is not a module instance"};

    const auto* src = mod->find_mod_source(source);
    if (!src) return {false, "unknown mod_source '" + source + "' on module '" + mod->name + "'"};
    const auto* dst = mod->find_mod_destination(destination);
    if (!dst) return {false, "unknown mod_destination '" + destination + "' on module '" + mod->name + "'"};

    // Lane rule: reject lane_aware source -> scalar destination
    if (src->shape == "lane_aware" && dst->shape != "lane_aware")
        return {false, "lane_aware source '" + source + "' cannot target scalar destination '" + destination + "'"};

    ModAssignmentDef a;
    a.source = source;
    a.destination = destination;
    a.amount = amount;
    a.polarity = polarity.empty() ? "unipolar" : polarity;
    a.curve = curve.empty() ? "linear" : curve;

    if (!graph_.add_mod_assignment(node_id, std::move(a)))
        return {false, "duplicate assignment '" + source + "' -> '" + destination + "'"};

    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "added mod assignment: " + source + " -> " + destination};
}

CommandResult RuntimeAPI::remove_mod_assignment(const std::string& node_id,
                                                const std::string& source, const std::string& destination) {
    if (!graph_.remove_mod_assignment(node_id, source, destination))
        return {false, "assignment not found: " + source + " -> " + destination + " on " + node_id};

    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "removed mod assignment: " + source + " -> " + destination};
}

CommandResult RuntimeAPI::update_mod_assignment(const std::string& node_id,
                                                const std::string& source, const std::string& destination,
                                                float amount, const std::string& polarity,
                                                const std::string& curve) {
    if (!graph_.update_mod_assignment(node_id, source, destination, amount, polarity, curve))
        return {false, "assignment not found: " + source + " -> " + destination + " on " + node_id};

    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "updated mod assignment: " + source + " -> " + destination};
}

CommandResult RuntimeAPI::list_mod_sources(const std::string& node_id) {
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    if (!subgraph_modules_) return {false, "no module registry"};
    const auto* mod = subgraph_modules_->find(ndef->type);
    if (!mod) return {false, "node '" + node_id + "' is not a module instance"};

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& s : mod->mod_sources) {
        nlohmann::json obj;
        obj["name"] = s.name;
        if (!s.description.empty()) obj["description"] = s.description;
        obj["shape"] = s.shape;
        obj["polarity"] = s.polarity;
        obj["kind"] = s.kind;
        if (!s.group.empty()) obj["group"] = s.group;
        arr.push_back(std::move(obj));
    }
    return {true, arr.dump()};
}

CommandResult RuntimeAPI::list_mod_destinations(const std::string& node_id) {
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    if (!subgraph_modules_) return {false, "no module registry"};
    const auto* mod = subgraph_modules_->find(ndef->type);
    if (!mod) return {false, "node '" + node_id + "' is not a module instance"};

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : mod->mod_destinations) {
        nlohmann::json obj;
        obj["name"] = d.name;
        if (!d.description.empty()) obj["description"] = d.description;
        obj["shape"] = d.shape;
        if (!d.group.empty()) obj["group"] = d.group;
        arr.push_back(std::move(obj));
    }
    return {true, arr.dump()};
}

CommandResult RuntimeAPI::list_mod_assignments(const std::string& node_id) {
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};

    const auto* assignments = graph_.find_mod_assignments(node_id);
    nlohmann::json arr = nlohmann::json::array();
    if (assignments) {
        for (const auto& a : *assignments) {
            nlohmann::json obj;
            obj["source"] = a.source;
            obj["destination"] = a.destination;
            obj["amount"] = static_cast<double>(a.amount);
            obj["polarity"] = a.polarity;
            obj["curve"] = a.curve;
            arr.push_back(std::move(obj));
        }
    }
    return {true, arr.dump()};
}

} // namespace vivid
