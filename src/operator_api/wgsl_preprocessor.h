#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace vivid {

struct WgslPreprocessResult {
    bool ok = false;
    std::string output;
    std::string error;
};

namespace detail {

inline bool parse_include_directive(const std::string& line, std::string& include_path) {
    // Supports:
    //   // @include "foo.wgsl"
    //   // @include 'foo.wgsl'
    std::string s = line;
    auto non_ws = s.find_first_not_of(" \t");
    if (non_ws == std::string::npos) return false;
    s = s.substr(non_ws);
    if (s.rfind("//", 0) != 0) return false;
    s = s.substr(2);
    non_ws = s.find_first_not_of(" \t");
    if (non_ws == std::string::npos) return false;
    s = s.substr(non_ws);
    const std::string kTag = "@include";
    if (s.rfind(kTag, 0) != 0) return false;
    s = s.substr(kTag.size());
    non_ws = s.find_first_not_of(" \t");
    if (non_ws == std::string::npos) return false;
    s = s.substr(non_ws);
    if (s.size() < 3) return false;
    char q = s[0];
    if (q != '"' && q != '\'') return false;
    auto end = s.find(q, 1);
    if (end == std::string::npos || end <= 1) return false;
    include_path = s.substr(1, end - 1);
    return !include_path.empty();
}

inline bool read_file_text(const std::filesystem::path& path, std::string& out) {
    std::ifstream ifs(path);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    out = ss.str();
    return true;
}

inline std::string format_chain(const std::vector<std::filesystem::path>& include_stack) {
    std::ostringstream ss;
    for (size_t i = 0; i < include_stack.size(); ++i) {
        ss << include_stack[i].string();
        if (i + 1 < include_stack.size()) ss << " -> ";
    }
    return ss.str();
}

inline bool preprocess_file_recursive(
    const std::filesystem::path& file_path,
    std::unordered_set<std::string>& active,
    std::vector<std::filesystem::path>& include_stack,
    std::ostringstream& out,
    std::string& error
) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(file_path, ec);
    if (ec || canonical.empty()) canonical = file_path;
    const std::string key = canonical.string();

    if (active.find(key) != active.end()) {
        include_stack.push_back(canonical);
        error = "WGSL include cycle detected: " + format_chain(include_stack);
        include_stack.pop_back();
        return false;
    }

    std::string content;
    if (!read_file_text(canonical, content)) {
        error = "WGSL include not found: " + canonical.string();
        if (!include_stack.empty()) {
            error += " (include chain: " + format_chain(include_stack) + ")";
        }
        return false;
    }

    active.insert(key);
    include_stack.push_back(canonical);

    std::istringstream input(content);
    std::string line;
    int line_no = 0;
    while (std::getline(input, line)) {
        line_no++;
        std::string include_rel;
        if (!parse_include_directive(line, include_rel)) {
            out << line << '\n';
            continue;
        }

        std::filesystem::path include_path = canonical.parent_path() / include_rel;
        if (!std::filesystem::exists(include_path)) {
            // Compatibility: allow lib/ fallback for shared shader snippets.
            include_path = canonical.parent_path() / "lib" / include_rel;
        }
        out << "// ---- begin include: " << include_rel << " ----\n";
        if (!preprocess_file_recursive(include_path, active, include_stack, out, error)) {
            error += " [from " + canonical.string() + ":" + std::to_string(line_no) + "]";
            active.erase(key);
            include_stack.pop_back();
            return false;
        }
        out << "// ---- end include: " << include_rel << " ----\n";
    }

    active.erase(key);
    include_stack.pop_back();
    return true;
}

} // namespace detail

inline WgslPreprocessResult preprocess_wgsl_file(const std::filesystem::path& file_path) {
    WgslPreprocessResult result;
    std::unordered_set<std::string> active;
    std::vector<std::filesystem::path> include_stack;
    std::ostringstream out;
    if (!detail::preprocess_file_recursive(file_path, active, include_stack, out, result.error)) {
        result.ok = false;
        return result;
    }
    result.ok = true;
    result.output = out.str();
    return result;
}

} // namespace vivid
