#include "runtime/control/graph_file_io.h"
#include "runtime/graph/graph.h"
#include "runtime/packages/package_manager.h"
#include "common/string_util.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_set>

namespace vivid {

namespace {

std::string trim_copy(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= csv.size()) {
        size_t comma = csv.find(',', pos);
        if (comma == std::string::npos) comma = csv.size();
        std::string tok = trim_copy(csv.substr(pos, comma - pos));
        if (!tok.empty()) out.push_back(tok);
        pos = comma + 1;
    }
    return out;
}

std::string join_csv(const std::vector<std::string>& items) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ", ";
        out += items[i];
    }
    return out;
}

bool load_graph_quiet(const std::filesystem::path& graph_path,
                      vivid::Graph& graph,
                      std::string* error = nullptr) {
    nlohmann::json root;
    try {
        std::ifstream ifs(graph_path);
        if (!ifs) {
            if (error) *error = "Failed to read graph JSON";
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        if (error) *error = "Failed to read graph JSON";
        return false;
    }
    graph.set_source_path(graph_path.string());
    if (!graph.load_from_json_doc(root, true, true)) {
        if (error) *error = "Graph JSON failed validation";
        return false;
    }
    return true;
}

nlohmann::json preview_controls_to_json(const vivid::GraphContentMeta& meta) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& ctrl : meta.preview_controls) {
        nlohmann::json item = nlohmann::json::object();
        item["node"] = ctrl.node;
        item["param"] = ctrl.param;
        if (!ctrl.label.empty()) item["label"] = ctrl.label;
        out.push_back(std::move(item));
    }
    return out;
}

void populate_example_entry_from_graph(const vivid::Graph& graph,
                                       const std::filesystem::path& graph_path,
                                       const std::filesystem::path& graphs_root,
                                       vivid::ExampleEntry& out) {
    const auto& meta = graph.meta();
    std::string rel = std::filesystem::relative(graph_path, graphs_root).generic_string();
    std::string stem = graph_path.stem().string();
    out = {};
    out.path = rel;
    out.id = meta.id.empty() ? stem : meta.id;
    out.title = meta.title.empty() ? stem : meta.title;
    out.summary = meta.description;
    out.tags = meta.tags;
    out.difficulty = meta.difficulty.empty() ? "intermediate" : meta.difficulty;
    out.domains = meta.domains;
    out.requires_packages = meta.requires_packages;
    out.featured_rank = meta.featured_rank >= 0 ? meta.featured_rank : 1000;
    out.estimated_minutes = meta.estimated_minutes >= 0 ? meta.estimated_minutes : 0;
    out.content_kind = meta.content_kind;
    out.category = meta.category;
    out.family = meta.family;
    out.role = meta.role;
    out.playability = meta.playability;
    for (const auto& ctrl : meta.preview_controls) {
        out.preview_controls.push_back({ctrl.node, ctrl.param, ctrl.label});

        const auto* node = graph.find_node(ctrl.node);
        if (!node) continue;

        vivid::PreviewSnapshotRow row;
        row.label = ctrl.label.empty() ? ctrl.param : ctrl.label;

        auto fit = node->params.find(ctrl.param);
        if (fit != node->params.end()) {
            row.value = vivid::format_float(fit->second, 2);
        } else {
            auto sit = node->string_params.find(ctrl.param);
            if (sit == node->string_params.end()) continue;
            row.value = sit->second;
        }
        out.preview_rows.push_back(std::move(row));
    }
}

} // namespace

bool load_example_entry_from_graph(const std::filesystem::path& graph_path,
                                   const std::filesystem::path& graphs_root,
                                   vivid::ExampleEntry& out) {
    vivid::Graph graph;
    if (!load_graph_quiet(graph_path, graph)) return false;
    populate_example_entry_from_graph(graph, graph_path, graphs_root, out);
    return true;
}

bool load_graph_meta_edit_data(const std::string& graph_path,
                               vivid::GraphMetaEditData& out,
                               std::string& error) {
    vivid::Graph graph;
    if (!load_graph_quiet(graph_path, graph, &error)) return false;

    const auto& meta = graph.meta();
    out = {};
    out.path = graph_path;
    auto stem = std::filesystem::path(graph_path).stem().string();
    out.id = meta.id.empty() ? stem : meta.id;
    out.title = meta.title.empty() ? stem : meta.title;
    out.description = meta.description;
    out.tags_csv = join_csv(meta.tags);
    out.difficulty = meta.difficulty.empty() ? "intermediate" : meta.difficulty;
    out.domains_csv = join_csv(meta.domains);
    out.requires_packages_csv = join_csv(meta.requires_packages);
    out.featured_rank = std::to_string(meta.featured_rank >= 0 ? meta.featured_rank : 1000);
    out.content_kind = meta.content_kind;
    out.category = meta.category;
    out.family = meta.family;
    out.role = meta.role;
    out.playability = meta.playability;
    if (!meta.preview_controls.empty())
        out.preview_controls_json = preview_controls_to_json(meta).dump();
    return true;
}

bool save_graph_meta_edit_data(const vivid::GraphMetaEditData& in, std::string& error) {
    vivid::Graph graph;
    if (!load_graph_quiet(in.path, graph, &error)) {
        error = "Failed to read graph JSON for save";
        return false;
    }

    auto& meta = graph.meta_mut();
    meta = {};
    meta.id = trim_copy(in.id);
    meta.title = trim_copy(in.title);
    meta.description = trim_copy(in.description);
    meta.difficulty = trim_copy(in.difficulty);
    meta.tags = split_csv(in.tags_csv);
    meta.domains = split_csv(in.domains_csv);
    meta.requires_packages = split_csv(in.requires_packages_csv);
    int rank = 1000;
    try {
        if (!trim_copy(in.featured_rank).empty()) rank = std::stoi(trim_copy(in.featured_rank));
    } catch (...) {}
    meta.featured_rank = rank;

    auto set_or_erase = [&](const char* key, const std::string& val) {
        std::string v = trim_copy(val);
        if (std::string(key) == "content_kind") meta.content_kind = v;
        else if (std::string(key) == "category") meta.category = v;
        else if (std::string(key) == "family") meta.family = v;
        else if (std::string(key) == "role") meta.role = v;
        else if (std::string(key) == "playability") meta.playability = v;
    };
    set_or_erase("content_kind", in.content_kind);
    set_or_erase("category", in.category);
    set_or_erase("family", in.family);
    set_or_erase("role", in.role);
    set_or_erase("playability", in.playability);

    if (!in.preview_controls_json.empty()) {
        try {
            auto preview = nlohmann::json::parse(in.preview_controls_json);
            if (preview.is_array()) {
                for (const auto& item : preview) {
                    if (!item.is_object()) continue;
                    vivid::GraphPreviewControl ctrl;
                    if (item.contains("node") && item["node"].is_string())
                        ctrl.node = item["node"].get<std::string>();
                    if (item.contains("param") && item["param"].is_string())
                        ctrl.param = item["param"].get<std::string>();
                    if (item.contains("label") && item["label"].is_string())
                        ctrl.label = item["label"].get<std::string>();
                    if (!ctrl.node.empty() && !ctrl.param.empty())
                        meta.preview_controls.push_back(std::move(ctrl));
                }
            }
        } catch (...) {}
    }

    if (!graph.save(in.path.c_str())) {
        error = "Failed to write graph JSON";
        return false;
    }
    return true;
}

static std::vector<vivid::ExampleEntry>
discover_examples_recursive(const std::filesystem::path& graphs_root) {
    std::vector<vivid::ExampleEntry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(graphs_root, ec)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(graphs_root, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".json") continue;
        vivid::ExampleEntry item;
        if (load_example_entry_from_graph(e.path(), graphs_root, item))
            out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const vivid::ExampleEntry& a,
                                         const vivid::ExampleEntry& b) {
        if (a.featured_rank != b.featured_rank) return a.featured_rank < b.featured_rank;
        return a.title < b.title;
    });
    return out;
}

std::vector<vivid::ExampleEntry>
discover_examples_with_packages(const std::filesystem::path& graphs_root,
                                vivid::PackageManager* pkg_manager) {
    std::vector<vivid::ExampleEntry> out = discover_examples_recursive(graphs_root);
    if (!pkg_manager) return out;

    std::unordered_set<std::string> seen_paths;
    for (const auto& e : out) seen_paths.insert(e.path);

    for (const auto& pkg : pkg_manager->list()) {
        if (pkg.path.empty()) continue;
        std::error_code ec;
        std::filesystem::path pkg_graphs_root = std::filesystem::path(pkg.path) / "graphs";
        if (!std::filesystem::is_directory(pkg_graphs_root, ec)) continue;

        auto pkg_examples = discover_examples_recursive(pkg_graphs_root);
        for (auto& e : pkg_examples) {
            std::filesystem::path abs_path = pkg_graphs_root / e.path;
            std::string open_path = abs_path.lexically_normal().string();
            if (!seen_paths.insert(open_path).second) continue;
            e.path = open_path;

            e.package_name = pkg.name;
            if (e.requires_packages.empty()) {
                e.requires_packages.push_back(pkg.name);
            }
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(), [](const vivid::ExampleEntry& a,
                                         const vivid::ExampleEntry& b) {
        if (a.featured_rank != b.featured_rank) return a.featured_rank < b.featured_rank;
        return a.title < b.title;
    });
    return out;
}

std::string resolve_graph_input_path(const std::string& input,
                                     const std::filesystem::path& graphs_root,
                                     const std::vector<vivid::ExampleEntry>& examples) {
    if (input.empty()) return input;
    std::filesystem::path p(input);
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return p.string();
    if (p.is_relative()) {
        std::filesystem::path in_graphs = graphs_root / p;
        if (std::filesystem::exists(in_graphs, ec)) return in_graphs.string();
    }
    const std::string filename = p.filename().string();
    for (const auto& e : examples) {
        if (e.id == input || e.id == filename ||
            std::filesystem::path(e.path).filename().string() == filename) {
            std::filesystem::path candidate = graphs_root / e.path;
            if (std::filesystem::exists(candidate, ec)) return candidate.string();
        }
    }
    return input;
}

std::filesystem::path expand_tilde_path(const std::string& input) {
    if (input.empty()) return {};
    if (input[0] != '~') return std::filesystem::path(input);
    const char* home = ::getenv("HOME");
    if (!home) return std::filesystem::path(input);
    if (input.size() == 1) return std::filesystem::path(home);
    if (input[1] == '/' || input[1] == '\\')
        return std::filesystem::path(home) / input.substr(2);
    return std::filesystem::path(input);
}

} // namespace vivid
