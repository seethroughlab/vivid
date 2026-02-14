// Project Manifest — Machine-readable project metadata (vivid-project.json)

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace vivid {

/// A parameter description from vivid-project.json
struct ManifestParam {
    std::string name;         // e.g. "noise.scale"
    std::string description;
    float min = 0.0f;
    float max = 1.0f;
    float defaultValue = 0.0f;
};

/// Preview/export defaults from vivid-project.json
struct ManifestPreview {
    float duration = 0.0f;    // seconds
    float fps = 60.0f;
    int width = 0, height = 0;
    std::string script;       // path to default playback script (relative to project)
};

/// Parsed vivid-project.json
struct ProjectManifest {
    std::string name;
    std::string chain = "chain.cpp";
    ManifestPreview preview;
    std::vector<ManifestParam> params;
    std::string assertions;   // path to assertion file
    bool loaded = false;      // true if file was found and parsed
};

/// Load vivid-project.json from a project directory.
/// Returns a manifest with loaded=false if the file doesn't exist (not an error).
/// Returns a manifest with loaded=false and sets error if parsing fails.
ProjectManifest loadProjectManifest(const std::filesystem::path& projectDir,
                                    std::string* error = nullptr);

} // namespace vivid
