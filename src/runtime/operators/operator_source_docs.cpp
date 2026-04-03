#include "runtime/operators/operator_source_docs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace vivid {
namespace fs = std::filesystem;

namespace {

struct TypeDefinition {
    fs::path path;
    std::vector<std::string> lines;
    size_t line_index = 0;
    std::vector<std::string> bases;
};

static bool is_source_like_extension(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".mm" ||
           ext == ".h" || ext == ".hh" || ext == ".hpp";
}

static bool should_skip_dir(const fs::path& path) {
    const std::string name = path.filename().string();
    return name == ".git" || name == "build" || name == "site" ||
           name == "__pycache__" || name == ".pytest_cache";
}

static std::string normalize_root(const std::string& root) {
    if (root.empty()) return {};
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(root, ec);
    if (ec) normalized = fs::absolute(root).lexically_normal();
    return normalized.generic_string();
}

static std::string relative_to_root(const std::string& root, const fs::path& path) {
    if (root.empty()) return path.generic_string();
    std::error_code ec;
    fs::path rel = fs::relative(path, root, ec);
    if (ec) return path.generic_string();
    return rel.generic_string();
}

static std::vector<std::string> read_lines(const fs::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line))
        lines.push_back(line);
    return lines;
}

static std::string trim_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

static std::string camel_to_snake_id(std::string name) {
    if (name.size() > 2) {
        const std::string suffix = name.substr(name.size() - 2);
        if (suffix == "Au" || suffix == "Fr")
            name.resize(name.size() - 2);
    }
    std::string out;
    out.reserve(name.size() + 8);
    for (size_t i = 0; i < name.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (std::isupper(ch)) {
            if (!out.empty() &&
                (std::islower(static_cast<unsigned char>(out.back())) ||
                 (i + 1 < name.size() &&
                  std::islower(static_cast<unsigned char>(name[i + 1]))))) {
                out.push_back('_');
            }
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

static std::string join_lines(const std::vector<std::string>& lines,
                              size_t start,
                              size_t end) {
    std::ostringstream ss;
    for (size_t i = start; i < end && i < lines.size(); ++i) {
        if (i > start) ss << '\n';
        ss << lines[i];
    }
    return ss.str();
}

static std::optional<std::string> extract_doc_block(const std::vector<std::string>& lines,
                                                    size_t type_line) {
    if (type_line == 0 || type_line > lines.size()) return std::nullopt;

    std::optional<size_t> end_line;
    for (size_t cursor = type_line; cursor-- > 0;) {
        const std::string stripped = trim_copy(lines[cursor]);
        if (stripped.empty())
            continue;
        if (stripped.size() >= 2 && stripped.rfind("*/") == stripped.size() - 2) {
            end_line = cursor;
            break;
        }
        return std::nullopt;
    }
    if (!end_line.has_value()) return std::nullopt;

    std::optional<size_t> start_line;
    for (size_t cursor = *end_line + 1; cursor-- > 0;) {
        if (lines[cursor].find("/**") != std::string::npos) {
            start_line = cursor;
            break;
        }
        if (cursor == 0) break;
    }
    if (!start_line.has_value()) return std::nullopt;

    std::vector<std::string> cleaned;
    cleaned.reserve(*end_line - *start_line + 1);
    std::regex open_re(R"(^\s*/\*\*\s?)");
    std::regex close_re(R"(\s?\*/\s*$)");
    std::regex star_re(R"(^\s*\*\s?)");
    for (size_t i = *start_line; i <= *end_line; ++i) {
        std::string s = lines[i];
        s = std::regex_replace(s, open_re, "");
        s = std::regex_replace(s, close_re, "");
        s = std::regex_replace(s, star_re, "");
        if (trim_copy(s) == "*")
            s.clear();
        cleaned.push_back(s);
    }

    while (!cleaned.empty() && trim_copy(cleaned.front()).empty())
        cleaned.erase(cleaned.begin());
    while (!cleaned.empty() && trim_copy(cleaned.back()).empty())
        cleaned.pop_back();

    if (cleaned.empty()) return std::nullopt;
    return join_lines(cleaned, 0, cleaned.size());
}

static std::vector<std::string> extract_base_names(std::string decl) {
    auto colon = decl.find(':');
    if (colon == std::string::npos) return {};
    decl = decl.substr(colon + 1);
    auto brace = decl.find('{');
    if (brace != std::string::npos)
        decl = decl.substr(0, brace);
    auto semi = decl.find(';');
    if (semi != std::string::npos)
        decl = decl.substr(0, semi);

    std::regex ident_re(R"(\b([A-Za-z_]\w*)\b)");
    std::vector<std::string> bases;
    for (auto it = std::sregex_iterator(decl.begin(), decl.end(), ident_re);
         it != std::sregex_iterator(); ++it) {
        std::string id = (*it)[1].str();
        if (id == "public" || id == "protected" || id == "private" ||
            id == "virtual" || id == "final" || id == "override")
            continue;
        bases.push_back(std::move(id));
    }
    bases.erase(std::unique(bases.begin(), bases.end()), bases.end());
    return bases;
}

static std::optional<TypeDefinition> find_type_definition_in_file(
        const fs::path& path, const std::string& type_name) {
    const auto lines = read_lines(path);
    if (lines.empty()) return std::nullopt;
    const std::regex pattern("^\\s*(struct|class)\\s+" + type_name + R"(\b)");
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!std::regex_search(lines[i], pattern)) continue;
        std::string decl = lines[i];
        size_t cursor = i + 1;
        while (decl.find('{') == std::string::npos &&
               decl.find(';') == std::string::npos &&
               cursor < lines.size() &&
               cursor <= i + 8) {
            decl += "\n" + lines[cursor++];
        }
        TypeDefinition def;
        def.path = path;
        def.lines = lines;
        def.line_index = i;
        def.bases = extract_base_names(decl);
        return def;
    }
    return std::nullopt;
}

static std::vector<std::string> parse_include_targets(const std::vector<std::string>& lines) {
    std::vector<std::string> includes;
    const std::regex include_re(R"INCLUDE(^\s*#include\s+"([^"]+)")INCLUDE");
    for (const auto& line : lines) {
        std::smatch match;
        if (std::regex_search(line, match, include_re))
            includes.push_back(match[1].str());
    }
    return includes;
}

static std::optional<fs::path> resolve_include_path(
        const fs::path& current_file,
        const std::string& include_target,
        const std::string& root,
        const std::unordered_map<std::string, std::vector<std::string>>& files_by_name) {
    const fs::path current_dir = current_file.parent_path();
    fs::path direct = (current_dir / include_target).lexically_normal();
    if (fs::exists(direct)) {
        std::error_code ec;
        fs::path abs = fs::weakly_canonical(direct, ec);
        if (ec) abs = fs::absolute(direct).lexically_normal();
        const std::string abs_s = abs.generic_string();
        if (root.empty() || abs_s.rfind(root, 0) == 0)
            return abs;
    }

    fs::path rooted = (fs::path(root) / include_target).lexically_normal();
    if (!root.empty() && fs::exists(rooted))
        return rooted;

    const std::string filename = fs::path(include_target).filename().string();
    auto it = files_by_name.find(filename);
    if (it == files_by_name.end() || it->second.empty())
        return std::nullopt;
    return fs::path(it->second.front());
}

static std::optional<TypeDefinition> find_type_definition_recursive(
        const std::string& root,
        const fs::path& start_path,
        const std::string& type_name,
        const std::unordered_map<std::string, std::vector<std::string>>& files_by_name,
        std::unordered_set<std::string>& visited_files,
        const std::vector<std::string>& fallback_files) {
    const fs::path normalized_start = start_path.lexically_normal();
    const std::string key = normalized_start.generic_string();
    if (!visited_files.insert(key).second)
        return std::nullopt;

    auto def = find_type_definition_in_file(normalized_start, type_name);
    if (def) return def;

    const auto lines = read_lines(normalized_start);
    for (const auto& include_target : parse_include_targets(lines)) {
        auto include_path = resolve_include_path(normalized_start, include_target, root, files_by_name);
        if (!include_path.has_value()) continue;
        auto nested = find_type_definition_recursive(root, *include_path, type_name,
                                                     files_by_name, visited_files, fallback_files);
        if (nested) return nested;
    }

    for (const auto& path_s : fallback_files) {
        if (!visited_files.count(path_s)) {
            auto nested = find_type_definition_recursive(root, fs::path(path_s), type_name,
                                                         files_by_name, visited_files, fallback_files);
            if (nested) return nested;
        }
    }

    return std::nullopt;
}

static nlohmann::json parse_doc_block(const std::string& raw) {
    nlohmann::json result = nlohmann::json::object();
    result["brief"] = nullptr;
    result["body"] = nullptr;
    result["tips"] = nlohmann::json::array();
    result["related"] = nlohmann::json::array();
    result["recipes"] = nlohmann::json::array();
    result["pitfalls"] = nlohmann::json::array();
    result["best_used_with"] = nlohmann::json::array();
    result["common_companions"] = nlohmann::json::array();
    result["params"] = nlohmann::json::array();
    result["inputs"] = nlohmann::json::array();
    result["outputs"] = nlohmann::json::array();

    std::unordered_map<std::string, size_t> param_idx;
    std::unordered_map<std::string, size_t> input_idx;
    std::unordered_map<std::string, size_t> output_idx;
    std::vector<std::string> body_lines;
    std::string current_tag;
    std::string current_name;
    int current_index = -1;

    auto flush_body = [&]() {
        if (body_lines.empty()) return;
        std::ostringstream ss;
        for (size_t i = 0; i < body_lines.size(); ++i) {
            if (i > 0) ss << '\n';
            ss << body_lines[i];
        }
        const std::string body = trim_copy(ss.str());
        if (!body.empty())
            result["body"] = body;
        body_lines.clear();
    };

    auto append_named_doc = [&](const char* key,
                                std::unordered_map<std::string, size_t>& index,
                                const std::string& name,
                                const std::string& doc) {
        auto& arr = result[key];
        auto it = index.find(name);
        if (it == index.end()) {
            arr.push_back({{"name", name}, {"doc", doc}});
            index[name] = arr.size() - 1;
            current_index = static_cast<int>(arr.size() - 1);
        } else {
            arr[it->second]["doc"] = doc;
            current_index = static_cast<int>(it->second);
        }
    };

    std::istringstream input(raw);
    std::string line;
    const std::regex tag_re(R"(^@(\w+)\s*(.*))");
    while (std::getline(input, line)) {
        std::smatch match;
        if (std::regex_match(line, match, tag_re)) {
            const std::string tag = match[1].str();
            const std::string rest = trim_copy(match[2].str());

            if (tag == "brief") {
                flush_body();
                result["brief"] = rest;
                current_tag = "body";
                current_name.clear();
                current_index = -1;
            } else if (tag == "tip") {
                flush_body();
                result["tips"].push_back(rest);
                current_tag = tag;
                current_index = static_cast<int>(result["tips"].size() - 1);
            } else if (tag == "see") {
                flush_body();
                result["related"] = nlohmann::json::array();
                std::stringstream ss(rest);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim_copy(item);
                    if (!item.empty())
                        result["related"].push_back(item);
                }
                current_tag = tag;
                current_index = -1;
            } else if (tag == "recipe") {
                flush_body();
                result["recipes"].push_back(rest);
                current_tag = tag;
                current_index = static_cast<int>(result["recipes"].size() - 1);
            } else if (tag == "pitfall") {
                flush_body();
                result["pitfalls"].push_back(rest);
                current_tag = tag;
                current_index = static_cast<int>(result["pitfalls"].size() - 1);
            } else if (tag == "family") {
                flush_body();
                result["operator_family"] = rest;
                current_tag = tag;
                current_index = -1;
            } else if (tag == "best_used_with" || tag == "common_companions") {
                flush_body();
                result[tag] = nlohmann::json::array();
                std::stringstream ss(rest);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim_copy(item);
                    if (!item.empty())
                        result[tag].push_back(item);
                }
                current_tag = tag;
                current_index = -1;
            } else if (tag == "param" || tag == "input" || tag == "output") {
                flush_body();
                std::stringstream ss(rest);
                std::string name;
                ss >> name;
                std::string doc;
                std::getline(ss, doc);
                doc = trim_copy(doc);
                current_tag = tag;
                current_name = name;
                if (!name.empty()) {
                    if (tag == "param")
                        append_named_doc("params", param_idx, name, doc);
                    else if (tag == "input")
                        append_named_doc("inputs", input_idx, name, doc);
                    else
                        append_named_doc("outputs", output_idx, name, doc);
                } else {
                    current_index = -1;
                }
            } else {
                if (current_tag == "body")
                    body_lines.push_back(line);
            }
            continue;
        }

        if (current_tag == "body") {
            body_lines.push_back(line);
        } else if ((current_tag == "tip" || current_tag == "recipe" || current_tag == "pitfall") &&
                   current_index >= 0) {
            const char* key = current_tag == "tip" ? "tips" :
                              current_tag == "recipe" ? "recipes" : "pitfalls";
            auto& arr = result[key];
            std::string existing = arr[static_cast<size_t>(current_index)].get<std::string>();
            arr[static_cast<size_t>(current_index)] = trim_copy(existing + " " + trim_copy(line));
        } else if ((current_tag == "param" || current_tag == "input" || current_tag == "output") &&
                   current_index >= 0) {
            const char* key = current_tag == "param" ? "params" :
                              current_tag == "input" ? "inputs" : "outputs";
            auto& entry = result[key][static_cast<size_t>(current_index)];
            std::string existing = entry.value("doc", "");
            entry["doc"] = trim_copy(existing + " " + trim_copy(line));
        } else if (current_tag.empty()) {
            body_lines.push_back(line);
        }
    }

    flush_body();
    return result;
}

static nlohmann::json resolve_from_type(const std::string& root,
                                        const TypeDefinition& def,
                                        const std::unordered_map<std::string, std::vector<std::string>>& files_by_name,
                                        const std::vector<std::string>& fallback_files,
                                        std::unordered_set<std::string>& visited_types);

static nlohmann::json resolve_from_name(const std::string& root,
                                        const fs::path& start_path,
                                        const std::string& type_name,
                                        const std::unordered_map<std::string, std::vector<std::string>>& files_by_name,
                                        const std::vector<std::string>& fallback_files,
                                        std::unordered_set<std::string>& visited_types) {
    if (!visited_types.insert(type_name).second)
        return nullptr;

    std::unordered_set<std::string> visited_files;
    auto def = find_type_definition_recursive(root, start_path, type_name, files_by_name,
                                              visited_files, fallback_files);
    if (!def) return nullptr;
    return resolve_from_type(root, *def, files_by_name, fallback_files, visited_types);
}

static nlohmann::json resolve_from_type(const std::string& root,
                                        const TypeDefinition& def,
                                        const std::unordered_map<std::string, std::vector<std::string>>& files_by_name,
                                        const std::vector<std::string>& fallback_files,
                                        std::unordered_set<std::string>& visited_types) {
    if (auto raw = extract_doc_block(def.lines, def.line_index)) {
        nlohmann::json doc = parse_doc_block(*raw);
        doc["has_docs"] = true;
        doc["source_path"] = relative_to_root(root, def.path);
        return doc;
    }

    for (const auto& base : def.bases) {
        auto doc = resolve_from_name(root, def.path, base, files_by_name, fallback_files, visited_types);
        if (!doc.is_null() && doc.value("has_docs", false))
            return doc;
    }

    nlohmann::json fallback = nlohmann::json::object();
    fallback["has_docs"] = false;
    fallback["source_path"] = relative_to_root(root, def.path);
    return fallback;
}

static std::string make_cache_key(const std::string& scope,
                                  const std::string& name,
                                  const std::string& root,
                                  const std::string& operator_name) {
    return scope + '\n' + name + '\n' + root + '\n' + operator_name;
}

} // namespace

void OperatorSourceDocs::set_core_source_root(std::string root) {
    root = normalize_root(root);
    if (root == core_source_root_)
        return;
    invalidate_core();
    core_source_root_ = std::move(root);
}

void OperatorSourceDocs::invalidate_core() {
    if (!core_source_root_.empty())
        root_indexes_.erase(core_source_root_);
    std::vector<std::string> erase_keys;
    for (const auto& [key, _] : cache_) {
        if (key.rfind("core\n", 0) == 0)
            erase_keys.push_back(key);
    }
    for (const auto& key : erase_keys)
        cache_.erase(key);
}

void OperatorSourceDocs::invalidate_package(const std::string& package_name,
                                            const std::string& package_root) {
    if (!package_root.empty())
        root_indexes_.erase(normalize_root(package_root));
    std::vector<std::string> erase_keys;
    const std::string prefix = "pkg\n" + package_name + "\n";
    for (const auto& [key, _] : cache_) {
        if (key.rfind(prefix, 0) == 0)
            erase_keys.push_back(key);
    }
    for (const auto& key : erase_keys)
        cache_.erase(key);
}

nlohmann::json OperatorSourceDocs::resolve_core(const std::string& operator_name) {
    return resolve(make_cache_key("core", "", core_source_root_, operator_name),
                   core_source_root_, operator_name);
}

nlohmann::json OperatorSourceDocs::resolve_package(const std::string& package_name,
                                                   const std::string& package_root,
                                                   const std::string& operator_name) {
    const std::string normalized_root = normalize_root(package_root);
    return resolve(make_cache_key("pkg", package_name, normalized_root, operator_name),
                   normalized_root, operator_name);
}

nlohmann::json OperatorSourceDocs::resolve(const std::string& cache_key,
                                           const std::string& root,
                                           const std::string& operator_name) {
    auto it = cache_.find(cache_key);
    if (it != cache_.end())
        return it->second;

    if (root.empty() || !fs::exists(root)) {
        cache_[cache_key] = nullptr;
        return nullptr;
    }

    auto& index = root_indexes_[root];
    if (!index.indexed) {
        std::error_code ec;
        fs::recursive_directory_iterator iter(root, ec), end;
        for (; !ec && iter != end; iter.increment(ec)) {
            const fs::path path = iter->path();
            if (iter->is_directory(ec)) {
                if (should_skip_dir(path))
                    iter.disable_recursion_pending();
                continue;
            }
            if (!iter->is_regular_file(ec) || !is_source_like_extension(path))
                continue;

            const std::string normalized = path.lexically_normal().generic_string();
            index.files_by_name[path.filename().string()].push_back(normalized);
            index.searchable_files.push_back(normalized);

            const std::string ext = path.extension().string();
            if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" && ext != ".mm")
                continue;

            std::ifstream f(path);
            if (!f.is_open()) continue;
            std::ostringstream ss;
            ss << f.rdbuf();
            const std::string text = ss.str();
            const std::regex reg_re(R"(VIVID_REGISTER\(\s*(\w+))");
            for (auto reg_it = std::sregex_iterator(text.begin(), text.end(), reg_re);
                 reg_it != std::sregex_iterator(); ++reg_it) {
                index.registrations[(*reg_it)[1].str()] = normalized;
            }
        }
        index.indexed = true;
    }

    auto reg_it = index.registrations.find(operator_name);
    if (reg_it == index.registrations.end()) {
        cache_[cache_key] = nullptr;
        return nullptr;
    }

    std::unordered_set<std::string> visited_types;
    nlohmann::json doc = resolve_from_name(root, fs::path(reg_it->second), operator_name,
                                           index.files_by_name, index.searchable_files,
                                           visited_types);
    cache_[cache_key] = doc;
    return doc;
}

} // namespace vivid
