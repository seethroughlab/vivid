#include "test_helpers.h"
#include "test_wav_helper.h"
#include "runtime/assets/asset_library.h"

#include <filesystem>
#include <fstream>

int main() {
    std::fprintf(stderr, "=== test_asset_index ===\n");
    namespace fs = std::filesystem;

    // --- Test 1: round-trip import → restart → rediscover ---
    {
        ScopedTempDir tmp("index_roundtrip");
        fs::path source = tmp.path / "roundtrip.wav";
        write_test_wav(source.string(), 2048 * 3);

        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        std::string saved_id;

        // First "session": import
        {
            vivid::AssetLibrary lib;
            lib.set_workspace_root(workspace);
            auto result = lib.import_asset(vivid::AssetKind::Wavetable, source.string());
            check(result.ok, "initial import succeeded");
            saved_id = result.entry.asset_id;
        }

        // Second "session": discover from workspace
        {
            vivid::AssetLibrary lib;
            lib.set_workspace_root(workspace);
            lib.discover_workspace_assets(workspace);

            auto entries = lib.list();
            check(entries.size() == 1, "rediscovered 1 workspace entry after restart");
            if (!entries.empty()) {
                check(entries[0].asset_id == saved_id, "asset_id survives restart");
                check(entries[0].scope == vivid::AssetScope::Workspace, "scope is workspace");
                check(entries[0].kind_meta.is_object(), "kind metadata survives restart");
                check(entries[0].kind_meta.value("frame_count", 0u) == 3,
                      "frame count survives restart");
            }
        }
    }

    // --- Test 2: merged listing with scope filter ---
    {
        ScopedTempDir tmp("index_merged");
        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        // Create package assets
        fs::path pkg_dir = tmp.path / "pkg";
        fs::path wt_dir = pkg_dir / "assets" / "wavetables";
        fs::create_directories(wt_dir);
        write_test_wav((wt_dir / "factory.wav").string(), 2048);

        // Import a workspace asset
        fs::path source = tmp.path / "user.wav";
        write_test_wav(source.string(), 2048 * 2);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);
        lib.discover_package_assets("test-pkg", pkg_dir.string(),
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});
        lib.import_asset(vivid::AssetKind::Wavetable, source.string());

        auto all = lib.list();
        check(all.size() == 2, "merged list has 2 entries");

        auto pkg_only = lib.list(vivid::AssetKind::Wavetable, vivid::AssetScope::Package);
        check(pkg_only.size() == 1, "package filter returns 1");
        check(pkg_only[0].scope == vivid::AssetScope::Package, "filtered entry is package");

        auto ws_only = lib.list(vivid::AssetKind::Wavetable, vivid::AssetScope::Workspace);
        check(ws_only.size() == 1, "workspace filter returns 1");
        check(ws_only[0].scope == vivid::AssetScope::Workspace, "filtered entry is workspace");
    }

    // --- Test 3: find by asset_id ---
    {
        ScopedTempDir tmp("index_find");
        fs::path wt_dir = tmp.path / "assets" / "wavetables";
        fs::create_directories(wt_dir);
        write_test_wav((wt_dir / "findme.wav").string(), 2048);

        vivid::AssetLibrary lib;
        lib.discover_package_assets("pkg", tmp.str(),
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});

        auto entries = lib.list();
        check(!entries.empty(), "has entries");
        if (!entries.empty()) {
            const auto* found = lib.find(entries[0].asset_id);
            check(found != nullptr, "found by asset_id");
            check(found->display_name == entries[0].display_name, "found entry matches");
        }

        check(lib.find("nonexistent_id") == nullptr, "missing id returns nullptr");
    }

    // --- Test 4: refresh reloads workspace assets ---
    {
        ScopedTempDir tmp("index_refresh");
        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        fs::path pkg_dir = tmp.path / "pkg";
        fs::path wt_dir = pkg_dir / "assets" / "wavetables";
        fs::create_directories(wt_dir);
        write_test_wav((wt_dir / "factory.wav").string(), 2048);

        fs::path source = tmp.path / "refresh.wav";
        write_test_wav(source.string(), 2048);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);
        lib.discover_package_assets("refresh-pkg", pkg_dir.string(),
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});
        lib.import_asset(vivid::AssetKind::Wavetable, source.string());
        check(lib.size() == 2, "merged package + workspace entries before refresh");

        lib.refresh();
        check(lib.size() == 2, "merged package + workspace entries after refresh");
        check(lib.list(vivid::AssetKind::Wavetable, vivid::AssetScope::Package).size() == 1,
              "refresh rebuilds package entries");
        check(lib.list(vivid::AssetKind::Wavetable, vivid::AssetScope::Workspace).size() == 1,
              "refresh preserves workspace entries");
    }

    std::fprintf(stderr, "\n%d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
