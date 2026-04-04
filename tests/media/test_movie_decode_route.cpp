#include "operators/shared/movie_decode/codec_probe.h"
#include "operators/shared/movie_decode/decoder_factory.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include "test_helpers.h"

static int g_fail = 0;

static std::string fixture_path(const char* root, const char* name) {
    std::filesystem::path p(root);
    p /= "assets/sync";
    p /= name;
    return p.string();
}

int main(int argc, char** argv) {
    const char* root = (argc > 1) ? argv[1] : ".";
    ScopedTempDir sandbox("movie_decode_route");

    const std::string hap1 = fixture_path(root, "sync-test-hap.mov");
    const std::string hap5 = fixture_path(root, "sync-test-hap-alpha.mov");
    const std::string hapy = fixture_path(root, "sync-test-hapq.mov");
    const std::string h264 = fixture_path(root, "sync-test-h264.mp4");
    const std::string hevc = fixture_path(root, "sync-test-hevc.mp4");

    check(std::filesystem::exists(hap1), "Hap1 fixture exists");
    check(std::filesystem::exists(hap5), "Hap5 fixture exists");
    check(std::filesystem::exists(hapy), "HapY fixture exists");
    check(std::filesystem::exists(h264), "h264 fixture exists");
    check(std::filesystem::exists(hevc), "hevc fixture exists");

    auto p_hap1 = probe_video_codec_fourcc(hap1);
    auto p_hap5 = probe_video_codec_fourcc(hap5);
    auto p_hapy = probe_video_codec_fourcc(hapy);
    auto p_h264 = probe_video_codec_fourcc(h264);
    auto p_hevc = probe_video_codec_fourcc(hevc);

    check(p_hap1.ok && p_hap1.is_hap, "Hap1 probe recognized as HAP");
    check(p_hap5.ok && p_hap5.is_hap, "Hap5 probe recognized as HAP");
    check(p_hapy.ok && p_hapy.is_hap, "HapY probe recognized as HAP");
    check(p_h264.ok && !p_h264.is_hap, "h264 probe recognized as non-HAP");
    check(p_hevc.ok && !p_hevc.is_hap, "hevc probe recognized as non-HAP");

    auto d_hap_bc = decide_decoder_route(p_hap1, true);
    auto d_hap_no_bc = decide_decoder_route(p_hap1, false);
    auto d_h264 = decide_decoder_route(p_h264, true);
    auto d_hevc = decide_decoder_route(p_hevc, false);

    check(d_hap_bc.backend == DecoderBackend::HAP, "HAP + BC => HAP backend");
    check(d_hap_no_bc.backend == DecoderBackend::AVF, "HAP + !BC => AVF backend");
    check(d_hap_no_bc.hap_bc_unavailable_fallback, "HAP + !BC marks fallback");
    check(d_h264.backend == DecoderBackend::AVF, "h264 => AVF backend");
    check(d_hevc.backend == DecoderBackend::AVF, "hevc => AVF backend");

    VideoCodecProbeResult notch{};
    notch.ok = true;
    notch.is_notchlc = true;
    auto d_notch = decide_decoder_route(notch, true);
    check(d_notch.backend == DecoderBackend::AVF, "NotchLC routes to AVF");
    check(d_notch.probe_notchlc, "NotchLC probe bit retained");

    const auto bad = load_video_decoder_for_path(sandbox.file_str("vivid_movie_missing_notch.mov"),
                                                  true, nullptr);
    check(!bad.success, "missing file load fails deterministically");
    check(bad.diagnostics.find("avf_open_failed") != std::string::npos,
          "missing file includes avf_open_failed diagnostics");

    return g_fail == 0 ? 0 : 1;
}
