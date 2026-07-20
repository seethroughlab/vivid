#include "cli/control_handlers_audio_domains.h"
#include "cli/audio_analysis_tools.h"
#include "cli/control_handlers_internal.h"

#include "audio/audio_clip_shared.h"
#include "audio/vst3_host.h"
#include "transport.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

void register_audio_analysis_handlers(Handlers& handlers_) {
    namespace P = vivid::session;
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
    // ADR-0024 Phase 5: per-band energy spectrum. Source spec is the top-level body ({path} |
    // {track,scene} | live/master); `bands` = octave|mel|linear.
    handlers_["analyze_spectrum"] = [](const ControlCtx& c, const json& b) {
        std::vector<float> L, R; uint32_t sr = 0; json source, e;
        if (!resolve_audio_source(c, b, 4.0, L, R, sr, source, e)) return e;
        const std::string mode = b.value("bands", std::string("octave"));
        if (mode != "octave" && mode != "mel" && mode != "linear")
            return err(code::kBadArg, "bands must be octave, mel, or linear");
        json spec = analyze_spectrum_bands(L, R, sr, mode);
        // Loudest band → a compact summary the agent can read without scanning the array.
        double best = -1e9; json loudest;
        for (const auto& band : spec.value("bands", json::array()))
            if (band.value("rms", 0.0) > best) { best = band.value("rms", 0.0); loudest = band; }
        json r = ok();
        r["source"] = source;
        r["spectrum"] = spec;
        r["summary"] = "Spectrum (" + mode + "): centroid=" +
                       std::to_string(spec.value("spectral_centroid_hz", 0.0)) + "Hz, loudest band ~" +
                       std::to_string(loudest.value("center_hz", 0.0)) + "Hz";
        return r;
    };
    // ADR-0024 Phase 5: before/after comparison. Two source specs `a` and `b` (each {path} |
    // {track,scene} | live/master); returns each analysis, the deltas, and a plain verdict.
    handlers_["compare_audio"] = [](const ControlCtx& c, const json& b) {
        if (!b.contains("a") || !b.contains("b"))
            return err(code::kBadArg, "need two source specs: a and b (each {path} | {track,scene} | {source:'master',duration_seconds})");
        std::vector<float> aL, aR, bL, bR; uint32_t asr = 0, bsr = 0; json aSrc, bSrc, e;
        if (!resolve_audio_source(c, b["a"], 4.0, aL, aR, asr, aSrc, e)) return e;
        if (!resolve_audio_source(c, b["b"], 4.0, bL, bR, bsr, bSrc, e)) return e;
        json A = analyze_pcm(aL, aR, asr, b.value("windows", 16));
        json B = analyze_pcm(bL, bR, bsr, b.value("windows", 16));
        auto d = [&](const char* k) { return B.value(k, 0.0) - A.value(k, 0.0); };
        const double loud_db = (A.value("rms", 0.0) > 1e-9 && B.value("rms", 0.0) > 1e-9)
                             ? 20.0 * std::log10(B.value("rms", 0.0) / A.value("rms", 0.0)) : 0.0;
        const double d_centroid = d("spectral_centroid_proxy_hz");
        const double d_trans = d("transient_density_per_second");
        const json aB = A.value("bands", json::object()), bB = B.value("bands", json::object());
        json delta = {
            {"rms", d("rms")}, {"loudness_db", loud_db}, {"peak", d("peak")},
            {"crest_factor", d("crest_factor")}, {"spectral_centroid_proxy_hz", d_centroid},
            {"transient_density_per_second", d_trans},
            {"clipping_samples", B.value("clipping_samples", 0) - A.value("clipping_samples", 0)},
            {"bands", { {"low", bB.value("low", 0.0) - aB.value("low", 0.0)},
                        {"mid", bB.value("mid", 0.0) - aB.value("mid", 0.0)},
                        {"high", bB.value("high", 0.0) - aB.value("high", 0.0)} }}
        };
        std::string s = "B vs A: ";
        s += (loud_db > 0.5 ? "louder" : loud_db < -0.5 ? "quieter" : "similar loudness");
        s += std::string(", ") + (d_centroid > 50 ? "brighter" : d_centroid < -50 ? "darker" : "similar brightness");
        s += std::string(", ") + (d_trans > 0.3 ? "more transient-dense" : d_trans < -0.3 ? "less transient-dense" : "similar transient density");
        if (B.value("clipping_samples", 0) > A.value("clipping_samples", 0)) s += ", MORE clipping";
        json r = ok();
        r["a"] = { {"source", aSrc}, {"analysis", A} };
        r["b"] = { {"source", bSrc}, {"analysis", B} };
        r["delta"] = delta;
        r["summary"] = s;
        return r;
    };

}

}  // namespace vivid
