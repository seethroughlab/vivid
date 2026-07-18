#include "cli/control_handlers_internal.h"
#include "cli/control_json.h"           // operator_to_json (native audio-op discovery)
#include "cli/operator_catalog.h"       // native_audio_ops — shared enumeration (ADR-0023 step 7)

#include "audio/vst3_host.h"
#include "audio/plugin_catalog.h"
#include "audio/sampler.h"
#include "audio/audio_clip_shared.h"
#include "midi/midi_clip.h"
#include "midi/note_json.h"
#include "transport.h"                  // Transport (set_bpm / transport handlers)
#include "gpu/visual_graph.h"           // VisualGraph (op registry for audio-op discovery)
#include "ui/node_graph.h"              // NodeGraph (bridge return path)

#include <cctype>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace vivid {
namespace {

std::string lower_copy_audio(std::string s) {
    for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

json analyze_pcm(const std::vector<float>& inL, const std::vector<float>& inR, uint32_t sr, int windows = 16) {
    const size_t n = std::min(inL.size(), inR.empty() ? inL.size() : inR.size());
    if (n == 0 || sr == 0) return { {"ok", false}, {"error", "empty audio"} };
    const std::vector<float>& R = inR.empty() ? inL : inR;
    double sum2 = 0.0, peak = 0.0, abs_sum = 0.0;
    int clips = 0, silence = 0, zc = 0;
    float prev_m = 0.f;
    // Cheap three-band analysis mirrors the live mapping idea without using an FFT.
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

bool parse_nonnegative_int(const std::string& text, int& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0' || v < 0 || v > 1000000) return false;
    out = static_cast<int>(v);
    return true;
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

}  // namespace

// Audio authoring: transport/BPM, per-track launch/gain/arm/note/record/metronome/params/clips,
// audio-clip warp + shaping, the clip pool, native audio operators (instrument/effects) + the
// per-track audio node graph (add/remove/connect/param), slicing, and discovery.
void register_audio_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
    // ---------------- audio authoring ----------------
    handlers_["set_bpm"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        const double bpm = b.value("bpm", 120.0);
        if (!(bpm > 0.0) || bpm > 1000.0) return err(code::kBadArg, "bpm out of range (0, 1000]");
        c.transport->bpm.store(bpm, std::memory_order_relaxed);
        return ok();
    };
    // Transport play/stop. set_playing{playing} sets it; toggle_play flips; reset_transport
    // returns to the top (bar 1). Pausing freezes the clock so clips stop advancing.
    handlers_["set_playing"] = [](const ControlCtx& c, const json& b) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        c.transport->set_playing(b.value("playing", true));
        json r = ok(); r["playing"] = c.transport->is_playing(); return r;
    };
    handlers_["toggle_play"] = [](const ControlCtx& c, const json&) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        json r = ok(); r["playing"] = c.transport->toggle_playing(); return r;
    };
    handlers_["reset_transport"] = [](const ControlCtx& c, const json&) {
        if (!c.transport) return err(code::kNoTransport, "no transport");
        c.transport->reset();
        json r = ok(); r["beats"] = 0.0; return r;
    };
    handlers_["launch_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_launch_clip(c.session, track, scene);
        return ok();
    };
    handlers_["launch_scene"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int scene = b.value("scene", 0);
        json e; if (!need_scene(c.session, scene, e)) return e;
        P::session_launch_scene(c.session, scene);
        return ok();
    };
    handlers_["set_track_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_gain(c.session, track, b.value("gain", 0.8f));
        return ok();
    };
    // ADR-0022 P1b: the master node's gain (the session's single sink). Default 1.0 (unity).
    handlers_["set_master_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_set_master_gain(c.session, b.value("gain", 1.0f));
        return ok();
    };
    // ADR-0022 P1b.4: solo/mute. Silence a track in the master mix (mute), or hear only the
    // soloed track(s) (solo). The track's own meter stays pre-mute.
    handlers_["set_track_mute"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_mute(c.session, track, b.value("mute", true));
        return ok();
    };
    handlers_["set_track_solo"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_track_solo(c.session, track, b.value("solo", true));
        return ok();
    };
    handlers_["arm_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1);   // -1 disarms
        if (track >= 0) { json e; if (!need_track(c.session, track, e)) return e; }
        P::session_set_armed_track(c.session, track);
        json r = ok(); r["armed"] = P::session_armed_track(c.session); return r;
    };
    handlers_["note_on"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        if (P::session_armed_track(c.session) < 0) return err(code::kBadArg, "no armed track");
        const int pitch = b.value("pitch", -1);
        if (pitch < 0 || pitch > 127) return err(code::kBadArg, "pitch out of range [0,127]");
        P::session_note_on(c.session, pitch, b.value("vel", 0.8f));
        return ok();
    };
    handlers_["note_off"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int pitch = b.value("pitch", -1);
        if (pitch < 0 || pitch > 127) return err(code::kBadArg, "pitch out of range [0,127]");
        P::session_note_off(c.session, pitch);
        return ok();
    };
    handlers_["record"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const bool on = b.value("on", true);
        if (on && P::session_armed_track(c.session) < 0) return err(code::kBadArg, "no armed track");
        P::session_set_recording(c.session, on, b.value("count_in", 0.0));
        json r = ok(); r["recording"] = P::session_is_recording(c.session); return r;
    };
    handlers_["set_clip_loop"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_set_clip_loop(c.session, track, scene, b.value("loop_start", 0.0), b.value("loop_end", 0.0));
        return ok();
    };
    handlers_["metronome"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        P::session_set_metronome(c.session, b.value("on", true) ? 1 : 0);
        json r = ok(); r["metronome"] = P::session_get_metronome(c.session); return r;
    };
    handlers_["set_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), device = b.value("device", 0), index = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const int pc = P::session_param_count(c.session, track, device);
        if (pc == 0) return err(code::kOutOfRange, "device " + std::to_string(device) + " has no params (or out of range)");
        if (!in_range(index, pc)) return err(code::kOutOfRange, "param index " + std::to_string(index) + " out of range [0," + std::to_string(pc) + ")");
        P::session_set_param(c.session, track, device, P::session_param_id(c.session, track, device, index), b.value("value", 0.f));
        return ok();
    };
    handlers_["set_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        std::vector<P::ClipNote> notes;
        if (b.contains("notes"))
            for (const auto& jn : b["notes"]) {
                P::ClipNote cn{ jn.value("p", 60), jn.value("s", 0.0), jn.value("d", 0.25), jn.value("v", 0.8f), {} };
                P::expr_from_json(jn, cn);   // optional per-note bend/pressure/timbre curves
                notes.push_back(std::move(cn));
            }
        P::session_set_clip(c.session, track, scene,
                            notes.data(), static_cast<int>(notes.size()), b.value("length", 4.0));
        json r = ok(); r["notes"] = static_cast<int>(notes.size()); return r;
    };
    // Read a MIDI clip back (the read half that read-modify-write authoring tools need).
    handlers_["get_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::ClipNote buf[1024];
        const int n = P::session_get_clip(c.session, track, scene, buf, 1024);
        json notes = json::array();
        for (int i = 0; i < n; ++i) {
            json jn = { {"p", buf[i].pitch}, {"s", buf[i].start}, {"d", buf[i].dur}, {"v", buf[i].vel} };
            P::expr_to_json(buf[i], jn);
            notes.push_back(jn);
        }
        json r = ok(); r["notes"] = notes; r["length"] = P::session_clip_length(c.session, track, scene); return r;
    };
    // ---------------- audio-clip warp / shaping (A2) ----------------
    handlers_["audio_set_warp"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const std::string m = b.value("mode", std::string("complex"));
        const int mode = (m == "beats") ? 1 : (m == "repitch") ? 2 : 0;
        P::session_set_audio_warp(c.session, track, scene, b.value("enabled", true) ? 1 : 0, mode);
        json r = ok(); r["warp"] = P::session_get_audio_warp(c.session, track, scene); return r;
    };
    handlers_["audio_set_pitch"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_pitch(c.session, track, scene, b.value("semitones", 0.0f));
        json r = ok(); r["semitones"] = P::session_get_audio_pitch(c.session, track, scene); return r;
    };
    handlers_["audio_set_gain"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_gain(c.session, track, scene, b.value("gain", 1.0f));
        return ok();
    };
    handlers_["audio_set_reverse"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        P::session_set_audio_reverse(c.session, track, scene, b.value("on", true) ? 1 : 0);
        return ok();
    };
    handlers_["audio_auto_warp"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const int npts = P::session_audio_auto_warp(c.session, track, scene, b.value("sensitivity", 0.5f));
        json r = ok(); r["markers"] = npts; return r;
    };
    handlers_["capture_audio"] = [](const ControlCtx& c, const json& b) {
        std::vector<float> L, R; uint32_t sr = 0; double requested = 0.0; json source; json e;
        if (!copy_live_capture(c, b, 4.0, L, R, sr, requested, source, e)) return e;
        json r = ok();
        r["captured"] = true;
        r["source"] = source;
        r["sample_rate"] = sr;
        r["frames"] = static_cast<int>(L.size());
        r["duration_seconds"] = sr ? static_cast<double>(L.size()) / sr : 0.0;
        r["requested_seconds"] = requested;
        if (c.transport && source.value("source", std::string()) == "master")
            r["capacity_seconds"] = sr ? static_cast<double>(c.transport->capture_capacity_frames()) / sr : 0.0;
        else
            r["capacity_seconds"] = 30.0;
        r["summary"] = "Captured recent live audio from the bounded runtime buffer.";
        r["next_tools"] = {"analyze_audio", "summarize_mix", "detect_onsets"};
        return r;
    };
    handlers_["analyze_audio_clip"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        if (!P::session_track_is_audio(c.session, track)) return err(code::kBadArg, "track is not an audio track");
        std::vector<float> L, R; uint32_t sr = 0;
        if (P::session_audio_copy_pcm(c.session, track, scene, L, R, &sr) <= 0)
            return err(code::kBadArg, "audio clip is empty");
        json a = analyze_pcm(L, R, sr, b.value("windows", 16));
        json r = ok();
        r["source"] = { {"kind", "clip"}, {"track", track}, {"scene", scene},
                        {"path", P::session_get_audio_path(c.session, track, scene)},
                        {"src_bpm", P::session_get_audio_src_bpm(c.session, track, scene)} };
        r["analysis"] = a;
        r["summary"] = "Analyzed audio clip: rms=" + std::to_string(a.value("rms", 0.0)) +
                       ", peak=" + std::to_string(a.value("peak", 0.0)) +
                       ", transients=" + std::to_string(a.value("transient_count", 0));
        return r;
    };
    handlers_["analyze_audio_file"] = [](const ControlCtx& c, const json& b) {
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "need path");
        std::vector<float> L, R; uint32_t sr = 0;
        if (!load_pcm_file(path, 48000, L, R, sr)) return err(code::kIoError, "could not decode audio file");
        json a = analyze_pcm(L, R, sr, b.value("windows", 16));
        json r = ok();
        r["source"] = { {"kind", "file"}, {"path", path} };
        r["analysis"] = a;
        r["summary"] = "Analyzed audio file: rms=" + std::to_string(a.value("rms", 0.0)) +
                       ", peak=" + std::to_string(a.value("peak", 0.0)) +
                       ", transients=" + std::to_string(a.value("transient_count", 0));
        return r;
    };
    handlers_["analyze_audio"] = [](const ControlCtx& c, const json& b) {
        if (b.contains("track") && b.contains("scene")) {
            if (!c.session) return err(code::kNoSession, "no session");
            std::vector<float> L, R; uint32_t sr = 0;
            const int track = b.value("track", 0), scene = b.value("scene", 0);
            json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
            if (!P::session_track_is_audio(c.session, track)) return err(code::kBadArg, "track is not an audio track");
            if (P::session_audio_copy_pcm(c.session, track, scene, L, R, &sr) <= 0) return err(code::kBadArg, "audio clip is empty");
            json r = ok(); r["source"] = { {"kind", "clip"}, {"track", track}, {"scene", scene} };
            r["analysis"] = analyze_pcm(L, R, sr, b.value("windows", 16)); return r;
        }
        std::vector<float> L, R; uint32_t sr = 0; double requested = 0.0; json source; json e;
        if (!copy_live_capture(c, b, 4.0, L, R, sr, requested, source, e)) return e;
        json a = analyze_pcm(L, R, sr, b.value("windows", 16));
        json r = ok();
        r["source"] = source;
        r["requested_seconds"] = requested;
        r["analysis"] = a;
        r["summary"] = "Analyzed recent live audio: rms=" + std::to_string(a.value("rms", 0.0)) +
                       ", peak=" + std::to_string(a.value("peak", 0.0)) +
                       ", transients=" + std::to_string(a.value("transient_count", 0));
        if (c.transport) {
            r["live_snapshot"] = { {"level", c.transport->level.load(std::memory_order_relaxed)},
                                   {"transient", c.transport->transient.load(std::memory_order_relaxed)},
                                   {"bands", { {"low", c.transport->band_low.load(std::memory_order_relaxed)},
                                                {"mid", c.transport->band_mid.load(std::memory_order_relaxed)},
                                                {"high", c.transport->band_high.load(std::memory_order_relaxed)} }} };
        }
        return r;
    };
    handlers_["detect_onsets"] = [](const ControlCtx& c, const json& b) {
        std::vector<float> L, R; uint32_t sr = 0;
        json source;
        if (b.contains("path")) {
            const std::string path = b.value("path", std::string());
            if (!load_pcm_file(path, 48000, L, R, sr)) return err(code::kIoError, "could not decode audio file");
            source = { {"kind", "file"}, {"path", path} };
        } else if (b.contains("track") && b.contains("scene")) {
            if (!c.session) return err(code::kNoSession, "no session");
            const int track = b.value("track", 0), scene = b.value("scene", 0);
            json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
            if (!P::session_track_is_audio(c.session, track)) return err(code::kBadArg, "track is not an audio track");
            if (P::session_audio_copy_pcm(c.session, track, scene, L, R, &sr) <= 0) return err(code::kBadArg, "audio clip is empty");
            source = { {"kind", "clip"}, {"track", track}, {"scene", scene} };
        } else {
            double requested = 0.0; json e;
            if (!copy_live_capture(c, b, 4.0, L, R, sr, requested, source, e)) return e;
            source["requested_seconds"] = requested;
        }
        auto tr = audio_clip_ed::detect_transients(L, R.empty() ? L : R, sr, b.value("sensitivity", 0.5f));
        json onsets = json::array();
        for (const auto& t : tr) onsets.push_back({ {"seconds", static_cast<double>(t.source_sample) / sr},
                                                    {"sample", t.source_sample}, {"strength", t.strength} });
        json r = ok();
        r["source"] = source;
        r["onsets"] = onsets;
        r["count"] = static_cast<int>(onsets.size());
        r["tempo_bpm_estimate"] = audio_clip_ed::estimate_bpm(tr, sr);
        return r;
    };
    handlers_["summarize_mix"] = [](const ControlCtx& c, const json& b) {
        std::vector<float> L, R; uint32_t sr = 0; double requested = 0.0; json source; json e;
        if (!copy_live_capture(c, b, 8.0, L, R, sr, requested, source, e)) return e;
        json a = analyze_pcm(L, R, sr, b.value("windows", 24));
        json r = ok();
        r["source"] = source;
        r["requested_seconds"] = requested;
        r["summary"] = "Live audio over " + std::to_string(a.value("duration_seconds", 0.0)) +
                       "s: rms=" + std::to_string(a.value("rms", 0.0)) +
                       ", peak=" + std::to_string(a.value("peak", 0.0)) +
                       ", transients=" + std::to_string(a.value("transient_count", 0));
        r["analysis"] = a;
        if (c.transport) {
            r["live_snapshot"] = { {"level", c.transport->level.load(std::memory_order_relaxed)},
                                   {"transient", c.transport->transient.load(std::memory_order_relaxed)},
                                   {"bands", { {"low", c.transport->band_low.load(std::memory_order_relaxed)},
                                                {"mid", c.transport->band_mid.load(std::memory_order_relaxed)},
                                                {"high", c.transport->band_high.load(std::memory_order_relaxed)} }} };
        }
        return r;
    };
    // ---------------- clip pool (loose clips that live outside the grid) ----------------
    // The pool is UI-thread-only storage; these handlers run on the UI thread (like all others).
    handlers_["list_pool"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json arr = json::array();
        for (int i = 0, n = P::session_pool_count(c.session); i < n; ++i)
            arr.push_back({ {"index", i}, {"name", P::session_pool_name(c.session, i)},
                            {"length", P::session_pool_length(c.session, i)},
                            {"kind", P::session_pool_is_audio(c.session, i) ? "audio" : "midi"} });
        json r = ok(); r["pool"] = arr; return r;
    };
    // Move a grid clip into the pool (MIDI or audio): the source cell is cleared (the clip
    // leaves the session). Returns the new pool index.
    handlers_["pool_stash"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        std::string name = b.value("name", std::string());
        if (name.empty()) { char nm[28]; std::snprintf(nm, sizeof nm, "%.12s %c", P::session_track_name(c.session, track), 'A' + scene); name = nm; }
        if (P::session_track_is_audio(c.session, track)) {
            const int idx = P::session_pool_stash_audio(c.session, track, scene, name.c_str());
            if (idx < 0) return err(code::kBadArg, "clip is empty");
            json r = ok(); r["index"] = idx; r["kind"] = "audio"; return r;
        }
        P::ClipNote buf[1024];
        const int n = P::session_get_clip(c.session, track, scene, buf, 1024);
        if (n <= 0) return err(code::kBadArg, "clip is empty");
        const double len = P::session_clip_length(c.session, track, scene);
        const int idx = P::session_pool_add(c.session, buf, n, len, name.c_str());
        P::session_set_clip(c.session, track, scene, nullptr, 0, len);   // take it out of the grid
        json r = ok(); r["index"] = idx; r["kind"] = "midi"; return r;
    };
    // Place a pool clip into a grid cell, overwriting it. Types must match: an audio clip
    // goes on an audio track, a MIDI clip on an instrument track.
    handlers_["pool_place"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1), track = b.value("track", 0), scene = b.value("scene", 0);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        json e; if (!need_track(c.session, track, e) || !need_scene(c.session, scene, e)) return e;
        const bool poolAudio = P::session_pool_is_audio(c.session, index);
        const bool trackAudio = P::session_track_is_audio(c.session, track);
        if (poolAudio != trackAudio)
            return err(code::kBadArg, poolAudio ? "audio clip needs an audio track" : "MIDI clip needs an instrument track");
        if (poolAudio) {
            if (!P::session_pool_place_audio(c.session, index, track, scene)) return err(code::kInternal, "place failed");
            json r = ok(); r["kind"] = "audio"; return r;
        }
        P::ClipNote buf[1024];
        const int n = P::session_pool_get(c.session, index, buf, 1024);
        P::session_set_clip(c.session, track, scene, buf, n, P::session_pool_length(c.session, index));
        json r = ok(); r["notes"] = n; r["kind"] = "midi"; return r;
    };
    handlers_["pool_remove"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int index = b.value("index", -1);
        if (!in_range(index, P::session_pool_count(c.session))) return err(code::kOutOfRange, "pool index " + std::to_string(index) + " out of range");
        P::session_pool_remove(c.session, index);
        return ok();
    };
    handlers_["add_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = b.value("name", std::string());
        // A ".vst3" path (from list_plugins) loads that bundle directly as an effect.
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".vst3") == 0) {
            const bool okk = P::session_add_effect(c.session, track, name.c_str());
            return okk ? ok() : err(code::kInternal, "add failed (not an effect, or load error)");
        }
        // Any installed plugin, by name (the five hard-coded labels are gone).
        const bool okk = P::session_add_effect_by_name(c.session, track, name.c_str());
        return okk ? ok() : err(code::kNotFound, "no installed plugin named '" + name + "'");
    };
    // Every installed plugin (VST3 today), for the browser. A path from here works as
    // an "instrument" for add_track or a "name" for add_effect. CLAP/AU hosts TBD.
    handlers_["list_plugins"] = [](const ControlCtx&, const json&) {
        json arr = json::array();
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            // `class` is what the background probe found (instrument / effect / note-effect), so an
            // agent can pick the right one instead of guessing from the name. "unknown" = not probed
            // yet (the first run classifies in the background).
            arr.push_back({ {"name", p.name}, {"path", p.path},
                            {"format", P::plugin_format_name(p.format)},
                            {"class", P::plugin_class_name(p.cls)},
                            {"vendor", p.vendor},
                            {"probed", p.probed} });
        }
        json r = ok(); r["plugins"] = arr; return r;
    };
    // CLAP hosting: assign a `.clap` bundle as a track's instrument, or append one as an effect.
    // Loading is ASYNC — a slow plugin ctor (e.g. Surge scanning its wavetable dir) runs on a
    // background worker so it never wedges the control-server drain. These return immediately with
    // `loading:true`; poll `plugin_load_status` (or watch get_audio_graph) until the node appears.
    handlers_["set_track_clap_instrument"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string path = b.value("path", std::string());
        if (!P::session_request_track_clap_instrument(c.session, track, path.c_str()))
            return err(code::kBadArg, "could not queue CLAP instrument: '" + path + "'");
        json r = ok(); r["loading"] = !path.empty(); return r;   // "" clears inline (not loading)
    };
    handlers_["add_track_clap_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string path = b.value("path", std::string());
        if (!P::session_request_track_clap_effect(c.session, track, path.c_str()))
            return err(code::kBadArg, "could not queue CLAP effect: '" + path + "'");
        json r = ok(); r["loading"] = true; return r;   // effect index is known once applied (see get_audio_graph)
    };
    // Poll the async CLAP loader: {pending: <in-flight loads>, error: "<last failure or ''>"}.
    handlers_["plugin_load_status"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        json r = ok();
        r["pending"] = P::session_plugin_loads_pending(c.session);
        r["error"] = P::session_last_plugin_load_error(c.session);
        return r;
    };
    // Generic preset browse/load for a track's instrument (no per-plugin code). list_presets
    // scans + returns [{name,id}]; the agent picks by name (sonic-intent guidance) and calls
    // load_preset with the id. CLAP today via the plugin's preset-discovery + preset-load exts.
    handlers_["list_presets"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string filter = b.value("filter", std::string());   // narrow by name substring
        const int n = P::session_track_preset_scan(c.session, track, filter.c_str());
        json arr = json::array();
        for (int i = 0; i < n; ++i) {
            json p = { {"name", P::session_track_preset_name(c.session, track, i)},
                       {"id",   P::session_track_preset_id(c.session, track, i)} };
            const char* cat = P::session_track_preset_category(c.session, track, i);
            if (cat && *cat) p["category"] = cat;
            const int tn = P::session_track_preset_tag_count(c.session, track, i);
            if (tn > 0) { json tags = json::array();
                for (int k = 0; k < tn; ++k) tags.push_back(P::session_track_preset_tag(c.session, track, i, k));
                p["tags"] = std::move(tags); }
            p["loadable"] = P::session_track_preset_loadable(c.session, track, i) != 0;  // false = browse-only
            arr.push_back(std::move(p));
        }
        json r = ok(); r["count"] = n; r["presets"] = arr; return r;
    };
    handlers_["load_preset"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string id = b.value("id", b.value("preset", std::string()));
        if (id.empty()) return err(code::kBadArg, "need a preset id (from list_presets)");
        if (!P::session_track_preset_load(c.session, track, id.c_str()))
            return err(code::kBadArg, "preset load failed (no instrument loaded, or unknown/invalid preset id): '" + id + "'");
        return ok();
    };
    handlers_["remove_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), effect = b.value("effect", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!in_range(effect, P::session_effect_count(c.session, track)))
            return err(code::kOutOfRange, "effect index " + std::to_string(effect) + " out of range");
        P::session_remove_effect(c.session, track, effect);
        return ok();
    };
    // Everything that can be added as an EFFECT: native audio operators + every installed plugin
    // the probe classified as an effect. (It used to return five hard-coded VST3 names.)
    handlers_["list_effects"] = [](const ControlCtx& c, const json&) {
        json arr = json::array();
        for (int k = 0, n = c.session ? P::session_available_audio_op_count(c.session, 0) : 0; k < n; ++k)
            arr.push_back({ {"name", P::session_available_audio_op_name(c.session, 0, k)}, {"format", "native"} });
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            if (p.cls == P::kClassInstrument || p.cls == P::kClassFailed || p.cls == P::kClassCrashed) continue;
            arr.push_back({ {"name", p.name}, {"path", p.path},
                            {"format", P::plugin_format_name(p.format)},
                            {"class", P::plugin_class_name(p.cls)} });
        }
        json r = ok(); r["effects"] = arr; return r;
    };
    // --- Native audio operators (AO-1). index -1 = instrument slot, >=0 = effect. ---
    handlers_["add_audio_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int idx = P::session_add_audio_effect(c.session, track, op.c_str());
        if (idx < 0) return err(code::kBadArg, "not a valid audio effect operator: '" + op + "'");
        json r = ok(); r["index"] = idx; return r;
    };
    handlers_["remove_audio_effect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_remove_audio_effect(c.session, track, index);
        return ok();
    };
    // A bare native-instrument track (no VST3 handle) — the home for a native audio node graph.
    // This is the only programmatic way to create a graph-capable track (add_track's instrument
    // path builds a VST3/plugin track; slice_to_midi needs an audio clip). Optional "instrument"
    // sets the native instrument op in the same call so the track is graph-ok immediately.
    handlers_["add_graph_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string name = b.value("name", std::string());
        const int idx = P::session_add_graph_track(c.session, name.c_str());
        if (idx < 0) return err(code::kInternal, "add_graph_track failed (kMaxTracks reached?)");
        const std::string inst = b.value("instrument", std::string());
        if (!inst.empty() && !P::session_set_track_audio_instrument(c.session, idx, inst.c_str()))
            return err(code::kBadArg, "track created but instrument invalid: '" + inst + "'");
        json r = ok(); r["track"] = idx; return r;
    };
    handlers_["set_track_audio_instrument"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        if (!P::session_set_track_audio_instrument(c.session, track, op.c_str()))
            return err(code::kBadArg, "not a valid audio instrument operator: '" + op + "'");
        return ok();
    };
    handlers_["set_audio_op_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", -1), param = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_op_param_set(c.session, track, index, param, b.value("value", 0.f));
        return ok();
    };
    handlers_["set_audio_op_param_by_name"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), index = b.value("index", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = lower_copy_audio(b.value("name", b.value("param", std::string())));
        if (name.empty()) return err(code::kBadArg, "need name");
        const int pc = P::session_audio_op_param_count(c.session, track, index);
        int hit = -1;
        for (int p = 0; p < pc; ++p) {
            if (lower_copy_audio(P::session_audio_op_param_name(c.session, track, index, p)) == name) {
                if (hit >= 0) return err(code::kBadArg, "ambiguous param name '" + name + "'");
                hit = p;
            }
        }
        if (hit < 0) return err(code::kNotFound, "no native audio param named '" + name + "'");
        P::session_audio_op_param_set(c.session, track, index, hit, b.value("value", 0.f));
        json r = ok(); r["param"] = hit; r["name"] = P::session_audio_op_param_name(c.session, track, index, hit); return r;
    };
    // AG-1 step 2: authoritative topology edits. The first flips the track's audio graph to the
    // editable source of truth; get_audio_graph reflects the result (nodes/edges/output_id).
    handlers_["audio_graph_add_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add audio effect node: '" + op + "'");
        json r = ok(); r["node"] = nid; return r;
    };
    // A2: spawn a VST3/CLAP plugin as a graph NODE (the peer of audio_graph_add_op, which is
    // native-only). `path` = the bundle; `source` = instrument (fan-in) vs effect (splice). The
    // node id comes back immediately — a CLAP binds when its async load lands, so poll
    // get_audio_graph / plugin_loads_pending to know when it's live.
    handlers_["audio_graph_add_plugin"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string path = b.value("path", std::string());
        if (path.empty()) return err(code::kBadArg, "path required (a .vst3 / .clap bundle)");
        const bool clap = path.size() > 5 && path.compare(path.size() - 5, 5, ".clap") == 0;
        const int fmt = b.contains("format") ? b.value("format", 0)
                                             : (clap ? P::kFmtCLAP : P::kFmtVST3);
        const int src = b.value("source", 0) ? 1 : 0;
        const std::string uid = b.value("uid", std::string());
        const int nid = P::session_audio_graph_add_plugin(c.session, track, path.c_str(), fmt, src, uid.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add plugin node: '" + path + "'");
        json r = ok(); r["node"] = nid;
        r["ready"] = P::session_audio_graph_node_plugin_ready(c.session, track, nid);   // 0 = still loading
        return r;
    };
    handlers_["audio_graph_add_source"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_source(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add audio source node: '" + op + "' (unknown or not an instrument)");
        json r = ok(); r["node"] = nid; return r;
    };
    handlers_["audio_graph_set_node_key_range"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        const int lo = b.value("lo", 0), hi = b.value("hi", 127);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_node_key_range_set(c.session, track, node, lo, hi);
        return ok();
    };
    handlers_["audio_graph_remove_node"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        if (!P::session_audio_graph_remove_node(c.session, track, node))
            return err(code::kBadArg, "node not removable (unknown, or an instrument/output)");
        return ok();
    };
    // ADR-0015: `kind` picks the signal the edge carries — "audio" (default; sums at the
    // destination) or "note" (merges). A note edge is how an instrument gets its notes once the
    // graph, rather than an invisible per-track broadcast, is doing the routing.
    handlers_["audio_graph_connect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string kind = b.value("kind", std::string("audio"));
        if (kind != "audio" && kind != "note") return err(code::kBadArg, "kind must be 'audio' or 'note'");
        if (!P::session_audio_graph_connect_kind(c.session, track, from, to, kind == "note" ? 1 : 0))
            return err(code::kBadArg, "edge rejected (duplicate, self-loop, unknown node, or would create a cycle)");
        return ok();
    };
    // A native NOTE EFFECT (ADR-0015), e.g. "Arp": notes in -> notes out, no audio. Wire MidiIn ->
    // it -> an instrument with NOTE edges.
    handlers_["audio_graph_add_note_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_note_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add note op '" + op + "'");
        json r = ok(); r["node"] = nid; return r;
    };
    // A native MODULATOR (ADR-0022), e.g. "LFO": no audio, emits a 0..1 control signal. Wire its
    // output to a param with audio_graph_connect_control.
    handlers_["audio_graph_add_mod_op"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string op = b.value("op", std::string());
        const int nid = P::session_audio_graph_add_mod_op(c.session, track, op.c_str());
        if (nid < 0) return err(code::kBadArg, "could not add modulator '" + op + "' (unknown op or not a modulator)");
        json r = ok(); r["node"] = nid; return r;
    };
    // Wire a modulator -> one param of a node (ADR-0022). amount is a fraction of the param's
    // declared range; bipolar straddles the base (an LFO for pitch/pan), unipolar runs up from it.
    handlers_["audio_graph_connect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_audio_graph_connect_control(c.session, track, from, to, param, amount, curve, invert, bipolar))
            return err(code::kBadArg, "control edge rejected (duplicate param, self-loop, unknown node, or would create a cycle)");
        return ok();
    };
    handlers_["audio_graph_disconnect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_disconnect_control(c.session, track, from, to, param);
        return ok();
    };
    // ADR-0022 P2a.2: a SESSION-level CROSS-TRACK control edge — a modulator on `src_track` node
    // `src_node` drives `dst_track` node `dst_node`'s `param`. Tracks are indices; nodes are stable
    // graph node ids; shape = amount/curve/invert/bipolar (same as the in-track edge).
    handlers_["session_connect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_connect_control(c.session, src_track, src_node, dst_track, dst_node, param, amount, curve, invert, bipolar))
            return err(code::kBadArg, "cross-track control edge rejected (unknown track/node, no param, or duplicate)");
        return ok();
    };
    handlers_["session_disconnect_control"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        P::session_disconnect_control(c.session, src_track, src_node, dst_track, dst_node, param);
        return ok();
    };
    handlers_["session_set_control_shape"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int src_track = b.value("src_track", 0), src_node = b.value("src_node", -1);
        const int dst_track = b.value("dst_track", 0), dst_node = b.value("dst_node", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, src_track, e)) return e;
        if (!need_track(c.session, dst_track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_set_control_shape(c.session, src_track, src_node, dst_track, dst_node, param, amount, curve, invert, bipolar))
            return err(code::kNotFound, "no cross-track control edge for that (src, dst, param)");
        return ok();
    };
    // Re-shape an existing modulation edge (ADR-0022) without rewiring.
    handlers_["audio_graph_set_control_shape"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        const int param = b.value("param", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const float amount = b.value("amount", 1.f), curve = b.value("curve", 0.f);
        const int invert = b.value("invert", false) ? 1 : 0, bipolar = b.value("bipolar", false) ? 1 : 0;
        if (!P::session_audio_graph_set_control_shape(c.session, track, from, to, param, amount, curve, invert, bipolar))
            return err(code::kNotFound, "no control edge for that (from, to, param)");
        return ok();
    };
    // The track's note stream as a NODE (ADR-0015). Wire its note edge into an instrument.
    handlers_["audio_graph_add_midi_in"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        const int nid = P::session_audio_graph_add_midi_in(c.session, track);
        if (nid < 0) return err(code::kInternal, "could not add a MidiIn node");
        json r = ok(); r["node"] = nid; return r;
    };
    handlers_["audio_graph_disconnect"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), from = b.value("from", -1), to = b.value("to", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_disconnect(c.session, track, from, to);
        return ok();
    };
    handlers_["audio_graph_set_node_param"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1), param = b.value("param", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        P::session_audio_graph_node_param_set(c.session, track, node, param, b.value("value", 0.f));
        return ok();
    };
    handlers_["audio_graph_set_node_param_by_name"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), node = b.value("node", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const std::string name = lower_copy_audio(b.value("name", b.value("param", std::string())));
        if (name.empty()) return err(code::kBadArg, "need name");
        const int pc = P::session_audio_graph_node_param_count(c.session, track, node);
        int hit = -1;
        for (int p = 0; p < pc; ++p) {
            if (lower_copy_audio(P::session_audio_graph_node_param_name(c.session, track, node, p)) == name) {
                if (hit >= 0) return err(code::kBadArg, "ambiguous param name '" + name + "'");
                hit = p;
            }
        }
        if (hit < 0) return err(code::kNotFound, "no audio graph node param named '" + name + "'");
        P::session_audio_graph_node_param_set(c.session, track, node, hit, b.value("value", 0.f));
        json r = ok(); r["param"] = hit; r["name"] = P::session_audio_graph_node_param_name(c.session, track, node, hit); return r;
    };
    handlers_["slice_to_midi"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0), scene = b.value("scene", 0);
        const int mode = b.value("mode", 1);   // 1=transients, 3=16-grid
        json e; if (!need_track(c.session, track, e)) return e;
        const int nt = P::session_slice_to_midi(c.session, track, scene, mode);
        if (nt < 0) return err(code::kBadArg, "slice-to-MIDI failed (not an audio clip, or no slices)");
        json r = ok(); r["track"] = nt; return r;
    };
    handlers_["list_audio_ops"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        auto op_json = [&](int index) {
            json jo; jo["type"] = P::session_audio_op_type(c.session, track, index);
            json ps = json::array();
            for (int p = 0; p < P::session_audio_op_param_count(c.session, track, index); ++p)
                ps.push_back({ {"name", P::session_audio_op_param_name(c.session, track, index, p)},
                               {"value", P::session_audio_op_param_get(c.session, track, index, p)} });
            jo["params"] = ps; return jo;
        };
        json r = ok();
        if (*P::session_audio_op_type(c.session, track, -1)) r["instrument"] = op_json(-1);
        json fx = json::array();
        for (int i = 0; i < P::session_audio_effect_count(c.session, track); ++i) fx.push_back(op_json(i));
        r["effects"] = fx; return r;
    };

    // AG-1: the track's authoritative audio graph (nodes + edges the RT executor runs). Distinct
    // from list_audio_ops (the linear device view): this reports the persistent topology model —
    // stable node ids, kinds, and (from_id -> to_id) edges — that the audio-graph UI + future
    // rewiring build on. graph_ok is false for VST3 / inline tracks (empty graph).
    handlers_["get_audio_graph"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", 0);
        json e; if (!need_track(c.session, track, e)) return e;
        static const char* kKind[] = { "instrument", "effect", "output", "midi_in", "note_effect", "modulator" };   // 3,4 ADR-0015; 5 ADR-0022
        json r = ok();
        r["graph_ok"]   = P::session_track_audio_graph_ok(c.session, track) != 0;
        r["output_id"]  = P::session_track_audio_graph_output_id(c.session, track);
        json nodes = json::array();
        for (int i = 0; i < P::session_track_audio_graph_node_count(c.session, track); ++i) {
            const int k   = P::session_track_audio_graph_node_kind(c.session, track, i);
            const int nid = P::session_track_audio_graph_node_id(c.session, track, i);
            int nin = 0, nout = 0;   // ADR-0015: does it take / emit notes?
            P::session_track_audio_graph_node_note_ports(c.session, track, i, &nin, &nout);
            json jn = { {"id",   nid},
                        {"gnid", P::session_track_audio_graph_node_gnid(c.session, track, i)},   // ADR-0022 P2b.3c: session-global id (-1 if unassigned)
                        {"kind", (k >= 0 && k < 6) ? kKind[k] : "unknown"},
                        {"note_in", nin != 0}, {"note_out", nout != 0},
                        {"type", P::session_track_audio_graph_node_type(c.session, track, i)} };
            if (k == 0) {   // source node: report its key range (a key-split shows disjoint ranges)
                int lo = 0, hi = 127;
                if (P::session_audio_graph_node_key_range_get(c.session, track, nid, &lo, &hi)) {
                    jn["key_lo"] = lo; jn["key_hi"] = hi;
                }
            }
            // ADR-0022: each param as base / value (resolved) / wired — the same shape the visuals
            // introspection dump uses. `value` == `base` unless a control edge drives the param.
            json params = json::array();
            for (int p = 0; p < P::session_audio_graph_node_param_count(c.session, track, nid); ++p) {
                const bool wired = P::session_audio_graph_node_param_wired(c.session, track, nid, p) != 0;
                json jp = { {"name",  P::session_audio_graph_node_param_name(c.session, track, nid, p)},
                            {"base",  P::session_audio_graph_node_param_get(c.session, track, nid, p)},
                            {"value", P::session_audio_graph_node_param_resolved(c.session, track, nid, p)},
                            {"wired", wired} };
                params.push_back(jp);
            }
            if (!params.empty()) jn["params"] = params;
            nodes.push_back(jn);
        }
        r["nodes"] = nodes;
        json edges = json::array();
        for (int i = 0; i < P::session_track_audio_graph_edge_count(c.session, track); ++i) {
            const int ek = P::session_track_audio_graph_edge_kind(c.session, track, i);
            // Which SIGNAL the wire carries. Without this an agent can't tell a note edge from an
            // audio one from a control one, and the three mean very different things.
            json je = { {"from", P::session_track_audio_graph_edge_from(c.session, track, i)},
                        {"to",   P::session_track_audio_graph_edge_to(c.session, track, i)},
                        {"kind", ek == 2 ? "control" : (ek == 1 ? "note" : "audio")} };
            if (ek == 2) {   // ADR-0022: a control edge carries its target param + shaper
                je["param"] = P::session_track_audio_graph_edge_dest_param(c.session, track, i);
                float amount = 1.f, curve = 0.f; int invert = 0, bipolar = 0;
                P::session_track_audio_graph_edge_control_shape(c.session, track, i, &amount, &curve, &invert, &bipolar);
                je["amount"] = amount; je["curve"] = curve;
                je["invert"] = invert != 0; je["bipolar"] = bipolar != 0;
            }
            edges.push_back(je);
        }
        r["edges"] = edges;
        // ADR-0022 P2a.2/P2a.3: the session-level CROSS-TRACK control edges (all of them — they are a
        // session concept, not per-track). Each is {src_track, src_node, dst_track, dst_node, param}
        // + shape, mirroring the in-track control-edge report.
        json xctl = json::array();
        for (int i = 0; i < P::session_xctl_count(c.session); ++i) {
            int st = 0, sn = 0, dt = 0, dn = 0, pr = 0, inv = 0, bip = 0; float am = 1.f, cv = 0.f;
            if (!P::session_xctl_get(c.session, i, &st, &sn, &dt, &dn, &pr, &am, &cv, &inv, &bip)) continue;
            xctl.push_back({ {"src_track", st}, {"src_node", sn}, {"dst_track", dt}, {"dst_node", dn},
                             {"param", pr}, {"amount", am}, {"curve", cv}, {"invert", inv != 0}, {"bipolar", bip != 0} });
        }
        r["xcontrol"] = xctl;
        return r;
    };

    // The catalog of NATIVE audio operators (the audio peer of list_operators, which is
    // visual): instruments (sources, no audio input) + effects (audio in->out). Names are
    // stable registry keys for set_track_audio_instrument / add_audio_effect.
    // The native-audio back-compat surface: instruments + effects with the full schema, like
    // list_operators. ADR-0023 step 7: `list_operator_catalog` is the unified (all-domain, plugins
    // included) discovery endpoint; this stays as the domain-scoped native-only view, sourcing its
    // entries from the same shared `native_audio_ops` builder so the two can't drift.
    handlers_["list_audio_operators"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        auto* reg = (c.vgraph && c.vgraph->registry()) ? c.vgraph->registry() : nullptr;
        json r = ok();
        r["instruments"] = control_json::native_audio_ops(c.session, reg, 1, "instrument");
        r["effects"]     = control_json::native_audio_ops(c.session, reg, 0, "audio_effect");
        return r;
    };

    // Everything that can START a signal: native instruments + every installed plugin the probe
    // classified as an instrument. (It used to return five hard-coded VST3 names, so a CLAP or a
    // native instrument could not begin a track at all.)
    handlers_["list_instruments"] = [](const ControlCtx& c, const json&) {
        json arr = json::array();
        for (int k = 0, n = c.session ? P::session_available_audio_op_count(c.session, 1) : 0; k < n; ++k)
            arr.push_back({ {"name", P::session_available_audio_op_name(c.session, 1, k)}, {"format", "native"} });
        for (int i = 0, n = P::plugin_count(); i < n; ++i) {
            const auto& p = P::plugin_at(i);
            if (p.cls != P::kClassInstrument) continue;
            arr.push_back({ {"name", p.name}, {"path", p.path},
                            {"format", P::plugin_format_name(p.format)} });
        }
        json r = ok(); r["instruments"] = arr; return r;
    };
    // Create a track. kind "instrument" (default) needs an "instrument" (catalog label or a
    // .vst3 path); kind "audio" makes a sampler track. Returns the new track index.
    handlers_["add_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const std::string kind = b.value("kind", std::string("instrument"));
        int idx = -1;
        if (kind == "audio") {
            idx = P::session_add_audio_track(c.session);
        } else {
            const std::string inst = b.value("instrument", std::string());
            if (inst.empty()) return err(code::kBadArg, "instrument track needs \"instrument\" (a catalog label or .vst3 path)");
            idx = P::session_add_instrument_track(c.session, inst.c_str());
            if (idx < 0) return err(code::kNotFound, "no instrument matched '" + inst + "' (or kMaxTracks reached)");
        }
        if (idx < 0) return err(code::kInternal, "add_track failed (kMaxTracks reached?)");
        json r = ok(); r["track"] = idx; return r;
    };
    // Append a scene (grid row): grows every track by one empty clip slot. Returns the new
    // scene index. Fails once the session reaches kMaxScenes.
    handlers_["add_scene"] = [](const ControlCtx& c, const json&) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int sc = P::session_add_scene(c.session);
        if (sc < 0) return err(code::kInternal, "add_scene failed (kMaxScenes reached?)");
        json r = ok(); r["scene"] = sc; return r;
    };
    // Delete a track. Also drops audio->visual mappings whose source encodes the removed
    // track's stable id; surviving mappings do not need index renumbering.
    handlers_["remove_track"] = [](const ControlCtx& c, const json& b) {
        if (!c.session) return err(code::kNoSession, "no session");
        const int track = b.value("track", -1);
        json e; if (!need_track(c.session, track, e)) return e;
        const int rid = P::session_track_id(c.session, track);   // capture the stable id before removal
        if (!P::session_remove_track(c.session, track)) return err(code::kInternal, "remove_track failed");
        int dropped = 0;
        if (c.graph) dropped = c.graph->drop_track_sources(rid);   // drop this track's mappings (id-based; survivors untouched)
        json r = ok(); r["removed"] = track; r["mappings_dropped"] = dropped; return r;
    };

}

}  // namespace vivid
