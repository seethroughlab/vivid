#pragma once
// Pure math helpers for audio clip editor hit-testing and geometry.
// No GLFW, no WGPU, no runtime headers — testable in isolation.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace audio_clip_ed {

struct LoopBounds {
    float start = 0.0f;
    float end = 1.0f;
};

struct WarpPoint {
    uint32_t source_sample = 0;
    double beat = 0.0;
};

struct TransientPoint {
    uint32_t source_sample = 0;
    float strength = 0.0f;
};

struct SliceRegion {
    uint32_t start = 0;
    uint32_t end = 0;
};

// Returns true if mx is within grab_px of handle_x (for a vertical handle line).
inline bool hit_handle_x(float mx, float handle_x, float grab_px = 6.0f) {
    return (mx >= handle_x - grab_px) && (mx < handle_x + grab_px);
}

// Clamp a normalized value to [0, 1].
inline float norm_clamp(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float min_region() {
    return 0.01f;
}

inline LoopBounds effective_loop_bounds(float clip_start, float clip_end,
                                        float loop_start, float loop_end) {
    const float cs = norm_clamp(clip_start);
    const float ce = std::max(cs + 1e-4f, norm_clamp(clip_end));
    const float ls = std::max(cs, std::min(loop_start, ce - 1e-4f));
    const float le = std::max(ls + 1e-4f, std::min(loop_end, ce));
    return {ls, le};
}

inline float pixel_delta_to_norm(float dx, float viewport_size, float screen_width) {
    const float safe_w = screen_width > 1.0f ? screen_width : 1.0f;
    return dx * viewport_size / safe_w;
}

inline float drag_clip_start(float original, float delta_norm, float clip_end) {
    return std::max(0.0f, std::min(original + delta_norm, clip_end - min_region()));
}

inline float drag_clip_end(float original, float delta_norm, float clip_start) {
    return std::max(clip_start + min_region(), std::min(original + delta_norm, 1.0f));
}

inline float drag_loop_start(float original, float delta_norm,
                             float clip_start, float loop_end) {
    return std::max(clip_start, std::min(original + delta_norm, loop_end - min_region()));
}

inline float drag_loop_end(float original, float delta_norm,
                           float loop_start, float clip_end) {
    return std::max(loop_start + min_region(), std::min(original + delta_norm, clip_end));
}

inline LoopBounds drag_loop_body(float original_start, float original_end,
                                 float delta_norm, float clip_start, float clip_end) {
    const float clip_len = std::max(1e-4f, clip_end - clip_start);
    const float length = std::min(clip_len, std::max(1e-4f, original_end - original_start));
    const float max_start = clip_end - length;
    const float new_start = std::max(clip_start, std::min(original_start + delta_norm, max_start));
    return {new_start, std::min(clip_end, new_start + length)};
}

// Format time as mm:ss.mm
inline void format_time(char* buf, size_t n, double sec) {
    if (sec < 0.0) sec = 0.0;
    const int   m  = static_cast<int>(sec) / 60;
    const float s  = static_cast<float>(sec - m * 60.0);
    std::snprintf(buf, n, "%d:%05.2f", m, s);
}

inline std::vector<WarpPoint> sanitize_warp_points(std::vector<WarpPoint> points) {
    std::sort(points.begin(), points.end(), [](const WarpPoint& a, const WarpPoint& b) {
        if (a.source_sample != b.source_sample) return a.source_sample < b.source_sample;
        return a.beat < b.beat;
    });
    points.erase(std::unique(points.begin(), points.end(), [](const WarpPoint& a, const WarpPoint& b) {
        return a.source_sample == b.source_sample;
    }), points.end());

    double last_beat = -1.0e100;
    for (auto& p : points) {
        if (!std::isfinite(p.beat)) p.beat = last_beat <= -1.0e90 ? 0.0 : last_beat;
        if (p.beat < last_beat) p.beat = last_beat;
        last_beat = p.beat;
    }
    return points;
}

inline std::vector<WarpPoint> parse_warp_points(const std::string& src) {
    std::vector<WarpPoint> out;
    if (src.empty()) return out;

    auto add = [&](uint32_t sample, double beat) {
        out.push_back({sample, beat});
    };

    try {
        auto j = nlohmann::json::parse(src);
        if (j.is_array()) {
            for (const auto& item : j) {
                if (!item.is_object()) continue;
                const auto sample = item.value("source_sample", 0u);
                const auto beat = item.value("beat", 0.0);
                add(sample, beat);
            }
        }
    } catch (...) {
        std::istringstream ss(src);
        std::string tok;
        while (ss >> tok) {
            const auto colon = tok.find(':');
            if (colon == std::string::npos) continue;
            try {
                const auto sample = static_cast<uint32_t>(std::stoul(tok.substr(0, colon)));
                const auto beat = std::stod(tok.substr(colon + 1));
                add(sample, beat);
            } catch (...) {
            }
        }
    }

    return sanitize_warp_points(std::move(out));
}

inline std::string serialize_warp_points(const std::vector<WarpPoint>& points) {
    const auto clean = sanitize_warp_points(points);
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : clean)
        j.push_back({{"source_sample", p.source_sample}, {"beat", p.beat}});
    return j.dump();
}

inline std::vector<TransientPoint> parse_transient_points(const std::string& src) {
    std::vector<TransientPoint> out;
    if (src.empty()) return out;
    try {
        auto j = nlohmann::json::parse(src);
        if (!j.is_array()) return out;
        for (const auto& item : j) {
            if (!item.is_object()) continue;
            out.push_back({item.value("source_sample", 0u),
                           item.value("strength", 1.0f)});
        }
    } catch (...) {
        return {};
    }
    std::sort(out.begin(), out.end(), [](const TransientPoint& a, const TransientPoint& b) {
        return a.source_sample < b.source_sample;
    });
    out.erase(std::unique(out.begin(), out.end(), [](const TransientPoint& a, const TransientPoint& b) {
        return a.source_sample == b.source_sample;
    }), out.end());
    return out;
}

inline std::string serialize_transient_points(const std::vector<TransientPoint>& points) {
    auto clean = points;
    std::sort(clean.begin(), clean.end(), [](const TransientPoint& a, const TransientPoint& b) {
        return a.source_sample < b.source_sample;
    });
    clean.erase(std::unique(clean.begin(), clean.end(), [](const TransientPoint& a, const TransientPoint& b) {
        return a.source_sample == b.source_sample;
    }), clean.end());
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : clean)
        j.push_back({{"source_sample", p.source_sample}, {"strength", p.strength}});
    return j.dump();
}

inline std::vector<uint32_t> parse_sample_points(const std::string& src) {
    std::vector<uint32_t> out;
    if (src.empty()) return out;
    try {
        auto j = nlohmann::json::parse(src);
        if (j.is_array()) {
            for (const auto& item : j) {
                if (item.is_number_unsigned()) out.push_back(item.get<uint32_t>());
                else if (item.is_object()) out.push_back(item.value("source_sample", 0u));
            }
        }
    } catch (...) {
        return {};
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

inline std::string serialize_sample_points(std::vector<uint32_t> points) {
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    nlohmann::json j = nlohmann::json::array();
    for (auto p : points) j.push_back(p);
    return j.dump();
}

inline std::vector<WarpPoint> compile_warp_points(std::vector<WarpPoint> points,
                                                  uint32_t clip_start_sample,
                                                  uint32_t clip_end_sample,
                                                  double fallback_beats) {
    std::vector<WarpPoint> out;
    if (clip_end_sample <= clip_start_sample) return out;

    for (const auto& p : points) {
        if (p.source_sample >= clip_start_sample && p.source_sample <= clip_end_sample)
            out.push_back(p);
    }

    if (out.empty() || out.front().source_sample > clip_start_sample)
        out.insert(out.begin(), {clip_start_sample, 0.0});
    if (out.back().source_sample < clip_end_sample) {
        const double last_beat = out.empty() ? 0.0 : out.back().beat;
        out.push_back({clip_end_sample, std::max(fallback_beats, last_beat + 1e-6)});
    }
    if (out.size() >= 2 && out.front().beat != 0.0) {
        const double offset = out.front().beat;
        for (auto& p : out) p.beat -= offset;
    }
    return out;
}

inline double source_for_warp_beat(const std::vector<WarpPoint>& points, double beat) {
    if (points.empty()) return 0.0;
    if (points.size() == 1 || beat <= points.front().beat)
        return static_cast<double>(points.front().source_sample);
    for (size_t i = 1; i < points.size(); ++i) {
        if (beat <= points[i].beat) {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            const double span = std::max(1e-9, b.beat - a.beat);
            const double t = std::clamp((beat - a.beat) / span, 0.0, 1.0);
            return static_cast<double>(a.source_sample) +
                   t * static_cast<double>(b.source_sample - a.source_sample);
        }
    }
    return static_cast<double>(points.back().source_sample);
}

inline double warp_total_beats(const std::vector<WarpPoint>& points) {
    return points.size() >= 2 ? std::max(1e-9, points.back().beat - points.front().beat) : 0.0;
}

inline float equal_power_fade_in(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return std::sin(t * 1.57079632679f);
}

inline float equal_power_fade_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return std::cos(t * 1.57079632679f);
}

inline double reverse_source_position(double pos, uint32_t start, uint32_t end) {
    return static_cast<double>(start + end) - pos;
}

inline double source_for_normalized_phase(double phase,
                                          uint32_t clip_start_sample,
                                          uint32_t clip_end_sample,
                                          bool reverse) {
    if (clip_end_sample <= clip_start_sample) return static_cast<double>(clip_start_sample);
    phase = std::clamp(phase, 0.0, 1.0);
    const uint32_t last = clip_end_sample - 1;
    const double pos = static_cast<double>(clip_start_sample) +
                       phase * static_cast<double>(last - clip_start_sample);
    return reverse ? reverse_source_position(pos, clip_start_sample, last) : pos;
}

inline double next_quantized_beat(double current_beats, int beats_per_bar, int quantize) {
    if (quantize <= 0) return current_beats;
    const double grid = quantize == 1 ? 1.0
                      : quantize == 2 ? static_cast<double>(std::max(1, beats_per_bar))
                                      : static_cast<double>(std::max(1, beats_per_bar) * 4);
    const double next = std::ceil((current_beats + 1e-9) / grid) * grid;
    return next <= current_beats + 1e-9 ? next + grid : next;
}

inline std::vector<TransientPoint> detect_transients(const std::vector<float>& left,
                                                     const std::vector<float>& right,
                                                     uint32_t sample_rate,
                                                     float sensitivity) {
    std::vector<TransientPoint> out;
    const size_t n = std::min(left.size(), right.size());
    if (n < 8 || sample_rate == 0) return out;
    const uint32_t min_gap = std::max(1u, sample_rate / 50u);
    const float threshold = 0.04f + (1.0f - std::clamp(sensitivity, 0.0f, 1.0f)) * 0.24f;
    float env = 0.0f;
    uint32_t last = 0;
    bool have_last = false;
    for (uint32_t i = 0; i < n; ++i) {
        const float mag = 0.5f * (std::fabs(left[i]) + std::fabs(right[i]));
        const float delta = mag - env;
        env = std::max(mag, env * 0.96f);
        if (delta > threshold && (!have_last || i - last >= min_gap)) {
            out.push_back({i, std::clamp(delta, 0.0f, 1.0f)});
            last = i;
            have_last = true;
        }
    }
    return out;
}

inline std::vector<SliceRegion> compile_slices(int mode,
                                               const std::vector<TransientPoint>& transients,
                                               const std::vector<uint32_t>& manual,
                                               uint32_t clip_start_sample,
                                               uint32_t clip_end_sample) {
    std::vector<uint32_t> starts;
    if (clip_end_sample <= clip_start_sample) return {};
    starts.push_back(clip_start_sample);
    if (mode == 1) {
        for (const auto& t : transients)
            if (t.source_sample > clip_start_sample && t.source_sample < clip_end_sample)
                starts.push_back(t.source_sample);
    } else if (mode == 2) {
        for (auto p : manual)
            if (p > clip_start_sample && p < clip_end_sample)
                starts.push_back(p);
    } else if (mode == 3) {
        const uint32_t len = clip_end_sample - clip_start_sample;
        for (uint32_t i = 1; i < 16; ++i)
            starts.push_back(clip_start_sample + static_cast<uint64_t>(len) * i / 16u);
    }
    std::sort(starts.begin(), starts.end());
    starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
    std::vector<SliceRegion> slices;
    for (size_t i = 0; i < starts.size(); ++i) {
        const uint32_t end = (i + 1 < starts.size()) ? starts[i + 1] : clip_end_sample;
        if (end > starts[i]) slices.push_back({starts[i], end});
    }
    return slices;
}

} // namespace audio_clip_ed
