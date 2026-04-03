#include "runtime/control/graph_file_io.h"
#include "runtime/core/main_helpers.h"
#include "runtime/packages/package_manager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <unordered_set>

namespace vivid {

bool load_example_entry_from_graph(const std::filesystem::path& graph_path,
                                          const std::filesystem::path& graphs_root,
                                          vivid::ui::ExampleEntry& out) {
    nlohmann::json root;
    try {
        std::ifstream ifs(graph_path);
        if (!ifs) return false;
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        return false;
    }
    if (!root.is_object()) return false;

    std::string rel = std::filesystem::relative(graph_path, graphs_root).generic_string();
    std::string stem = graph_path.stem().string();
    out.path = rel;
    out.id = stem;
    out.title = stem;
    out.summary = "";
    out.difficulty = "intermediate";
    out.featured_rank = 1000;

    if (root.contains("meta") && root["meta"].is_object()) {
        const auto& meta = root["meta"];
        if (meta.contains("id") && meta["id"].is_string()) out.id = meta["id"].get<std::string>();
        if (meta.contains("title") && meta["title"].is_string()) out.title = meta["title"].get<std::string>();
        if (meta.contains("description") && meta["description"].is_string()) out.summary = meta["description"].get<std::string>();
        if (meta.contains("difficulty") && meta["difficulty"].is_string()) out.difficulty = meta["difficulty"].get<std::string>();
        if (meta.contains("featured_rank") && meta["featured_rank"].is_number_integer()) {
            out.featured_rank = meta["featured_rank"].get<int>();
        }
        if (meta.contains("estimated_minutes") && meta["estimated_minutes"].is_number_integer()) {
            out.estimated_minutes = meta["estimated_minutes"].get<int>();
        }
        out.tags = json_str_array(meta.value("tags", nlohmann::json()));
        out.envs = json_str_array(meta.value("envs", nlohmann::json()));
        out.requires_packages = json_str_array(meta.value("requires_packages", nlohmann::json()));
    }

    if (out.title.empty()) out.title = stem;
    if (out.id.empty()) out.id = stem;
    return true;
}

bool load_graph_meta_edit_data(const std::string& graph_path,
                                      vivid::ui::GraphMetaEditData& out,
                                      std::string& error) {
    nlohmann::json root;
    try {
        std::ifstream ifs(graph_path);
        if (!ifs) {
            error = "Failed to read graph JSON";
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        error = "Failed to read graph JSON";
        return false;
    }
    if (!root.is_object()) {
        error = "Graph JSON root must be an object";
        return false;
    }
    out = {};
    out.path = graph_path;
    auto stem = std::filesystem::path(graph_path).stem().string();
    out.id = stem;
    out.title = stem;
    out.difficulty = "intermediate";
    out.featured_rank = "1000";
    if (root.contains("meta") && root["meta"].is_object()) {
        const auto& meta = root["meta"];
        if (meta.contains("id") && meta["id"].is_string()) out.id = meta["id"].get<std::string>();
        if (meta.contains("title") && meta["title"].is_string()) out.title = meta["title"].get<std::string>();
        if (meta.contains("description") && meta["description"].is_string()) out.description = meta["description"].get<std::string>();
        if (meta.contains("difficulty") && meta["difficulty"].is_string()) out.difficulty = meta["difficulty"].get<std::string>();
        if (meta.contains("featured_rank") && meta["featured_rank"].is_number_integer())
            out.featured_rank = std::to_string(meta["featured_rank"].get<int>());
        out.tags_csv = join_csv(json_str_array(meta.value("tags", nlohmann::json())));
        out.envs_csv = join_csv(json_str_array(meta.value("envs", nlohmann::json())));
        out.requires_packages_csv = join_csv(json_str_array(meta.value("requires_packages", nlohmann::json())));
    }
    return true;
}

bool save_graph_meta_edit_data(const vivid::ui::GraphMetaEditData& in, std::string& error) {
    nlohmann::json root;
    try {
        std::ifstream ifs(in.path);
        if (!ifs) {
            error = "Failed to read graph JSON for save";
            return false;
        }
        root = nlohmann::json::parse(ifs);
    } catch (const std::exception&) {
        error = "Failed to read graph JSON for save";
        return false;
    }
    if (!root.is_object()) {
        error = "Graph JSON root must be an object";
        return false;
    }

    if (!root.contains("meta") || !root["meta"].is_object()) {
        root["meta"] = nlohmann::json::object();
    }
    auto& meta = root["meta"];

    meta["id"] = trim_copy(in.id);
    meta["title"] = trim_copy(in.title);
    meta["description"] = trim_copy(in.description);
    meta["difficulty"] = trim_copy(in.difficulty);
    meta["tags"] = split_csv(in.tags_csv);
    meta["envs"] = split_csv(in.envs_csv);
    meta["requires_packages"] = split_csv(in.requires_packages_csv);
    int rank = 1000;
    try {
        if (!trim_copy(in.featured_rank).empty()) rank = std::stoi(trim_copy(in.featured_rank));
    } catch (...) {}
    meta["featured_rank"] = rank;

    try {
        std::ofstream ofs(in.path);
        if (!ofs) {
            error = "Failed to write graph JSON";
            return false;
        }
        ofs << root.dump(4) << '\n';
        return true;
    } catch (const std::exception&) {
        error = "Failed to write graph JSON";
        return false;
    }
}

static std::vector<vivid::ui::ExampleEntry>
discover_examples_recursive(const std::filesystem::path& graphs_root) {
    std::vector<vivid::ui::ExampleEntry> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(graphs_root, ec)) return out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(graphs_root, ec)) {
        if (ec) break;
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".json") continue;
        vivid::ui::ExampleEntry item;
        if (load_example_entry_from_graph(e.path(), graphs_root, item))
            out.push_back(std::move(item));
    }
    std::sort(out.begin(), out.end(), [](const vivid::ui::ExampleEntry& a,
                                         const vivid::ui::ExampleEntry& b) {
        if (a.featured_rank != b.featured_rank) return a.featured_rank < b.featured_rank;
        return a.title < b.title;
    });
    return out;
}

std::vector<vivid::ui::ExampleEntry>
discover_examples_with_packages(const std::filesystem::path& graphs_root,
                                vivid::PackageManager* pkg_manager) {
    std::vector<vivid::ui::ExampleEntry> out = discover_examples_recursive(graphs_root);
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

            if (e.requires_packages.empty()) {
                e.requires_packages.push_back(pkg.name);
            }
            out.push_back(std::move(e));
        }
    }

    std::sort(out.begin(), out.end(), [](const vivid::ui::ExampleEntry& a,
                                         const vivid::ui::ExampleEntry& b) {
        if (a.featured_rank != b.featured_rank) return a.featured_rank < b.featured_rank;
        return a.title < b.title;
    });
    return out;
}

std::string resolve_graph_input_path(const std::string& input,
                                            const std::filesystem::path& graphs_root,
                                            const std::vector<vivid::ui::ExampleEntry>& examples) {
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
    const char* home = std::getenv("HOME");
    if (!home) return std::filesystem::path(input);
    if (input.size() == 1) return std::filesystem::path(home);
    if (input[1] == '/' || input[1] == '\\')
        return std::filesystem::path(home) / input.substr(2);
    return std::filesystem::path(input);
}

} // namespace vivid
