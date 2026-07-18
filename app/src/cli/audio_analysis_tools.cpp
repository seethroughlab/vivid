#include "cli/audio_analysis_tools.h"

#include "cli/control_handlers_internal.h"
#include "audio/audio_clip_shared.h"
#include "audio/sampler.h"
#include "audio/vst3_host.h"
#include "transport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace vivid {
namespace {

bool parse_nonnegative_int(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0' || v < 0 || v > 1000000) return false;
    out = static_cast<int>(v);
    return true;
}

double requested_capture_seconds(const ControlCtx& c, const json& b, double fallback_seconds) {
    double seconds = b.value("duration_seconds", 0.0);
    const double beats = b.value("duration_beats", 0.0);
    if (seconds <= 0.0 && beats > 0.0) {
        const double bpm = c.transport ? c.transport->bpm.load(std::memory_order_relaxed) : 120.0;
        seconds = beats * 60.0 / std::max(1.0, bpm);
    }
    if (seconds <= 0.0) seconds = fallback_seconds;
    return std::clamp(seconds, 0.05, 30.0);
}

bool resolve_live_track(const ControlCtx& c, const json& b, const std::string& source, int& track, json& e) {
    if (!c.session) { e = err(code::kNoSession, "no session"); return false; }
    int candidate = -1;
    if (b.contains("track") && !b.contains("scene")) {
        candidate = b.value("track", -1);
    } else if (source.rfind("track:", 0) == 0) {
        if (!parse_nonnegative_int(source.substr(6), candidate)) {
            e = err(code::kBadArg, "bad track source; use track:<index>");
            return false;
        }
    } else if (source.rfind("track_index:", 0) == 0) {
        if (!parse_nonnegative_int(source.substr(12), candidate)) {
            e = err(code::kBadArg, "bad track source; use track_index:<index>");
            return false;
        }
    } else if (source.rfind("track_", 0) == 0) {
        int id_or_index = -1;
        if (!parse_nonnegative_int(source.substr(6), id_or_index)) {
            e = err(code::kBadArg, "bad track source; use track_<id>");
            return false;
        }
        if (id_or_index >= 0 && id_or_index < vivid::session::session_track_count(c.session)) {
            candidate = id_or_index;
        } else {
            for (int i = 0, n = vivid::session::session_track_count(c.session); i < n; ++i) {
                if (vivid::session::session_track_id(c.session, i) == id_or_index) { candidate = i; break; }
            }
        }
    }
    if (candidate < 0) {
        e = err(code::kBadArg, "need live source master or track:<index>");
        return false;
    }
    if (!need_track(c.session, candidate, e)) return false;
    track = candidate;
    return true;
}

}  // namespace

std::string lower_copy_audio(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

json analyze_pcm(const std::vector<float>& inL, const std::vector<float>& inR, uint32_t sr, int windows) {
    const size_t n = std::min(inL.size(), inR.empty() ? inL.size() : inR.size());
    if (n == 0 || sr == 0) return { {"ok", false}, {"error", "empty audio"} };
    const std::vector<float>& R = inR.empty() ? inL : inR;
    double sum2 = 0.0, peak = 0.0, abs_sum = 0.0;
    int clips = 0, silence = 0, zc = 0;
    float prev_m = 0.f;
    float lo = 0.f, hi = 0.f;
    double lo2 = 0.0, mid2 = 0.0, hi2 = 0.0, flux = 0.0;
    const float a_lo = 0.01f, a_hi = 0.20f;
    double prev_abs = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const float m = 0.5f * (inL[i] + R[i]);
        const double a = std::fabs(m);
        sum2 += static_cast<double>(m) * m;
        abs_sum += a;
        peak = std::max(peak, a);
        if (a >= 0.999) ++clips;
        if (a < 0.001) ++silence;
        if (i > 0 && ((m >= 0.f) != (prev_m >= 0.f))) ++zc;
        prev_m = m;
        lo += a_lo * (m - lo);
        const float low = lo;
        hi += a_hi * (m - hi);
        const float high = m - hi;
        const float mid = m - low - high;
        lo2 += low * low;
        mid2 += mid * mid;
        hi2 += high * high;
        if (i > 0) flux += std::max(0.0, a - prev_abs);
        prev_abs = a;
    }
    const double rms = std::sqrt(sum2 / static_cast<double>(n));
    const double crest = rms > 1e-9 ? peak / rms : 0.0;
    const double duration = static_cast<double>(n) / sr;
    auto trans = audio_clip_ed::detect_transients(inL, R, sr, 0.5f);
    const double bpm = audio_clip_ed::estimate_bpm(trans, sr);

    json win = json::array();
    windows = std::clamp(windows, 1, 128);
    for (int w = 0; w < windows; ++w) {
        const size_t a = n * static_cast<size_t>(w) / windows;
        const size_t b = n * static_cast<size_t>(w + 1) / windows;
        double e = 0.0, p = 0.0;
        for (size_t i = a; i < b; ++i) {
            const double m = 0.5 * (inL[i] + R[i]);
            e += m * m;
            p = std::max(p, std::fabs(m));
        }
        const size_t count = std::max<size_t>(1, b - a);
        win.push_back({ {"index", w}, {"start_seconds", static_cast<double>(a) / sr},
                        {"rms", std::sqrt(e / count)}, {"peak", p} });
    }

    json onsets = json::array();
    for (size_t i = 0; i < std::min<size_t>(trans.size(), 32); ++i)
        onsets.push_back({ {"seconds", static_cast<double>(trans[i].source_sample) / sr},
                           {"strength", trans[i].strength} });
    return {
        {"ok", true},
        {"sample_rate", sr},
        {"frames", static_cast<int>(n)},
        {"duration_seconds", duration},
        {"rms", rms},
        {"mean_abs", abs_sum / static_cast<double>(n)},
        {"peak", peak},
        {"clipping_samples", clips},
        {"crest_factor", crest},
        {"silence_ratio", static_cast<double>(silence) / static_cast<double>(n)},
        {"zero_crossing_rate_hz", duration > 0.0 ? zc / duration : 0.0},
        {"spectral_centroid_proxy_hz", duration > 0.0 ? (zc / duration) * 0.5 : 0.0},
        {"spectral_flux_proxy", flux / static_cast<double>(n)},
        {"bands", {
            {"low", std::sqrt(lo2 / static_cast<double>(n))},
            {"mid", std::sqrt(mid2 / static_cast<double>(n))},
            {"high", std::sqrt(hi2 / static_cast<double>(n))}
        }},
        {"transient_count", static_cast<int>(trans.size())},
        {"transient_density_per_second", duration > 0.0 ? trans.size() / duration : 0.0},
        {"tempo_bpm_estimate", bpm},
        {"strongest_onsets", onsets},
        {"energy_windows", win}
    };
}

bool load_pcm_file(const std::string& path, uint32_t sr_hint, std::vector<float>& L, std::vector<float>& R, uint32_t& sr) {
    vivid::session::Sampler smp;
    if (!vivid::session::sampler_load_wav(path, sr_hint ? sr_hint : 48000, 120.0, smp)) return false;
    L = smp.L;
    R = smp.R.empty() ? smp.L : smp.R;
    sr = smp.sr ? smp.sr : (sr_hint ? sr_hint : 48000);
    return !L.empty();
}

bool copy_live_capture(const ControlCtx& c, const json& b, double fallback_seconds,
                       std::vector<float>& L, std::vector<float>& R, uint32_t& sr,
                       double& requested_seconds, json& source_json, json& e) {
    const std::string source = lower_copy_audio(b.value("source", std::string("master")));
    requested_seconds = requested_capture_seconds(c, b, fallback_seconds);

    const bool explicit_live_track = b.contains("track") && !b.contains("scene");
    if (!explicit_live_track && (source.empty() || source == "master" || source == "mix")) {
        if (!c.transport) { e = err(code::kNoTransport, "no transport"); return false; }
        if (c.transport->capture_snapshot(requested_seconds, L, R, &sr) == 0) {
            e = err(code::kBadArg, "no live audio has reached the master capture buffer yet");
            return false;
        }
        source_json = { {"kind", "live_capture"}, {"source", "master"} };
        return true;
    }

    int track = -1;
    if (!resolve_live_track(c, b, source, track, e)) return false;
    if (vivid::session::session_track_capture_snapshot(c.session, track, requested_seconds, L, R, &sr) <= 0) {
        e = err(code::kBadArg, "no live audio has reached the track capture buffer yet");
        return false;
    }
    source_json = { {"kind", "live_capture"}, {"source", "track"}, {"track", track},
                    {"track_id", vivid::session::session_track_id(c.session, track)},
                    {"name", vivid::session::session_track_name(c.session, track)} };
    return true;
}

}  // namespace vivid
