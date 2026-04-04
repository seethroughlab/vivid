#include "test_helpers.h"
#include "test_wav_helper.h"
#include "runtime/assets/asset_library.h"

#include <filesystem>
#include <fstream>

int main() {
    std::fprintf(stderr, "=== test_asset_discovery ===\n");
    namespace fs = std::filesystem;

    // --- Test 1: discover package assets from declared directory ---
    {
        ScopedTempDir tmp("discover_pkg");
        fs::path wt_dir = tmp.path / "assets" / "wavetables";
        fs::create_directories(wt_dir);

        // Create two WAV files
        write_test_wav((wt_dir / "warm_pad.wav").string(), 2048 * 4);
        write_test_wav((wt_dir / "bright_lead.wav").string(), 2048 * 2);

        // Create a non-WAV file (should be ignored)
        { std::ofstream ofs(wt_dir / "readme.txt"); ofs << "not a wav"; }

        vivid::AssetLibrary lib;
        lib.discover_package_assets("test-pkg", tmp.str(),
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});

        auto entries = lib.list();
        check(entries.size() == 2, "discovered 2 package WAV assets");

        bool found_warm = false, found_bright = false;
        for (const auto& e : entries) {
            check(e.scope == vivid::AssetScope::Package, "scope is Package");
            check(e.package_name == "test-pkg", "package name correct");
            check(!e.asset_id.empty(), "asset_id generated");
            check(!e.source_hash.empty(), "source hash computed");
            check(e.kind_meta.is_object(), "kind metadata present");
            if (e.display_name == "Warm Pad") found_warm = true;
            if (e.display_name == "Bright Lead") found_bright = true;
        }
        check(found_warm, "found Warm Pad asset");
        check(found_bright, "found Bright Lead asset");
    }

    // --- Test 2: conventional fallback (no manifest, but assets/wavetables/ exists) ---
    {
        ScopedTempDir tmp("discover_conventional");
        fs::path conv_dir = tmp.path / "assets" / "wavetables";
        fs::create_directories(conv_dir);
        write_test_wav((conv_dir / "test.wav").string(), 2048);

        vivid::AssetLibrary lib;
        // Simulate the conventional fallback by passing the dir directly
        lib.discover_package_assets("conv-pkg", tmp.str(),
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});

        auto entries = lib.list();
        check(entries.size() == 1, "conventional dir discovered 1 asset");
    }

    // --- Test 3: nonexistent directory (no crash, no entries) ---
    {
        vivid::AssetLibrary lib;
        lib.discover_package_assets("empty-pkg", "/nonexistent/path",
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});
        check(lib.list().empty(), "nonexistent dir produces no entries");
    }

    // --- Test 4: workspace asset discovery from sidecars ---
    {
        ScopedTempDir tmp("discover_workspace");
        fs::path lib_root = tmp.path / "assets" / "library" / "wavetables" / "wt_test123";
        fs::path source_dir = lib_root / "source";
        fs::create_directories(source_dir);

        write_test_wav((source_dir / "my_wave.wav").string(), 2048 * 2);

        // Write sidecar
        std::ofstream ofs(lib_root / "asset.json");
        ofs << R"({
            "asset_id": "wt_test123",
            "kind": "wavetable",
            "display_name": "My Wave",
            "source_file": "my_wave.wav",
            "source_hash": "fnv1a:0x1234567890abcdef",
            "imported_at": "2026-04-04T00:00:00Z",
            "file_size": 16432,
            "file_format": "wav",
            "kind_meta": {
                "sample_rate": 44100,
                "channels": 1,
                "frame_count": 2,
                "samples_per_frame": 2048,
                "total_samples": 4096,
                "peak_amplitude": 0.8
            }
        })";
        ofs.close();

        vivid::AssetLibrary lib;
        lib.discover_workspace_assets(tmp.path);

        auto entries = lib.list();
        check(entries.size() == 1, "discovered 1 workspace asset from sidecar");
        if (!entries.empty()) {
            check(entries[0].asset_id == "wt_test123", "asset_id from sidecar");
            check(entries[0].scope == vivid::AssetScope::Workspace, "scope is Workspace");
            check(entries[0].display_name == "My Wave", "display name from sidecar");
            check(entries[0].kind_meta.is_object(), "kind metadata from sidecar");
            check(entries[0].kind_meta.value("frame_count", 0u) == 2,
                  "frame count from sidecar");
        }
    }

    // --- Test 5: clear_package_assets preserves workspace entries ---
    {
        ScopedTempDir tmp("clear_pkg");
        fs::path wt_dir = tmp.path / "assets" / "wavetables";
        fs::create_directories(wt_dir);
        write_test_wav((wt_dir / "pkg.wav").string(), 2048);

        fs::path ws_root = tmp.path / "workspace";
        fs::path ws_lib = ws_root / "assets" / "library" / "wavetables" / "wt_ws1";
        fs::create_directories(ws_lib / "source");
        write_test_wav((ws_lib / "source" / "user.wav").string(), 2048);
        { std::ofstream ofs(ws_lib / "asset.json");
          ofs << R"({"asset_id":"wt_ws1","kind":"wavetable","display_name":"User","source_file":"user.wav","source_hash":"x","imported_at":"","file_size":0,"file_format":"wav","kind_meta":{"sample_rate":44100,"channels":1,"frame_count":1,"samples_per_frame":2048,"total_samples":2048,"peak_amplitude":0.5}})"; }

        vivid::AssetLibrary lib;
        lib.discover_package_assets("pkg", tmp.str(),
                                    vivid::AssetKind::Wavetable, {"assets/wavetables"});
        lib.discover_workspace_assets(ws_root);
        check(lib.list().size() == 2, "2 total entries before clear");

        lib.clear_package_assets();
        auto remaining = lib.list();
        check(remaining.size() == 1, "1 entry after clearing package assets");
        check(remaining[0].scope == vivid::AssetScope::Workspace, "remaining entry is workspace");
    }

    std::fprintf(stderr, "\n%d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
