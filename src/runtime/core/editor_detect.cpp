#include "runtime/core/editor_detect.h"
#include <filesystem>

namespace vivid {

std::vector<DetectedEditor> detect_editors() {
    std::vector<DetectedEditor> editors;

    // Always first: system default
    editors.push_back({"System Default", ""});

    // Known editors: {app bundle name, display name}
    struct KnownEditor { const char* app_name; const char* display_name; };
    static const KnownEditor known[] = {
        {"Visual Studio Code.app", "Visual Studio Code"},
        {"Sublime Text.app",      "Sublime Text"},
        {"Zed.app",               "Zed"},
        {"BBEdit.app",            "BBEdit"},
        {"Nova.app",              "Nova"},
        {"TextEdit.app",          "TextEdit"},
    };

    const char* search_dirs[] = { "/Applications", "/System/Applications" };

    for (const auto& ed : known) {
        for (const char* dir : search_dirs) {
            std::string path = std::string(dir) + "/" + ed.app_name;
            if (std::filesystem::exists(path)) {
                editors.push_back({ed.display_name, ed.display_name});
                break;
            }
        }
    }

    // Always last: custom command
    editors.push_back({"Custom Command...", "custom"});

    return editors;
}

} // namespace vivid
