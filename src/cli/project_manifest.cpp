// Project Manifest — JSON parsing for vivid-project.json

#include <vivid/project_manifest.h>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace vivid {

ProjectManifest loadProjectManifest(const fs::path& projectDir, std::string* error) {
    ProjectManifest manifest;

    fs::path path = projectDir / "vivid-project.json";
    if (!fs::exists(path)) {
        return manifest;  // loaded=false, no error — file is optional
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        if (error) *error = "Cannot open vivid-project.json";
        return manifest;
    }

    json j;
    try {
        j = json::parse(file);
    } catch (const json::parse_error& e) {
        if (error) *error = "JSON parse error in vivid-project.json: " + std::string(e.what());
        return manifest;
    }

    // Name
    if (j.contains("name") && j["name"].is_string()) {
        manifest.name = j["name"].get<std::string>();
    }

    // Chain entry point
    if (j.contains("chain") && j["chain"].is_string()) {
        manifest.chain = j["chain"].get<std::string>();
    }

    // Preview defaults
    if (j.contains("preview") && j["preview"].is_object()) {
        auto& p = j["preview"];
        if (p.contains("duration") && p["duration"].is_number())
            manifest.preview.duration = p["duration"].get<float>();
        if (p.contains("fps") && p["fps"].is_number())
            manifest.preview.fps = p["fps"].get<float>();
        if (p.contains("resolution") && p["resolution"].is_array() && p["resolution"].size() == 2) {
            manifest.preview.width = p["resolution"][0].get<int>();
            manifest.preview.height = p["resolution"][1].get<int>();
        }
        if (p.contains("script") && p["script"].is_string())
            manifest.preview.script = p["script"].get<std::string>();
    }

    // Params
    if (j.contains("params") && j["params"].is_array()) {
        for (const auto& p : j["params"]) {
            ManifestParam param;
            if (p.contains("name") && p["name"].is_string())
                param.name = p["name"].get<std::string>();
            if (p.contains("description") && p["description"].is_string())
                param.description = p["description"].get<std::string>();
            if (p.contains("min") && p["min"].is_number())
                param.min = p["min"].get<float>();
            if (p.contains("max") && p["max"].is_number())
                param.max = p["max"].get<float>();
            if (p.contains("default") && p["default"].is_number())
                param.defaultValue = p["default"].get<float>();
            manifest.params.push_back(param);
        }
    }

    // Assertions
    if (j.contains("assertions") && j["assertions"].is_string()) {
        manifest.assertions = j["assertions"].get<std::string>();
    }

    manifest.loaded = true;
    return manifest;
}

} // namespace vivid
