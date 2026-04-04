#include "test_helpers.h"
#include "test_wav_helper.h"
#include "runtime/assets/asset_library_internal.h"

#include <filesystem>
#include <fstream>

static const vivid::AssetKindHandler* wavetable_handler(const vivid::AssetLibrary& lib) {
    return lib.kind_registry().find(vivid::AssetKind::Wavetable);
}

int main() {
    std::fprintf(stderr, "=== test_asset_metadata ===\n");
    namespace fs = std::filesystem;

    // --- Test 1: probe valid wavetable WAV ---
    {
        ScopedTempDir tmp("meta_valid");
        fs::path wav_path = tmp.path / "test.wav";
        uint32_t num_samples = 2048 * 4;  // 4 wavetable frames
        write_test_wav(wav_path.string(), num_samples, 44100, 1);

        vivid::WavetableAssetMeta meta;
        bool ok = vivid::asset_internal::probe_wavetable_metadata(wav_path.string(), meta);
        check(ok, "probe succeeded");
        check(meta.sample_rate == 44100, "sample rate is 44100");
        check(meta.channels == 1, "channels is 1");
        check(meta.frame_count == 4, "frame count is 4");
        check(meta.samples_per_frame == 2048, "samples_per_frame is 2048");
        check(meta.total_samples == num_samples, "total_samples correct");
        check(meta.peak_amplitude > 0.0f, "peak amplitude > 0");
        check(meta.peak_amplitude <= 1.0f, "peak amplitude <= 1");
    }

    // --- Test 2: probe stereo WAV ---
    {
        ScopedTempDir tmp("meta_stereo");
        fs::path wav_path = tmp.path / "stereo.wav";
        write_test_wav(wav_path.string(), 2048 * 2, 48000, 2);

        vivid::WavetableAssetMeta meta;
        bool ok = vivid::asset_internal::probe_wavetable_metadata(wav_path.string(), meta);
        check(ok, "stereo probe succeeded");
        check(meta.sample_rate == 48000, "sample rate is 48000");
        check(meta.channels == 2, "channels is 2");
        // frame_count is based on mono frames (total_pcm_frames / 2048)
        check(meta.frame_count == 2, "frame count based on PCM frames");
    }

    // --- Test 3: probe nonexistent file ---
    {
        vivid::WavetableAssetMeta meta;
        bool ok = vivid::asset_internal::probe_wavetable_metadata("/nonexistent.wav", meta);
        check(!ok, "probe of nonexistent file fails");
    }

    // --- Test 4: probe truncated file ---
    {
        ScopedTempDir tmp("meta_truncated");
        fs::path wav_path = tmp.path / "truncated.wav";
        { std::ofstream ofs(wav_path, std::ios::binary); ofs << "RIFF"; }

        vivid::WavetableAssetMeta meta;
        bool ok = vivid::asset_internal::probe_wavetable_metadata(wav_path.string(), meta);
        check(!ok, "probe of truncated file fails");
    }

    // --- Test 5: file hash is deterministic ---
    {
        ScopedTempDir tmp("meta_hash");
        fs::path wav_path = tmp.path / "hash_test.wav";
        write_test_wav(wav_path.string(), 2048);

        std::string h1 = vivid::asset_internal::compute_file_hash(wav_path.string());
        std::string h2 = vivid::asset_internal::compute_file_hash(wav_path.string());
        check(!h1.empty(), "hash is non-empty");
        check(h1 == h2, "hash is deterministic");
        check(h1.substr(0, 8) == "fnv1a:0x", "hash has fnv1a prefix");
    }

    // --- Test 6: asset ID is deterministic ---
    {
        std::string id1 = vivid::asset_internal::generate_asset_id(
            vivid::AssetKind::Wavetable, vivid::AssetScope::Package, "pkg", "assets/wt/foo.wav");
        std::string id2 = vivid::asset_internal::generate_asset_id(
            vivid::AssetKind::Wavetable, vivid::AssetScope::Package, "pkg", "assets/wt/foo.wav");
        check(id1 == id2, "asset ID is deterministic");
        check(id1.substr(0, 3) == "wt_", "asset ID has wt_ prefix");

        // Different path → different ID
        std::string id3 = vivid::asset_internal::generate_asset_id(
            vivid::AssetKind::Wavetable, vivid::AssetScope::Package, "pkg", "assets/wt/bar.wav");
        check(id1 != id3, "different path produces different ID");
    }

    // --- Test 7: display name sanitization ---
    {
        check(vivid::asset_internal::sanitize_display_name("warm_pad.wav") == "Warm Pad",
              "underscores to spaces, title case");
        check(vivid::asset_internal::sanitize_display_name("bright-lead.wav") == "Bright Lead",
              "hyphens to spaces, title case");
        check(vivid::asset_internal::sanitize_display_name("simple.wav") == "Simple",
              "single word capitalized");
    }

    // --- Test 8: sidecar round-trip ---
    {
        ScopedTempDir tmp("meta_sidecar");
        fs::path sidecar = tmp.path / "asset.json";

        vivid::AssetEntry entry;
        entry.asset_id = "wt_test";
        entry.kind = vivid::AssetKind::Wavetable;
        entry.display_name = "Test Wave";
        entry.scope = vivid::AssetScope::Workspace;
        entry.relative_path = "test.wav";
        entry.source_hash = "fnv1a:0xdeadbeef";
        entry.imported_at = "2026-04-04T00:00:00Z";
        entry.file_size = 12345;
        entry.file_format = "wav";
        entry.kind_meta = {
            {"sample_rate", 44100},
            {"channels", 1},
            {"frame_count", 8},
            {"samples_per_frame", 2048},
            {"total_samples", 16384},
            {"peak_amplitude", 0.95}
        };

        bool wrote = vivid::asset_internal::write_asset_sidecar(sidecar.string(), entry);
        check(wrote, "sidecar write succeeded");

        vivid::AssetEntry loaded;
        bool read = vivid::asset_internal::read_asset_sidecar(sidecar.string(), loaded);
        check(read, "sidecar read succeeded");
        check(loaded.asset_id == "wt_test", "asset_id round-trips");
        check(loaded.display_name == "Test Wave", "display_name round-trips");
        check(loaded.source_hash == "fnv1a:0xdeadbeef", "source_hash round-trips");
        check(loaded.file_size == 12345, "file_size round-trips");
        check(loaded.kind_meta.is_object(), "kind metadata round-trips");
        check(loaded.kind_meta.value("sample_rate", 0u) == 44100, "sample_rate round-trips");
        check(loaded.kind_meta.value("frame_count", 0u) == 8, "frame_count round-trips");
        check_float(static_cast<float>(loaded.kind_meta.value("peak_amplitude", 0.0)),
                    0.95f, "peak_amplitude round-trips");
    }

    // --- Test 9: wavetable handler validates and probes metadata ---
    {
        ScopedTempDir tmp("meta_handler");
        std::filesystem::path wav_path = tmp.path / "handler.wav";
        write_test_wav(wav_path.string(), 2048 * 2, 44100, 1);

        vivid::AssetLibrary lib;
        auto* handler = wavetable_handler(lib);
        check(handler != nullptr, "wavetable handler registered");
        if (handler) {
            std::string error;
            nlohmann::json kind_meta;
            check(handler->accepts_source_file(wav_path, error), "wavetable handler accepts wav");
            check(handler->probe_metadata(wav_path, kind_meta, error), "wavetable handler probes metadata");
            check(kind_meta.value("frame_count", 0u) == 2, "handler exposes frame_count in kind_meta");
            check(handler->conventional_package_dirs().size() == 1,
                  "handler exposes conventional package dirs");
            check(handler->conventional_package_dirs()[0] == "assets/wavetables",
                  "handler conventional dir is assets/wavetables");
        }
    }

    std::fprintf(stderr, "\n%d failure(s)\n", failures);
    return failures > 0 ? 1 : 0;
}
