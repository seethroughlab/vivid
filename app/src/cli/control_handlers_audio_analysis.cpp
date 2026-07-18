#include "cli/control_handlers_audio_domains.h"
#include "cli/audio_analysis_tools.h"
#include "cli/control_handlers_internal.h"

#include "audio/audio_clip_shared.h"
#include "audio/vst3_host.h"
#include "transport.h"

#include <atomic>
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

}

}  // namespace vivid
