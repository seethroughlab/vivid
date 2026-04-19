#pragma once
#include <cstdint>

namespace vivid {

// Result of a least-squares linear regression over a ring-buffered perf series.
// Samples are ordered oldest-to-newest with x as sample index (0..count-1), so
// `intercept` is the fitted value at the oldest sample and `y_at_newest` is the
// fitted value at the newest. `slope_per_sample` is the raw fit slope;
// `slope_per_second` scales it by the caller's sampling cadence for readable
// rates (e.g. MB/s for memory history).
struct PerfTrend {
    float slope_per_sample = 0.0f;
    float slope_per_second = 0.0f;
    float intercept = 0.0f;
    float y_at_newest = 0.0f;
    uint32_t sample_count = 0;
    bool valid = false;  // false until we have enough samples to be meaningful
};

// Minimum samples before a trend is reported as valid. Below this, the slope
// is noise-dominated and would jump around too much to be useful in the UI.
inline constexpr uint32_t kPerfTrendMinSamples = 8;

// Closed-form least-squares fit of y = slope*x + intercept, where x walks the
// ring buffer from oldest to newest. Uses Σx = n(n-1)/2 and Σx² = n(n-1)(2n-1)/6
// (no need to accumulate x values — only y and xy).
inline PerfTrend compute_perf_trend(const float* values,
                                    uint32_t buf_len,
                                    uint32_t write_idx,
                                    bool filled,
                                    float seconds_per_sample) {
    PerfTrend out;
    if (values == nullptr || buf_len == 0) return out;
    const uint32_t count = filled ? buf_len : write_idx;
    out.sample_count = count;
    if (count < kPerfTrendMinSamples) return out;

    const uint32_t first_idx = filled ? write_idx % buf_len : 0;
    double sum_y = 0.0;
    double sum_xy = 0.0;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t idx = filled ? (first_idx + i) % buf_len : i;
        const double y = static_cast<double>(values[idx]);
        sum_y += y;
        sum_xy += static_cast<double>(i) * y;
    }
    const double n = static_cast<double>(count);
    const double sum_x = n * (n - 1.0) * 0.5;
    const double sum_x2 = n * (n - 1.0) * (2.0 * n - 1.0) / 6.0;
    const double denom = n * sum_x2 - sum_x * sum_x;
    if (denom <= 0.0) return out;  // guards n==1 and precision underflow

    const double slope = (n * sum_xy - sum_x * sum_y) / denom;
    const double intercept = (sum_y - slope * sum_x) / n;

    out.slope_per_sample = static_cast<float>(slope);
    out.slope_per_second = (seconds_per_sample > 0.0f)
        ? static_cast<float>(slope / static_cast<double>(seconds_per_sample))
        : 0.0f;
    out.intercept = static_cast<float>(intercept);
    out.y_at_newest = static_cast<float>(intercept + slope * (n - 1.0));
    out.valid = true;
    return out;
}

}  // namespace vivid
