// ADR-0024 Phase 6: visual perception. capture_frame reads the active output back to CPU and saves a
// viewable PNG; analyze_frame turns a frame into structured perception (blank/brightness/contrast/
// colors/activity/hash); compare_frames diffs two frames (saved images or the live output). The GPU
// readback lives in VisualGraph::read_output_pixels; the CPU analysis + PNG in image_analysis_tools.
#include "cli/control_handlers.h"
#include "cli/control_handlers_internal.h"
#include "cli/audio_analysis_tools.h"   // compare_audio_specs (compare_variations composes audio + visual)
#include "cli/image_analysis_tools.h"
#include "gpu/visual_graph.h"
#include "platform/platform.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace vivid {

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
}

}  // namespace vivid
