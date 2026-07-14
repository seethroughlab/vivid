// Storage half of node presets (no NodeGraph dependency, so it links into the headless test).
// capture()/apply() live in node_presets_graph.cpp.
#include "app/node_presets.h"

#include "platform/platform.h"      // user_data_dir / executable_path

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vivid::node_presets {

namespace {

// A preset name is used as a filename, so keep it to a safe, portable set — no separators, no dots
// leading a traversal. Returns "" when nothing usable remains.
std::string sanitize(const std::string& name) {
    std::string out;
    for (char c : name)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' || c == '-') out += c;
    // trim surrounding spaces
    size_t a = out.find_first_not_of(' '), b = out.find_last_not_of(' ');
    return (a == std::string::npos) ? std::string() : out.substr(a, b - a + 1);
}

// Factory (read-only) preset dirs for an op type, in the same bundle/non-bundle convention as
// the shader + example search paths.
std::vector<std::string> factory_dirs(const std::string& op_type) {
    std::vector<std::string> dirs;
    const std::string exe = platform::executable_path();
    if (!exe.empty()) {
        const fs::path ed = fs::path(exe).parent_path();
        dirs.push_back((ed / ".." / "Resources" / "presets" / op_type).lexically_normal().string());
        dirs.push_back((ed / "presets" / op_type).lexically_normal().string());
    }
    return dirs;
}

}  // namespace

std::string user_presets_dir(const std::string& op_type) {
    const std::string base = platform::user_data_dir();
    if (base.empty() || op_type.empty()) return {};
    fs::path d = fs::path(base) / "presets" / op_type;
    std::error_code ec;
    fs::create_directories(d, ec);
    return ec ? std::string() : d.string();
}

std::vector<PresetInfo> list(const std::string& op_type) {
    std::vector<PresetInfo> out;
    std::error_code ec;
    auto scan = [&](const std::string& dir, bool factory) {
        if (dir.empty() || !fs::is_directory(dir, ec)) return;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!e.is_regular_file(ec) || e.path().extension() != ".json") continue;
            const std::string name = e.path().stem().string();
            if (std::any_of(out.begin(), out.end(), [&](const PresetInfo& p) { return p.name == name; }))
                continue;   // a user preset already shadows this factory name
            out.push_back({ name, e.path().string(), factory });
        }
    };
    scan(user_presets_dir(op_type), false);        // user first, so it shadows factory
    for (const auto& d : factory_dirs(op_type)) scan(d, true);
    std::sort(out.begin(), out.end(), [](const PresetInfo& a, const PresetInfo& b) { return a.name < b.name; });
    return out;
}

std::string save(const std::string& op_type, const std::string& name,
                 const nlohmann::json& data, std::string& err) {
    const std::string clean = sanitize(name);
    if (clean.empty()) { err = "invalid preset name"; return {}; }
    const std::string dir = user_presets_dir(op_type);
    if (dir.empty()) { err = "could not create the presets directory"; return {}; }
    const fs::path path = fs::path(dir) / (clean + ".json");
    std::ofstream os(path);
    if (!os) { err = "could not write " + path.string(); return {}; }
    nlohmann::json doc = data;
    doc["name"] = clean;
    doc["op_type"] = op_type;
    os << doc.dump(2);
    return path.string();
}

nlohmann::json load(const std::string& op_type, const std::string& name) {
    const std::string clean = sanitize(name);
    if (clean.empty()) return nullptr;
    std::vector<std::string> dirs = { user_presets_dir(op_type) };
    for (const auto& d : factory_dirs(op_type)) dirs.push_back(d);
    std::error_code ec;
    for (const auto& d : dirs) {
        if (d.empty()) continue;
        const fs::path p = fs::path(d) / (clean + ".json");
        if (!fs::exists(p, ec)) continue;
        std::ifstream is(p);
        if (!is) continue;
        try { nlohmann::json j; is >> j; return j; } catch (...) { return nullptr; }
    }
    return nullptr;
}

bool remove(const std::string& op_type, const std::string& name) {
    const std::string clean = sanitize(name);
    if (clean.empty()) return false;
    const std::string dir = user_presets_dir(op_type);
    if (dir.empty()) return false;
    std::error_code ec;
    return fs::remove(fs::path(dir) / (clean + ".json"), ec);
}

}  // namespace vivid::node_presets
