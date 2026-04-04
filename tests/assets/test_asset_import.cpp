#include "test_helpers.h"
#include "test_wav_helper.h"
#include "runtime/assets/asset_library.h"

#include <filesystem>
#include <fstream>

int main() {
    std::fprintf(stderr, "=== test_asset_import ===\n");
    namespace fs = std::filesystem;

    // --- Test 1: successful import ---
    {
        ScopedTempDir tmp("import_ok");
        fs::path source = tmp.path / "source_wave.wav";
        write_test_wav(source.string(), 2048 * 4);  // 4 frames

        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);

        auto result = lib.import_asset(vivid::AssetKind::Wavetable, source.string());
        check(result.ok, "import succeeded");
        check(!result.entry.asset_id.empty(), "asset_id assigned");
        check(result.entry.scope == vivid::AssetScope::Workspace, "scope is workspace");
        check(result.entry.display_name == "Source Wave", "display name derived");
        check(result.entry.file_format == "wav", "file format is wav");
        check(result.entry.kind_meta.is_object(), "kind metadata present");
        check(result.entry.kind_meta.value("frame_count", 0u) == 4, "frame count is 4");
        check(!result.entry.source_hash.empty(), "source hash computed");
        check(!result.entry.imported_at.empty(), "import timestamp set");

        // Verify file was copied
        check(fs::exists(result.entry.canonical_path), "file copied to workspace");

        // Verify sidecar exists
        fs::path asset_dir = workspace / "assets" / "library" / "wavetables" / result.entry.asset_id;
        check(fs::exists(asset_dir / "asset.json"), "sidecar written");

        // Verify cache dir created
        fs::path cache_dir = workspace / "assets" / "library" / ".cache" / "wavetables" / result.entry.asset_id;
        check(fs::is_directory(cache_dir), "cache directory created");

        // Verify in-memory index
        check(lib.size() == 1, "library has 1 entry");
        check(lib.find(result.entry.asset_id) != nullptr, "findable by asset_id");
    }

    // --- Test 2: idempotent import (same file again) ---
    {
        ScopedTempDir tmp("import_idempotent");
        fs::path source = tmp.path / "wave.wav";
        write_test_wav(source.string(), 2048 * 2);

        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);

        auto r1 = lib.import_asset(vivid::AssetKind::Wavetable, source.string());
        auto r2 = lib.import_asset(vivid::AssetKind::Wavetable, source.string());
        check(r1.ok && r2.ok, "both imports succeed");
        check(r1.entry.asset_id == r2.entry.asset_id, "same asset_id for same file");
        check(lib.size() == 1, "still only 1 entry (idempotent)");
    }

    // --- Test 3: different paths with same basename remain distinct assets ---
    {
        ScopedTempDir tmp("import_same_basename");
        fs::path source_a = tmp.path / "a" / "shared.wav";
        fs::path source_b = tmp.path / "b" / "shared.wav";
        fs::create_directories(source_a.parent_path());
        fs::create_directories(source_b.parent_path());
        write_test_wav(source_a.string(), 2048);
        write_test_wav(source_b.string(), 2048 * 2);

        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);

        auto r1 = lib.import_asset(vivid::AssetKind::Wavetable, source_a.string());
        auto r2 = lib.import_asset(vivid::AssetKind::Wavetable, source_b.string());
        check(r1.ok && r2.ok, "same-basename imports succeed");
        check(r1.entry.asset_id != r2.entry.asset_id, "same-basename different paths stay distinct");
        check(lib.size() == 2, "two workspace assets retained");
    }

    // --- Test 4: import nonexistent file ---
    {
        ScopedTempDir tmp("import_missing");
        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);

        auto result = lib.import_asset(vivid::AssetKind::Wavetable, "/nonexistent/file.wav");
        check(!result.ok, "import of nonexistent file fails");
        check(!result.error.empty(), "error message provided");
    }

    // --- Test 5: import non-WAV file ---
    {
        ScopedTempDir tmp("import_bad_format");
        fs::path bad = tmp.path / "not_a_wav.txt";
        { std::ofstream ofs(bad); ofs << "hello"; }

        fs::path workspace = tmp.path / "workspace";
        fs::create_directories(workspace);

        vivid::AssetLibrary lib;
        lib.set_workspace_root(workspace);

        auto result = lib.import_asset(vivid::AssetKind::Wavetable, bad.string());
        check(!result.ok, "import of non-WAV fails");
    }

    // --- Test 6: import without workspace root ---
    {
        vivid::AssetLibrary lib;
        auto result = lib.import_asset(vivid::AssetKind::Wavetable, "/some/file.wav");
        check(!result.ok, "import fails without workspace root");
    }

    std::fprintf(stderr, "\n%d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
