#include "cli/project_recovery.h"

#include "packages/package_manifest.h"   // parse_package_manifest (package source-existence check)

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace vivid::recovery {

namespace fs = std::filesystem;

namespace {

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return s;
}

bool contains_ci(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return lower_copy(haystack).find(lower_copy(needle)) != std::string::npos;
}

}  // namespace

json recovery_action(const std::string& title, const std::string& detail) {
    return { {"title", title}, {"detail", detail} };
}

bool ident_looks_like_path(const std::string& id) {
    if (id.find('/') != std::string::npos) return true;
    const std::string lo = lower_copy(id);
    return lo.size() >= 5 && (lo.rfind(".clap") == lo.size() - 5 || lo.rfind(".vst3") == lo.size() - 5);
}

std::string saved_track_plugin_identity(const json& track) {
    if (track.value("kind", std::string("instrument")) == "audio") return std::string();
    if (track.contains("audio_instrument")) return std::string();   // built-in native op — always present
    if (track.contains("clap_instrument")) return track.value("clap_instrument", std::string());
    return track.value("instrument", track.value("name", std::string()));
}

json plugin_recovery_action(const std::string& id) {
    if (contains_ci(id, "surge"))
        return recovery_action("Install Surge XT",
            "Download from https://surge-synthesizer.github.io/ or run `brew install --cask surge-xt`, "
            "then relaunch Vivid so plugin discovery refreshes.");
    return recovery_action("Install a missing plugin",
        "Install '" + id + "', then quit and relaunch Vivid so plugin discovery refreshes.");
}

RecoveryReport analyze_saved_project(const json& saved_session, const std::string& project_dir,
                                     bool has_package, const PluginResolver& resolve) {
    RecoveryReport rep;
    std::set<std::string> action_titles;   // dedup: many tracks may need the same plugin
    auto add_action = [&](const json& a) {
        const std::string t = a.value("title", std::string());
        if (action_titles.insert(t).second) rep.next_actions.push_back(a);
    };

    // Plugin readiness: cross-reference the saved session's intended plugins against this machine. A
    // track whose plugin is missing loaded as a silent placeholder (persist.cpp) — invisible otherwise.
    for (const auto& track : saved_session.value("tracks", json::array())) {
        const std::string id = saved_track_plugin_identity(track);
        if (id.empty() || (resolve && resolve(id))) continue;
        rep.degraded = true;
        rep.issues.push_back({ {"level", "warn"}, {"issue", "plugin not installed"},
                               {"plugin", id},
                               {"track", track.value("name", track.value("instrument", std::string()))},
                               {"suggestion", "Install the plugin and relaunch Vivid so discovery refreshes; "
                                              "the track loaded silent."} });
        add_action(plugin_recovery_action(id));
    }

    // Package sources: a manifest naming an operator whose source file is gone will fail to
    // build/register (the node then shows up broken in the caller's visual-op pass). Name the missing
    // source here so the fix is unambiguous.
    if (has_package && !project_dir.empty()) {
        PackageManifest pm = parse_package_manifest(project_dir);
        if (!pm.ok) {
            rep.degraded = true;
            rep.issues.push_back({ {"level", "error"}, {"issue", "package manifest invalid"},
                                   {"detail", pm.error} });
        } else {
            std::error_code ec;
            for (const auto& op : pm.operators) {
                if (op.source.empty()) continue;
                if (fs::exists(fs::path(project_dir) / op.source, ec)) continue;
                rep.degraded = true;
                rep.issues.push_back({ {"level", "error"}, {"issue", "package operator source missing"},
                                       {"operator", op.name}, {"source", op.source} });
                add_action(recovery_action("Restore a package operator source",
                    "Operator '" + op.name + "' references missing source '" + op.source +
                    "'. Restore the file or update vivid-package.json, then call reload_operator_package."));
            }
        }
    }

    return rep;
}

}  // namespace vivid::recovery
