#include "ui/graph/operator_layout.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace vivid::ui {

namespace {

bool read_text_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

}  // namespace

bool OperatorLayout::load_file(const std::string& path) {
    std::string buf;
    if (!read_text_file(path, buf)) return false;

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(buf);
    } catch (const std::exception& e) {
        std::fprintf(stderr,
                     "[vivid] operator_layout: failed to parse %s: %s\n",
                     path.c_str(), e.what());
        return false;
    }

    const auto ops_it = root.find("operators");
    if (ops_it == root.end() || !ops_it->is_array()) return false;

    size_t added = 0;
    for (const auto& entry : *ops_it) {
        if (!entry.is_object()) continue;
        const auto name_it = entry.find("name");
        const auto xy_it = entry.find("xy");
        if (name_it == entry.end() || !name_it->is_string()) continue;
        if (xy_it == entry.end() || !xy_it->is_array() || xy_it->size() < 2) continue;

        OperatorLayoutEntry e;
        e.x = (*xy_it)[0].get<float>();
        e.y = (*xy_it)[1].get<float>();
        if (const auto k = entry.find("kind"); k != entry.end() && k->is_string()) {
            e.kind = k->get<std::string>();
        }
        if (const auto h = entry.find("hash"); h != entry.end() && h->is_string()) {
            e.hash = h->get<std::string>();
        }
        if (const auto b = entry.find("brief"); b != entry.end() && b->is_string()) {
            e.brief = b->get<std::string>();
        }
        if (const auto lb = entry.find("lane_behavior"); lb != entry.end() && lb->is_string()) {
            e.lane_behavior = lb->get<std::string>();
        }
        if (const auto ni = entry.find("num_inputs"); ni != entry.end() && ni->is_number_integer()) {
            e.num_inputs = ni->get<int>();
        }
        if (const auto no = entry.find("num_outputs"); no != entry.end() && no->is_number_integer()) {
            e.num_outputs = no->get<int>();
        }
        if (const auto rel = entry.find("related"); rel != entry.end() && rel->is_array()) {
            e.related.reserve(rel->size());
            for (const auto& r : *rel) {
                if (r.is_string()) e.related.push_back(r.get<std::string>());
            }
        }
        entries_[name_it->get<std::string>()] = std::move(e);
        ++added;
    }
    return added > 0;
}

bool OperatorLayout::load(const std::string& resources_dir,
                         const std::string& config_dir) {
    entries_.clear();

    if (!resources_dir.empty()) {
        load_file(resources_dir + "/operator_embeddings.json");
    }
    if (!config_dir.empty()) {
        // User cache wins over bundled data when both describe the same
        // operator — this is how launch-time sidecar output takes effect.
        load_file(config_dir + "/cache/operator_layout.json");
    }
    return !entries_.empty();
}

const OperatorLayoutEntry* OperatorLayout::find(const std::string& type) const {
    auto it = entries_.find(type);
    if (it == entries_.end()) return nullptr;
    return &it->second;
}

}  // namespace vivid::ui
