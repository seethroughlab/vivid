#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

bool contains_any(const std::string& content, const std::vector<std::string>& needles,
                  std::vector<std::string>* found) {
    bool hit = false;
    for (const auto& needle : needles) {
        if (content.find(needle) != std::string::npos) {
            hit = true;
            if (found) found->push_back(needle);
        }
    }
    return hit;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_ui_localization_guard <source_dir>\n");
        return 1;
    }

    const std::filesystem::path root = argv[1];

    struct ForbiddenFilePatterns {
        std::string rel_path;
        std::vector<std::string> forbidden;
    };

    const std::vector<ForbiddenFilePatterns> checks = {
        {"src/ui/dialogs/dialog_manager.cpp", {
            "error.empty() ? \"Package action failed\"",
            "pkg_browser.action_error_display = \"Build failed - see Build Console\"",
        }},
        {"src/ui/dialogs/dialog_manager_input.cpp", {
            "example_browser.action_error = \"Opening anyway with missing package: \" + missing;",
            "\"Missing package: \" + missing + \" (press Enter again to open anyway)\"",
            "\"Missing package: \" + missing + \" (click Open again to continue)\"",
            "graph_meta.error = err.empty() ? \"Failed to save meta\" : err;",
            "graph_meta.error = \"Preview controls must reference a valid node/param pair\";",
            "pkg_browser.action_error = \"Failed to unlink \" + entry.name;",
            "pkg_browser.action_error = \"Failed to uninstall \" + entry.name;",
            "pkg_browser.action_error = \"Failed to install \" + entry.name;",
            "? \"Failed to rebuild \" + entry.name",
        }},
        {"src/ui/graph/node_graph_draw.cpp", {
            "const char* label = \"MISSING\";",
            "? \"try rebuild\"",
            "? \"ABI mismatch\"",
            ": \"not installed\";",
        }},
        {"src/ui/graph/node_graph_draw_inspector.cpp", {
            "(\"ERROR: \" + sel_node->error_message)",
        }},
        {"src/ui/graph/node_graph_draw_inspector_sections.cpp", {
            "? \"(none)\" : preset_display.c_str();",
            "current_preset.empty() ? \"(none)\" : current_preset.c_str();",
            "std::string header_label = \"State \" + std::to_string(si);",
            "tr.draw_text(px + 8.0f, py + 3.0f, \"+ assignment\",",
        }},
        {"src/ui/graph/node_graph_input_click_widgets.cpp", {
            "dropdown_labels.push_back(\"(none)\")",
            "PresetMenuNode{\"(none)\", \"\", false, false, {}}",
        }},
        {"src/ui/graph/node_graph_draw_overlays.cpp", {
            "\"Delete \" + std::to_string(selected_node_ids_.size()) + \" Nodes\"",
        }},
        {"src/ui/dialogs/dialog_manager_draw.cpp", {
            "\"Version \" VIVID_CORE_VERSION",
        }},
    };

    std::vector<std::string> offenders;

    for (const auto& check_file : checks) {
        const auto path = root / check_file.rel_path;
        const std::string content = read_file(path);
        std::vector<std::string> found;
        if (contains_any(content, check_file.forbidden, &found)) {
            for (const auto& needle : found) {
                offenders.push_back(check_file.rel_path + " -> " + needle);
            }
        }
    }

    const auto menu_path = root / "src/runtime/platform/macos_menu.mm";
    const std::string menu_content = read_file(menu_path);
    const std::regex title_re(R"(initWithTitle:@\"([A-Za-z][^\"]*)\")");
    for (std::sregex_iterator it(menu_content.begin(), menu_content.end(), title_re), end; it != end; ++it) {
        const std::string title = (*it)[1].str();
        if (title == "Services") continue;
        offenders.push_back("src/runtime/platform/macos_menu.mm -> raw menu title: " + title);
    }

    if (!offenders.empty()) {
        std::fprintf(stderr, "Found non-localized desktop UI strings that should route through i18n:\n");
        for (const auto& offender : offenders) {
            std::fprintf(stderr, "  %s\n", offender.c_str());
        }
    }

    check(offenders.empty(), "Desktop UI string regressions stay routed through i18n (allowing only explicit non-localized exceptions)");

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
