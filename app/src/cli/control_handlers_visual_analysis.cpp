// ADR-0024 Phase 6: visual perception. capture_frame reads the active output back to CPU and saves a
// viewable PNG; analyze_frame turns a frame into structured perception (blank/brightness/contrast/
// colors/activity/hash); compare_frames diffs two frames (saved images or the live output). The GPU
// readback lives in VisualGraph::read_output_pixels; the CPU analysis + PNG in image_analysis_tools.
#include "cli/control_handlers.h"
#include "cli/control_handlers_internal.h"
#include "cli/audio_analysis_tools.h"   // compare_audio_specs (compare_variations composes audio + visual)
#include "cli/image_analysis_tools.h"
#include "gpu/visual_graph.h"
#include "gpu/gpu_context.h"            // c.app->gpu — full-window screenshot (capture_interface)
#include "platform/platform.h"
#include "app/app.h"                    // c.app->reactivity (the per-frame perception ring)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace vivid {
namespace {

// Reactive-visuals loop: motion is no longer measured across calls. The frame loop pushes a sample
// per ~12fps into App::reactivity (visual metrics + the same-frame master-audio energy), so a SINGLE
// call reads a genuine time-series. These handlers just read c.app->reactivity — no readback here.
// The ring and these handlers both run on the frame/UI thread (process_pending), so this is race-free.
double steady_seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

}  // namespace

// Resolve a frame SPEC to RGBA8: {path} decodes a saved image; otherwise capture the ACTIVE output.
static bool resolve_frame(const ControlCtx& c, const json& spec, std::vector<uint8_t>& rgba,
                          uint32_t& w, uint32_t& h, json& source, json& e) {
    if (spec.is_object() && spec.contains("path")) {
        const std::string path = spec.value("path", std::string());
        if (!load_image(path, rgba, w, h)) { e = err(code::kIoError, "could not decode image: " + path); return false; }
        source = { {"kind", "file"}, {"path", path} };
        return true;
    }
    if (!c.vgraph) { e = err(code::kNoVgraph, "no visual graph"); return false; }
    if (!c.vgraph->read_output_pixels(rgba, w, h)) {
        e = err(code::kBadArg, "no visual output to capture (nothing feeds the Output node)"); return false;
    }
    source = { {"kind", "active_output"}, {"width", w}, {"height", h} };
    return true;
}

// The compare_frames CORE — two frame specs → {a, b, delta, summary}. Shared by the compare_frames
// handler and compare_variations. Returns null + sets `e` if a frame cannot resolve.
static json compare_frame_specs(const ControlCtx& c, const json& a, const json& b, json& e) {
    std::vector<uint8_t> ra, rb; uint32_t aw = 0, ah = 0, bw = 0, bh = 0; json aSrc, bSrc;
    if (!resolve_frame(c, a, ra, aw, ah, aSrc, e)) return json();
    if (!resolve_frame(c, b, rb, bw, bh, bSrc, e)) return json();
    const json A = analyze_rgba(ra.data(), aw, ah);
    const json B = analyze_rgba(rb.data(), bw, bh);
    const int ham = hash_hamming(A.value("hash", std::string()), B.value("hash", std::string()));
    auto d = [&](const char* k) { return B.value(k, 0.0) - A.value(k, 0.0); };
    json delta = { {"hash_hamming", ham}, {"brightness", d("brightness")},
                   {"contrast", d("contrast")}, {"activity", d("activity")} };
    std::string s = "B vs A: ";
    s += (ham <= 4 ? "visually near-identical" : ham <= 16 ? "moderately different" : "very different");
    const double db = d("brightness");
    s += std::string(", ") + (db > 0.03 ? "brighter" : db < -0.03 ? "darker" : "similar brightness");
    return { {"a", { {"source", aSrc}, {"analysis", A} }},
             {"b", { {"source", bSrc}, {"analysis", B} }},
             {"delta", delta}, {"summary", s} };
}

// ADR-0024 Phase 8 tail: merge an audio + visual comparison of two variations. Each of a,b may carry
// {audio: <source spec>, frame: <frame spec>}; a dimension is compared only when BOTH sides supply it.
// Returns {audio?, visual?, dims:[...]} or null (e set) on error / when no dimension is present.
static json compare_variation_object(const ControlCtx& c, const json& a, const json& b, json& e) {
    const bool hasAudio = a.is_object() && b.is_object() && a.contains("audio") && b.contains("audio");
    const bool hasFrame = a.is_object() && b.is_object() && a.contains("frame") && b.contains("frame");
    if (!hasAudio && !hasFrame) { e = err(code::kBadArg, "each of a,b needs an 'audio' and/or 'frame' spec to compare"); return json(); }
    json out = json::object(); json dims = json::array();
    if (hasAudio) { const json aud = compare_audio_specs(c, a["audio"], b["audio"], 16, e); if (aud.is_null()) return json(); out["audio"] = aud; dims.push_back("audio"); }
    if (hasFrame) { const json vis = compare_frame_specs(c, a["frame"], b["frame"], e);      if (vis.is_null()) return json(); out["visual"] = vis; dims.push_back("visual"); }
    out["dims"] = dims;
    return out;
}

void register_visual_analysis_handlers(Handlers& handlers_) {
    // Capture the active output to CPU + a PNG. `path` optional (else <user_data>/captures/frame.png).
    handlers_["capture_frame"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no visual graph");
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0;
        if (!c.vgraph->read_output_pixels(rgba, w, h)) {
            json r = ok();
            r["captured"] = false;
            r["reason"] = "no visual output (nothing feeds the Output node — the canvas is empty)";
            return r;
        }
        std::string path = b.value("path", std::string());
        if (path.empty()) {
            namespace fs = std::filesystem;
            const fs::path dir = fs::path(vivid::platform::user_data_dir()) / "captures";
            std::error_code ec; fs::create_directories(dir, ec);
            path = (dir / "frame.png").string();
        }
        const bool saved = write_png(path, rgba.data(), w, h);
        const json a = analyze_rgba(rgba.data(), w, h);
        json r = ok();
        r["captured"] = true; r["width"] = w; r["height"] = h;
        if (saved) r["path"] = path; else r["warning"] = "capture ok but PNG write failed: " + path;
        r["is_blank"] = a.value("is_blank", false);
        r["brightness"] = a.value("brightness", 0.0);
        r["summary"] = "Captured " + std::to_string(w) + "x" + std::to_string(h) +
                       (a.value("is_blank", false) ? " (BLANK — " + a.value("blank_reason", std::string()) + ")" : "") +
                       (saved ? " -> " + path : "");
        return r;
    };
    // Screenshot the WHOLE interface — UI chrome + panels + node graph + canvas — not just the visual
    // output (that's capture_frame). Reads the app's own composited window framebuffer (no screen-
    // recording permission). Armed here; taken in end_frame after the MSAA resolve → the PNG lands ~1
    // frame later. Useful for docs and for an agent to actually SEE the interface it's driving.
    handlers_["capture_interface"] = [](const ControlCtx& c, const json& b) {
        if (!c.app || !c.app->gpu) return err(code::kBadArg, "no gpu context");
        if (!c.app->gpu->surface_supports_copy_src())
            return err(code::kInternal, "this GPU's surface is not copyable — interface capture unavailable");
        std::string path = b.value("path", std::string());
        if (path.empty()) {
            namespace fs = std::filesystem;
            const fs::path dir = fs::path(vivid::platform::user_data_dir()) / "captures";
            std::error_code ec; fs::create_directories(dir, ec);
            path = (dir / "interface.png").string();
        }
        c.app->gpu->request_interface_capture(path);
        json r = ok();
        r["path"] = path;
        r["pending"] = true;
        r["summary"] = "interface screenshot queued -> " + path +
                       " (whole UI, not just the canvas; written within ~1 frame)";
        return r;
    };
    // Structured perception of the active output (or a saved image via {path}).
    handlers_["analyze_frame"] = [](const ControlCtx& c, const json& b) {
        std::vector<uint8_t> rgba; uint32_t w = 0, h = 0; json source, e;
        if (!resolve_frame(c, b, rgba, w, h, source, e)) return e;
        const json a = analyze_rgba(rgba.data(), w, h);
        json r = ok();
        r["source"] = source;
        r["analysis"] = a;
        r["summary"] = a.value("is_blank", false)
            ? ("Frame is BLANK (" + a.value("blank_reason", std::string()) + ")")
            : ("Frame " + std::to_string(w) + "x" + std::to_string(h) + ": brightness=" +
               std::to_string(a.value("brightness", 0.0)) + ", contrast=" + std::to_string(a.value("contrast", 0.0)) +
               ", activity=" + std::to_string(a.value("activity", 0.0)));
        return r;
    };
    // Before/after: two frame specs `a` and `b` (each {path:'...'} or {} to capture the live output).
    handlers_["compare_frames"] = [](const ControlCtx& c, const json& b) {
        if (!b.contains("a") || !b.contains("b"))
            return err(code::kBadArg, "need two frame specs: a and b (each {path:'...'} for a saved image, or {} to capture the current output)");
        json e; const json res = compare_frame_specs(c, b["a"], b["b"], e);
        if (res.is_null()) return e;
        json r = ok(); r.update(res); return r;
    };
    // ADR-0024 Phase 8 tail: compare two VARIATIONS across audio AND visual at once. Each of a,b is
    // {audio: <source spec>, frame: <frame spec>}; composes compare_audio + compare_frames.
    handlers_["compare_variations"] = [](const ControlCtx& c, const json& b) {
        if (!b.contains("a") || !b.contains("b"))
            return err(code::kBadArg, "need a and b, each with an 'audio' (source spec) and/or 'frame' (frame spec)");
        json e; const json cmp = compare_variation_object(c, b["a"], b["b"], e);
        if (cmp.is_null()) return e;
        std::string s = "B vs A —";
        if (cmp.contains("audio"))  s += " audio: " + cmp["audio"].value("summary", std::string()) + ";";
        if (cmp.contains("visual")) s += " visual: " + cmp["visual"].value("summary", std::string()) + ";";
        json r = ok(); r.update(cmp); r["summary"] = s; return r;
    };
    // ADR-0024 Phase 8 tail: same inputs as compare_variations, but articulate the notable differences
    // as measured TRADEOFFS (each with a direction + magnitude + good/bad note). Optional `criteria`
    // (list of aspect keywords) narrows what to emphasize.
    handlers_["explain_tradeoffs"] = [](const ControlCtx& c, const json& b) {
        if (!b.contains("a") || !b.contains("b"))
            return err(code::kBadArg, "need a and b, each with an 'audio' (source spec) and/or 'frame' (frame spec)");
        json e; const json cmp = compare_variation_object(c, b["a"], b["b"], e);
        if (cmp.is_null()) return e;
        std::vector<std::string> crit;
        if (b.contains("criteria") && b["criteria"].is_array())
            for (const auto& x : b["criteria"]) if (x.is_string()) crit.push_back(lower_copy_audio(x.get<std::string>()));
        json tradeoffs = json::array();
        auto want = [&](const std::string& aspect) {
            if (crit.empty()) return true;
            for (const auto& k : crit) if (aspect.find(k) != std::string::npos || k.find(aspect) != std::string::npos) return true;
            return false; };
        auto add = [&](const std::string& aspect, double v, double thr, const char* up, const char* down, const char* unit) {
            if (std::fabs(v) < thr || !want(aspect)) return;
            char buf[64]; std::snprintf(buf, sizeof buf, "%+.2f%s", v, unit);
            tradeoffs.push_back({ {"aspect", aspect}, {"delta", v}, {"note", std::string(v > 0 ? up : down) + " (" + buf + ")"} }); };
        if (cmp.contains("audio")) {
            const json ad = cmp["audio"].value("delta", json::object());
            add("loudness",   ad.value("loudness_db", 0.0), 0.5, "B louder", "B quieter", " dB");
            add("clipping",   static_cast<double>(ad.value("clipping_samples", 0)), 1.0, "B clips more (worse)", "B clips less (better)", " samples");
            add("brightness", ad.value("spectral_centroid_proxy_hz", 0.0), 50.0, "B brighter", "B darker", " Hz");
            add("transients", ad.value("transient_density_per_second", 0.0), 0.3, "B more transient-dense", "B calmer", "/s");
        }
        if (cmp.contains("visual")) {
            const json vd = cmp["visual"].value("delta", json::object());
            add("change",     static_cast<double>(vd.value("hash_hamming", 0)), 5.0, "B looks different", "B looks similar", " ham");
            add("brightness", vd.value("brightness", 0.0), 0.03, "B visually brighter", "B visually darker", "");
            add("activity",   vd.value("activity", 0.0), 0.02, "B busier", "B calmer", "");
        }
        json r = ok(); r.update(cmp);
        r["tradeoffs"] = tradeoffs;
        r["summary"] = tradeoffs.empty() ? "A and B are effectively equivalent on the measured criteria"
                                         : (std::to_string(tradeoffs.size()) + " notable difference(s) between A and B");
        return r;
    };
    // Motion over a recent window, read from the always-on reactivity ring. Reliable in a SINGLE call
    // now (the frame loop fills the ring at ~12fps). `duration_seconds` sets the window. The app must
    // be playing for motion to register.
    handlers_["analyze_visual_motion"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kBadArg, "no app context");
        const double window = b.value("duration_seconds", 2.0);
        c.app->reactivity.arm(steady_seconds());   // keep the ring sampling while perception is in use
        const json m = c.app->reactivity.motion(window, steady_seconds());
        json r = ok();
        r.update(m);
        const int n = m.value("samples", 0);
        r["summary"] = n < 2
            ? "no motion history yet — is the app playing? (the ring fills at ~12fps while running)"
            : (m.value("is_moving", false)
                   ? "moving (avg change " + std::to_string(m.value("inter_frame_change", 0.0)) + "/sample over " + std::to_string(n) + " samples)"
                   : "static / near-static over " + std::to_string(n) + " samples");
        return r;
    };
    // A rolled-up view of the live output — the current frame's perception plus recent motion, one call.
    handlers_["summarize_visual_output"] = [](const ControlCtx& c, const json& b) {
        if (!c.vgraph) return err(code::kNoVgraph, "no visual graph");
        std::vector<uint8_t> px; uint32_t w = 0, h = 0;
        if (!c.vgraph->read_output_pixels(px, w, h)) {
            json r = ok(); r["captured"] = false; r["reason"] = "no visual output (nothing feeds the Output node)"; return r;
        }
        const json a = analyze_rgba(px.data(), w, h);
        const double window = b.value("duration_seconds", 2.0);
        if (c.app) c.app->reactivity.arm(steady_seconds());
        json r = ok();
        r["frame"] = a;
        r["motion"] = c.app ? c.app->reactivity.motion(window, steady_seconds())
                            : json{ {"samples", 0}, {"motion_score", 0.0}, {"is_moving", false} };
        r["summary"] = a.value("is_blank", false)
            ? ("Output is BLANK (" + a.value("blank_reason", std::string()) + ")")
            : (std::to_string(w) + "x" + std::to_string(h) + ": brightness=" + std::to_string(a.value("brightness", 0.0)) +
               ", activity=" + std::to_string(a.value("activity", 0.0)) +
               (r["motion"].value("is_moving", false) ? ", MOVING" : ", static"));
        return r;
    };
    // Reactive-visuals loop: the primary "measure a change" tool. Three modes over the reactivity ring:
    //   mode="frame" — current-frame perception (brightness/contrast/activity/blank/hash)
    //   mode="audio" — windowed master energy/bands/onsets sampled at the frame rate
    //   mode="av"    — the three reactivity lenses: per-axis correlations (energy↔brightness/motion/
    //                  contrast), per-band correlations, and onset-aligned response rate + latency.
    // Read all three av lenses: overall correlation ≈ 0 with a high onset_response_rate is not "dead"
    // — it's event-driven reactivity that Pearson can't see. window_seconds defaults to 3 (av needs a
    // few seconds of history). node_id is accepted but currently scoped to the whole output.
    handlers_["analyze_output"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kBadArg, "no app context");
        const std::string mode = b.value("mode", std::string("frame"));
        const double now = steady_seconds();
        c.app->reactivity.arm(now);   // arm the ring so it samples while you're measuring (0 cost otherwise)
        json r = ok();
        r["mode"] = mode;
        if (mode == "frame") {
            if (!c.vgraph) return err(code::kNoVgraph, "no visual graph");
            std::vector<uint8_t> px; uint32_t w = 0, h = 0;
            if (!c.vgraph->read_output_pixels(px, w, h))
                return err(code::kBadArg, "no visual output to capture (nothing feeds the Output node)");
            const json a = analyze_rgba(px.data(), w, h);
            r["metrics"] = { {"frame", a} };
            r["summary"] = a.value("is_blank", false)
                ? ("Frame is BLANK (" + a.value("blank_reason", std::string()) + ")")
                : ("brightness=" + std::to_string(a.value("brightness", 0.0)) +
                   ", contrast=" + std::to_string(a.value("contrast", 0.0)) +
                   ", activity=" + std::to_string(a.value("activity", 0.0)));
            return r;
        }
        if ((mode == "av" || mode == "audio") && !c.app->reactivity.enabled()) {
            r["summary"] = "perception is DISABLED — call set_perception_enabled(true) to sample the "
                           "reactivity ring (it's off so it can't cost framerate while you watch)";
            r["disabled"] = true;
            return r;
        }
        const double window = b.value("window_seconds", 3.0);
        if (mode == "audio") {
            r["metrics"] = { {"audio", c.app->reactivity.audio_window(window, now)} };
            r["summary"] = "windowed audio energy over " + std::to_string(window) + "s";
            return r;
        }
        if (mode == "av") {
            const json av = c.app->reactivity.av_metrics(window, now);
            r["metrics"] = { {"reactivity", av} };
            if (av.value("status", std::string()) == "insufficient_samples") {
                r["summary"] = "insufficient samples — is the app playing? call again after ~0.5s";
            } else {
                char buf[192];
                std::snprintf(buf, sizeof buf,
                    "onset_response_rate=%.2f, energy_motion_corr=%.2f, latency=%.0fms over %d samples",
                    av.value("onset_response_rate", 0.0), av.value("energy_motion_correlation", 0.0),
                    av.value("reactivity_latency_ms", 0.0), av.value("samples", 0));
                r["summary"] = buf;
            }
            return r;
        }
        return err(code::kBadArg, "unknown mode '" + mode + "' (expected frame|audio|av)");
    };
    // Master switch for the perception ring. Disable it so an agent's analyze_output/judge calls can't
    // arm the ring and drop the live framerate while you watch; re-enable to measure again.
    handlers_["set_perception_enabled"] = [](const ControlCtx& c, const json& b) {
        if (!c.app) return err(code::kBadArg, "no app context");
        const bool on = b.value("enabled", true);
        c.app->reactivity.set_enabled(on);
        json r = ok();
        r["enabled"] = on;
        r["summary"] = on ? "perception ENABLED (ring samples on-demand while measuring)"
                          : "perception DISABLED (zero readback cost; analyze_output(av|audio) returns no data until re-enabled)";
        return r;
    };
    handlers_["perception_status"] = [](const ControlCtx& c, const json&) {
        if (!c.app) return err(code::kBadArg, "no app context");
        json r = ok();
        r["enabled"] = c.app->reactivity.enabled();
        r["samples"] = c.app->reactivity.samples();
        return r;
    };
}

}  // namespace vivid
