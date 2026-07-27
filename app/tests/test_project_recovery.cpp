// ADR-0040 Phase 3 (Fulfillment Gate #8): beginner-recovery diagnostics. Pure coverage of
// cli/project_recovery.h — the analysis that turns a project's silent load-time degradations (a
// track's plugin not installed, a package operator source missing) into named, recoverable issues
// with next_actions. Resolver-injected, so no plugin catalog / App / GPU is needed.
#include "cli/project_recovery.h"
#include "test_helpers.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>

namespace fs = std::filesystem;
namespace rec = vivid::recovery;
using json = nlohmann::json;

// True if any issue has field == value.
static bool has_issue(const json& issues, const std::string& field, const std::string& value) {
    for (const auto& i : issues)
        if (i.value(field, std::string()) == value) return true;
    return false;
}

static bool has_action(const json& actions, const std::string& title) {
    for (const auto& a : actions)
        if (a.value("title", std::string()) == title) return true;
    return false;
}

static int count_issue(const json& issues, const std::string& issue) {
    int n = 0;
    for (const auto& i : issues) if (i.value("issue", std::string()) == issue) ++n;
    return n;
}

int main() {
    // --- 1. saved_track_plugin_identity: what external plugin each saved track shape depends on. ---
    {
        CHECK(rec::saved_track_plugin_identity({ {"kind", "audio"} }).empty());                  // audio track
        CHECK(rec::saved_track_plugin_identity(
                  { {"kind", "instrument"}, {"audio_instrument", { {"op", "Sampler"} }} }).empty());  // native op
        CHECK(rec::saved_track_plugin_identity(
                  { {"kind", "instrument"}, {"clap_instrument", "/x/Surge XT.clap"} }) == "/x/Surge XT.clap");
        CHECK(rec::saved_track_plugin_identity(
                  { {"kind", "instrument"}, {"instrument", "Surge XT"} }) == "Surge XT");
        CHECK(rec::saved_track_plugin_identity(
                  { {"kind", "instrument"}, {"name", "Foo"} }) == "Foo");                         // name fallback
        // CLAP path wins over a label when both are present (matches the load branch order).
        CHECK(rec::saved_track_plugin_identity(
                  { {"clap_instrument", "/a.clap"}, {"instrument", "X"} }) == "/a.clap");
    }

    // --- 2. ident_looks_like_path: label vs on-disk path. ---
    {
        CHECK(!rec::ident_looks_like_path("Surge XT"));
        CHECK(rec::ident_looks_like_path("/Library/Audio/Plug-Ins/CLAP/Surge XT.clap"));
        CHECK(rec::ident_looks_like_path("Thing.vst3"));
        CHECK(rec::ident_looks_like_path("bar.clap"));
    }

    // --- 3. Plugin readiness against a resolver: only genuinely-unavailable plugins are flagged. ---
    {
        const std::set<std::string> present = { "TestTone" };
        auto resolve = [&](const std::string& id) { return present.count(id) > 0; };
        json session = { {"tracks", json::array({
            { {"kind", "instrument"}, {"instrument", "TestTone"} },              // present -> no issue
            { {"kind", "instrument"}, {"instrument", "Nonexistent Synth"} },     // missing
            { {"kind", "instrument"}, {"clap_instrument", "/nope/Surge XT.clap"} }, // missing (surge)
            { {"kind", "audio"} },                                              // no plugin
            { {"kind", "instrument"}, {"audio_instrument", { {"op", "Sampler"} }} }, // native
        })} };
        auto rep = rec::analyze_saved_project(session, "", /*has_package*/false, resolve);
        CHECK(rep.degraded);
        CHECK(count_issue(rep.issues, "plugin not installed") == 2);
        CHECK(has_issue(rep.issues, "plugin", "Nonexistent Synth"));
        CHECK(has_issue(rep.issues, "plugin", "/nope/Surge XT.clap"));
        CHECK(has_action(rep.next_actions, "Install a missing plugin"));   // generic
        CHECK(has_action(rep.next_actions, "Install Surge XT"));           // surge-specific hint
    }

    // --- 4. A healthy project reports nothing (no false positives). ---
    {
        auto resolve = [](const std::string&) { return true; };
        json session = { {"tracks", json::array({
            { {"kind", "instrument"}, {"instrument", "Surge XT"} },
            { {"kind", "audio"} },
        })} };
        auto rep = rec::analyze_saved_project(session, "", false, resolve);
        CHECK(!rep.degraded);
        CHECK(rep.issues.empty());
        CHECK(rep.next_actions.empty());
    }

    // --- 5. Package operator source missing on disk is named as a recoverable issue. ---
    {
        const fs::path dir = fs::temp_directory_path() / "vivid_recovery_test_pkg";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        std::ofstream(dir / "vivid-package.json")
            << R"({"name":"p","version":"0.1.0","operators":[{"name":"Ghost","source":"ghost.cpp","kind":"gpu_visual"}]})";
        // ghost.cpp deliberately absent.
        auto resolve = [](const std::string&) { return true; };
        auto rep = rec::analyze_saved_project(json::object(), dir.string(), /*has_package*/true, resolve);
        CHECK(rep.degraded);
        CHECK(has_issue(rep.issues, "issue", "package operator source missing"));
        CHECK(has_issue(rep.issues, "operator", "Ghost"));
        CHECK(has_action(rep.next_actions, "Restore a package operator source"));
        fs::remove_all(dir, ec);
    }

    // --- 6. A present package source produces no issue. ---
    {
        const fs::path dir = fs::temp_directory_path() / "vivid_recovery_test_pkg_ok";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
        std::ofstream(dir / "vivid-package.json")
            << R"({"name":"p","version":"0.1.0","operators":[{"name":"Real","source":"real.cpp","kind":"gpu_visual"}]})";
        std::ofstream(dir / "real.cpp") << "// present\n";
        auto resolve = [](const std::string&) { return true; };
        auto rep = rec::analyze_saved_project(json::object(), dir.string(), true, resolve);
        CHECK(!rep.degraded);
        CHECK(rep.issues.empty());
        fs::remove_all(dir, ec);
    }

    return vivid::test::summary("test_project_recovery");
}
