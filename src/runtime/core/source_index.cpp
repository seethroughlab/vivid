#include "runtime/core/source_index.h"

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

constexpr std::size_t kMaxIndexedFileBytes = 512 * 1024;
constexpr std::size_t kMaxSearchLimit = 100;
constexpr std::size_t kMaxReferenceLimit = 200;
constexpr std::size_t kMaxReadFileBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxSearchSnippet = 200;

const std::vector<std::string>& allowed_root_names() {
    static const std::vector<std::string> kNames = {"src", "operators", "mcp", "tests", "docs"};
    return kNames;
}

bool is_allowed_root_name(const std::string& name) {
    const auto& names = allowed_root_names();
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool should_skip_dir(const fs::path& path) {
    const std::string name = path.filename().string();
    return name == ".git" || name == "build" || name == "site" ||
           name == "__pycache__" || name == ".pytest_cache" ||
           name == ".venv" || name == ".venv-mcp";
}

bool is_text_like_extension(const fs::path& path) {
    static const std::unordered_set<std::string> kExts = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl", ".mm",
        ".py", ".md", ".txt", ".json", ".toml", ".cmake", ".wgsl", ".yml",
        ".yaml", ".sh", ".inc", ".plist", ".pbxproj", ".css", ".html",
    };
    const std::string ext = path.extension().string();
    return !ext.empty() && kExts.count(ext) > 0;
}

std::string trim_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

std::string normalize_root(const std::string& root) {
    if (root.empty()) return {};
    std::error_code ec;
    fs::path normalized = fs::weakly_canonical(root, ec);
    if (ec) normalized = fs::absolute(root).lexically_normal();
    return normalized.generic_string();
}

std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string normalize_ext(std::string ext) {
    ext = trim_copy(ext);
    if (ext.empty()) return ext;
    if (ext.front() != '.')
        ext.insert(ext.begin(), '.');
    return to_lower_copy(ext);
}

std::size_t clamp_limit(std::size_t value, std::size_t fallback, std::size_t max_value) {
    if (value == 0) value = fallback;
    return std::min(value, max_value);
}

bool glob_matches(const std::string& pattern, const std::string& text) {
    std::string rx = "^";
    rx.reserve(pattern.size() * 2 + 4);
    for (char ch : pattern) {
        switch (ch) {
            case '*': rx += ".*"; break;
            case '?': rx += "."; break;
            case '.': case '+': case '(': case ')': case '[': case ']':
            case '{': case '}': case '^': case '$': case '|': case '\\':
                rx.push_back('\\');
                rx.push_back(ch);
                break;
            default:
                rx.push_back(ch);
                break;
        }
    }
    rx += "$";
    try {
        return std::regex_match(text, std::regex(rx, std::regex::ECMAScript));
    } catch (...) {
        return false;
    }
}

std::string escape_regex(std::string text) {
    static const std::regex re(R"([.^$|()\\[*+?{\]])");
    return std::regex_replace(text, re, R"(\$&)");
}

std::vector<std::string> read_lines(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(f, line))
        lines.push_back(line);
    return lines;
}

std::string read_all(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string join_lines(const std::vector<std::string>& lines, std::size_t start, std::size_t end) {
    std::ostringstream ss;
    for (std::size_t i = start; i < end && i < lines.size(); ++i) {
        if (i > start) ss << '\n';
        ss << lines[i];
    }
    return ss.str();
}

struct ClassifiedOccurrence {
    std::string kind;
    bool is_definition = false;
};

std::optional<ClassifiedOccurrence> classify_occurrence(const std::string& line,
                                                        const std::string& symbol) {
    if (symbol.empty()) return std::nullopt;
    const std::string escaped = escape_regex(symbol);
    try {
        if (std::regex_search(line, std::regex("^\\s*#\\s*define\\s+" + escaped + "\\b"))) {
            return ClassifiedOccurrence{"macro", true};
        }
        std::smatch type_match;
        if (std::regex_search(line, type_match,
                              std::regex("\\b(class|struct|enum|namespace)\\s+" + escaped + "\\b"))) {
            return ClassifiedOccurrence{type_match[1].str(), true};
        }
        if (std::regex_search(line, std::regex("\\b(using\\s+" + escaped + "\\b|typedef\\b.*\\b" + escaped + "\\b)"))) {
            return ClassifiedOccurrence{"alias", true};
        }
        if (std::regex_search(line, std::regex("(^|[^.>\\w])" + escaped +
                                                  R"(\s*\([^;]*\)\s*(const)?\s*(\{|;|$)))"))) {
            return ClassifiedOccurrence{"function", true};
        }
        if (std::regex_search(line, std::regex("\\b" + escaped + "\\b"))) {
            return ClassifiedOccurrence{"identifier", false};
        }
    } catch (...) {
        return std::nullopt;
    }
    return std::nullopt;
}

bool token_matches(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    try {
        return std::regex_search(haystack, std::regex("\\b" + escape_regex(needle) + "\\b"));
    } catch (...) {
        return haystack.find(needle) != std::string::npos;
    }
}

}  // namespace

void SourceIndex::set_checkout_root(std::string root) {
    root = normalize_root(root);
    if (root == checkout_root_) return;
    checkout_root_ = std::move(root);
    invalidate();
}

void SourceIndex::set_bundled_root(std::string root) {
    root = normalize_root(root);
    if (root == bundled_root_) return;
    bundled_root_ = std::move(root);
    invalidate();
}

void SourceIndex::invalidate() {
    indexed_ = false;
    files_.clear();
    file_index_by_rel_path_.clear();
}

std::vector<SourceIndex::ActiveRoot> SourceIndex::active_roots() const {
    std::vector<ActiveRoot> roots;
    for (const auto& name : allowed_root_names()) {
        const fs::path checkout_path = checkout_root_.empty() ? fs::path() : (fs::path(checkout_root_) / name);
        const fs::path bundled_path = bundled_root_.empty() ? fs::path() : (fs::path(bundled_root_) / name);
        std::error_code ec;
        if (!checkout_root_.empty() && fs::is_directory(checkout_path, ec) && !ec) {
            roots.push_back({name, checkout_path.lexically_normal().generic_string(), "checkout"});
            continue;
        }
        ec.clear();
        if (!bundled_root_.empty() && fs::is_directory(bundled_path, ec) && !ec) {
            roots.push_back({name, bundled_path.lexically_normal().generic_string(), "bundle"});
        }
    }
    return roots;
}

void SourceIndex::ensure_indexed() const {
    if (indexed_) return;

    files_.clear();
    file_index_by_rel_path_.clear();

    for (const auto& root : active_roots()) {
        std::error_code ec;
        fs::recursive_directory_iterator iter(root.path, ec), end;
        for (; !ec && iter != end; iter.increment(ec)) {
            const fs::path path = iter->path();
            if (iter->is_directory(ec)) {
                if (should_skip_dir(path))
                    iter.disable_recursion_pending();
                continue;
            }
            if (!iter->is_regular_file(ec) || !is_text_like_extension(path))
                continue;

            std::error_code size_ec;
            const auto file_size = static_cast<std::size_t>(fs::file_size(path, size_ec));
            if (!size_ec && file_size > kMaxIndexedFileBytes)
                continue;

            IndexedFile entry;
            entry.root_name = root.name;
            entry.origin = root.origin;
            entry.rel_path = (fs::path(root.name) / fs::relative(path, root.path, ec)).generic_string();
            if (ec) {
                ec.clear();
                continue;
            }
            entry.abs_path = path.lexically_normal().generic_string();
            entry.lines = read_lines(entry.abs_path);
            if (entry.lines.empty() && file_size > 0)
                continue;
            file_index_by_rel_path_[entry.rel_path] = files_.size();
            files_.push_back(std::move(entry));
        }
    }

    indexed_ = true;
}

nlohmann::json SourceIndex::list_roots() const {
    nlohmann::json roots = nlohmann::json::array();
    const auto active = active_roots();
    for (const auto& name : allowed_root_names()) {
        nlohmann::json item = nlohmann::json::object();
        item["name"] = name;
        const fs::path checkout_path = checkout_root_.empty() ? fs::path() : (fs::path(checkout_root_) / name);
        const fs::path bundled_path = bundled_root_.empty() ? fs::path() : (fs::path(bundled_root_) / name);
        std::error_code ec;
        const bool checkout_available = !checkout_root_.empty() && fs::is_directory(checkout_path, ec) && !ec;
        ec.clear();
        const bool bundle_available = !bundled_root_.empty() && fs::is_directory(bundled_path, ec) && !ec;
        item["checkout_available"] = checkout_available;
        item["bundle_available"] = bundle_available;
        item["selected"] = false;
        for (const auto& selected : active) {
            if (selected.name == name) {
                item["selected"] = true;
                item["origin"] = selected.origin;
                item["path"] = selected.path;
                break;
            }
        }
        roots.push_back(std::move(item));
    }
    return roots;
}

nlohmann::json SourceIndex::search(const std::string& query,
                                   const std::vector<std::string>& roots,
                                   std::size_t limit,
                                   const std::vector<std::string>& file_types,
                                   const std::vector<std::string>& path_globs) const {
    ensure_indexed();

    const std::string trimmed_query = trim_copy(query);
    if (trimmed_query.empty())
        return nlohmann::json{{"ok", false}, {"error", "query must not be empty"}};

    std::unordered_set<std::string> root_filter;
    if (!roots.empty()) {
        for (const auto& root : roots) {
            if (!is_allowed_root_name(root))
                return nlohmann::json{{"ok", false}, {"error", "unknown source root: " + root}};
            root_filter.insert(root);
        }
    }

    std::unordered_set<std::string> ext_filter;
    for (const auto& ext : file_types) {
        const std::string normalized = normalize_ext(ext);
        if (!normalized.empty())
            ext_filter.insert(normalized);
    }

    const std::string lower_query = to_lower_copy(trimmed_query);
    nlohmann::json matches = nlohmann::json::array();
    const std::size_t capped_limit = clamp_limit(limit, 20, kMaxSearchLimit);

    for (const auto& file : files_) {
        if (!root_filter.empty() && !root_filter.count(file.root_name))
            continue;
        if (!ext_filter.empty()) {
            const std::string ext = to_lower_copy(fs::path(file.rel_path).extension().string());
            if (!ext_filter.count(ext))
                continue;
        }
        if (!path_globs.empty()) {
            bool any_glob = false;
            for (const auto& glob : path_globs) {
                if (glob_matches(glob, file.rel_path)) {
                    any_glob = true;
                    break;
                }
            }
            if (!any_glob)
                continue;
        }

        for (std::size_t i = 0; i < file.lines.size(); ++i) {
            const std::string lower_line = to_lower_copy(file.lines[i]);
            const auto pos = lower_line.find(lower_query);
            if (pos == std::string::npos)
                continue;

            std::string snippet = trim_copy(file.lines[i]);
            if (snippet.size() > kMaxSearchSnippet)
                snippet = snippet.substr(0, kMaxSearchSnippet) + "...";

            matches.push_back({
                {"path", file.rel_path},
                {"root", file.root_name},
                {"origin", file.origin},
                {"line", static_cast<int>(i + 1)},
                {"column", static_cast<int>(pos + 1)},
                {"match_kind", "text"},
                {"snippet", snippet},
            });
            if (matches.size() >= capped_limit) {
                const auto count = matches.size();
                return nlohmann::json{{"ok", true}, {"query", trimmed_query}, {"matches", std::move(matches)}, {"count", count}};
            }
        }
    }

    const auto count = matches.size();
    return nlohmann::json{{"ok", true}, {"query", trimmed_query}, {"matches", std::move(matches)}, {"count", count}};
}

nlohmann::json SourceIndex::read_file(const std::string& path, std::size_t max_bytes) const {
    ensure_indexed();

    fs::path rel(path);
    if (path.empty() || rel.is_absolute())
        return nlohmann::json{{"ok", false}, {"error", "path must be a repo-relative allowlisted path"}};

    rel = rel.lexically_normal();
    for (const auto& part : rel) {
        if (part == "..")
            return nlohmann::json{{"ok", false}, {"error", "path must not escape the allowlisted roots"}};
    }
    const std::string rel_path = rel.generic_string();
    const auto slash = rel_path.find('/');
    const std::string root_name = (slash == std::string::npos) ? rel_path : rel_path.substr(0, slash);
    if (!is_allowed_root_name(root_name))
        return nlohmann::json{{"ok", false}, {"error", "path root is not allowlisted: " + root_name}};

    auto it = file_index_by_rel_path_.find(rel_path);
    if (it == file_index_by_rel_path_.end())
        return nlohmann::json{{"ok", false}, {"error", "source file not found: " + rel_path}};

    const IndexedFile& file = files_[it->second];
    const std::string content = read_all(file.abs_path);
    const std::size_t capped_bytes = clamp_limit(max_bytes, 200000, kMaxReadFileBytes);
    const bool truncated = content.size() > capped_bytes;
    const std::string returned = truncated ? content.substr(0, capped_bytes) : content;

    return nlohmann::json{
        {"ok", true},
        {"path", file.rel_path},
        {"root", file.root_name},
        {"origin", file.origin},
        {"content", returned},
        {"truncated", truncated},
        {"bytes_returned", returned.size()},
        {"total_bytes", content.size()},
    };
}

nlohmann::json SourceIndex::read_span(const std::string& path, int start_line, int end_line) const {
    ensure_indexed();
    if (start_line <= 0 || end_line <= 0 || end_line < start_line)
        return nlohmann::json{{"ok", false}, {"error", "invalid line range"}};

    fs::path rel(path);
    if (path.empty() || rel.is_absolute())
        return nlohmann::json{{"ok", false}, {"error", "path must be a repo-relative allowlisted path"}};
    rel = rel.lexically_normal();
    for (const auto& part : rel) {
        if (part == "..")
            return nlohmann::json{{"ok", false}, {"error", "path must not escape the allowlisted roots"}};
    }
    const std::string rel_path = rel.generic_string();
    auto it = file_index_by_rel_path_.find(rel_path);
    if (it == file_index_by_rel_path_.end())
        return nlohmann::json{{"ok", false}, {"error", "source file not found: " + rel_path}};

    const IndexedFile& file = files_[it->second];
    const std::size_t start_idx = static_cast<std::size_t>(start_line - 1);
    const std::size_t end_idx = static_cast<std::size_t>(end_line);
    if (start_idx >= file.lines.size())
        return nlohmann::json{{"ok", false}, {"error", "start_line out of range"}};

    const std::size_t actual_end = std::min(end_idx, file.lines.size());
    nlohmann::json lines = nlohmann::json::array();
    for (std::size_t i = start_idx; i < actual_end; ++i) {
        lines.push_back({{"line", static_cast<int>(i + 1)}, {"text", file.lines[i]}});
    }

    return nlohmann::json{
        {"ok", true},
        {"path", file.rel_path},
        {"root", file.root_name},
        {"origin", file.origin},
        {"start_line", start_line},
        {"end_line", static_cast<int>(actual_end)},
        {"content", join_lines(file.lines, start_idx, actual_end)},
        {"lines", std::move(lines)},
    };
}

nlohmann::json SourceIndex::find_symbol(const std::string& name,
                                        const std::vector<std::string>& roots,
                                        std::size_t limit) const {
    ensure_indexed();
    const std::string trimmed = trim_copy(name);
    if (trimmed.empty())
        return nlohmann::json{{"ok", false}, {"error", "name must not be empty"}};

    std::unordered_set<std::string> root_filter;
    if (!roots.empty()) {
        for (const auto& root : roots) {
            if (!is_allowed_root_name(root))
                return nlohmann::json{{"ok", false}, {"error", "unknown source root: " + root}};
            root_filter.insert(root);
        }
    }

    nlohmann::json matches = nlohmann::json::array();
    const std::size_t capped_limit = clamp_limit(limit, 20, kMaxSearchLimit);

    auto append_matches = [&](bool definitions_only) {
        for (const auto& file : files_) {
            if (!root_filter.empty() && !root_filter.count(file.root_name))
                continue;
            for (std::size_t i = 0; i < file.lines.size(); ++i) {
                auto occ = classify_occurrence(file.lines[i], trimmed);
                if (!occ.has_value())
                    continue;
                if (definitions_only && !occ->is_definition)
                    continue;
                matches.push_back({
                    {"path", file.rel_path},
                    {"root", file.root_name},
                    {"origin", file.origin},
                    {"line", static_cast<int>(i + 1)},
                    {"kind", occ->kind},
                    {"is_definition", occ->is_definition},
                    {"snippet", trim_copy(file.lines[i])},
                });
                if (matches.size() >= capped_limit)
                    return true;
            }
        }
        return false;
    };

    if (!append_matches(true))
        append_matches(false);

    const auto count = matches.size();
    return nlohmann::json{{"ok", true}, {"name", trimmed}, {"matches", std::move(matches)}, {"count", count}};
}

nlohmann::json SourceIndex::find_references(const std::string& name,
                                            const std::vector<std::string>& roots,
                                            std::size_t limit) const {
    ensure_indexed();
    const std::string trimmed = trim_copy(name);
    if (trimmed.empty())
        return nlohmann::json{{"ok", false}, {"error", "name must not be empty"}};

    std::unordered_set<std::string> root_filter;
    if (!roots.empty()) {
        for (const auto& root : roots) {
            if (!is_allowed_root_name(root))
                return nlohmann::json{{"ok", false}, {"error", "unknown source root: " + root}};
            root_filter.insert(root);
        }
    }

    nlohmann::json matches = nlohmann::json::array();
    const std::size_t capped_limit = clamp_limit(limit, 50, kMaxReferenceLimit);

    for (const auto& file : files_) {
        if (!root_filter.empty() && !root_filter.count(file.root_name))
            continue;
        for (std::size_t i = 0; i < file.lines.size(); ++i) {
            if (!token_matches(file.lines[i], trimmed))
                continue;
            auto occ = classify_occurrence(file.lines[i], trimmed);
            matches.push_back({
                {"path", file.rel_path},
                {"root", file.root_name},
                {"origin", file.origin},
                {"line", static_cast<int>(i + 1)},
                {"kind", occ.has_value() ? occ->kind : "identifier"},
                {"is_definition", occ.has_value() ? occ->is_definition : false},
                {"snippet", trim_copy(file.lines[i])},
            });
            if (matches.size() >= capped_limit) {
                const auto count = matches.size();
                return nlohmann::json{{"ok", true}, {"name", trimmed}, {"matches", std::move(matches)}, {"count", count}};
            }
        }
    }

    const auto count = matches.size();
    return nlohmann::json{{"ok", true}, {"name", trimmed}, {"matches", std::move(matches)}, {"count", count}};
}

}  // namespace vivid
