#include "app/reactivity_ring.h"

#include "cli/image_analysis_tools.h"   // analyze_rgba, hash_hamming
#include "transport.h"

#include <algorithm>
#include <cmath>

namespace vivid {
namespace {

// Nearest-neighbour decimation of a tightly-packed RGBA8 frame to a <=maxEdge thumbnail. Cheap and
// good enough for both the metric proxies (brightness/contrast/activity/hash) and the Phase-2
// montage. Writes dst (dw*dh*4) and reports the chosen thumbnail size.
void downsample_rgba(const uint8_t* src, uint32_t w, uint32_t h, uint32_t maxEdge,
                     std::vector<uint8_t>& dst, uint16_t& dw, uint16_t& dh) {
    if (!src || w == 0 || h == 0) { dst.clear(); dw = dh = 0; return; }
    const uint32_t longest = std::max(w, h);
    uint32_t ow = w, oh = h;
    if (longest > maxEdge) {
        const double scale = static_cast<double>(maxEdge) / static_cast<double>(longest);
        ow = std::max<uint32_t>(1, static_cast<uint32_t>(w * scale));
        oh = std::max<uint32_t>(1, static_cast<uint32_t>(h * scale));
    }
    dst.assign(static_cast<size_t>(ow) * oh * 4, 0);
    for (uint32_t y = 0; y < oh; ++y) {
        const uint32_t sy = std::min(h - 1, static_cast<uint32_t>((static_cast<uint64_t>(y) * h) / oh));
        for (uint32_t x = 0; x < ow; ++x) {
            const uint32_t sx = std::min(w - 1, static_cast<uint32_t>((static_cast<uint64_t>(x) * w) / ow));
            const uint8_t* sp = src + (static_cast<size_t>(sy) * w + sx) * 4;
            uint8_t* dp = dst.data() + (static_cast<size_t>(y) * ow + x) * 4;
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    dw = static_cast<uint16_t>(ow);
    dh = static_cast<uint16_t>(oh);
}

// Pearson correlation of two equal-length series. Returns 0 when either has ~no variance (a flat
// series can't correlate) rather than NaN, so the JSON is always finite.
double pearson(const std::vector<double>& a, const std::vector<double>& b) {
    const size_t n = std::min(a.size(), b.size());
    if (n < 2) return 0.0;
    double ma = 0, mb = 0;
    for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (size_t i = 0; i < n; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    if (saa < 1e-12 || sbb < 1e-12) return 0.0;
    double r = sab / std::sqrt(saa * sbb);
    return std::clamp(r, -1.0, 1.0);
}

// Temporal motion: mean absolute luma difference between two same-size RGBA thumbnails, normalized to
// [0,1]. This is far more sensitive to the smooth scale/color motion of procedural visuals than an
// 8x8 average-hash Hamming (which barely flips bits when a blob breathes) — the frame-diff registers
// exactly the "something moved" signal onset-response and correlation need.
float frame_diff(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, uint16_t w, uint16_t h) {
    if (a.empty() || a.size() != b.size()) return 0.f;
    const size_t px = static_cast<size_t>(w) * h;
    if (px == 0) return 0.f;
    double acc = 0.0;
    for (size_t i = 0; i < px; ++i) {
        const size_t o = i * 4;
        const double la = 0.299 * a[o] + 0.587 * a[o + 1] + 0.114 * a[o + 2];
        const double lb = 0.299 * b[o] + 0.587 * b[o + 1] + 0.114 * b[o + 2];
        acc += std::fabs(la - lb);
    }
    return static_cast<float>(acc / (px * 255.0));
}

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const size_t m = v.size() / 2;
    return (v.size() % 2) ? v[m] : 0.5 * (v[m - 1] + v[m]);
}

}  // namespace

void ReactivityRing::push(const uint8_t* rgba, uint32_t w, uint32_t h, const Transport& t, double now) {
    Sample smp;
    smp.t = now;
    downsample_rgba(rgba, w, h, kThumbMax, smp.thumb, smp.thumb_w, smp.thumb_h);
    if (!smp.thumb.empty()) {
        const json a = analyze_rgba(smp.thumb.data(), smp.thumb_w, smp.thumb_h);
        smp.brightness = static_cast<float>(a.value("brightness", 0.0));
        smp.contrast   = static_cast<float>(a.value("contrast", 0.0));
        smp.activity   = static_cast<float>(a.value("activity", 0.0));
        smp.hash       = a.value("hash", std::string());
    }
    smp.energy    = t.level.load(std::memory_order_relaxed);
    smp.band_low  = t.band_low.load(std::memory_order_relaxed);
    smp.band_mid  = t.band_mid.load(std::memory_order_relaxed);
    smp.band_high = t.band_high.load(std::memory_order_relaxed);
    smp.transient = t.transient.load(std::memory_order_relaxed);

    if (!s_.empty()) {
        const Sample& prev = s_.back();
        // Temporal frame-diff motion (same-size thumbnails). Falls back to 0 if sizes differ (e.g. the
        // output resolution just changed) — one stale sample, self-corrects next frame.
        if (prev.thumb_w == smp.thumb_w && prev.thumb_h == smp.thumb_h)
            smp.motion = frame_diff(prev.thumb, smp.thumb, smp.thumb_w, smp.thumb_h);
        // Onset = transient crosses the threshold from below (a discrete new audio event).
        smp.onset = smp.transient >= kOnsetThresh && prev.transient < kOnsetThresh;
    }

    s_.push_back(std::move(smp));
    while (s_.size() > 2 && now - s_.front().t > kWindowMax) s_.pop_front();
    last_push_ = now;
}

std::vector<const ReactivityRing::Sample*> ReactivityRing::window_samples(double window, double now) const {
    std::vector<const Sample*> out;
    for (auto it = s_.rbegin(); it != s_.rend(); ++it) {
        if (now - it->t > window && out.size() >= 2) break;
        out.push_back(&(*it));
    }
    std::reverse(out.begin(), out.end());
    return out;
}

json ReactivityRing::motion(double window, double now) const {
    const auto win = window_samples(window, now);
    if (win.size() < 2)
        return { {"samples", static_cast<int>(win.size())}, {"span_seconds", 0.0},
                 {"inter_frame_change", 0.0}, {"motion_score", 0.0}, {"is_moving", false},
                 {"brightness_range", 0.0} };
    double sumMotion = 0.0; double bmin = 1e9, bmax = -1e9;
    for (size_t i = 0; i < win.size(); ++i) {
        bmin = std::min<double>(bmin, win[i]->brightness);
        bmax = std::max<double>(bmax, win[i]->brightness);
        if (i > 0) sumMotion += win[i]->motion;   // motion is already hamming/64 vs the prior sample
    }
    const double meanMotionHam = (sumMotion / (win.size() - 1)) * 64.0;   // back to hamming units
    const double span = win.back()->t - win.front()->t;
    return { {"samples", static_cast<int>(win.size())}, {"span_seconds", span},
             {"inter_frame_change", meanMotionHam},
             {"motion_score", std::min(1.0, meanMotionHam / 32.0)},
             {"is_moving", meanMotionHam > 3.0}, {"brightness_range", bmax - bmin} };
}

json ReactivityRing::audio_window(double window, double now) const {
    const auto win = window_samples(window, now);
    if (win.empty()) return { {"samples", 0}, {"rms", 0.0} };
    double e = 0, tr = 0, bl = 0, bm = 0, bh = 0; int onsets = 0;
    for (const Sample* p : win) {
        e += p->energy; tr += p->transient; bl += p->band_low; bm += p->band_mid; bh += p->band_high;
        if (p->onset) ++onsets;
    }
    const double n = static_cast<double>(win.size());
    return { {"samples", static_cast<int>(win.size())}, {"rms", e / n}, {"transient", tr / n},
             {"band_low", bl / n}, {"band_mid", bm / n}, {"band_high", bh / n}, {"onsets", onsets} };
}

json ReactivityRing::av_metrics(double window, double now) const {
    const auto win = window_samples(window, now);
    if (win.size() < 4)
        return { {"status", "insufficient_samples"}, {"samples", static_cast<int>(win.size())},
                 {"note", "the reactivity ring needs a few frames of history; ensure the app is "
                          "playing and call again after ~0.5s"} };

    std::vector<double> energy, brightness, contrast, motion, bl, bm, bh;
    energy.reserve(win.size());
    for (const Sample* p : win) {
        energy.push_back(p->energy);
        brightness.push_back(p->brightness);
        contrast.push_back(p->contrast);
        motion.push_back(p->motion);
        bl.push_back(p->band_low); bm.push_back(p->band_mid); bh.push_back(p->band_high);
    }

    // Onset-aligned reactivity: for each onset, is there a motion SPIKE within ~400ms that rises
    // clearly above the window's typical motion? A baseline-relative test (not a fixed absolute) is
    // scale-robust — frame-diff luma deltas are small in absolute terms — and correctly separates a
    // punctual burst (spikes above baseline → high response rate) from continuous coupling (motion
    // tracks energy smoothly with no per-onset spike → low response rate, high correlation instead).
    double baselineMotion = median(motion);
    const double respondThresh = baselineMotion + std::max(0.002, 0.5 * baselineMotion);
    int onsets = 0, responded = 0;
    std::vector<double> latencies;
    for (size_t i = 0; i < win.size(); ++i) {
        if (!win[i]->onset) continue;
        ++onsets;
        const double t0 = win[i]->t;
        double bestMotion = 0.0, bestT = t0;
        for (size_t j = i; j < win.size(); ++j) {
            if (win[j]->t - t0 > 0.4) break;
            if (win[j]->motion > bestMotion) { bestMotion = win[j]->motion; bestT = win[j]->t; }
        }
        if (bestMotion > respondThresh) { ++responded; latencies.push_back((bestT - t0) * 1000.0); }
    }

    double meanBrightness = 0, meanMotion = 0, meanContrast = 0;
    for (const Sample* p : win) { meanBrightness += p->brightness; meanMotion += p->motion; meanContrast += p->contrast; }
    const double n = static_cast<double>(win.size());
    meanBrightness /= n; meanMotion /= n; meanContrast /= n;

    auto bandBlock = [&](const std::vector<double>& axis) {
        return json{ {"bass", pearson(bl, axis)}, {"mid", pearson(bm, axis)}, {"treble", pearson(bh, axis)} };
    };

    return {
        {"status", "ok"},
        {"samples", static_cast<int>(win.size())},
        {"span_seconds", win.back()->t - win.front()->t},
        {"mean_brightness", meanBrightness},
        {"contrast", meanContrast},
        {"motion_magnitude", meanMotion},
        {"energy_brightness_correlation", pearson(energy, brightness)},
        {"energy_motion_correlation", pearson(energy, motion)},
        {"energy_contrast_correlation", pearson(energy, contrast)},
        {"detected_onsets", onsets},
        {"onset_response_rate", onsets ? static_cast<double>(responded) / onsets : 0.0},
        {"reactivity_latency_ms", median(latencies)},
        {"band_brightness_correlations", bandBlock(brightness)},
        {"band_motion_correlations", bandBlock(motion)},
        {"band_contrast_correlations", bandBlock(contrast)},
    };
}

}  // namespace vivid
