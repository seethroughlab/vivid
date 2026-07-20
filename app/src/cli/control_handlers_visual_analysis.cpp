// ADR-0024 Phase 6: visual perception. capture_frame reads the active output back to CPU and saves a
// viewable PNG; analyze_frame turns a frame into structured perception (blank/brightness/contrast/
// colors/activity/hash); compare_frames diffs two frames (saved images or the live output). The GPU
// readback lives in VisualGraph::read_output_pixels; the CPU analysis + PNG in image_analysis_tools.
#include "cli/control_handlers.h"
#include "cli/control_handlers_internal.h"
#include "cli/image_analysis_tools.h"
#include "gpu/visual_graph.h"
#include "platform/platform.h"

#include <cstdint>
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
        std::vector<uint8_t> ra, rb; uint32_t aw = 0, ah = 0, bw = 0, bh = 0; json aSrc, bSrc, e;
        if (!resolve_frame(c, b["a"], ra, aw, ah, aSrc, e)) return e;
        if (!resolve_frame(c, b["b"], rb, bw, bh, bSrc, e)) return e;
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
        json r = ok();
        r["a"] = { {"source", aSrc}, {"analysis", A} };
        r["b"] = { {"source", bSrc}, {"analysis", B} };
        r["delta"] = delta;
        r["summary"] = s;
        return r;
    };
}

}  // namespace vivid
