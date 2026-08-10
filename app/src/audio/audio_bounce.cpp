#include "audio/audio_bounce.h"

#include "audio/vst3_host.h"   // vivid::session::session_process
#include "transport.h"

#include "miniaudio.h"         // ma_encoder (dr_wav-backed WAV writer; encoding is compiled in)

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

namespace vivid {

namespace {

// Accept only an absolute path with no ".." traversal and a .wav extension — mirrors
// video_recorder.cpp's is_safe_recording_path, so a scripted MCP caller (or a stray menu path)
// can't write outside an intended location or hand ma_encoder a container it won't produce.
bool is_safe_wav_path(const std::string& p, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    if (p.empty())   return fail("path is empty");
    if (p[0] != '/') return fail("path must be absolute");
    if (p.find("..") != std::string::npos) return fail("path must not contain '..'");
    auto dot = p.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : p.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext != "wav") return fail("path must end in .wav");
    return true;
}

}  // namespace

bool bounce_session_to_wav(vivid::session::Session* session, const Transport& tr,
                           const BounceRequest& req, BounceResult& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!session) return fail("no session");
    if (!is_safe_wav_path(req.path, err)) return false;

    const uint32_t sr = tr.audio_sample_rate();
    if (sr == 0) return fail("audio sample rate unknown (audio device not initialized)");
    const double bpm = tr.bpm.load(std::memory_order_relaxed);
    const int    bpb = tr.beats_per_bar.load(std::memory_order_relaxed);

    // Resolve the length: explicit seconds wins; else derive from bars via the transport tempo.
    double seconds = req.seconds;
    if (seconds <= 0.0 && req.bars > 0.0 && bpm > 0.0)
        seconds = req.bars * bpb * 60.0 / bpm;
    if (seconds <= 0.0) return fail("seconds (or bars, with a running tempo) must be > 0");

    const uint32_t block = req.block ? req.block : 1024;
    const uint64_t total_frames = static_cast<uint64_t>(std::ceil(seconds * static_cast<double>(sr)));

    ma_encoder_config ecfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sr);
    ma_encoder enc;
    if (ma_encoder_init_file(req.path.c_str(), &ecfg, &enc) != MA_SUCCESS)
        return fail("could not open output file for writing: " + req.path);

    std::vector<float> buf(static_cast<size_t>(block) * 2, 0.f);
    double beats = 0.0;   // LOCAL clock — render from the top, exactly like render_span
    const double bps = bpm / 60.0;
    uint64_t written = 0;
    float peak = 0.f;
    while (written < total_frames) {
        const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(block, total_frames - written));
        std::fill(buf.begin(), buf.end(), 0.f);
        vivid::session::session_process(session, buf.data(), n, sr, bpm, beats, bpb,
                                        /*playing*/true, /*release_all*/false);
        const size_t samples = static_cast<size_t>(n) * 2;
        for (size_t i = 0; i < samples; ++i) peak = std::max(peak, std::fabs(buf[i]));
        ma_uint64 frames_written = 0;
        ma_encoder_write_pcm_frames(&enc, buf.data(), n, &frames_written);
        beats += static_cast<double>(n) / static_cast<double>(sr) * bps;
        written += n;
    }
    ma_encoder_uninit(&enc);

    out.path         = req.path;
    out.frames       = written;
    out.duration_sec = static_cast<double>(written) / static_cast<double>(sr);
    out.peak         = peak;
    out.clipped      = peak > 1.0f;
    return true;
}

}  // namespace vivid
